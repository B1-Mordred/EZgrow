import test from 'node:test';
import { readFileSync } from 'node:fs';
import { strict as assert } from 'node:assert';

const webUiSource = readFileSync(new URL('../WebUI.cpp', import.meta.url), 'utf8');

test('serves logo image via dedicated handler', () => {
  const pattern = new RegExp(
    String.raw`static void handleLogoPng\(\)\s*\{\s*streamStaticFile\(\"\/logo-ezgrow\.png\",\s*\"image\/png\"\);\s*\}`,
    's'
  );
  assert.match(webUiSource, pattern);
});

test('registers logo route in static assets block', () => {
  const pattern = new RegExp(
    String.raw`server\.on\(\"\/logo-ezgrow\.png\",\s*HTTP_GET,\s*handleLogoPng\);`
  );
  assert.match(webUiSource, pattern);
});
