// Writes the grey palette the Black Flame shaders use (uncompressed 32-bit DDS, 256x64).
// Columns are the greyscale input (0 = left), rows are identical. Usage: node make-gradient.js <out.dds>
var fs = require('fs');

var WIDTH = 256, HEIGHT = 64;
var RGB_LOW = 20, RGB_HIGH = 130;            // dark grey at the faint end, mid grey at the hot end
var ALPHA_RISE = [0.10, 0.45], ALPHA_FALL = [0.80, 1.00], ALPHA_END = 0.6;

function smoothstep(a, b, x) { var t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); }

function pixel(x) {
    var grey = Math.round(RGB_LOW + (RGB_HIGH - RGB_LOW) * x);
    var alpha = smoothstep(ALPHA_RISE[0], ALPHA_RISE[1], x) * (1 - (1 - ALPHA_END) * smoothstep(ALPHA_FALL[0], ALPHA_FALL[1], x));
    return [grey, grey, grey, Math.round(alpha * 255)];
}

function writeDds(path) {
    var header = Buffer.alloc(128);
    header.write('DDS ', 0, 'latin1');
    header.writeUInt32LE(124, 4);                  // header size
    header.writeUInt32LE(0x100F, 8);               // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    header.writeUInt32LE(HEIGHT, 12);
    header.writeUInt32LE(WIDTH, 16);
    header.writeUInt32LE(WIDTH * 4, 20);           // pitch
    header.writeUInt32LE(32, 76);                  // pixel format size
    header.writeUInt32LE(0x41, 80);                // RGB | ALPHAPIXELS
    header.writeUInt32LE(32, 88);                  // bit count
    header.writeUInt32LE(0x00FF0000, 92);          // R mask
    header.writeUInt32LE(0x0000FF00, 96);          // G mask
    header.writeUInt32LE(0x000000FF, 100);         // B mask
    header.writeUInt32LE(0xFF000000, 104);         // A mask
    header.writeUInt32LE(0x1000, 108);             // TEXTURE
    var body = Buffer.alloc(WIDTH * HEIGHT * 4);
    for (var y = 0; y < HEIGHT; y++) {
        for (var x = 0; x < WIDTH; x++) {
            var p = pixel(x / (WIDTH - 1)), o = (y * WIDTH + x) * 4;
            body[o] = p[2]; body[o + 1] = p[1]; body[o + 2] = p[0]; body[o + 3] = p[3];   // BGRA
        }
    }
    fs.writeFileSync(path, Buffer.concat([header, body]));
}

if (process.argv.length < 3) { console.error('usage: node make-gradient.js <out.dds>'); process.exit(1); }
writeDds(process.argv[2]);
console.log('wrote ' + process.argv[2]);
