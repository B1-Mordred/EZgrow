import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

test('pushSpark clears data after repeated invalid samples', async () => {
  const app = await loadApp();
  const arr = [];

  app.pushSpark('s1', arr, 10);
  app.pushSpark('s1', arr, null);
  app.pushSpark('s1', arr, NaN);
  app.pushSpark('s1', arr, undefined);

  assert.equal(arr.length, 0);

  app.pushSpark('s1', arr, 42);
  assert.deepEqual(arr, [42]);
});

test('pushSpark trims history to the last 60 entries', async () => {
  const app = await loadApp();
  const arr = [];

  for (let i = 0; i < 65; i++) {
    app.pushSpark('s2', arr, i);
  }

  assert.equal(arr.length, 60);
  assert.equal(arr[0], 5);
  assert.equal(arr[arr.length - 1], 64);
});
