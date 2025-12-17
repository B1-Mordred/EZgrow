import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

test('prepareHistoryDatasets maps soil readings and labels', async () => {
  const app = await loadApp();
  const points = [
    { t: 0, temp: null, hum: null, l1: 1, l2: 0, soil1: 20, soil2: 30 },
    { t: 1710000000, temp: 22.3, hum: 50, l1: 0, l2: 1, s1: 55, s2: 65 },
  ];

  const result = app.prepareHistoryDatasets(points, 'UTC', ['Alpha', 'Beta']);

  assert.equal(result.labels.length, points.length);
  assert.equal(result.labels[0], '0'); // falls back to index when timestamp missing
  assert.ok(result.labels[1].length > 0); // formatted timestamp label

  assert.deepEqual(result.temps, [null, 22.3]);
  assert.deepEqual(result.hums, [null, 50]);
  assert.deepEqual(result.light1, [1, 0]);
  assert.deepEqual(result.light2, [0, 1]);
  assert.deepEqual(result.soil1, [20, 55]); // supports soil1/s1 keys
  assert.deepEqual(result.soil2, [30, 65]);
  assert.deepEqual(result.chamberLabels, ['Alpha', 'Beta']);
});
