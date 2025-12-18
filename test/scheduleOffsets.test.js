import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

const MIN = 60;

test('buildScheduleLabel adds timezone-aware deltas', async () => {
  const app = await loadApp();
  const label = app.buildScheduleLabel(6 * MIN, 18 * MIN, {
    timezoneLabel: 'UTC',
    nowMinutes: 12 * MIN,
    timeSynced: true,
  });

  assert.ok(label.includes('06:00')); // base schedule
  assert.ok(label.includes('UTC'));
  assert.ok(label.includes('on 6 h ago'));
  assert.ok(label.includes('off in 6 h'));
});

test('buildScheduleLabel handles schedules crossing midnight', async () => {
  const app = await loadApp();
  const label = app.buildScheduleLabel(22 * MIN, 6 * MIN, {
    timezoneLabel: 'UTC',
    nowMinutes: 23 * MIN,
    timeSynced: true,
  });

  assert.ok(label.includes('22:00'));
  assert.ok(label.includes('06:00'));
  assert.ok(label.includes('on 1 h ago'));
  assert.ok(label.includes('off in 7 h'));
});

test('renderPresetSchedules annotates preset tables', async () => {
  const app = await loadApp();
  document.body.innerHTML = `
    <table>
      <tr class="preset-row" data-l1-on="06:00" data-l1-off="18:00" data-l2-on="07:00" data-l2-off="19:00">
        <td class="preset-schedules"></td>
      </tr>
    </table>`;

  app.updateDeviceClockFromStatus({ time:'12:00:00', time_synced:true, timezone:'UTC' });
  app.renderPresetSchedules();

  const cellText = document.querySelector('.preset-schedules').textContent;
  assert.match(cellText, /UTC/);
  assert.match(cellText, /on 6 h ago/);
  assert.match(cellText, /off in 6 h/);
});
