/** Configurable logging for hevc.js — defaults to 'warn' in production. */

export type LogLevel = "debug" | "info" | "warn" | "error" | "silent";

const LEVELS: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
  silent: 4,
};

let currentLevel: LogLevel = "info";

export function setLogLevel(level: LogLevel): void {
  currentLevel = level;
}

export function getLogLevel(): LogLevel {
  return currentLevel;
}

export const log = {
  debug: (...args: unknown[]) => {
    if (LEVELS[currentLevel] <= LEVELS.debug) console.log("[hevc.js]", ...args);
  },
  info: (...args: unknown[]) => {
    if (LEVELS[currentLevel] <= LEVELS.info) console.log("[hevc.js]", ...args);
  },
  warn: (...args: unknown[]) => {
    if (LEVELS[currentLevel] <= LEVELS.warn) console.warn("[hevc.js]", ...args);
  },
  error: (...args: unknown[]) => {
    if (LEVELS[currentLevel] <= LEVELS.error) console.error("[hevc.js]", ...args);
  },
};
