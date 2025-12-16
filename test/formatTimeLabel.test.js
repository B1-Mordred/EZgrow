import test from 'node:test';
import assert from 'node:assert/strict';
import { JSDOM } from 'jsdom';

let appPromise;

function setupDom(){
  const dom = new JSDOM(`<!doctype html><body></body>`, { url: "http://localhost" });
  global.window = dom.window;
  global.document = dom.window.document;
  global.requestAnimationFrame = cb => cb();
}

async function loadApp(){
  if (!appPromise){
    setupDom();
    const appUrl = new URL('../data/app.js', import.meta.url).href;
    appPromise = import(appUrl).then(() => window.__app);
  }
  return appPromise;
}

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
