import test from 'node:test';
import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';

function setupDom(html = "<!doctype html><body data-page=''></body>", fetchImpl){
  const dom = new JSDOM(html, { url:"http://localhost" });
  const { window } = dom;
  window.requestAnimationFrame = cb => setTimeout(cb, 0);
  window.setInterval = () => 0;
  window.clearInterval = () => {};
  window.getComputedStyle = () => ({ getPropertyValue: () => "" });
  window.fetch = fetchImpl || (async () => ({ ok:true, json: async () => ({}) }));

  globalThis.window = window;
  globalThis.document = window.document;
  globalThis.requestAnimationFrame = window.requestAnimationFrame;
  globalThis.fetch = window.fetch;
  globalThis.getComputedStyle = window.getComputedStyle;
  globalThis.setInterval = window.setInterval;
  globalThis.clearInterval = window.clearInterval;

  const script = readFileSync(new URL('../data/app.js', import.meta.url), 'utf8');
  window.eval(script);
  window.document.dispatchEvent(new window.Event('DOMContentLoaded', { bubbles:true }));
  return dom;
}

test('deriveChamberLabels trims names and falls back to defaults', () => {
  const dom = setupDom();
  const derive = dom.window.__app.deriveChamberLabels;

  assert.deepEqual(
    derive([{ id:1, name:"  Herbs " }, { id:2, name:"" }]),
    ["Herbs", "Chamber 2"]
  );
  assert.deepEqual(derive([]), ["Chamber 1", "Chamber 2"]);
});

test('dashboard refresh populates chamber labels with fallbacks', async () => {
  const statusPayload = {
    time:"10:00:00",
    time_synced:true,
    timezone:"UTC",
    wifi:{ connected:false, mode:"AP" },
    sensors:{ temp_c:22.3, hum_rh:55, soil1:30, soil2:40 },
    chambers:[{ id:1, name:"Leafy Greens" }, { id:2, name:"" }],
    relays:{
      light1:{ state:1, auto:1, schedule:"08:00–20:00" },
      light2:{ state:0, auto:0, schedule:"20:00–06:00" },
      fan:{ state:0, auto:1 },
      pump:{ state:0, auto:1 }
    }
  };

  const dom = setupDom(
    "<!doctype html><body data-page='dashboard'><div id='lbl-s1'></div><div id='lbl-s2'></div><div id='ctl-light1-name'></div><div id='ctl-light2-name'></div></body>",
    async url => {
      if (String(url).startsWith('/api/status')) return { ok:true, json: async () => statusPayload };
      if (String(url).startsWith('/api/history')) return { ok:true, json: async () => ({ points: [] }) };
      return { ok:true, json: async () => ({}) };
    }
  );

  await new Promise(res => setTimeout(res, 10));

  const { document } = dom.window;
  assert.equal(document.querySelector('#lbl-s1').textContent, "Leafy Greens");
  assert.equal(document.querySelector('#lbl-s2').textContent, "Chamber 2");
  assert.equal(document.querySelector('#ctl-light1-name').textContent, "Leafy Greens");
  assert.equal(document.querySelector('#ctl-light2-name').textContent, "Chamber 2");
});
