import test from 'node:test';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';
import { strict as assert } from 'node:assert';

const script = readFileSync(new URL('../data/app.js', import.meta.url), 'utf8');

function setupDom({ confirmResult = true } = {}){
  const html = `<!doctype html><body data-page="config">
    <button id="rebootBtn" data-confirm="Confirm reboot?">Reboot</button>
  </body>`;

  const dom = new JSDOM(html, { url:"http://localhost" });
  const { window } = dom;
  const calls = [];

  window.fetch = async (url, opts = {}) => {
    calls.push({ url, opts });
    if (url.includes('/api/status')){
      return { ok:true, json: async () => ({ time:"12:00:00", time_synced:true, timezone:"UTC" }) };
    }
    if (url.includes('/api/reboot')){
      return { ok:true, json: async () => ({ message:"Rebooting now" }) };
    }
    return { ok:false, status:500, statusText:"fail", json: async () => ({}) };
  };
  window.requestAnimationFrame = cb => setTimeout(cb, 0);
  window.confirm = () => confirmResult;

  globalThis.window = window;
  globalThis.document = window.document;
  globalThis.requestAnimationFrame = window.requestAnimationFrame;
  globalThis.fetch = window.fetch;
  globalThis.confirm = window.confirm;

  window.eval(script);
  window.document.dispatchEvent(new window.Event('DOMContentLoaded', { bubbles:true }));

  return { dom, calls };
}

const tick = () => new Promise(resolve => setTimeout(resolve, 0));

test('reboot button posts to /api/reboot after confirmation', async () => {
  const { dom, calls } = setupDom();
  const { document } = dom.window;
  const btn = document.querySelector('#rebootBtn');

  await tick();
  btn.click();
  await tick();

  const rebootCall = calls.find(c => c.url.includes('/api/reboot'));
  assert.ok(rebootCall, 'expected reboot API to be called');
  assert.equal(rebootCall.opts?.method, 'POST');
  assert.equal(btn.disabled, true);
  const toast = document.querySelector('.toast');
  assert.ok(toast);
  assert.match(toast.textContent, /rebooting/i);
});

test('reboot is skipped when confirmation is declined', async () => {
  const { dom, calls } = setupDom({ confirmResult:false });
  const btn = dom.window.document.querySelector('#rebootBtn');

  btn.click();
  await tick();

  assert.ok(!calls.some(c => c.url.includes('/api/reboot')));
  assert.equal(btn.disabled, false);
});
