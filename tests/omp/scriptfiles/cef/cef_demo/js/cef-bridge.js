(function () {
	function fallbackLog(type, name, args) {
		console.log(`[CEF DEMO cef.${type}]`, name, args || []);
	}

	if (!window.cef) {
		window.cef = {
			on: function (name, callback) {
				fallbackLog('on', name);
			},
			emit: function (name) {
				const args = Array.prototype.slice.call(arguments, 1);
				fallbackLog('emit', name, args);
			}
		};
	}

  	window.CefDemo = window.CefDemo || {};

	window.CefDemo.on = function (name, callback) {
		if (!window.cef || typeof window.cef.on !== 'function') {
			fallbackLog('on', name);
			return;
		}
		window.cef.on(name, callback);
	};

	window.CefDemo.emit = function (name) {
		const args = Array.prototype.slice.call(arguments, 1);
		if (!window.cef || typeof window.cef.emit !== 'function') {
			fallbackLog('emit', name, args);
			return;
		}
		window.cef.emit.apply(window.cef, [name].concat(args));
	};
})();
