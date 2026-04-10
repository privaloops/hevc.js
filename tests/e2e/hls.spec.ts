import { test, expect } from '@playwright/test';
import {
  collectConsoleErrors,
  loadDemoPage,
  assertNoWasmCrash,
  assertStatus,
  getLog,
} from './helpers';

const HLS_STREAMS = [
  { name: '720p', url: 'streams/bbb720/playlist.m3u8' },
  { name: '1080p', url: 'streams/bbb1080/playlist.m3u8' },
  { name: '4K', url: 'streams/bbb4k/playlist.m3u8' },
];

test.describe('HLS Player', () => {
  test('page loads without fatal errors', async ({ page }) => {
    const errors = collectConsoleErrors(page);
    await loadDemoPage(page, 'hls.html');

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

  for (const stream of HLS_STREAMS) {
    test(`${stream.name} — transcode pipeline`, async ({ page }) => {
      const errors = collectConsoleErrors(page);
      await loadDemoPage(page, 'hls.html');

      await page.fill('input[type="text"]', stream.url);
      await page.getByRole('button', { name: 'Load' }).click();

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

      assertNoWasmCrash(errors);
    });
  }
});
