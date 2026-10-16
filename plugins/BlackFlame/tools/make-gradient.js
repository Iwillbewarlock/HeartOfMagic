// Writes the palettes the Black Flame shaders use (uncompressed 32-bit DDS, 256x64).
// Columns are the greyscale input (0 = left), rows are identical.
// Usage: node make-gradient.js <out.dds> [default|godskin|blackblade|messmer]
//   default     the grey palette of normal-blend palette shaders (GradBlackFlame.dds); the output with no name
//   godskin     bright-edge palettes for additive palette shaders (GradBlackFlameAdd.dds: Godskin ships there,
//   blackblade  the other two in data/optional/palettes - the colours are chosen by which file sits at that path)
//   messmer
var fs = require('fs');

var WIDTH = 256, HEIGHT = 64;
var RGB_LOW = 20, RGB_HIGH = 130;            // dark grey at the faint end, mid grey at the hot end
var ALPHA_RISE = [0.10, 0.45], ALPHA_FALL = [0.80, 1.00], ALPHA_END = 0.6;

// Stops [x, R, G, B, alpha 0..1], smoothstep between neighbours. The edge bands are wide plateaus on purpose: narrow peaks
// drew contour lines across the effect in the preview.
var PALETTES = {
    godskin: [[0, 10, 10, 14, 0], [0.12, 120, 124, 135, 0.5], [0.22, 235, 240, 250, 1], [0.42, 225, 230, 240, 1],
        [0.58, 60, 62, 70, 0.45], [0.8, 25, 25, 30, 0.3], [1, 90, 92, 100, 0.5]],
    blackblade: [[0, 8, 0, 2, 0], [0.12, 110, 8, 22, 0.5], [0.22, 225, 20, 40, 1], [0.42, 205, 16, 34, 1],
        [0.58, 40, 3, 8, 0.4], [0.8, 14, 1, 4, 0.28], [1, 95, 8, 20, 0.5]],
    messmer: [[0, 12, 2, 2, 0], [0.2, 140, 20, 12, 0.8], [0.34, 20, 4, 4, 0.25], [0.52, 200, 30, 15, 0.95],
        [0.7, 22, 5, 5, 0.3], [0.88, 220, 50, 18, 0.85], [1, 240, 80, 30, 0.7]]
};

function smoothstep(a, b, x) { var t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); }

function greyPixel(x) {
    var grey = Math.round(RGB_LOW + (RGB_HIGH - RGB_LOW) * x);
    var alpha = smoothstep(ALPHA_RISE[0], ALPHA_RISE[1], x) * (1 - (1 - ALPHA_END) * smoothstep(ALPHA_FALL[0], ALPHA_FALL[1], x));
    return [grey, grey, grey, Math.round(alpha * 255)];
}

function stopPixel(stops, x) {
    var i = 0;
    while (i < stops.length - 2 && x > stops[i + 1][0]) {
        i++;
    }
    var a = stops[i], b = stops[i + 1];
    var t = smoothstep(a[0], b[0], x);
    var out = [];
    for (var c = 1; c <= 4; c++) {
        out.push(a[c] + (b[c] - a[c]) * t);
    }
    return [Math.round(out[0]), Math.round(out[1]), Math.round(out[2]), Math.round(out[3] * 255)];
}

function pixelFor(name) {
    if (name === 'default') {
        return greyPixel;
    }
    var stops = PALETTES[name];
    return function (x) { return stopPixel(stops, x); };
}

function writeDds(path, pixel) {
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

var name = process.argv[3] || 'default';
if (process.argv.length < 3 || (name !== 'default' && !PALETTES.hasOwnProperty(name))) {
    console.error('usage: node make-gradient.js <out.dds> [default|godskin|blackblade|messmer]');
    process.exit(1);
}
writeDds(process.argv[2], pixelFor(name));
console.log('wrote ' + process.argv[2] + ' (' + name + ')');
