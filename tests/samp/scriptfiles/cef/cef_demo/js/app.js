(function () {
	const $ = (selector) => document.querySelector(selector);
	const logEl = $('#log');

	function log(message) {
		if (!logEl) 
			return;

		const line = document.createElement('div');
		line.textContent = `${new Date().toLocaleTimeString()} ${message}`;
		logEl.prepend(line);
	}

	function setupAnimation() {
		const box = $('#box');
		const fps = $('#fps');
		const frameEl = $('#frame');
		const timeEl = $('#time');
		const bar = $('#bar-fill');

		if (!box) 
			return;

		let x = 0;
		let dir = 1;
		let frames = 0;
		let fpsFrames = 0;
		let lastFps = performance.now();
		const start = performance.now();

		function tick(now) {
			frames++;
			fpsFrames++;

			const maxX = Math.max(1, window.innerWidth - 150);
			x += dir * 8;

			if (x >= maxX) { 
				x = maxX; dir = -1; 
			}

			if (x <= 0) { 
				x = 0; dir = 1; 
			}

			box.style.left = `${x}px`;
			box.style.background = frames % 120 < 60 ? '#ff4545' : '#3b82ff';

			if (frameEl) 
				frameEl.textContent = frames;

			if (timeEl) 
				timeEl.textContent = Math.floor(now - start);

			if (bar) 
				bar.style.width = `${Math.floor(((now - start) % 5000) / 5000 * 100)}%`;

			if (fps && now - lastFps >= 1000) {
				fps.textContent = fpsFrames;
				fpsFrames = 0;
				lastFps = now;
			}

			requestAnimationFrame(tick);
		}

		requestAnimationFrame(tick);
	}

	function setupEvents() {
		document.querySelectorAll('[data-cef-button]').forEach((button) => {
			button.addEventListener('click', () => {
				const action = button.getAttribute('data-cef-button');
				window.CefDemo.emit('demo:button', action);
				log(`JS -> Pawn demo:button ${action}`);
			});
		});

		const input = $('#demo-input');
		const submit = $('#send-input');
		if (input && submit) {
			submit.addEventListener('click', () => {
				window.CefDemo.emit('demo:input', input.value);
				log(`JS -> Pawn demo:input ${input.value}`);
			});
		}

		const ping = $('#send-ping');
		if (ping) {
			ping.addEventListener('click', () => {
				window.CefDemo.emit('demo:ping', 'hello from JavaScript', 42, true);
				log('JS -> Pawn demo:ping');
			});
		}

		window.CefDemo.on('demo:notify', function (message, count) {
			log(`Pawn -> JS demo:notify message=${message} count=${count}`);

			const notify = $('#notify');
			if (notify) 
				notify.textContent = `${message} (#${count})`;
		});

		window.CefDemo.on('demo:stats', function (health, armour, money) {
			log(`Pawn -> JS demo:stats health=${health} armour=${armour} money=${money}`);

			const stats = $('#server-stats');
			if (stats) 
				stats.textContent = `Health ${health} / Armour ${armour} / Money ${money}`;
		});

		window.CefDemo.on('cef:escape_menu', function (visible) {
			log(`Built-in cef:escape_menu visible=${visible}`);
			document.body.classList.toggle('pause-open', !!visible);
		});
	}

	setupAnimation();
	setupEvents();
})();
