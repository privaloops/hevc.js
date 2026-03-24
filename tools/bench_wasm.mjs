#!/usr/bin/env node
// Benchmark WASM decoder performance via Node.js
// Usage: node tools/bench_wasm.mjs <bitstream.265>

import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, resolve } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectDir = resolve(__dirname, '..');

// Load WASM module — Emscripten MODULARIZE assigns to global var
const wasmJs = readFileSync(resolve(projectDir, 'build-wasm/hevc-decode.js'), 'utf8');
const fn = new Function(wasmJs + '; return HEVCDecoderModule;');
const HEVCDecoderModule = fn();

const Module = await HEVCDecoderModule({
  locateFile: (path) => resolve(projectDir, 'build-wasm', path),
});

const file = process.argv[2];
if (!file) {
  console.error('Usage: node tools/bench_wasm.mjs <bitstream.265>');
  process.exit(1);
}

const data = readFileSync(file);
console.log(`Read ${data.length} bytes from ${file}`);

const ptr = Module._malloc(data.length);
Module.HEAPU8.set(data, ptr);

const dec = Module._hevc_decoder_create();

const t0 = performance.now();
const ret = Module._hevc_decoder_decode(dec, ptr, data.length);
const t1 = performance.now();

const frames = Module._hevc_decoder_get_frame_count(dec);
const ms = (t1 - t0).toFixed(1);
const fps = (frames / (t1 - t0) * 1000).toFixed(1);
const msFrame = ((t1 - t0) / frames).toFixed(1);

console.log(`Decoded ${frames} pictures in ${ms}ms (${fps} fps, ${msFrame} ms/frame)`);
if (ret !== 0) console.log(`(decode returned ${ret})`);

Module._hevc_decoder_destroy(dec);
Module._free(ptr);
