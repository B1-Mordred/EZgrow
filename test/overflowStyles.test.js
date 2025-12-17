import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

const css = fs.readFileSync(new URL('../data/app.css', import.meta.url), 'utf8');

const matchRule = (pattern, description) => {
  assert.match(css, pattern, `${description} should be present in app.css`);
};

test('control titles are clamped with overflow handling', () => {
  matchRule(/\.control-head\s*>\s*div:first-child\{[^}]*flex:1[^}]*min-width:0/si, 'control head flex sizing');
  matchRule(/\.control-title\{[^}]*text-overflow:ellipsis[^}]*white-space:nowrap[^}]*\}/si, 'control title ellipsis');
  matchRule(/\.control-title span\{[^}]*text-overflow:ellipsis/si, 'control title span ellipsis');
});

test('tile labels clamp long chamber names', () => {
  matchRule(/\.tile-label\{[^}]*text-overflow:ellipsis[^}]*\}/si, 'tile label ellipsis');
  matchRule(/\.tile-label span\{[^}]*text-overflow:ellipsis[^}]*\}/si, 'tile label span ellipsis');
});
