// Minimal asar extractor: node asar_extract.js <asar> <outdir> [filterRegex]
const fs = require('fs'), path = require('path');
const [,, asarPath, outDir, filt] = process.argv;
const fd = fs.openSync(asarPath, 'r');
const b = Buffer.alloc(16); fs.readSync(fd, b, 0, 16, 0);
const headerSize = b.readUInt32LE(4);          // pickle size
const jsonLen = b.readUInt32LE(12);
const hb = Buffer.alloc(jsonLen); fs.readSync(fd, hb, 0, jsonLen, 16);
const header = JSON.parse(hb.toString('utf8'));
const base = 8 + headerSize;
const re = filt ? new RegExp(filt) : null;
let n = 0, list = [];
function walk(node, rel) {
  for (const [name, e] of Object.entries(node.files || {})) {
    const p = rel ? rel + '/' + name : name;
    if (e.files) { walk(e, p); continue; }
    list.push(p + '\t' + (e.size||0) + (e.unpacked ? '\tUNPACKED' : ''));
    if (e.unpacked || (re && !re.test(p))) continue;
    const buf = Buffer.alloc(e.size);
    if (e.size) fs.readSync(fd, buf, 0, e.size, base + Number(e.offset));
    const op = path.join(outDir, p); fs.mkdirSync(path.dirname(op), {recursive: true}); fs.writeFileSync(op, buf); n++;
  }
}
walk(header, '');
fs.writeFileSync(path.join(outDir, '_filelist.txt'), list.join('\n'));
console.log('extracted', n, 'of', list.length);
