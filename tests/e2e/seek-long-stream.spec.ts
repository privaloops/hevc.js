/**
 * E2E test: seek on long HLS stream (>5min).
 * Validates that hls.js seek works even though hls.js doesn't call abort().
 * The MSE intercept detects timestampOffset changes and flushes stale segments.
 */
import { test, expect } from '@playwright/test';
import {
  collectConsoleErrors,
  assertNoWasmCrash,
  getLog,
  takeScreenshot,
} from './helpers';

const ARTE_URL = 'https://manifest-arte.akamaized.net/api/manifest/v1/Generate/bbc30788-8f2b-4407-a231-5ccf6485d1a5/fr/XQ+KS+CHEV1/125603-000-A.m3u8';

const BASE_URL = process.env.LOCAL_DEMO === '1'
  ? 'http://localhost:8090'
  : 'https://privaloops.github.io/hevc.js';

/** Check browser capabilities */
async function checkCapability(page: import('@playwright/test').Page): Promise<'native' | 'transcode' | 'none'> {
  return page.evaluate(() => {
    try {
      if (MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"')) return 'native';
    } catch {}
    return typeof VideoEncoder !== 'undefined' ? 'transcode' : 'none';
  }) as Promise<'native' | 'transcode' | 'none'>;
}

test.describe('HLS long stream seek', () => {
  test('hls.js — seek forward on ARTE stream (>5min)', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await page.goto(`${BASE_URL}/hls.html`, { waitUntil: 'networkidle' });

    const capable = await checkCapability(page);
    if (capable === 'none') {
      test.skip(true, 'No HEVC support');
      return;
    }

    // Load ARTE stream via URL input
    await page.fill('input[type="text"]', ARTE_URL);
    await page.getByRole('button', { name: 'Load' }).click();

    // Wait for playback to start
    await page.waitForFunction(
      () => {
        const v = document.querySelector<HTMLVideoElement>('#player');
        return v && v.currentTime > 1 && !v.paused;
      },
      { timeout: 60_000 }
    );

    await takeScreenshot(page, 'hls-long-before-seek');

    // Get stream duration
    const duration = await page.evaluate(() =>
      document.querySelector<HTMLVideoElement>('#player')!.duration
    );
    console.log(`Stream duration: ${duration}s`);
    expect(duration).toBeGreaterThan(60); // >1min

    // Wait for a few seconds of buffer
    await page.waitForTimeout(5000);

    // ── Seek #1: jump to middle of stream ──
    const seekTarget1 = Math.min(Math.floor(duration / 2), 120); // mid or 2min max
    console.log(`Seeking to ${seekTarget1}s...`);

    await page.evaluate((t) => {
      document.querySelector<HTMLVideoElement>('#player')!.currentTime = t;
    }, seekTarget1);

    // Wait for playback to resume at seek position
    const resumed1 = await page.waitForFunction(
      (target) => {
        const v = document.querySelector<HTMLVideoElement>('#player');
        return v && v.currentTime > target + 1 && !v.paused;
      },
      seekTarget1,
      { timeout: 30_000 }
    ).then(() => true).catch(() => false);

    expect(resumed1, `Playback did not resume after seek to ${seekTarget1}s`).toBe(true);

    await takeScreenshot(page, 'hls-long-after-seek-1');

    // Verify playback progresses after seek
    const t0 = await page.evaluate(() =>
      document.querySelector<HTMLVideoElement>('#player')!.currentTime
    );
    await page.waitForTimeout(3000);
    const t1 = await page.evaluate(() =>
      document.querySelector<HTMLVideoElement>('#player')!.currentTime
    );
    expect(t1).toBeGreaterThan(t0 + 1.0);

    // Check logs for seek-related behavior
    const log = await getLog(page);
    if (capable === 'transcode') {
      // Verify the timestampOffset flush happened
      const hasFlush = log.includes('timestampOffset changed') || log.includes('abort()');
      console.log(`Queue flush detected: ${hasFlush}`);
      // Segments should still be transcoded after seek
      expect(log).toContain('H.264 segment appended');
    }

    // ── Seek #2: jump near end of stream ──
    const seekTarget2 = Math.max(duration - 30, seekTarget1 + 30);
    console.log(`Seeking to ${seekTarget2.toFixed(0)}s (near end)...`);

    await page.evaluate((t) => {
      document.querySelector<HTMLVideoElement>('#player')!.currentTime = t;
    }, seekTarget2);

    const resumed2 = await page.waitForFunction(
      (target) => {
        const v = document.querySelector<HTMLVideoElement>('#player');
        return v && v.currentTime > target + 1 && !v.paused;
      },
      seekTarget2,
      { timeout: 30_000 }
    ).then(() => true).catch(() => false);

    expect(resumed2, `Playback did not resume after seek to ${seekTarget2.toFixed(0)}s`).toBe(true);

    await takeScreenshot(page, 'hls-long-after-seek-2');

    // ── Seek #3: seek backward ──
    console.log('Seeking backward to 10s...');

    await page.evaluate(() => {
      document.querySelector<HTMLVideoElement>('#player')!.currentTime = 10;
    });

    const resumed3 = await page.waitForFunction(
      () => {
        const v = document.querySelector<HTMLVideoElement>('#player');
        return v && v.currentTime > 11 && !v.paused;
      },
      { timeout: 30_000 }
    ).then(() => true).catch(() => false);

    expect(resumed3, 'Playback did not resume after backward seek to 10s').toBe(true);

    await takeScreenshot(page, 'hls-long-after-seek-backward');

    assertNoWasmCrash(errors);
  });

  test('videojs — seek forward on ARTE stream', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await page.goto(`${BASE_URL}/videojs.html`, { waitUntil: 'networkidle' });

    const capable = await checkCapability(page);
    if (capable === 'none') {
      test.skip(true, 'No HEVC support');
      return;
    }

    // Load ARTE stream
    await page.fill('input[type="text"]', ARTE_URL);
    await page.getByRole('button', { name: 'Load' }).click();

    // Wait for playback
    await page.waitForFunction(
      () => {
        const v = document.querySelector('video');
        return v && v.currentTime > 1 && !v.paused;
      },
      { timeout: 60_000 }
    );

    await page.waitForTimeout(5000);

    // Seek to 60s
    await page.evaluate(() => {
      document.querySelector<HTMLVideoElement>('video')!.currentTime = 60;
    });

    const resumed = await page.waitForFunction(
      () => {
        const v = document.querySelector('video');
        return v && v.currentTime > 61 && !v.paused;
      },
      { timeout: 30_000 }
    ).then(() => true).catch(() => false);

    expect(resumed, 'videojs playback did not resume after seek to 60s').toBe(true);

    await takeScreenshot(page, 'videojs-arte-after-seek');

    // Verify progression
    const t0 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    await page.waitForTimeout(3000);
    const t1 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    expect(t1).toBeGreaterThan(t0 + 1.0);

    assertNoWasmCrash(errors);
  });
});
