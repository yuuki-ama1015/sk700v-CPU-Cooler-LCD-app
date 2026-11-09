// Launch MasterCraft under the inspector and log all HID traffic plus the sensor JSON it
// receives from its sensor service (named pipe), for <seconds>.
// usage: node hid_sniff.js <logfile> <seconds>
const fs = require('fs');
const { launch } = require('./mc_inspect');

const [, , logFile, secsArg] = process.argv;
const SECS = Number(secsArg || 60);
const out = fs.createWriteStream(logFile, { flags: 'w' });
const w = (s) => { out.write(s + '\n'); };

// Logs sensor JSON chunks arriving over sockets / pipes.
const SOCK_HOOK = String.raw`(() => {
  const P = process.mainModule.require('net').Socket.prototype;
  if (P.__hooked) return 'already';
  P.__hooked = true;
  const oe = P.emit;
  globalThis.__sockn = 0;
  P.emit = function (ev, ...a) {
    if (ev === 'data' && a[0] && globalThis.__sockn < 400) {
      globalThis.__sockn++;
      const b = Buffer.isBuffer(a[0]) ? a[0] : Buffer.from(String(a[0]));
      globalThis.__hidlog.push(Date.now() + ' SOCK len=' + b.length + ' B64:' + b.toString('base64'));
    }
    return oe.call(this, ev, ...a);
  };
  return 'ok';
})()`;

(async () => {
  const mc = await launch({ log: w });
  const t0 = Date.now();
  let sockHooked = false;
  const timer = setInterval(async () => {
    try {
      if (!sockHooked && Date.now() - t0 > 3000) { sockHooked = true; w('# sock hook ' + await mc.evaluate(SOCK_HOOK)); }
      const v = await mc.drain();
      if (v) w(v);
    } catch (e) { w('# poll err ' + e.message); }
    if (Date.now() - t0 > SECS * 1000) { clearInterval(timer); w('# done'); out.end(); mc.close(); setTimeout(() => process.exit(0), 300); }
  }, 300);
})().catch(e => { w('# fatal ' + e.stack); out.end(); process.exit(1); });
