const playwrightPath = process.env.PLAYWRIGHT_PATH;
const { chromium } = await import(playwrightPath);

const browser = await chromium.launch();
const page = await browser.newPage();
const logs = [];
const failedRequests = [];
page.on('console', msg => logs.push(msg.text()));
page.on('requestfailed', req => failedRequests.push(req.url()));
page.on('response', res => {
  if (res.status() >= 400) failedRequests.push(`${res.status()} ${res.url()}`);
});

await page.goto('http://localhost:8765');

// Wait up to 15s for Worker transcoder ready
const deadline = Date.now() + 15000;
while (Date.now() < deadline) {
  if (logs.some(l => l.includes('Worker transcoder ready'))) break;
  await new Promise(r => setTimeout(r, 500));
}

await browser.close();

const hasReady = logs.some(l => l.includes('Worker transcoder ready'));
const hasError = logs.some(l => l.includes('[ERROR]') || l.includes('not supported'));

console.log('Console logs:');
logs.forEach(l => console.log('  ' + l));

if (failedRequests.length) {
  console.log('\nFailed requests:');
  failedRequests.forEach(r => console.log('  ' + r));
}

if (hasReady && !hasError) {
  console.log('\n=== PASS: transcoding started successfully ===');
  process.exit(0);
} else {
  console.log('\n=== FAIL: transcoding did not start ===');
  process.exit(1);
}
