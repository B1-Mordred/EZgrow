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

test('renders top navigation with dashboard/config and captive portal Wi-Fi setup only', () => {
  const navPattern = new RegExp(
    /if \(!sCaptivePortalActive\) {\s*page \+= "<a href='\/'";[\s\S]*?>Dashboard<\/a>";\s*page \+= "<a href='\/config'";[\s\S]*?>Config<\/a>";\s*} else {\s*page \+= "<a href='\/wifi'";[\s\S]*?>Wi-Fi Setup<\/a>";\s*}\s*page \+= "<\/div>";/s,
    's'
  );
  assert.match(webUiSource, navPattern);
  assert.doesNotMatch(webUiSource, />Wi-Fi<\/a>;?/);
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
    /\.brand-logo\s*{[^}]*display:block;[^}]*height:34px;[^}]*width:auto;[^}]*object-fit:contain;[^}]*}/s
  );
  assert.match(
    appCss,
    /\.brand-text\s*{[^}]*display:flex;[^}]*flex-direction:column;[^}]*font-weight:900;[^}]*font-size:1\.05rem;[^}]*letter-spacing:\.35px;[^}]*line-height:1\.1;[^}]*}/s
  );
});

test('styles the topbar with the dark theme background and accent divider', () => {
  assert.match(appCss, /\.topbar\s*{[^}]*background:var\(--bg-main\);[^}]*color:var\(--text-main\);[^}]*border-bottom:1px solid var\(--accent\);[^}]*}/s);
  assert.match(appCss, /\.stale \.topbar{[^}]*background:var\(--bg-panel\)[^}]*}/s);
});

test('includes key config tabs with Wi-Fi and Water & Air', () => {
  const tabsPattern = /data-tabs='config'[^>]*>.*data-tab='waterair'.*data-tab='profile'.*data-tab='wifi'>Wi-Fi<\/button>/s;
  assert.match(webUiSource, tabsPattern);
  assert.match(webUiSource, /<div class='tab-panel' data-tab='wifi'>/);
  assert.match(webUiSource, /data-tab='waterair'>/);
  assert.match(webUiSource, /data-tab='profile'>/);
});
