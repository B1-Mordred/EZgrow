import test from 'node:test';
import assert from 'node:assert/strict';
import { loadApp } from './helpers/appLoader.js';

test('initCharts renders temp/humidity and soil history without light datasets', async () => {
  await loadApp();

  const { document, window } = global;
  document.body.dataset.page = 'dashboard';

  const tempCanvas = document.createElement('canvas');
  tempCanvas.id = 'tempHumChart';
  tempCanvas.getContext = () => ({});
  document.body.appendChild(tempCanvas);

  const soilCanvas = document.createElement('canvas');
  soilCanvas.id = 'soilChart';
  soilCanvas.getContext = () => ({});
  document.body.appendChild(soilCanvas);

  const chartCalls = [];
  const originalChart = window.Chart;
  window.Chart = global.Chart = function Chart(ctx, config){
    chartCalls.push(config);
    return {};
  };

  const statusResponse = {
    sensors: { temp_c: 22.5, hum_rh: 50, soil1: 40, soil2: 55 },
    wifi: { connected: true, ssid: 'net', rssi: -42, ip: '192.168.1.2' },
    timezone: 'UTC',
    time: '12:00',
    time_synced: true,
    relays: {},
    chambers: [],
  };

  const historyResponse = {
    points: [
      { t: 1710000000, temp: 22.5, hum: 50, soil1: 40, soil2: 55, l1: 1, l2: 0 },
      { t: 1710003600, temp: 23.1, hum: 52, soil1: 42, soil2: 58, l1: 0, l2: 1 },
    ],
  };

  const originalFetch = global.fetch;
  let statusRequests = 0;
  let historyRequests = 0;
  global.fetch = async (url) => {
    if (url.includes('/api/status')){
      statusRequests++;
      return { ok: true, json: async () => statusResponse };
    }
    if (url.includes('/api/history')){
      historyRequests++;
      return { ok: true, json: async () => historyResponse };
    }
    throw new Error(`Unexpected fetch: ${url}`);
  };

  const originalSetInterval = window.setInterval;
  const originalGlobalSetInterval = global.setInterval;
  window.setInterval = global.setInterval = () => 0;

  const originalGetComputedStyle = global.getComputedStyle;
  window.getComputedStyle = global.getComputedStyle = () => ({ getPropertyValue: () => '' });

  document.dispatchEvent(new window.Event('DOMContentLoaded'));
  for (let i = 0; i < 5 && chartCalls.length === 0; i++){
    await new Promise(resolve => setTimeout(resolve, 0));
  }

  try{
    assert.ok(statusRequests >= 1);
    assert.ok(historyRequests >= 1);
    assert.equal(chartCalls.length, 2);
    const [tempHumConfig, soilConfig] = chartCalls;

    assert.deepEqual(tempHumConfig.data.datasets.map(ds => ds.label), [
      'Temperature (°C)',
      'Humidity (%)',
    ]);
    assert.deepEqual(tempHumConfig.data.datasets[0].data, [22.5, 23.1]);
    assert.deepEqual(tempHumConfig.data.datasets[1].data, [50, 52]);

    assert.deepEqual(soilConfig.data.datasets.map(ds => ds.label), [
      'Chamber 1 soil',
      'Chamber 2 soil',
    ]);
    assert.deepEqual(soilConfig.data.datasets[0].data, [40, 42]);
    assert.deepEqual(soilConfig.data.datasets[1].data, [55, 58]);

    assert.ok(soilConfig.data.datasets.every(ds => !/light/i.test(ds.label)));
  }finally{
    document.body.innerHTML = '';
    delete document.body.dataset.page;
    window.Chart = originalChart;
    global.Chart = originalChart;
    window.setInterval = originalSetInterval;
    global.setInterval = originalGlobalSetInterval;
    window.getComputedStyle = originalGetComputedStyle;
    global.getComputedStyle = originalGetComputedStyle;
    global.fetch = originalFetch;
  }
});
