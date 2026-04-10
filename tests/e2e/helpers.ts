import { type Page, expect } from '@playwright/test';

const BASE_URL = 'https://privaloops.github.io/hevc.js';

/** Collect console errors during page lifetime */
export function collectConsoleErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') errors.push(msg.text());
  });
  page.on('pageerror', (err) => {
    errors.push(`${err.name}: ${err.message}`);
  });
  return errors;
}

/** Navigate to a demo page and wait for initial load */
export async function loadDemoPage(page: Page, path: string) {
  await page.goto(`${BASE_URL}/${path}`, { waitUntil: 'networkidle' });
}

/** Click a preset button */
export async function loadPreset(page: Page, presetLabel: string) {
  await page.getByRole('button', { name: presetLabel }).click();
}

/** Check that no WASM RuntimeError occurred */
export function assertNoWasmCrash(errors: string[]) {
  const wasmErrors = errors.filter(
    (e) => e.includes('RuntimeError') || e.includes('unreachable') || e.includes('out of bounds')
  );
  expect(wasmErrors, `WASM crash detected: ${wasmErrors.join('; ')}`).toHaveLength(0);
}

/** Check status text matches expected state */
export async function assertStatus(page: Page, pattern: RegExp, timeout = 30_000) {
  await expect(page.locator('#status')).toHaveText(pattern, { timeout });
}

/** Get the log textarea content for diagnostics */
export async function getLog(page: Page): Promise<string> {
  return page.locator('#log').inputValue();
}
