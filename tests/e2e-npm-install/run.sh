#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Kill any leftover server on port 8765
lsof -ti :8765 | xargs kill 2>/dev/null || true

echo "=== e2e npm install test ==="
echo "Temp dir: $TMPDIR"

# 1. Pack both packages
echo "--- Packing @hevcjs/core..."
cd "$REPO_ROOT/packages/core"
CORE_TGZ=$(npm pack --pack-destination "$TMPDIR" 2>/dev/null | tail -1)

echo "--- Packing @hevcjs/dashjs-plugin..."
cd "$REPO_ROOT/packages/dashjs-plugin"
# Temporarily replace workspace:* with file reference
sed -i.bak 's|"@hevcjs/core": "workspace:\*"|"@hevcjs/core": "file:'"$TMPDIR/$CORE_TGZ"'"|' package.json
PLUGIN_TGZ=$(npm pack --pack-destination "$TMPDIR" 2>/dev/null | tail -1)
mv package.json.bak package.json

# 2. Create fresh project
echo "--- Creating test project..."
cd "$TMPDIR"
npm init -y > /dev/null 2>&1
npm install dashjs "$TMPDIR/$CORE_TGZ" "$TMPDIR/$PLUGIN_TGZ" > /dev/null 2>&1

# 3. Copy static assets from installed package
echo "--- Copying static assets..."
cp node_modules/@hevcjs/core/dist/transcode-worker.js .
cp node_modules/@hevcjs/core/dist/wasm/hevc-decode.js .
cp node_modules/@hevcjs/core/dist/wasm/hevc-decode.wasm .

# 4. Copy test stream
cp -r "$REPO_ROOT/demo/streams/dash_720p" "$TMPDIR/streams"

# 5. Bundle the plugin for browser use
echo "--- Bundling plugin..."
cat > entry.js << 'EOF'
export { attachHevcSupport } from '@hevcjs/dashjs-plugin';
EOF
npx esbuild entry.js --bundle --format=esm --outfile=bundle.js > /dev/null 2>&1

# 6. Create test page
cat > index.html << 'HTMLEOF'
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"><title>e2e test</title>
<script src="https://cdn.dashjs.org/v4.7.4/dash.all.min.js"></script>
<script src="hevc-decode.js"></script>
</head>
<body>
<video id="player" controls width="640"></video>
<script type="module">
import { attachHevcSupport } from './bundle.js';
const video = document.getElementById('player');
const player = dashjs.MediaPlayer().create();
await attachHevcSupport(player, {
  workerUrl: './transcode-worker.js',
  wasmUrl: './hevc-decode.js',
  forceTranscode: true,
});
player.initialize(video, './streams/manifest.mpd', true);
</script>
</body>
</html>
HTMLEOF

# 7. Start server and run Playwright test
echo "--- Files in test dir:"
ls "$TMPDIR"/index.html "$TMPDIR"/bundle.js "$TMPDIR"/transcode-worker.js "$TMPDIR"/hevc-decode.js "$TMPDIR"/hevc-decode.wasm "$TMPDIR"/streams/manifest.mpd 2>&1

echo "--- Starting server on port 8765..."
python3 -m http.server 8765 --directory "$TMPDIR" > /dev/null 2>&1 &
SERVER_PID=$!
sleep 1

echo "--- Running Playwright test..."
cd "$REPO_ROOT"
PLAYWRIGHT_PATH="$(ls -d "$REPO_ROOT"/node_modules/.pnpm/playwright-core@*/node_modules/playwright-core/index.mjs | head -1)"
PLAYWRIGHT_PATH="$PLAYWRIGHT_PATH" node "$SCRIPT_DIR/playwright-check.mjs" 2>&1
RESULT=$?

kill $SERVER_PID 2>/dev/null
exit $RESULT
