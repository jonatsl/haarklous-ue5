/* Injected into haarklous-game.html before any of its scripts run (tools/web-export/export.mjs).
 * - Blocks every network connection (no lobby server, no aerial imagery): the exporter must never talk to the live game.
 * - Tags each material made by RealMaterials (materials.js) with the function name and arguments, so Blender and Unreal
 *   know what a surface is (brick, plaster, wood floor ...) instead of guessing from colours.
 */
(() => {
  'use strict';
  const blocked = function () {throw new Error('Network blocked by the Haarklous exporter');};
  window.WebSocket = blocked;window.EventSource = blocked;
  window.fetch = () => Promise.reject(new Error('Network blocked by the Haarklous exporter'));
  const XO = window.XMLHttpRequest.prototype.open;
  window.XMLHttpRequest.prototype.open = function (m, url) {if (/^(https?|wss?):/i.test(String(url))) throw new Error('Network blocked');return XO.apply(this, arguments);};
  const plain = a => typeof a === 'number' || typeof a === 'string' || typeof a === 'boolean' ? a : Array.isArray(a) ? a.map(plain) : a && a.isColor ? a.getHex() : null;
  let real;
  Object.defineProperty(window, 'RealMaterials', {
    configurable: true,
    get: () => real,
    set: v => {
      real = {...v, create(T, renderer) {
        const lib = v.create(T, renderer), wrapped = {...lib};
        for (const [k, f] of Object.entries(lib)) {
          if (typeof f !== 'function' || ['boxUV', 'environment', 'mergeStatic', 'rand'].includes(k)) continue;
          wrapped[k] = (...args) => {const m = f(...args);if (m && m.isMaterial && !m.userData.hk) m.userData.hk = {fn: k, args: args.map(plain)};return m;};
        }
        window.__hkLib = wrapped;return wrapped;
      }};
    }
  });
})();
