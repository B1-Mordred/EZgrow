import test from 'node:test';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';
import { strict as assert } from 'node:assert';

function loadApp(){
  const dom = new JSDOM("<!doctype html><body data-page=''></body>", { url:"http://localhost" });
  const { window } = dom;
  window.requestAnimationFrame = cb => cb();
  window.fetch = async () => ({ ok:true, json: async () => ({}) });
  globalThis.window = window;
  globalThis.document = window.document;
  globalThis.requestAnimationFrame = window.requestAnimationFrame;
  globalThis.fetch = window.fetch;

  const script = readFileSync(new URL('../data/app.js', import.meta.url), 'utf8');
  window.eval(script);
  window.document.dispatchEvent(new window.Event('DOMContentLoaded', { bubbles:true }));
  return window.__app;
}

test('pushSpark clears after repeated invalid values', () => {
  const app = loadApp();
  const arr = [1,2];
  app.pushSpark('temp', arr, null);
  app.pushSpark('temp', arr, NaN);
  app.pushSpark('temp', arr, undefined);
  assert.equal(arr.length, 0);
});

test('pushSpark resets invalid counter on valid input', () => {
  const app = loadApp();
  const arr = [];
  app.pushSpark('hum', arr, null);
  app.pushSpark('hum', arr, 10);
  assert.deepEqual(arr, [10]);
  app.pushSpark('hum', arr, null);
  app.pushSpark('hum', arr, null);
  assert.equal(arr.length > 0, true);
  app.pushSpark('hum', arr, null);
  assert.equal(arr.length, 0);
});
