import { JSDOM } from 'jsdom';

let appPromise;

function setupDom(){
  const dom = new JSDOM(`<!doctype html><body></body>`, { url: "http://localhost" });
  global.window = dom.window;
  global.document = dom.window.document;
  global.requestAnimationFrame = cb => cb();
}

export async function loadApp(){
  if (!appPromise){
    setupDom();
    const appUrl = new URL('../../data/app.js', import.meta.url).href;
    appPromise = import(appUrl).then(() => window.__app);
  }
  return appPromise;
}
