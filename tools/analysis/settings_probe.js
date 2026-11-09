// Change each SK700V setting through MasterCraft's own IPC handlers (no UI clicks needed)
// and log the HID traffic, so the settings commands can be identified from the diff.
// The original settings are restored at the end. MasterCraft is killed when done.
// MasterCraft only sends to the LCD once per second while it receives sensor data, so the
// "MasterCraft Helper Service" must be running. Optional flag files let an elevated helper
// script start that service first (readyFlag) and restore it afterwards (doneFlag).
// usage: node settings_probe.js <logfile> [holdSeconds=6] [readyFlag] [doneFlag]
const fs = require('fs');
const { execSync } = require('child_process');
const { launch, IPC_HELPER } = require('./mc_inspect');

const [, , logFile, holdArg, readyFlag, doneFlag] = process.argv;
const HOLD = Number(holdArg || 6) * 1000;
const out = fs.createWriteStream(logFile, { flags: 'w' });
const w = (s) => { out.write(s + '\n'); };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

(async () => {
  if (readyFlag) {
    w('# waiting for ' + readyFlag);
    for (let i = 0; i < 180 && !fs.existsSync(readyFlag); i++) await sleep(1000);
    if (!fs.existsSync(readyFlag)) throw new Error('ready flag did not appear');
  }
  const mc = await launch({ log: w });
  const pump = setInterval(async () => { try { const v = await mc.drain(); if (v) w(v); } catch (e) { } }, 300);
  const call = async (channel, ...args) => {
    const r = await mc.evaluate(`globalThis.__invoke(${JSON.stringify(channel)}, ...${JSON.stringify(args)}).then(r => JSON.stringify(r))`, true);
    return r ? JSON.parse(r) : r;
  };
  const mark = (s) => mc.evaluate(`globalThis.__mark(${JSON.stringify(s)})`);

  try {
    // wait for the app and the device
    let serial = null;
    for (let i = 0; i < 60 && !serial; i++) {
      await sleep(1000);
      try {
        await mc.evaluate(IPC_HELPER);
        const list = await call('app/get-device-list');
        const s = String(JSON.stringify(list));
        w('# device list ' + s.slice(0, 600));
        const d = s.match(/"serialNumber":"([^"]+)"/);
        if (d) serial = d[1];
      } catch (e) { w('# waiting: ' + e.message); }
    }
    if (!serial) throw new Error('device not found');
    await sleep(3000);

    const orig = (await call('sk700/get-device-info', serial)).data;
    const origSetting = (await call('app/get-setting')).data;
    w('# original device ' + JSON.stringify(orig));
    w('# original setting ' + JSON.stringify(origSetting));

    await mark('baseline');
    await sleep(HOLD);

    const setDevice = async (label, mutate) => {
      const d = JSON.parse(JSON.stringify(orig));
      mutate(d);
      await mark(label);
      const r = await call('sk700/update-device-info', d);
      w('# ' + label + ' -> ' + JSON.stringify(r).slice(0, 300));
      await sleep(HOLD);
    };
    const setSetting = async (label, mutate) => {
      const s = JSON.parse(JSON.stringify(origSetting));
      mutate(s);
      await mark(label);
      const r = await call('app/set-setting', s);
      w('# ' + label + ' -> ' + JSON.stringify(r).slice(0, 300));
      await sleep(HOLD);
    };

    await setDevice('screenStatus=0', d => { d.options.screenStatus = 0; });
    await setDevice('screenStatus=1 (restore)', () => { });
    await setDevice('mode.value=' + (orig.mode.value ? 0 : 1), d => { d.mode.value = orig.mode.value ? 0 : 1; });
    await setDevice('mode.value=' + orig.mode.value + ' (restore)', () => { });
    await setSetting('temperatureDisplay=' + (origSetting.temperatureDisplay ? 0 : 1), s => { s.temperatureDisplay = origSetting.temperatureDisplay ? 0 : 1; });
    await setSetting('temperatureDisplay=' + origSetting.temperatureDisplay + ' (restore)', () => { });

    const after = (await call('sk700/get-device-info', serial)).data;
    w('# restored device ' + JSON.stringify(after));
    w('# restored setting ' + JSON.stringify((await call('app/get-setting')).data));
  } catch (e) {
    w('# error ' + (e.stack || e.message));
  }
  await sleep(1000);
  const v = await mc.drain().catch(() => '');
  if (v) w(v);
  clearInterval(pump);
  mc.close();
  try { execSync('taskkill /PID ' + mc.child.pid + ' /T /F', { stdio: 'ignore' }); } catch (e) { }
  if (doneFlag) fs.writeFileSync(doneFlag, 'done');
  w('# done');
  out.end();
  setTimeout(() => process.exit(0), 300);
})().catch(e => {
  w('# fatal ' + e.stack);
  if (doneFlag) try { fs.writeFileSync(doneFlag, 'failed'); } catch (x) { }
  out.end();
  process.exit(1);
});
