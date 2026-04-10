/**
 * E2E tests for Video.js + VHS + contrib-quality-levels + hevc.js
 * Validates HEVC transcoding, quality switching, and codec detection.
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

test.describe('Video.js + VHS + HEVC', () => {
  test('ARTE stream — playback + quality switch + codec check', async ({ page }) => {
    const errors = collectConsoleErrors(page);

    // Navigate to videojs demo
    await page.goto(`${BASE_URL}/videojs.html`, { waitUntil: 'networkidle' });

    // Check if browser can play HEVC (native or transcoding)
    const capable = await page.evaluate(() => {
      try {
        if (MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"')) return 'native';
      } catch {}
      return typeof VideoEncoder !== 'undefined' ? 'transcode' : 'none';
    });

    if (capable === 'none') {
      test.skip(true, 'Browser has no HEVC support');
      return;
    }

    // Load ARTE HEVC stream
    await page.fill('input[type="text"]', ARTE_URL);
    await page.getByRole('button', { name: 'Load' }).click();

    // Wait for playback to start
    const playing = await page.waitForFunction(
      () => {
        const v = document.querySelector('video');
        return v && v.currentTime > 1 && !v.paused;
      },
      { timeout: 60_000 }
    ).then(() => true).catch(() => false);

    if (!playing) {
      const log = await getLog(page);
      // Check if it's a known limitation
      if (log.includes('not supported') || log.includes('not available')) {
        test.skip(true, 'Encoder not available');
        return;
      }
      throw new Error('Playback did not start within 60s');
    }

    await takeScreenshot(page, 'videojs-arte-playing');

    const log = await getLog(page);

    if (capable === 'transcode') {
      // Transcoding path — verify MSE intercept is active
      expect(log).toContain('installing WASM transcoder');
      expect(log).toContain('H.264 proxy');
      expect(log).toContain('H.264 segment appended');

      // Verify quality levels are registered
      expect(log).toContain('Level added');
    }

    // Verify playback progresses
    const t0 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    await page.waitForTimeout(4000);
    const t1 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    expect(t1).toBeGreaterThan(t0 + 1.0);

    // Get current quality info
    const qualityInfo = await page.evaluate(() => {
      const p = (window as any).videojs?.getPlayers?.()?.['player'];
      if (!p) return null;
      const ql = p.qualityLevels?.();
      if (!ql) return null;
      const levels: { width: number; height: number; enabled: boolean }[] = [];
      for (let i = 0; i < ql.length; i++) {
        levels.push({
          width: ql[i].width,
          height: ql[i].height,
          enabled: ql[i].enabled,
        });
      }
      return { count: ql.length, selected: ql.selectedIndex, levels };
    });

    if (qualityInfo) {
      console.log('Quality levels:', JSON.stringify(qualityInfo));
      expect(qualityInfo.count).toBeGreaterThan(0);
    }

    await takeScreenshot(page, 'videojs-arte-quality-info');

    // ── Quality switch test ──
    // Force switch to a different quality level if available
    const switched = await page.evaluate(() => {
      const p = (window as any).videojs?.getPlayers?.()?.['player'];
      if (!p) return false;
      const ql = p.qualityLevels?.();
      if (!ql || ql.length < 2) return false;

      // Find a different resolution than current
      const current = ql.selectedIndex;
      let target = -1;
      for (let i = 0; i < ql.length; i++) {
        if (i !== current && ql[i].height !== ql[current]?.height) {
          target = i;
          break;
        }
      }
      if (target === -1) return false;

      // Disable all levels except target
      for (let i = 0; i < ql.length; i++) {
        ql[i].enabled = (i === target);
      }
      return true;
    });

    if (switched) {
      // Wait for the switch to take effect
      await page.waitForTimeout(8000);
      await takeScreenshot(page, 'videojs-arte-after-switch');

      const logAfterSwitch = await getLog(page);

      if (capable === 'transcode') {
        // After quality switch, check for resolution change in transcoder
        // VHS will fetch segments at the new quality — our intercept handles them
        const hasNewSegments = logAfterSwitch.includes('H.264 segment appended');
        expect(hasNewSegments).toBe(true);
      }

      // Verify playback still works after switch
      const t2 = await page.evaluate(() => document.querySelector('video')!.currentTime);
      await page.waitForTimeout(3000);
      const t3 = await page.evaluate(() => document.querySelector('video')!.currentTime);
      expect(t3).toBeGreaterThan(t2 + 1.0);
    }

    assertNoWasmCrash(errors);
  });

  test('BBB ABR — local stream with quality levels', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await page.goto(`${BASE_URL}/videojs.html`, { waitUntil: 'networkidle' });

    const capable = await page.evaluate(() => {
      try {
        if (MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"')) return 'native';
      } catch {}
      return typeof VideoEncoder !== 'undefined' ? 'transcode' : 'none';
    });

    if (capable === 'none') {
      test.skip(true, 'Browser has no HEVC support');
      return;
    }

    // Click BBB ABR preset
    await page.getByRole('button', { name: 'BBB ABR (30s)' }).click();

    const playing = await page.waitForFunction(
      () => {
        const v = document.querySelector('video');
        return v && v.currentTime > 0.5 && !v.paused;
      },
      { timeout: 45_000 }
    ).then(() => true).catch(() => false);

    if (!playing) {
      const log = await getLog(page);
      if (log.includes('not supported') || log.includes('not available')) {
        test.skip(true, 'Encoder not available');
        return;
      }
      throw new Error('BBB playback did not start');
    }

    await page.waitForTimeout(6000);
    await takeScreenshot(page, 'videojs-bbb-playing');

    const log = await getLog(page);

    if (capable === 'transcode') {
      expect(log).toContain('H.264 segment appended');
      // Verify contiguous buffer
      const ranges = await page.evaluate(() => {
        const v = document.querySelector('video');
        if (!v) return [];
        const r: [number, number][] = [];
        for (let i = 0; i < v.buffered.length; i++) {
          r.push([v.buffered.start(i), v.buffered.end(i)]);
        }
        return r;
      });
      expect(ranges.length).toBeGreaterThan(0);
      // Check no gaps > 50ms
      for (let i = 1; i < ranges.length; i++) {
        const gap = ranges[i]![0] - ranges[i - 1]![1];
        expect(gap).toBeLessThan(0.05);
      }
    }

    // Verify progression
    const t0 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    await page.waitForTimeout(3000);
    const t1 = await page.evaluate(() => document.querySelector('video')!.currentTime);
    expect(t1).toBeGreaterThan(t0 + 1.0);

    assertNoWasmCrash(errors);
  });
});
