// Records what MasterCraft sends to the cooler when Windows sleeps and wakes.
// usage: node power_probe.js <logfile> [startupSeconds=20]
//
// Starts MasterCraft with the HID hook, then emits Electron's powerMonitor 'suspend' and
// 'resume' events in the main process (the same events Windows sleep / wake would raise)
// and logs every HID call around them. No real sleep is needed.
const fs = require('fs');
const { launch } = require('./mc_inspect');

const logFile = process.argv[2] || 'power_probe.log';
const startupSeconds = Number(process.argv[3] || 20);
const out = fs.createWriteStream(logFile);
const log = (s) => { out.write(s + '\n'); console.log(s); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const mc = await launch({ log });
  const step = async (name, ms) => {
    await sleep(ms);
    const hid = await mc.drain();
    log(`# --- ${name} (${hid ? hid.split('\n').length : 0} HID lines)`);
    if (hid) log(hid);
  };
  try {
    await step(`startup ${startupSeconds}s`, startupSeconds * 1000);
    const pm = "process.mainModule.require('electron').powerMonitor";
    log('# listeners suspend=' + await mc.evaluate(`${pm}.listenerCount('suspend')`) +
        ' resume=' + await mc.evaluate(`${pm}.listenerCount('resume')`));
    log('# suspend handler: ' + await mc.evaluate(`${pm}.listeners('suspend').map(String).join(' | ')`));
    log('# resume handler: ' + await mc.evaluate(`${pm}.listeners('resume').map(String).join(' | ')`));
    await mc.evaluate(`globalThis.__hidlog.push(Date.now() + ' EMIT suspend'); ${pm}.emit('suspend'); 'ok'`);
    await step('after suspend 10s', 10000);
    await mc.evaluate(`globalThis.__hidlog.push(Date.now() + ' EMIT resume'); ${pm}.emit('resume'); 'ok'`);
    await step('after resume 20s', 20000);
  } catch (e) {
    log('# error ' + e.message);
  } finally {
    try { await mc.evaluate("process.mainModule.require('electron').app.exit(0)"); } catch (e) { }
    mc.close();
    out.end();
    setTimeout(() => process.exit(0), 1000);
  }
})();
