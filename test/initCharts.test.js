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
  const chartInstances = [];
  const originalChart = window.Chart;
  window.Chart = global.Chart = function Chart(ctx, config){
    chartCalls.push(config);
    const inst = {
      data: config.data,
      options: config.options,
      updateCalls: 0,
      update(){ this.updateCalls++; }
    };
    chartInstances.push(inst);
    return inst;
  };

  const statusResponse = {
    sensors: { temp_c: 22.5, hum_rh: 50, soil1: 40, soil2: 55 },
    wifi: { connected: true, ssid: 'net', rssi: -42, ip: '192.168.1.2' },
    timezone: 'UTC',
    time: '12:00',
    time_synced: true,
    relays: {},
    chambers: [],
    chart_scales: { temp_min: 5, temp_max: 35, hum_min: 10, hum_max: 90 },
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
  const historyUrls = [];
  global.fetch = async (url) => {
    if (url.includes('/api/status')){
      statusRequests++;
      return { ok: true, json: async () => statusResponse };
    }
    if (url.includes('/api/history')){
      historyRequests++;
      historyUrls.push(String(url));
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
    assert.equal(tempHumConfig.options.scales.y.min, 5);
    assert.equal(tempHumConfig.options.scales.y.max, 35);
    assert.equal(tempHumConfig.options.scales.y1.min, 10);
    assert.equal(tempHumConfig.options.scales.y1.max, 90);

    assert.deepEqual(soilConfig.data.datasets.map(ds => ds.label), [
      'Chamber 1 soil',
      'Chamber 2 soil',
    ]);
    assert.deepEqual(soilConfig.data.datasets[0].data, [40, 42]);
    assert.deepEqual(soilConfig.data.datasets[1].data, [55, 58]);

    assert.ok(soilConfig.data.datasets.every(ds => !/light/i.test(ds.label)));
    assert.ok(historyUrls.length >= 1);
    assert.match(historyUrls[0], /[?&]days=1\b/);
  }finally{
    document.body.innerHTML = '';
    delete document.body.dataset.page;
    window.localStorage.clear();
    window.Chart = originalChart;
    global.Chart = originalChart;
    window.setInterval = originalSetInterval;
    global.setInterval = originalGlobalSetInterval;
    window.getComputedStyle = originalGetComputedStyle;
    global.getComputedStyle = originalGetComputedStyle;
    global.fetch = originalFetch;
  }
});

test('history range selection persists and refreshes charts', async () => {
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

  const rangeSelect = document.createElement('select');
  rangeSelect.id = 'historyRange';
  ['1','2','3','4','5','6','7'].forEach(v => {
    const opt = document.createElement('option');
    opt.value = v;
    opt.textContent = `${v} days`;
    rangeSelect.appendChild(opt);
  });
  document.body.appendChild(rangeSelect);

  window.localStorage.setItem('historyRangeDays', '2');

  const chartInstances = [];
  const originalChart = window.Chart;
  window.Chart = global.Chart = function Chart(ctx, config){
    const inst = {
      data: config.data,
      options: config.options,
      updateCalls: 0,
      update(){ this.updateCalls++; },
    };
    chartInstances.push(inst);
    return inst;
  };

  const statusResponse = {
    sensors: { temp_c: 22.5, hum_rh: 50, soil1: 40, soil2: 55 },
    wifi: { connected: true, ssid: 'net', rssi: -42, ip: '192.168.1.2' },
    timezone: 'UTC',
    time: '12:00',
    time_synced: true,
    relays: {},
    chambers: [],
    chart_scales: { temp_min: 5, temp_max: 35, hum_min: 10, hum_max: 90 },
  };

  const historyResponses = [
    { points: [ { t: 1710000000, temp: 21.5, hum: 48, soil1: 38, soil2: 52 } ] },
    { points: [ { t: 1710003600, temp: 24.2, hum: 55, soil1: 47, soil2: 60 } ] },
  ];

  const originalFetch = global.fetch;
  let statusRequests = 0;
  let historyRequests = 0;
  const historyUrls = [];
  global.fetch = async (url) => {
    if (url.includes('/api/status')){
      statusRequests++;
      return { ok: true, json: async () => statusResponse };
    }
    if (url.includes('/api/history')){
      const idx = Math.min(historyRequests, historyResponses.length - 1);
      historyRequests++;
      historyUrls.push(String(url));
      return { ok: true, json: async () => historyResponses[idx] };
    }
    throw new Error(`Unexpected fetch: ${url}`);
  };

  const originalSetInterval = window.setInterval;
  const originalGlobalSetInterval = global.setInterval;
  window.setInterval = global.setInterval = () => 0;

  const originalGetComputedStyle = global.getComputedStyle;
  window.getComputedStyle = global.getComputedStyle = () => ({ getPropertyValue: () => '' });

  document.dispatchEvent(new window.Event('DOMContentLoaded'));
  for (let i = 0; i < 5 && chartInstances.length === 0; i++){
    await new Promise(resolve => setTimeout(resolve, 0));
  }

  try{
    assert.ok(statusRequests >= 1);
    assert.ok(historyRequests >= 1);
    assert.equal(chartInstances.length, 2);
    assert.deepEqual(chartInstances[0].data.datasets[0].data, [21.5]);
    assert.equal(window.localStorage.getItem('historyRangeDays'), '2');

    rangeSelect.value = '7';
    rangeSelect.dispatchEvent(new window.Event('change'));
    for (let i = 0; i < 10 && historyRequests < 2; i++){
      await new Promise(resolve => setTimeout(resolve, 0));
    }

    assert.ok(historyRequests >= 2);
    for (let i = 0; i < 5; i++){
      await new Promise(resolve => setTimeout(resolve, 0));
    }
    assert.deepEqual(chartInstances[0].data.datasets[0].data, [24.2]);
    assert.equal(window.localStorage.getItem('historyRangeDays'), '7');
    assert.ok(historyUrls.some(u => /[?&]days=7\b/.test(u)));
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
