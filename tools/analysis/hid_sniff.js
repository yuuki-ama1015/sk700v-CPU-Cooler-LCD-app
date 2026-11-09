// Launch MasterCraft with --inspect-brk, hook node-hid native binding, log all HID traffic.
// usage: node hid_sniff.js <logfile> <seconds>
const { spawn } = require('child_process');
const fs = require('fs');
const [, , logFile, secsArg] = process.argv;
const SECS = Number(secsArg || 60);
const PORT = 9229;
const out = fs.createWriteStream(logFile, { flags: 'w' });
const w = (s) => { out.write(s + '\n'); };

const HOOK = String.raw`(() => {
  if (globalThis.__hidHooked) return 'already';
  globalThis.__hidHooked = true;
  globalThis.__hidlog = [];
  const hex = b => { try { return Buffer.from(b).toString('hex'); } catch (e) { return String(b); } };
  const log = m => { globalThis.__hidlog.push(Date.now() + ' ' + m); };
  const isBuf = a => a && (Array.isArray(a) || a instanceof Uint8Array);
  const fmt = a => isBuf(a) ? 'HEX:' + hex(a) + ' len=' + a.length : (typeof a === 'function' ? '<cb>' : JSON.stringify(a));
  const wrapProto = (proto, kind) => {
    for (const name of Object.getOwnPropertyNames(proto)) {
      if (name === 'constructor') continue;
      const orig = proto[name];
      if (typeof orig !== 'function') continue;
      proto[name] = function (...args) {
        const argStr = args.map(fmt).join(', ');
        const nargs = args.map(a => typeof a === 'function' ? function (err, data) {
          if (data && data.length) log(kind + '.' + name + ' CB <- HEX:' + hex(data));
          else if (err) log(kind + '.' + name + ' CB err ' + err);
          return a.apply(this, arguments);
        } : a);
        let r;
        try { r = orig.apply(this, nargs); } catch (e) { log(kind + '.' + name + '(' + argStr + ') THROW ' + e.message); throw e; }
        if (r && typeof r.then === 'function') {
          r.then(v => log(kind + '.' + name + '(' + argStr + ') => ' + fmt(v)), e => log(kind + '.' + name + '(' + argStr + ') REJECT ' + e.message));
        } else {
          log(kind + '.' + name + '(' + argStr + ') -> ' + fmt(r));
        }
        return r;
      };
    }
  };
  const origDlopen = process.dlopen;
  process.dlopen = function (mod, filename, ...rest) {
    const ret = origDlopen.call(this, mod, filename, ...rest);
    try {
      log('DLOPEN ' + filename);
      if (/node-hid/i.test(filename)) {
        const b = mod.exports;
        log('BINDING keys=' + Object.keys(b).join(','));
        // native methods are non-writable: build a fake prototype + instance proxy instead
        const realOf = new WeakMap();
        const C = b.HID, RP = C.prototype, fakeProto = {};
        for (const n of Object.getOwnPropertyNames(RP)) {
          if (n === 'constructor' || typeof RP[n] !== 'function') continue;
          const fn = RP[n], kind = 'HID', name = n;
          const wrapped = function (...args) {
            const self = realOf.get(this) || this;
            const argStr = args.map(fmt).join(', ');
            const nargs = args.map(a => typeof a === 'function' ? function (err, data) {
              if (data && data.length) log(kind + '.' + name + ' CB <- HEX:' + hex(data));
              else if (err) log(kind + '.' + name + ' CB err ' + err);
              return a.apply(this, arguments);
            } : a);
            let r;
            try { r = fn.apply(self, nargs); } catch (e) { log(kind + '.' + name + '(' + argStr + ') THROW ' + e.message); throw e; }
            if (name !== 'read') log(kind + '.' + name + '(' + argStr + ') -> ' + fmt(r));
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
        for (const k of ['openAsyncHIDDevice']) {
          const o = b[k];
          if (typeof o === 'function') b[k] = function (...a) { log('binding.' + k + '(' + a.map(fmt).join(', ') + ')'); return o.apply(this, a); };
        }
      }
    } catch (e) { log('hook error ' + e.message); }
    return ret;
  };
  return 'hooked';
})()`;

async function getWsUrl() {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${PORT}/json/list`);
      const j = await r.json();
      if (j.length) return j[0].webSocketDebuggerUrl;
    } catch (e) { }
    await new Promise(r => setTimeout(r, 200));
  }
  throw new Error('inspector not reachable');
}

(async () => {
  const child = spawn('C:\\MasterCraft\\MasterCraft.exe', [`--inspect-brk=${PORT}`], { detached: true, stdio: 'ignore', cwd: 'C:\\MasterCraft' });
  child.unref();
  w('# spawned pid ' + child.pid);
  const url = await getWsUrl();
  w('# ws ' + url);
  const ws = new WebSocket(url);
  let id = 0; const pending = new Map();
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  let hooked = false;
  ws.onmessage = async (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
    if (m.method === 'Debugger.paused') {
      if (!hooked) {
        hooked = true;
        const cf = m.params.callFrames[0].callFrameId;
        const r = await send('Debugger.evaluateOnCallFrame', { callFrameId: cf, expression: HOOK, returnByValue: true });
        w('# hook result ' + JSON.stringify(r.result && (r.result.result || r.result)));
      }
      await send('Debugger.resume');
      await send('Debugger.disable');
    }
  };
  await new Promise(r => ws.onopen = r);
  await send('Runtime.enable');
  await send('Debugger.enable');
  await send('Runtime.runIfWaitingForDebugger');
  const t0 = Date.now();
  const IPC_HOOK = String.raw`(() => {
    const { webContents, ipcMain } = process.mainModule.require('electron');
    const keep = s => /emperature/.test(s) && s.length < 40000;
    let n = 0;
    for (const wc of webContents.getAllWebContents()) {
      if (wc.__hooked) continue; wc.__hooked = true; n++;
      const orig = wc.send.bind(wc);
      wc.send = (ch, ...args) => { try { const s = JSON.stringify(args); if (keep(s)) globalThis.__hidlog.push(Date.now() + ' IPC-SEND ' + ch + ' ' + s); } catch (e) { } return orig(ch, ...args); };
    }
    const m = ipcMain._invokeHandlers; let h = 0;
    if (m && !m.__hooked) { m.__hooked = true;
      for (const [k, fn] of m) { h++; m.set(k, async (...a) => { const r = await fn(...a); try { const s = JSON.stringify(r); if (keep(s)) globalThis.__hidlog.push(Date.now() + ' IPC-INVOKE ' + k + ' ' + s); } catch (e) { } return r; }); }
    }
    const net = process.mainModule.require('net');
    const P = net.Socket.prototype;
    if (!P.__hooked) { P.__hooked = true; const oe = P.emit; globalThis.__sockn = 0;
      P.emit = function (ev, ...a) {
        if (ev === 'data' && a[0] && globalThis.__sockn < 400) { globalThis.__sockn++;
          const b = Buffer.isBuffer(a[0]) ? a[0] : Buffer.from(String(a[0]));
          globalThis.__hidlog.push(Date.now() + ' SOCK ' + (this.remoteAddress || this._pipeName || this.path || '?') + ' len=' + b.length + ' B64:' + b.toString('base64'));
        }
        return oe.call(this, ev, ...a);
      };
    }
    return 'wc=' + n + ' handlers=' + h;
  })()`;
  let ipcHooked = false;
  const timer = setInterval(async () => {
    if (!ipcHooked && Date.now() - t0 > 8000) {
      ipcHooked = true;
      const r = await send('Runtime.evaluate', { expression: IPC_HOOK, returnByValue: true });
      w('# ipc hook ' + JSON.stringify(r.result && (r.result.result || r.result.exceptionDetails)));
    }
    try {
      const r = await send('Runtime.evaluate', { expression: "globalThis.__hidlog ? globalThis.__hidlog.splice(0).join('\\n') : ''", returnByValue: true });
      const v = r.result && r.result.result && r.result.result.value;
      if (v) w(v);
    } catch (e) { w('# poll err ' + e.message); }
    if (Date.now() - t0 > SECS * 1000) { clearInterval(timer); w('# done'); out.end(); ws.close(); setTimeout(() => process.exit(0), 300); }
  }, 300);
})().catch(e => { w('# fatal ' + e.stack); out.end(); process.exit(1); });
