// Shared helpers for analysing MasterCraft through the Node inspector.
//
// launch() starts MasterCraft.exe with --inspect-brk, installs HID_HOOK before any app code
// runs (so every node-hid write / read is logged), then resumes the app. Log lines are
// collected in globalThis.__hidlog inside MasterCraft and pulled out with drain().
const { spawn, execSync } = require('child_process');

const MASTERCRAFT_EXE = 'C:\\MasterCraft\\MasterCraft.exe';

// Hooks the node-hid native binding (HID.node). Its methods are non-writable, so a fake
// prototype + instance proxy is used. Logs writes, reads (with data) and device opens.
const HID_HOOK = String.raw`(() => {
  if (globalThis.__hidHooked) return 'already';
  globalThis.__hidHooked = true;
  globalThis.__hidlog = [];
  const hex = b => { try { return Buffer.from(b).toString('hex'); } catch (e) { return String(b); } };
  const log = m => { globalThis.__hidlog.push(Date.now() + ' ' + m); };
  const isBuf = a => a && (Array.isArray(a) || a instanceof Uint8Array);
  const fmt = a => isBuf(a) ? 'HEX:' + hex(a) + ' len=' + a.length : (typeof a === 'function' ? '<cb>' : JSON.stringify(a));
  const origDlopen = process.dlopen;
  process.dlopen = function (mod, filename, ...rest) {
    const ret = origDlopen.call(this, mod, filename, ...rest);
    try {
      if (/node-hid/i.test(filename)) {
        const b = mod.exports;
        log('BINDING keys=' + Object.keys(b).join(','));
        const realOf = new WeakMap();
        const C = b.HID, RP = C.prototype, fakeProto = {};
        for (const n of Object.getOwnPropertyNames(RP)) {
          if (n === 'constructor' || typeof RP[n] !== 'function') continue;
          const fn = RP[n], name = n;
          const wrapped = function (...args) {
            const self = realOf.get(this) || this;
            const argStr = args.map(fmt).join(', ');
            const nargs = args.map(a => typeof a === 'function' ? function (err, data) {
              if (data && data.length) log('HID.' + name + ' CB <- HEX:' + hex(data));
              else if (err) log('HID.' + name + ' CB err ' + err);
              return a.apply(this, arguments);
            } : a);
            let r;
            try { r = fn.apply(self, nargs); } catch (e) { log('HID.' + name + '(' + argStr + ') THROW ' + e.message); throw e; }
            if (name !== 'read') log('HID.' + name + '(' + argStr + ') -> ' + fmt(r));
            return r;
          };
          Object.defineProperty(fakeProto, n, { value: wrapped, enumerable: Object.getOwnPropertyDescriptor(RP, n).enumerable, writable: true, configurable: true });
        }
        b.HID = new Proxy(C, {
          get(t, k) { return k === 'prototype' ? fakeProto : Reflect.get(t, k); },
          construct(t, a) {
            log('new HID(' + a.map(fmt).join(', ') + ')');
            const inst = Reflect.construct(t, a, t);
            const px = new Proxy(inst, { get(o, k) { if (Object.prototype.hasOwnProperty.call(fakeProto, k)) return fakeProto[k]; const v = o[k]; return typeof v === 'function' ? v.bind(o) : v; } });
            realOf.set(px, inst);
            return px;
          }
        });
      }
    } catch (e) { log('hook error ' + e.message); }
    return ret;
  };
  return 'hooked';
})()`;

// Run after the app has started: lets the caller invoke ipcMain handlers directly
// (the same handlers the renderer reaches with ipcRenderer.invoke).
const IPC_HELPER = String.raw`(() => {
  // Electron stores a wrapper per channel that answers through event._reply / event._throw.
  globalThis.__invoke = (channel, ...args) => new Promise((resolve, reject) => {
    const { ipcMain, webContents } = process.mainModule.require('electron');
    const h = ipcMain._invokeHandlers && ipcMain._invokeHandlers.get(channel);
    if (!h) return reject(new Error('no handler for ' + channel));
    const wc = webContents.getAllWebContents()[0];
    const ev = { sender: wc, senderFrame: wc && wc.mainFrame, frameId: 1, processId: 0, _reply: resolve, _throw: reject };
    Promise.resolve(h(ev, ...args)).catch(reject);
  });
  globalThis.__mark = (s) => globalThis.__hidlog.push(Date.now() + ' STEP ' + s);
  return 'ok';
})()`;

async function getWsUrl(port) {
  for (let i = 0; i < 150; i++) {
    try {
      const j = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      if (j.length) return j[0].webSocketDebuggerUrl;
    } catch (e) { }
    await new Promise(r => setTimeout(r, 200));
  }
  throw new Error('inspector not reachable');
}

function masterCraftRunning() {
  try {
    return /MasterCraft\.exe/i.test(execSync('tasklist /FI "IMAGENAME eq MasterCraft.exe" /NH', { encoding: 'utf8' }));
  } catch (e) { return false; }
}

async function launch({ port = 9229, args = [], log = () => { } } = {}) {
  // MasterCraft is single-instance: a second copy just hands over to the running one and exits.
  if (masterCraftRunning()) throw new Error('MasterCraft is already running; close it first (tray icon -> exit, or taskkill /IM MasterCraft.exe)');
  const child = spawn(MASTERCRAFT_EXE, [`--inspect-brk=${port}`, ...args], { detached: true, stdio: 'ignore', cwd: 'C:\\MasterCraft' });
  child.unref();
  log('# spawned pid ' + child.pid);
  const ws = new WebSocket(await getWsUrl(port));
  let id = 0;
  const pending = new Map();
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  let hooked = false;
  ws.onmessage = async (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
    if (m.method === 'Debugger.paused') {
      if (!hooked) {
        hooked = true;
        const r = await send('Debugger.evaluateOnCallFrame', { callFrameId: m.params.callFrames[0].callFrameId, expression: HID_HOOK, returnByValue: true });
        log('# hook ' + JSON.stringify(r.result && (r.result.result || r.result)));
      }
      await send('Debugger.resume');
      await send('Debugger.disable');
    }
  };
  await new Promise(r => { ws.onopen = r; });
  await send('Runtime.enable');
  await send('Debugger.enable');
  await send('Runtime.runIfWaitingForDebugger');

  // Evaluate an expression in MasterCraft's main process. Returns the value or throws.
  const evaluate = async (expression, awaitPromise = false) => {
    const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise });
    const res = r.result || {};
    if (res.exceptionDetails) throw new Error((res.exceptionDetails.exception && res.exceptionDetails.exception.description) || res.exceptionDetails.text);
    return res.result && res.result.value;
  };
  const drain = async () => (await evaluate("globalThis.__hidlog ? globalThis.__hidlog.splice(0).join('\\n') : ''")) || '';
  const close = () => { try { ws.close(); } catch (e) { } };
  return { child, send, evaluate, drain, close };
}

module.exports = { launch, HID_HOOK, IPC_HELPER };
