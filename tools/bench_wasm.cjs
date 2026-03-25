#!/usr/bin/env node
// Benchmark WASM decoder via Node.js
// Usage: node tools/bench_wasm.cjs <bitstream.265> [wasm_dir]

const fs = require('fs');
const path = require('path');
const { performance } = require('perf_hooks');

const file = process.argv[2];
const wasmDir = process.argv[3] || 'build-wasm';
if (!file) {
    console.error('Usage: node tools/bench_wasm.cjs <bitstream.265> [wasm_dir]');
    process.exit(1);
}

const wasmPath = path.resolve(__dirname, '..', wasmDir);

// Emscripten 5 with MODULARIZE: the JS file is an IIFE that assigns to
// a var AND does module.exports. But an early `return` in the userAgent
// check causes the IIFE to return undefined in Node.js.
// Workaround: eval the JS in the current context where require/process exist.
const jsCode = fs.readFileSync(path.resolve(wasmPath, 'hevc-decode.js'), 'utf8');
eval(jsCode);

// Now HEVCDecoderModule should be in scope as a global var
if (typeof HEVCDecoderModule !== 'function') {
    console.error('Failed to load HEVCDecoderModule (got ' + typeof HEVCDecoderModule + ')');
    process.exit(1);
}

const data = fs.readFileSync(file);

async function main() {
    const Module = await HEVCDecoderModule({
        locateFile: (p) => path.resolve(wasmPath, p),
    });

    console.log(`Read ${data.length} bytes from ${file}`);
    console.log(`WASM: ${wasmDir}\n`);

    const ptr = Module._malloc(data.length);
    Module.HEAPU8.set(data, ptr);

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
}

main().catch(e => { console.error(e); process.exit(1); });
