import test from 'node:test';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';
import { strict as assert } from 'node:assert';

const script = readFileSync(new URL('../data/app.js', import.meta.url), 'utf8');

function setupDom(){
  const html = `<!doctype html><body data-page="config">
    <select id="prof-ch1" name="growProfileCh1">
      <option value="0" data-label="Custom" data-c1-dry="10" data-c1-wet="20" data-c2-dry="30" data-c2-wet="40" data-l1-on="06:00" data-l1-off="18:00" data-l1-auto="1" data-l2-on="07:00" data-l2-off="19:00" data-l2-auto="0">Custom</option>
      <option value="1" data-label="Seedling" data-c1-dry="34" data-c1-wet="44" data-c2-dry="54" data-c2-wet="64" data-l1-on="06:30" data-l1-off="18:00" data-l1-auto="1" data-l2-on="07:30" data-l2-off="19:30" data-l2-auto="1" data-auto-fan="1" data-auto-pump="0" data-set-auto-fan="1" data-set-auto-pump="0">Seedling</option>
    </select>
    <select id="prof-ch2" name="growProfileCh2">
      <option value="0" data-label="Custom" data-c1-dry="10" data-c1-wet="20" data-c2-dry="30" data-c2-wet="40" data-l1-on="06:00" data-l1-off="18:00" data-l1-auto="1" data-l2-on="07:00" data-l2-off="19:00" data-l2-auto="0">Custom</option>
      <option value="1" data-label="Seedling" data-c1-dry="34" data-c1-wet="44" data-c2-dry="54" data-c2-wet="64" data-l1-on="06:30" data-l1-off="18:00" data-l1-auto="1" data-l2-on="07:30" data-l2-off="19:30" data-l2-auto="1" data-auto-fan="1" data-auto-pump="0" data-set-auto-fan="1" data-set-auto-pump="0">Seedling</option>
    </select>
    <div class="profile-preview" data-preview="ch1" data-chamber-name="Alpha" data-light-label="Light 1">
      <div class="preview-grid">
        <div class="preview-item"><label>Soil</label><div class="pv-soil"></div></div>
        <div class="preview-item"><label>Light schedule</label><div class="pv-light"></div></div>
        <div class="preview-item"><label>Light mode</label><div class="pv-mode"></div></div>
        <div class="preview-item"><label>Automation</label><div class="pv-auto"></div></div>
      </div>
    </div>
    <div class="profile-preview" data-preview="ch2" data-chamber-name="Beta" data-light-label="Light 2">
      <div class="preview-grid">
        <div class="preview-item"><label>Soil</label><div class="pv-soil"></div></div>
        <div class="preview-item"><label>Light schedule</label><div class="pv-light"></div></div>
        <div class="preview-item"><label>Light mode</label><div class="pv-mode"></div></div>
        <div class="preview-item"><label>Automation</label><div class="pv-auto"></div></div>
      </div>
    </div>
  </body>`;

  const dom = new JSDOM(html, { url:"http://localhost" });
  const { window } = dom;
  window.fetch = async () => ({ ok:true, json: async () => ({ time:"12:00", time_synced:true, timezone:"UTC" }) });
  window.requestAnimationFrame = cb => setTimeout(cb, 0);
  window.confirm = () => true;

  globalThis.window = window;
  globalThis.document = window.document;
  globalThis.requestAnimationFrame = window.requestAnimationFrame;
  globalThis.fetch = window.fetch;
  globalThis.confirm = window.confirm;

  window.eval(script);
  window.document.dispatchEvent(new window.Event('DOMContentLoaded', { bubbles:true }));
  return dom;
}

test('readProfileOption returns chamber-specific values', () => {
  const dom = setupDom();
  const { document, __app } = dom.window;

  const select1 = document.querySelector('#prof-ch1');
  select1.value = '1';
  const p1 = __app.readProfileOption(select1, 0);
  assert.equal(p1.soilDry, 34);
  assert.equal(p1.soilWet, 44);
  assert.equal(p1.lightOn, '06:30');
  assert.equal(p1.lightAuto, true);

  const select2 = document.querySelector('#prof-ch2');
  select2.value = '1';
  const p2 = __app.readProfileOption(select2, 1);
  assert.equal(p2.soilDry, 54);
  assert.equal(p2.soilWet, 64);
  assert.equal(p2.lightOn, '07:30');
  assert.equal(p2.lightAuto, true);
});

test('renderChamberPreview populates preview text', () => {
  const dom = setupDom();
  const { document, __app } = dom.window;
  const select1 = document.querySelector('#prof-ch1');
  select1.value = '1';

  const preview = document.querySelector('[data-preview="ch1"]');
  __app.renderChamberPreview(select1, preview, 0);

  assert.equal(document.querySelector('.pv-soil').textContent, '34% dry / 44% wet');
  assert.ok(document.querySelector('.pv-light').textContent.includes('06:30'));
  assert.ok(document.querySelector('.pv-mode').textContent.includes('AUTO'));
  const autoText = document.querySelector('.pv-auto').textContent;
  assert.ok(autoText.includes('Fan'));
});

test('buildChamberConfirmMessage includes target values', () => {
  const dom = setupDom();
  const { document, __app } = dom.window;
  const select2 = document.querySelector('#prof-ch2');
  select2.value = '1';

  const data = __app.readProfileOption(select2, 1);
  const msg = __app.buildChamberConfirmMessage(data, 'Beta', 'Light 2');

  assert.ok(!msg.includes('34% dry / 44% wet')); // should use chamber 2 data
  assert.ok(msg.includes('54% dry / 64% wet'));
  assert.ok(msg.includes('07:30'));
  assert.ok(msg.includes('AUTO'));
});
