#include "resource_manager.hpp"

#include <filesystem>
#include <fstream>

#include <miniz.h>

#include "gta.hpp"
#include "network/network_manager.hpp"
#include "system/logger.hpp"
#include "shared/crypto.hpp"
#include "shared/utils.hpp"
#include "ui/download_dialog.hpp"

// Reads a loose file from disk into a byte buffer.
// Returns false if the file cannot be opened or read (e.g. missing), which is
// the normal signal to fall back to the downloaded/cached .pak content.
static bool ReadLocalFileBytes(const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open())
		return false;

	std::streamoff size = file.tellg();
	if (size < 0)
		return false;

	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (size > 0 && !file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size)))
		return false;

	out = std::move(buffer);
	return true;
}

ResourceManager::ResourceManager(Gta& gta) : gta_(gta) {}


void ResourceManager::SetNetworkManager(NetworkManager& net)
{
	net_ = &net;
}

void ResourceManager::SetDownloadDialog(DownloadDialog& dialog)
{
    download_dialog_ = &dialog;
}

void ResourceManager::Initialize()
{
	base_cache_path_ = gta_.GetUserFilesPath() + "/cef/cache/";
	std::filesystem::create_directories(base_cache_path_);

	// Loose build-shipped assets root: <game_dir>/cef/
	// Files placed here (e.g. cef/img/<file>) are served directly by
	// GetFileContent, letting the launcher bundle heavy/static assets so players
	// don't have to download them from the server on connect.
	std::string game_dir = gta_.GetGameDirPath();
	if (!game_dir.empty())
	{
		std::filesystem::path root = std::filesystem::path(game_dir) / "cef";
		local_resources_path_ = root.string();
		if (!local_resources_path_.empty() &&
			local_resources_path_.back() != '/' &&
			local_resources_path_.back() != '\\')
		{
			local_resources_path_ += '/';
		}
		LOG_INFO("[ResourceManager] Local build assets root: {}", local_resources_path_);
	}
}

void ResourceManager::SetResourcesLoaderUiEnabled(bool enabled)
{
	resources_loader_ui_enabled_ = enabled;

	if (download_dialog_)
		download_dialog_->SetEnabled(enabled);
}

void ResourceManager::OnConnect(const std::string& ip, uint16_t port)
{
	server_ip_ = ip + "_" + std::to_string(port);
	server_cache_path_ = base_cache_path_ + "/" + server_ip_ + "/";

	std::filesystem::create_directories(server_cache_path_);

	LOG_INFO("[ResourceManager] Connected to server, cache path: {}", server_cache_path_);
}

void ResourceManager::OnDisconnect()
{
	std::lock_guard<std::mutex> lock(download_mutex_);

	state_ = DownloadState::IDLE;
	server_manifest_ = nlohmann::json{};
	download_progress_.clear();
	assembling_files_.clear();
}

void ResourceManager::SetMasterKey(const std::vector<uint8_t>& key)
{
	master_key_ = key;
}

void ResourceManager::OnManifestReceived(const std::string& manifestJson)
{
	try {
		server_manifest_ = nlohmann::json::parse(manifestJson);
		LOG_DEBUG("[ResourceManager] Manifest received with {} resources", server_manifest_.size());
	}
	catch (const std::exception& e) {
		LOG_ERROR("[ResourceManager] Failed to parse manifest: {}", e.what());
	}
}

void ResourceManager::MarkAsReadyToDownload()
{
	DownloadState expected = DownloadState::IDLE;
	if (state_.compare_exchange_strong(expected, DownloadState::AWAITING_TRIGGER)) {
		LOG_INFO("[ResourceManager] Ready to download.");
	}
}

void ResourceManager::TriggerDownload()
{
	DownloadState expected = DownloadState::AWAITING_TRIGGER;
	if (!state_.compare_exchange_strong(expected, DownloadState::VERIFYING_CACHE)) {
		return;
	}

	if (!net_) {
		state_ = DownloadState::AWAITING_TRIGGER;
		return;
	}

	if (net_->GetState() != ConnectionState::CONNECTED) {
		state_ = DownloadState::AWAITING_TRIGGER;
		return;
	}

	if (master_key_.empty()) {
		LOG_WARN("[ResourceManager] Master key not yet received, waiting...");
		state_ = DownloadState::AWAITING_TRIGGER;
		return;
	}

	state_ = DownloadState::VERIFYING_CACHE;
	LOG_INFO("[ResourceManager] Verifying cache...");

	std::vector<std::pair<std::string, std::string>> files_to_request;
	std::vector<FileProgressData> progress_list;

	for (auto& [resourceName, files] : server_manifest_.items()) {
        for (auto& file_entry : files) {
            std::string path = file_entry["path"];
            std::string server_hash = file_entry["hash"];
            size_t server_size = file_entry["size"];
            std::string local_path = server_cache_path_ + path;

            bool file_exists = std::filesystem::exists(local_path);
            if (file_exists) {
                size_t actual_size = std::filesystem::file_size(local_path);
                std::string local_hash = CalculateSHA256(local_path);

                if (local_hash == server_hash) {

                    if (LoadPakIntoVFS(resourceName, local_path)) {
                        continue;
                    }
                }
            }

            files_to_request.push_back({ resourceName, path });
            FileProgressData progress;
            progress.fileName = path;
            progress.fileHash = server_hash;
            progress.totalSize = server_size;
            progress.isComplete = false;
            progress_list.push_back(std::move(progress));
        }
    }

	download_progress_ = std::move(progress_list);

	if (!download_progress_.empty()) {
		state_ = DownloadState::DOWNLOADING;
		last_packet_time_ = std::chrono::steady_clock::now();

		std::vector<std::pair<std::string, size_t>> dialog_files;
		for (const auto& p : download_progress_) {
			dialog_files.emplace_back(p.fileName, p.totalSize);
		}

		LOG_DEBUG("[ResourceManager] Starting download for {} file(s):", download_progress_.size());
		download_dialog_->Start(dialog_files);

		RequestFilesPacket request_packet;
		request_packet.files = std::move(files_to_request);
		net_->SendPacket(PacketType::RequestFiles, request_packet);
	}
	else {
		LOG_INFO("[ResourceManager] All local resources are up-to-date.");
		state_ = DownloadState::COMPLETED;
	}
}

void ResourceManager::OnFileData(const FileDataPacket& packet)
{
	if (state_ != DownloadState::DOWNLOADING) {
		LOG_WARN("[ResourceManager] Received FileData in wrong state: {}", static_cast<int>(state_.load()));
		return;
	}

	last_packet_time_ = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(download_mutex_);

	std::string fileKey = packet.resourceName + "/" + packet.relativePath;

	auto& assembly = assembling_files_[fileKey];

	if (assembly.totalChunks == 0)
	{
		assembly.totalChunks = packet.totalChunks;
		assembly.chunks.resize(packet.totalChunks);

		//LOG_INFO("[Download] Starting assembly for '{}' ({} chunks)", packet.relativePath, packet.totalChunks);
	}

	if (packet.totalChunks != assembly.totalChunks)
	{
		LOG_ERROR("[ResourceManager] Chunk count mismatch for '{}': expected {}, got {}", packet.relativePath, assembly.totalChunks, packet.totalChunks);
		return;
	}

	if (packet.chunkIndex >= assembly.totalChunks)
	{
		LOG_ERROR("[ResourceManager] Invalid chunk index {} for '{}'", packet.chunkIndex, packet.relativePath);
		return;
	}

	if (assembly.chunks[packet.chunkIndex].empty())
	{
		assembly.chunks[packet.chunkIndex] = packet.data;
		assembly.receivedChunks++;

		for (size_t i = 0; i < download_progress_.size(); ++i) {
			if (download_progress_[i].fileHash == packet.fileHash) {
				auto& progress = download_progress_[i];
				download_progress_[i].bytesReceived += packet.data.size();

				int percentage = (progress.totalSize > 0)
					? static_cast<int>((static_cast<double>(progress.bytesReceived) / progress.totalSize) * 100.0)
					: 100;
				
				download_dialog_->Update(static_cast<uint32_t>(i), download_progress_[i].bytesReceived);
			}
		}
	}

	if (assembly.receivedChunks == assembly.totalChunks)
	{
		LOG_INFO("[ResourceManager] All chunks received for '{}', assembling...", packet.relativePath);

		std::vector<uint8_t> completeFile;
		size_t totalSize = 0;
		for (const auto& chunk : assembly.chunks) {
			totalSize += chunk.size();
		}

		completeFile.reserve(totalSize);

		for (const auto& chunk : assembly.chunks) {
			completeFile.insert(completeFile.end(), chunk.begin(), chunk.end());
		}

		std::string receivedHash = CalculateSHA256FromData(completeFile);
		if (receivedHash != packet.fileHash)
		{
			LOG_ERROR("[ResourceManager] Hash mismatch for '{}': expected {}, got {}", packet.relativePath, packet.fileHash, receivedHash);

			assembling_files_.erase(fileKey);

			for (auto& progress : download_progress_)
			{
				if (progress.fileName == packet.relativePath) {
					progress.isComplete = false;
					break;
				}
			}
			return;
		}

		std::string savePath = server_cache_path_ + "/" + packet.relativePath;
		std::filesystem::create_directories(std::filesystem::path(savePath).parent_path());

		try {
			std::ofstream outFile(savePath, std::ios::binary);
			outFile.write(reinterpret_cast<const char*>(completeFile.data()), completeFile.size());
			outFile.close();

			LOG_INFO("[ResourceManager] File '{}' saved successfully ({} bytes)", packet.relativePath, completeFile.size());

			if (LoadPakIntoVFS(packet.resourceName, savePath))
			{
				LOG_INFO("[ResourceManager] Loaded '{}' into VFS", packet.resourceName);
			}

			for (auto& progress : download_progress_)
			{
				if (progress.fileName == packet.relativePath) {
					progress.isComplete = true;
					progress.fileHash = receivedHash;
					break;
				}
			}

			assembling_files_.erase(fileKey);

			bool allComplete = true;

			for (const auto& progress : download_progress_)
			{
				if (!progress.isComplete) {
					allComplete = false;
					break;
				}
			}

			if (allComplete)
			{
				LOG_INFO("[ResourceManager] All downloads complete!");

				state_ = DownloadState::COMPLETED;
				download_dialog_->Finish();
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR("[ResourceManager] Failed to save file '{}': {}", packet.relativePath, e.what());
		}
	}
}

bool ResourceManager::LoadPakIntoVFS(const std::string& resourceName, const std::string& pakPath)
{
	if (master_key_.empty())
	{
		LOG_ERROR("[ResourceManager] Cannot load PAK: master key not set");
		return false;
	}

	mz_zip_archive zip{};
	if (!mz_zip_reader_init_file(&zip, pakPath.c_str(), 0))
	{
		LOG_ERROR("[ResourceManager] Failed to open PAK file: {}", pakPath);
		return false;
	}

	const mz_uint num_files = mz_zip_reader_get_num_files(&zip);
	LOG_DEBUG("[ResourceManager] Loading PAK with {} files", num_files);

	VirtualFileSystem vfs;

	for (mz_uint i = 0; i < num_files; ++i)
	{
		mz_zip_archive_file_stat file_stat;
		if (!mz_zip_reader_file_stat(&zip, i, &file_stat))
			continue;

		size_t uncomp_size = 0;
		void* p = mz_zip_reader_extract_to_heap(&zip, i, &uncomp_size, 0);
		if (!p)
		{
			LOG_WARN("[ResourceManager] Failed to extract file '{}' from PAK", file_stat.m_filename);
			continue;
		}

		std::vector<uint8_t> entry_data(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + uncomp_size);
		mz_free(p);

		std::vector<uint8_t> decoded;
		if (!DecodeResourceEntry(entry_data, master_key_, decoded))
		{
			LOG_WARN("[ResourceManager] Failed to decode file '{}'", file_stat.m_filename);
			continue;
		}

		const auto decoded_size = decoded.size();
		vfs[file_stat.m_filename] = std::move(decoded);
		LOG_DEBUG("[ResourceManager] Loaded file: {} ({} bytes)", file_stat.m_filename, decoded_size);
	}

	mz_zip_reader_end(&zip);

	{
		std::lock_guard<std::mutex> lock(vfs_mutex_);
		loaded_resources_vfs_[resourceName] = std::move(vfs);
	}

	LOG_INFO("[ResourceManager] Loaded resource '{}' into VFS ({} files)", resourceName, num_files);
	return true;
}

bool ResourceManager::GetFileContent(const std::string& resourceName,
	const std::string& internalPath,
	std::vector<uint8_t>& outContent)
{
	// Local build override (local-first, server fallback):
	// Prefer a loose file shipped with the client build at
	// <game_dir>/cef/<resourceName>/<internalPath> (e.g. cef/img/school.jpg).
	// It is served as-is (no download, no decryption). When the file is not
	// present locally, we fall through to the downloaded/cached .pak content
	// held in the in-memory VFS below.
	if (!local_resources_path_.empty() &&
		resourceName.find("..") == std::string::npos &&
		internalPath.find("..") == std::string::npos)
	{
		const std::string local_path = local_resources_path_ + resourceName + "/" + internalPath;
		if (ReadLocalFileBytes(local_path, outContent))
		{
			LOG_DEBUG("[ResourceManager] Served '{}/{}' from local build folder", resourceName, internalPath);
			return true;
		}
	}

	std::lock_guard<std::mutex> lock(vfs_mutex_);

	auto it = loaded_resources_vfs_.find(resourceName);
	if (it == loaded_resources_vfs_.end())
	{
		LOG_WARN("[ResourceManager] Resource '{}' not found in VFS", resourceName);
		return false;
	}

	auto& vfs = it->second;
	auto file_it = vfs.find(internalPath);
	if (file_it == vfs.end())
	{
		LOG_WARN("[ResourceManager] File '{}' not found in resource '{}'", internalPath, resourceName);
		return false;
	}

	outContent = file_it->second;
	return true;
}
