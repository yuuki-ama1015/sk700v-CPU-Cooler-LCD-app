const { Worker, isMainThread } = require('worker_threads'); const os = require('os');
if (isMainThread) {
  const n = os.cpus().length; const ms = Number(process.argv[2] || 15000);
  console.log(Date.now(), 'LOAD START', n, 'threads'); const ws = []; for (let i = 0; i < n; i++) ws.push(new Worker(__filename, { argv: [ms] }));
  let d = 0; ws.forEach(w => w.on('exit', () => { if (++d === n) console.log(Date.now(), 'LOAD END'); }));
} else {
  // tight FP + integer loop, time checked only every 2^20 iterations
  const end = Date.now() + Number(process.argv[2]);
  const a = new Float64Array(4096); for (let i = 0; i < a.length; i++) a[i] = i * 1.0001;
  let acc = 0, k = 0;
  while (true) {
    for (let j = 0; j < 1048576; j++) { const v = a[j & 4095]; acc += v * v * 1.0000001 + Math.sqrt(v + j); a[j & 4095] = acc * 1e-9; }
    if (++k % 4 === 0 && Date.now() > end) break;
  }
  if (acc === 42) console.log(acc);
}
