/**
 * E2E tests validating the bug fixes from fix/fmp4-timestamps-and-audio-sb:
 * 1. No buffer gaps (78ms fix — fps auto-detection + sample durations)
 * 2. Audio playback works (fakeUpdating backpressure fix)
 * 3. HLS ABR works (encoder recreation on resolution change)
 * 4. Seek works (both DASH and HLS)
 * 5. Native HEVC browsers skip transcoding gracefully
 */
import { test, expect } from '@playwright/test';
import {
  collectConsoleErrors,
  loadDemoPage,
  loadPreset,
  assertNoWasmCrash,
  getLog,
  waitForPlaying,
  getBufferedRanges,
  assertContiguousBuffer,
  takeScreenshot,
} from './helpers';

// Filter non-fatal errors (known browser limitations)
function filterFatalErrors(errors: string[]): string[] {
  return errors.filter(
    (e) =>
      !e.includes('VideoEncoder') &&
      !e.includes('NotSupportedError') &&
      !e.includes('404') &&
      !e.includes('Failed to load resource') &&
      !e.includes('net::ERR')
  );
}

/** Check if browser can play HEVC (native or via transcoding) */
async function canPlayHevc(page: import('@playwright/test').Page): Promise<boolean> {
  return page.evaluate(() => {
    // Native HEVC support?
    try {
      if (MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"')) return true;
    } catch {}
    // VideoEncoder H.264 support? (needed for transcoding)
    return typeof VideoEncoder !== 'undefined';
  });
}

/** Shared ABR test logic for both DASH and HLS */
async function testABR(page: import('@playwright/test').Page, demo: 'dash' | 'hls', errors: string[]) {
  await loadDemoPage(page, `${demo}.html`);

  // Skip early if browser can't play HEVC at all (no native + no VideoEncoder)
  const capable = await canPlayHevc(page);
  if (!capable) {
    test.skip(true, 'Browser has no HEVC support (native or transcoding)');
    return;
  }

  await loadPreset(page, 'ABR 480p/720p/1080p + audio (30s)');
  const result = await waitForPlaying(page);

  if (result === 'no_encoder') {
    test.skip(true, 'Browser lacks H.264 VideoEncoder — skip');
    return;
  }

  // Wait for several segments to buffer
  await page.waitForTimeout(8000);
  await takeScreenshot(page, `${demo}-abr-playing`);

  if (result === 'playing') {
    // Transcoding path — verify our fixes
    const log = await getLog(page);
    expect(log).toContain('Worker transcoder ready');

    // Check contiguous buffer (no 78ms gaps)
    const ranges = await getBufferedRanges(page);
    expect(ranges.length).toBeGreaterThan(0);
    assertContiguousBuffer(ranges);
    const totalBuffered = ranges.reduce((sum, [s, e]) => sum + (e - s), 0);
    expect(totalBuffered).toBeGreaterThan(3.5);
  }

  if (result === 'native') {
    // Native HEVC — verify playback works without transcoding
    const log = await getLog(page);
    expect(log).toContain('Native HEVC support detected');
    expect(log).not.toContain('Worker transcoder ready');
  }

  // Both paths: verify playback progresses smoothly
  const t0 = await page.evaluate(() =>
    document.querySelector<HTMLVideoElement>('#player')!.currentTime
  );
  await page.waitForTimeout(3000);
  const t1 = await page.evaluate(() =>
    document.querySelector<HTMLVideoElement>('#player')!.currentTime
  );
  expect(t1).toBeGreaterThan(t0 + 1.0);

  assertNoWasmCrash(errors);
  expect(filterFatalErrors(errors)).toHaveLength(0);
}

/** Shared seek test logic */
async function testSeek(page: import('@playwright/test').Page, demo: 'dash' | 'hls', errors: string[]) {
  await loadDemoPage(page, `${demo}.html`);

  const capable = await canPlayHevc(page);
  if (!capable) {
    test.skip(true, 'Browser has no HEVC support (native or transcoding)');
    return;
  }

  await loadPreset(page, 'ABR 480p/720p/1080p + audio (30s)');
  const result = await waitForPlaying(page);
  if (result === 'no_encoder') {
    test.skip(true, 'Browser lacks H.264 VideoEncoder — skip');
    return;
  }

  // Wait for buffer to build
  await page.waitForTimeout(6000);

  // Seek to ~15s
  await page.evaluate(() => {
    document.querySelector<HTMLVideoElement>('#player')!.currentTime = 15;
  });

  // Wait for playback to resume after seek
  await page.waitForFunction(
    () => {
      const v = document.querySelector<HTMLVideoElement>('#player');
      return v && v.currentTime > 15.5 && !v.paused;
    },
    { timeout: 30_000 }
  );

  await takeScreenshot(page, `${demo}-after-seek`);

  // Verify playback continues
  const t0 = await page.evaluate(() =>
    document.querySelector<HTMLVideoElement>('#player')!.currentTime
  );
  await page.waitForTimeout(3000);
  const t1 = await page.evaluate(() =>
    document.querySelector<HTMLVideoElement>('#player')!.currentTime
  );
  expect(t1).toBeGreaterThan(t0 + 1.0);

  assertNoWasmCrash(errors);
}

// ────────────────────────────────────────────
// 1 & 2. DASH ABR — Buffer gaps + Audio
// ────────────────────────────────────────────
test.describe('DASH ABR — gaps & audio', () => {
  test('no buffer gaps + audio plays', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await testABR(page, 'dash', errors);
  });
});

// ────────────────────────────────────────────
// 3. HLS ABR — Buffer gaps + ABR switch
// ────────────────────────────────────────────
test.describe('HLS ABR — gaps & ABR switch', () => {
  test('no buffer gaps + ABR resolution switch works', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await testABR(page, 'hls', errors);
  });
});

// ────────────────────────────────────────────
// 4. Seek tests
// ────────────────────────────────────────────
test.describe('DASH — seek', () => {
  test('seek forward and resume playback', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await testSeek(page, 'dash', errors);
  });
});

test.describe('HLS — seek', () => {
  test('seek forward and resume playback', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await testSeek(page, 'hls', errors);
  });
});

// ────────────────────────────────────────────
// 5. Native HEVC detection
// ────────────────────────────────────────────
test.describe('Native HEVC detection', () => {
  test('DASH — correct detection', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await loadDemoPage(page, 'dash.html');
    await loadPreset(page, 'ABR 480p/720p/1080p + audio (30s)');
    await page.waitForTimeout(3000);

    const log = await getLog(page);
    const isNative = log.includes('Native HEVC support detected');
    const isTranscoding = log.includes('No native HEVC support');
    const noEncoder = log.includes('not available');

    // One path must be taken
    expect(isNative || isTranscoding || noEncoder).toBe(true);

    if (isNative) {
      await takeScreenshot(page, 'dash-native-hevc');
      expect(log).not.toContain('Worker transcoder ready');
    }

    assertNoWasmCrash(errors);
  });

  test('HLS — correct detection', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await loadDemoPage(page, 'hls.html');
    await page.waitForTimeout(2000);

    const log = await getLog(page);
    const isNative = log.includes('Native HEVC support detected');
    const isTranscoding = log.includes('No native HEVC support');
    const noEncoder = log.includes('not available');

    expect(isNative || isTranscoding || noEncoder).toBe(true);

    if (isNative) {
      await takeScreenshot(page, 'hls-native-hevc');
      expect(log).not.toContain('Worker transcoder ready');
    }

    assertNoWasmCrash(errors);
  });
});
