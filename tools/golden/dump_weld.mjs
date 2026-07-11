// Golden dump: run the reference weldVertices on a deterministic fixture and
// write inputs + outputs for the C++ harness to compare against.
// Usage: node tools/golden/dump_weld.mjs
import { weldVertices } from '../../reference/js/meshIndex.js';
import { writeFileSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const outDir = join(dirname(fileURLToPath(import.meta.url)), 'out');
mkdirSync(outDir, { recursive: true });

// Deterministic 32-bit LCG (same constants in the C++ side).
let seed = 0x12345678 >>> 0;
function rnd() {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  return seed / 4294967296;
}

// 100k points in a 30mm cube, quantized to 0.01mm steps so grid collisions
// occur, plus float noise below all grids, plus exact duplicates.
const N = 100000;
const pos = new Float32Array(N * 3);
for (let i = 0; i < N; i++) {
  if (i % 7 === 3 && i > 0) {
    // exact duplicate of an earlier point
    const src = Math.floor(rnd() * i);
    pos[i * 3] = pos[src * 3];
    pos[i * 3 + 1] = pos[src * 3 + 1];
    pos[i * 3 + 2] = pos[src * 3 + 2];
    continue;
  }
  for (let c = 0; c < 3; c++) {
    const base = Math.round((rnd() * 30 - 15) * 100) / 100; // 0.01mm grid
    const noise = (rnd() - 0.5) * 2e-6;                     // sub-grid noise
    pos[i * 3 + c] = base + noise;
  }
}

writeFileSync(join(outDir, 'weld_input.f32'), Buffer.from(pos.buffer));

for (const quant of [1e4, 1e5, 1e6]) {
  const { vertexId, uniqueCount } = weldVertices(pos, N, quant);
  const out = Buffer.alloc(4 + vertexId.length * 4);
  out.writeUInt32LE(uniqueCount, 0);
  Buffer.from(vertexId.buffer).copy(out, 4);
  writeFileSync(join(outDir, `weld_${quant.toExponential(0).replace('+','')}.bin`), out);
  console.log(`quant ${quant}: uniqueCount=${uniqueCount}`);
}
console.log('weld golden written');
