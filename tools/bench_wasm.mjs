#!/usr/bin/env node
// Benchmark WASM decoder performance via Node.js
// Usage: node tools/bench_wasm.mjs <bitstream.265> [wasm_dir]
// Default wasm_dir: build-wasm

import { readFileSync } from 'fs';
import { fileURLToPath, pathToFileURL } from 'url';
import { dirname, resolve } from 'path';
import vm from 'vm';

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectDir = resolve(__dirname, '..');

const file = process.argv[2];
const wasmDir = process.argv[3] || 'build-wasm';
if (!file) {
    console.error('Usage: node tools/bench_wasm.mjs <bitstream.265> [wasm_dir]');
    process.exit(1);
}

const wasmPath = resolve(projectDir, wasmDir);

// Dynamic import of the Emscripten module
const modulePath = pathToFileURL(resolve(wasmPath, 'hevc-decode.js')).href;

// For Emscripten MODULARIZE: the .js file sets a global var.
// We use dynamic import with a data URL trick for CommonJS compatibility.
const jsCode = readFileSync(resolve(wasmPath, 'hevc-decode.js'), 'utf8');

// Run in a context that has Node.js globals
const script = new vm.Script(jsCode + '\nHEVCDecoderModule;', {
    filename: resolve(wasmPath, 'hevc-decode.js'),
});

// Create a context with all required Node.js globals
const context = vm.createContext({
    ...globalThis,
    __filename: resolve(wasmPath, 'hevc-decode.js'),
    __dirname: wasmPath,
    require: (await import('module')).createRequire(resolve(wasmPath, 'hevc-decode.js')),
    process,
    console,
    URL,
    Buffer,
    WebAssembly,
    globalThis: {
        ...globalThis,
        process,
        WebAssembly,
    },
    performance,
    setTimeout,
});

const HEVCDecoderModule = script.runInContext(context);

const Module = await HEVCDecoderModule({
    locateFile: (path) => resolve(wasmPath, path),
});

const data = readFileSync(file);
console.log(`Read ${data.length} bytes from ${file}`);
console.log(`WASM: ${wasmDir}\n`);

const ptr = Module._malloc(data.length);
Module.HEAPU8.set(data, ptr);

// Run 3 times, take best
const results = [];
for (let run = 0; run < 3; run++) {
    const dec = Module._hevc_decoder_create();

    const t0 = performance.now();
    Module._hevc_decoder_decode(dec, ptr, data.length);
    const t1 = performance.now();

    const frames = Module._hevc_decoder_get_frame_count(dec);
    const ms = t1 - t0;
    const fps = (frames / ms) * 1000;
    results.push({ frames, ms, fps });

    console.log(`  Run ${run + 1}: ${frames} frames in ${ms.toFixed(1)}ms (${fps.toFixed(1)} fps, ${(ms / frames).toFixed(1)} ms/frame)`);

    Module._hevc_decoder_destroy(dec);
}

const best = results.reduce((a, b) => (a.ms < b.ms ? a : b));
const avg = results.reduce((a, b) => a + b.ms, 0) / results.length;
console.log(`\n  Best: ${best.fps.toFixed(1)} fps (${best.ms.toFixed(1)}ms)`);
console.log(`  Avg:  ${(best.frames / avg * 1000).toFixed(1)} fps (${avg.toFixed(1)}ms)`);

Module._free(ptr);
