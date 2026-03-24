import { defineConfig } from "tsup";

export default defineConfig([
  {
    entry: ["src/index.ts"],
    format: ["esm"],
    dts: true,
    clean: true,
    sourcemap: true,
    outDir: "dist",
    external: ["./wasm/hevc-decode.js", "mp4box"],
  },
  {
    entry: ["src/transcode-worker.ts"],
    format: ["esm"],
    dts: false,
    clean: false,
    sourcemap: true,
    outDir: "dist",
    external: ["mp4box"],
  },
]);
