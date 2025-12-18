import test from 'node:test';
import { readFileSync } from 'node:fs';
import { strict as assert } from 'node:assert';

const webUiSource = readFileSync(new URL('../WebUI.cpp', import.meta.url), 'utf8');

test('includes favicon link after title block', () => {
  const pattern = new RegExp(
    String.raw`page \+= "<title>";\s*page \+= title;\s*page \+= "<\/title>";\s*page \+= "<link rel='icon' href='\/logo-ezgrow\.png' type='image\/png'>";`,
    's'
  );
  assert.match(webUiSource, pattern);
});

test('renders brand with logo and text', () => {
  const pattern = new RegExp(
    String.raw`page \+= "<div class='brand'><img src='\/logo-ezgrow\.png' class='brand-logo' alt='EZgrow logo'><span class='brand-text'>EZgrow<\/span><\/div>";`
  );
  assert.match(webUiSource, pattern);
});
