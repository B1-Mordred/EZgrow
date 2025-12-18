import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

test('resolveChartScales clamps to defaults when ranges are invalid', async () => {
  const app = await loadApp();
  const { resolveChartScales, defaultChartScales } = app;

  const invalid = resolveChartScales({ temp_min: 30, temp_max: 20, hum_min: 80, hum_max: 40 });
  assert.equal(invalid.tempMin, defaultChartScales.tempMin);
  assert.equal(invalid.tempMax, defaultChartScales.tempMax);
  assert.equal(invalid.humMin, defaultChartScales.humMin);
  assert.equal(invalid.humMax, defaultChartScales.humMax);

  const valid = resolveChartScales({ temp_min: 12.5, temp_max: 34, hum_min: 5, hum_max: 95 });
  assert.equal(valid.tempMin, 12.5);
  assert.equal(valid.tempMax, 34);
  assert.equal(valid.humMin, 5);
  assert.equal(valid.humMax, 95);
});

