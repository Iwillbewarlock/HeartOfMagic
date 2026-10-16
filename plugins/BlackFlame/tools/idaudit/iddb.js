// Address Library reader, mirrors CommonLib REL/IDDB.cpp: formats 1 and 2 (unpack_file) and 5 (dense u32 table).
// Usage: node iddb.js <file.bin> [id ...]   (no ids: print header + count)
'use strict';
var fs = require('fs');
function loadV5(b) {
  // format i32, version u32[4], name[64], pointerSize i32, dataFormat i32, offsetCount i32, then u32 offsets by id
  var p = 4, ver = [];
  for (var k = 0; k < 4; k++) { ver.push(b.readUInt32LE(p)); p += 4; }
  var name = b.toString('latin1', p, p + 64).replace(/\0.*$/, ''); p += 64;
  var ptr = b.readInt32LE(p); p += 4;
  var dataFormat = b.readInt32LE(p); p += 4;
  var count = b.readInt32LE(p); p += 4;
  if (b.length < p + count * 4) throw new Error('format 5 file truncated: ' + count + ' offsets need ' + (p + count * 4) + ' bytes, file has ' + b.length);
  var map = new Map();
  for (var id = 0; id < count; id++) {
    var off = b.readUInt32LE(p + id * 4);
    if (off !== 0) map.set(BigInt(id), BigInt(off));
  }
  return { format: 5, ver: ver.join('.'), name: name, ptr: ptr, dataFormat: dataFormat, count: count, end: p + count * 4, size: b.length, map: map };
}
function load(path) {
  var b = fs.readFileSync(path), p = 0;
  function u8() { return b[p++]; }
  function u16() { var v = b.readUInt16LE(p); p += 2; return v; }
  function u32() { var v = b.readUInt32LE(p); p += 4; return v; }
  function i32() { var v = b.readInt32LE(p); p += 4; return v; }
  function u64() { var v = b.readBigUInt64LE(p); p += 8; return v; }
  var format = i32();
  if (format === 5) return loadV5(b);
  if (format !== 1 && format !== 2) throw new Error('unsupported format ' + format);
  var ver = [i32(), i32(), i32(), i32()];
  var nameLen = i32(); var name = b.toString('latin1', p, p + nameLen); p += nameLen;
  var ptr = BigInt(i32()); var count = i32();
  var map = new Map(), prevID = 0n, prevOff = 0n, id = 0n, off = 0n;
  for (var i = 0; i < count; i++) {
    var t = u8(), lo = t & 0xF, hi = t >> 4;
    switch (lo) {
      case 0: id = u64(); break; case 1: id = prevID + 1n; break;
      case 2: id = prevID + BigInt(u8()); break; case 3: id = prevID - BigInt(u8()); break;
      case 4: id = prevID + BigInt(u16()); break; case 5: id = prevID - BigInt(u16()); break;
      case 6: id = BigInt(u16()); break; case 7: id = BigInt(u32()); break;
      default: throw new Error('bad id type ' + lo + ' at entry ' + i);
    }
    var tmp = (hi & 8) ? prevOff / ptr : prevOff;
    switch (hi & 7) {
      case 0: off = u64(); break; case 1: off = tmp + 1n; break;
      case 2: off = tmp + BigInt(u8()); break; case 3: off = tmp - BigInt(u8()); break;
      case 4: off = tmp + BigInt(u16()); break; case 5: off = tmp - BigInt(u16()); break;
      case 6: off = BigInt(u16()); break; case 7: off = BigInt(u32()); break;
    }
    if (hi & 8) off *= ptr;
    map.set(id, off); prevOff = off; prevID = id;
  }
  return { format: format, ver: ver.join('.'), name: name, ptr: Number(ptr), count: count, end: p, size: b.length, map: map };
}
module.exports = load;
if (require.main === module) {
  var db = load(process.argv[2]);
  console.log('format', db.format, 'version', db.ver, 'name', db.name, 'ptr', db.ptr, 'count', db.count, 'consumed', db.end, '/', db.size);
  process.argv.slice(3).forEach(function (s) {
    var o = db.map.get(BigInt(s));
    console.log(s, o === undefined ? 'MISSING' : ('0x' + o.toString(16)));
  });
}
