import test from 'node:test';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';
import { strict as assert } from 'node:assert';

function setupDom(html = "<!doctype html><body data-page=''></body>"){
  const dom = new JSDOM(html, { url:"http://localhost" });
  const { window } = dom;
  window.requestAnimationFrame = cb => setTimeout(cb, 0);
  window.fetch = async () => ({ ok:true, json: async () => ({}) });

  globalThis.window = window;
  globalThis.document = window.document;
  globalThis.requestAnimationFrame = window.requestAnimationFrame;
  globalThis.fetch = window.fetch;

  const script = readFileSync(new URL('../data/app.js', import.meta.url), 'utf8');
  window.eval(script);
  window.document.dispatchEvent(new window.Event('DOMContentLoaded', { bubbles:true }));
  return dom;
}

test('withRelayGuard disables controls during requests and restores prior state', async () => {
  const dom = setupDom();
  const { document } = dom.window;

  const segAuto = document.createElement('button');
  segAuto.id = 'seg-light1-auto';
  const segMan = document.createElement('button');
  segMan.id = 'seg-light1-man';
  const tog = document.createElement('button');
  tog.id = 'tog-light1';
  tog.disabled = true;

  document.body.append(segAuto, segMan, tog);

  const guard = dom.window.__app.withRelayGuard;
  const during = [];

  await guard('light1', async () => {
    during.push([segAuto.disabled, segMan.disabled, tog.disabled]);
    return 'ok';
  });

  assert.deepEqual(during, [[true, true, true]]);
  assert.equal(segAuto.disabled, false);
  assert.equal(segMan.disabled, false);
  assert.equal(tog.disabled, true);
  assert.equal(segAuto.hasAttribute('aria-busy'), false);
  assert.equal(segMan.hasAttribute('aria-busy'), false);
  assert.equal(tog.hasAttribute('aria-busy'), false);
});

test('withRelayGuard surfaces errors with toasts and restores controls', async () => {
  const dom = setupDom();
  const { document } = dom.window;

  const segAuto = document.createElement('button');
  segAuto.id = 'seg-light2-auto';
  const segMan = document.createElement('button');
  segMan.id = 'seg-light2-man';
  const tog = document.createElement('button');
  tog.id = 'tog-light2';

  document.body.append(segAuto, segMan, tog);

  const guard = dom.window.__app.withRelayGuard;
  let error;
  try{
    await guard('light2', async () => {
      throw new Error('boom');
    }, { errorMessage:'Toggle failed' });
  }catch(e){
    error = e;
  }

  assert.ok(error instanceof Error);
  assert.equal(segAuto.disabled, false);
  assert.equal(segMan.disabled, false);
  assert.equal(tog.disabled, false);
  const toastEl = document.querySelector('.toast');
  assert.ok(toastEl);
  assert.ok(toastEl.textContent.includes('Toggle failed: boom'));
});

test('withRelayGuard tolerates missing controls', async () => {
  const dom = setupDom("<!doctype html><body data-page=''></body>");
  let ran = false;
  await dom.window.__app.withRelayGuard('pump', async () => {
    ran = true;
    return 'ok';
  });
  assert.equal(ran, true);
});
