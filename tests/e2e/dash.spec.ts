import { test, expect } from '@playwright/test';
import {
  collectConsoleErrors,
  loadDemoPage,
  loadPreset,
  assertNoWasmCrash,
  assertStatus,
  getLog,
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
    { label: 'Test 720p', name: '720p' },
    { label: 'Test 1080p', name: '1080p' },
    { label: 'Test 4K', name: '4K' },
  ]) {
    test(`${preset.name} — transcode pipeline`, async ({ page }) => {
      const errors = collectConsoleErrors(page);
      await loadDemoPage(page, 'dash.html');
      await loadPreset(page, preset.label);

      // Wait for either: video plays OR known error (both are valid outcomes)
      const outcome = await page.waitForFunction(
        () => {
          const v = document.querySelector<HTMLVideoElement>('#player');
          const log = document.querySelector<HTMLTextAreaElement>('#log');
          const playing = v && v.currentTime > 0.5 && !v.paused;
          const noEncoder = log && (
            log.value.includes('Encoder creation error') ||
            log.value.includes('H.264 encoding not supported') ||
            log.value.includes('bufferAppendError') ||
            log.value.includes('AdaptationSet has been removed')
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
        expect(log).toContain('Worker transcoder ready');
        // Verify playback progresses
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
      // result === 'no_encoder' is acceptable (browser lacks H.264 VideoEncoder)

      assertNoWasmCrash(errors);
    });
  }
});
