import test from 'node:test';
import { readFileSync } from 'node:fs';
import { strict as assert } from 'node:assert';

const webUiSource = readFileSync(new URL('../WebUI.cpp', import.meta.url), 'utf8');
const appCss = readFileSync(new URL('../data/app.css', import.meta.url), 'utf8');

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

test('renders history charts without the light canvas', () => {
  assert.match(webUiSource, /<canvas id='tempHumChart'[^>]*><\/canvas>/);
  assert.match(webUiSource, /<canvas id='soilChart'[^>]*><\/canvas>/);
  assert.doesNotMatch(webUiSource, /id='lightChart'/);
});

test('applies brand layout and sizing styles', () => {
  assert.match(appCss, /\.brand\s*{[^}]*display:flex;[^}]*align-items:center;[^}]*gap:12px;[^}]*}/s);
  assert.match(
    appCss,
    /\.brand-logo\s*{[^}]*display:flex;[^}]*align-items:center;[^}]*justify-content:center;[^}]*width:40px;[^}]*height:40px;[^}]*padding:6px;[^}]*border-radius:12px;[^}]*}/s
  );
  assert.match(
    appCss,
    /\.brand-text\s*{[^}]*display:flex;[^}]*flex-direction:column;[^}]*font-weight:900;[^}]*font-size:1\.05rem;[^}]*letter-spacing:\.35px;[^}]*line-height:1\.1;[^}]*}/s
  );
});
