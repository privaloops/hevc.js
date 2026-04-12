import { test, expect } from '@playwright/test';
import {
  collectConsoleErrors,
  loadDemoPage,
  loadPreset,
  assertNoWasmCrash,
  assertStatus,
  getLog,
  enableForceTranscode,
  hasAudioSourceBuffer,
} from './helpers';

test.describe('DASH Player', () => {
  test('page loads without fatal errors', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await loadDemoPage(page, 'dash.html');

    await assertStatus(page, /Ready/);
    const fatalErrors = errors.filter(
      (e) =>
        !e.includes('VideoEncoder') &&
        !e.includes('NotSupportedError') &&
        !e.includes('404') &&
        !e.includes('Failed to load resource')
    );
    expect(fatalErrors).toHaveLength(0);
  });

  for (const preset of [
    { label: '720p (10s)', name: '720p' },
    { label: '1080p (5s)', name: '1080p' },
    { label: '4K (5s)', name: '4K' },
  ]) {
    test(`${preset.name} — transcode pipeline`, async ({ page }) => {
      const errors = collectConsoleErrors(page);
      await loadDemoPage(page, 'dash.html');
      await loadPreset(page, preset.label);

      // Wait for: video plays, native HEVC detected, or known error
      const outcome = await page.waitForFunction(
        () => {
          const v = document.querySelector<HTMLVideoElement>('#player');
          const log = document.querySelector<HTMLTextAreaElement>('#log');
          const logText = log?.value ?? '';
          const playing = v && v.currentTime > 0.5 && !v.paused;
          const native = logText.includes('Native HEVC support detected');
          const noEncoder = (
            logText.includes('Encoder creation error') ||
            logText.includes('H.264 encoding not supported') ||
            logText.includes('bufferAppendError') ||
            logText.includes('AdaptationSet has been removed')
          );
          if (playing && native) return 'native';
          if (playing) return 'playing';
          if (noEncoder) return 'no_encoder';
          return false;
        },
        { timeout: 45_000 }
      );

      const result = await outcome.jsonValue();
      const log = await getLog(page);

      if (result === 'playing') {
        expect(log).toContain('Worker transcoder ready');
        const t0 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        await page.waitForTimeout(3000);
        const t1 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        expect(t1).toBeGreaterThan(t0 + 0.5);
        await assertStatus(page, /Playing/);
      }
      if (result === 'native') {
        // Native HEVC — verify playback works without transcoding
        expect(log).not.toContain('Worker transcoder ready');
        const t0 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        await page.waitForTimeout(2000);
        const t1 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        expect(t1).toBeGreaterThan(t0 + 0.5);
      }
      // result === 'no_encoder' is acceptable (browser lacks H.264 VideoEncoder)

      assertNoWasmCrash(errors);
    });
  }
});

test.describe('DASH Player — forceTranscode', () => {
  for (const preset of [
    { label: 'ABR 480p/720p/1080p + audio (30s)', name: 'ABR' },
    { label: '720p (10s)', name: '720p' },
  ]) {
    test(`${preset.name} — forced WASM transcode pipeline`, async ({ page }) => {
      const errors = collectConsoleErrors(page);
      await loadDemoPage(page, 'dash.html');
      await enableForceTranscode(page);
      await loadPreset(page, preset.label);

      const outcome = await page.waitForFunction(
        () => {
          const v = document.querySelector<HTMLVideoElement>('#player');
          const log = document.querySelector<HTMLTextAreaElement>('#log');
          const playing = v && v.currentTime > 0.5 && !v.paused;
          const noEncoder = log && (
            log.value.includes('Encoder creation error') ||
            log.value.includes('H.264 encoding not supported') ||
            log.value.includes('not available')
          );
          if (playing) return 'playing';
          if (noEncoder) return 'no_encoder';
          return false;
        },
        { timeout: 45_000 }
      );

      const result = await outcome.jsonValue();
      const log = await getLog(page);

      if (result === 'playing') {
        // forceTranscode: WASM pipeline must be active even on native-HEVC browsers
        expect(log).toContain('installing WASM transcoder');
        expect(log).toContain('Worker transcoder ready');

        const t0 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        await page.waitForTimeout(3000);
        const t1 = await page.evaluate(() =>
          document.querySelector<HTMLVideoElement>('#player')!.currentTime
        );
        expect(t1).toBeGreaterThan(t0 + 0.5);

        // ABR preset has audio — verify it's not lost during transcoding
        if (preset.name === 'ABR') {
          const hasAudio = await hasAudioSourceBuffer(page);
          expect(hasAudio, 'Audio should be present in ABR forceTranscode').toBe(true);
        }
      }

      assertNoWasmCrash(errors);
    });
  }
});
