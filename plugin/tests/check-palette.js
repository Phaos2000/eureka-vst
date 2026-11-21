// Checks that every pixel of every PNG in a folder is one of the 16 palette
// colours of the pixel faceplate. Usage: node check-palette.js <folder>
// Decodes PNGs itself (zlib from Node), 8-bit RGB / RGBA / greyscale /
// palette, non-interlaced.
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");

const PALETTE = [
  "000000", "101418", "2a2a2e", "4a4a50", "808084", "c0c0c0", "ffffff",
  "0f2e28", "2f8f78", "5ff0c8", "5a1008", "ff3a2a", "4e2810", "8c4c22",
  "d8b04a", "000080",
];
const allowed = new Set(PALETTE.map((h) => parseInt(h, 16)));

function decode(file) {
  const buf = fs.readFileSync(file);
  let pos = 8, width, height, depth, colourType, interlace, plte = null;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString("ascii", pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === "IHDR") {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      depth = data[8]; colourType = data[9]; interlace = data[12];
    } else if (type === "PLTE") plte = data;
    else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    pos += 12 + len;
  }
  if (depth !== 8 || interlace !== 0) throw new Error(`unsupported PNG (depth ${depth}, interlace ${interlace})`);
  const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colourType];
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(stride * height);
  let prev = Buffer.alloc(stride);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    const cur = out.subarray(y * stride, (y + 1) * stride);
    for (let i = 0; i < stride; i++) {
      const a = i >= channels ? cur[i - channels] : 0, b = prev[i], c = i >= channels ? prev[i - channels] : 0;
      let v = line[i];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[i] = v & 255;
    }
    prev = cur;
  }
  const rgbAt = (i) => {
    const o = i * channels;
    if (colourType === 3) return plte.readUIntBE(out[o] * 3, 3);
    if (colourType === 0 || colourType === 4) return out[o] * 0x10101;
    return (out[o] << 16) | (out[o + 1] << 8) | out[o + 2];
  };
  const alphaAt = (i) => (colourType === 6 ? out[i * 4 + 3] : colourType === 4 ? out[i * 2 + 1] : 255);
  return { width, height, rgbAt, alphaAt };
}

const dir = process.argv[2] || path.join(__dirname, "snapshots");
const files = fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith(".png")).sort();
let bad = 0;
for (const f of files) {
  const img = decode(path.join(dir, f));
  const used = new Set();
  const stray = new Map();
  for (let i = 0; i < img.width * img.height; i++) {
    const rgb = img.rgbAt(i);
    if (img.alphaAt(i) !== 255 || !allowed.has(rgb)) {
      const key = rgb.toString(16).padStart(6, "0") + (img.alphaAt(i) !== 255 ? "@a" + img.alphaAt(i) : "");
      stray.set(key, (stray.get(key) || 0) + 1);
    } else used.add(rgb);
  }
  if (stray.size) {
    bad++;
    const list = [...stray].slice(0, 5).map(([k, n]) => `#${k} x${n}`).join(", ");
    console.log(`FAIL ${f} ${img.width}x${img.height}: ${stray.size} colours outside the palette (${list})`);
  } else console.log(`ok   ${f} ${img.width}x${img.height}: ${used.size} palette colours`);
}
console.log(bad ? `${bad} of ${files.length} files FAIL` : `all ${files.length} files use only the 16 palette colours`);
process.exit(bad || files.length === 0 ? 1 : 0);
