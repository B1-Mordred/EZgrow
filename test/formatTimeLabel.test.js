import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

test('uses provided timezone when supported', async () => {
  const app = await loadApp();
  const date = new Date('2024-01-01T12:00:00Z');
  const expected = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', timeZone: 'America/New_York' });
  const result = app.formatTimeLabel(date, 'America/New_York');
  assert.equal(result, expected);
});

test('falls back when timezone is unsupported', async () => {
  const app = await loadApp();
  const date = new Date('2024-01-01T12:00:00Z');
  const fallback = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  const result = app.formatTimeLabel(date, 'Invalid/Zone');
  assert.equal(result, fallback);
});
