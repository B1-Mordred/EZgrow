(() => {
  "use strict";

  const $  = (s, r=document) => r.querySelector(s);
  const $$ = (s, r=document) => Array.from(r.querySelectorAll(s));

  function toast(msg){
    const t = document.createElement("div");
    t.className = "toast";
    t.textContent = msg;
    document.body.appendChild(t);
    requestAnimationFrame(()=>t.classList.add("show"));
    setTimeout(()=> {
      t.classList.remove("show");
      setTimeout(()=>t.remove(), 250);
    }, 2200);
  }

  function initTabs(){
    $$(".tabs[data-tabs]").forEach(tabsEl => {
      const key = tabsEl.getAttribute("data-persist") || "";
      const panelsWrap = tabsEl.nextElementSibling;
      const tabs = $$(".tab", tabsEl);

      function activate(name){
        tabs.forEach(b => b.classList.toggle("active", b.dataset.tab === name));
        if (panelsWrap){
          $$(".tab-panel", panelsWrap).forEach(p => p.classList.toggle("active", p.dataset.tab === name));
        }
        if (key) localStorage.setItem(key, name);
      }

      const saved = key ? localStorage.getItem(key) : null;
      const first = tabs[0]?.dataset.tab;
      activate(saved || first);

      tabs.forEach(b => b.addEventListener("click", () => activate(b.dataset.tab)));
    });
  }

  async function apiGet(url){
    const r = await fetch(url, { cache: "no-store" });
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    return r.json();
  }

  function setText(id, v){
    const el = $(id);
    if (el) el.textContent = v;
  }

  function setBadge(id, on, auto){
    const bState = $(`#b-${id}`);
    const bMode  = $(`#m-${id}`);
    const sw     = $(`#sw-${id}`);
    const btn    = $(`#btn-${id}`);

    if (bState){
      bState.textContent = on ? "ON" : "OFF";
      bState.classList.toggle("on",  !!on);
      bState.classList.toggle("off", !on);
    }
    if (bMode){
      bMode.textContent = auto ? "AUTO" : "MAN";
      bMode.classList.toggle("auto", !!auto);
      bMode.classList.toggle("man",  !auto);
    }
    if (sw){
      sw.checked = !!on;
      sw.disabled = !!auto;
    }
    if (btn){
      btn.textContent = auto ? "Switch to MANUAL" : "Switch to AUTO";
    }
  }

  async function initDashboard(){
    if (document.body.dataset.page !== "dashboard") return;

    async function refresh(){
      const s = await apiGet(`/api/status?ts=${Date.now()}`);

      setText("#top-time", s.time_synced ? s.time : "syncing…");

      if (s.wifi?.connected){
        setText("#top-conn", `${s.wifi.ssid} (${s.wifi.rssi} dBm) · ${s.wifi.ip}`);
      } else {
        setText("#top-conn", s.wifi?.mode === "AP" ? "AP mode" : "not connected");
      }

      const temp = (s.sensors?.temp_c == null) ? "N/A" : s.sensors.temp_c.toFixed(1);
      const hum  = (s.sensors?.hum_rh == null) ? "N/A" : Math.round(s.sensors.hum_rh).toString();
      setText("#v-temp", temp);
      setText("#v-hum", hum);
      setText("#v-s1",  (s.sensors?.soil1 ?? 0).toString());
      setText("#v-s2",  (s.sensors?.soil2 ?? 0).toString());

      for (const id of ["light1","light2","fan","pump"]){
        const r = s.relays?.[id];
        if (!r) continue;
        setBadge(id, r.state, r.auto);
        const sched = $(`#sched-${id}`);
        if (sched && r.schedule) sched.textContent = r.schedule;
      }
    }

    // Wire controls
    ["light1","light2","fan","pump"].forEach(id => {
      const sw = $(`#sw-${id}`);
      if (sw){
        sw.addEventListener("change", async () => {
          try{
            await apiGet(`/api/toggle?id=${encodeURIComponent(id)}`);
            await refresh();
          }catch(e){ toast(`Toggle failed: ${e.message}`); }
        });
      }
      const btn = $(`#btn-${id}`);
      if (btn){
        btn.addEventListener("click", async () => {
          try{
            const mode = $(`#m-${id}`)?.textContent === "AUTO";
            const next = mode ? 0 : 1;
            await apiGet(`/api/mode?id=${encodeURIComponent(id)}&auto=${next}`);
            await refresh();
          }catch(e){ toast(`Mode change failed: ${e.message}`); }
        });
      }
    });

    // History charts (once)
    let chartsInit = false;
    async function initCharts(){
      if (chartsInit) return;
      const c1 = $("#tempHumChart");
      const c2 = $("#lightChart");
      if (!c1 || !c2 || !window.Chart) return;

      const accent = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#12a150";
      const muted  = getComputedStyle(document.documentElement).getPropertyValue("--muted").trim()  || "#6b7c85";

      const d = await apiGet(`/api/history?ts=${Date.now()}`);
      const pts = d.points || [];
      if (!pts.length) return;

      const labels = pts.map((p, idx) => {
        if (p.t && p.t > 0){
          const dt = new Date(p.t * 1000);
          return dt.toLocaleTimeString([], { hour: "2-digit", minute:"2-digit" });
        }
        return String(idx);
      });

      const temps = pts.map(p => p.temp);
      const hums  = pts.map(p => p.hum);
      const l1    = pts.map(p => p.l1);
      const l2    = pts.map(p => p.l2);

      new Chart(c1.getContext("2d"), {
        type:"line",
        data:{ labels, datasets:[
          { label:"Temperature (°C)", data:temps, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)", tension:0.2, yAxisID:"y" },
          { label:"Humidity (%)",     data:hums,  borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)", tension:0.2, yAxisID:"y1" }
        ]},
        options:{
          responsive:true,
          interaction:{ mode:"index", intersect:false },
          scales:{
            y:{ position:"left",  title:{ display:true, text:"Temperature (°C)" } },
            y1:{ position:"right", title:{ display:true, text:"Humidity (%)" }, grid:{ drawOnChartArea:false } }
          }
        }
      });

      new Chart(c2.getContext("2d"), {
        type:"line",
        data:{ labels, datasets:[
          { label:"Light 1", data:l1, stepped:true, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)" },
          { label:"Light 2", data:l2, stepped:true, borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)" }
        ]},
        options:{
          responsive:true,
          interaction:{ mode:"index", intersect:false },
          scales:{
            y:{ min:-0.1, max:1.1, ticks:{ stepSize:1 }, title:{ display:true, text:"Light state (0/1)" } },
            x:{ ticks:{ maxTicksLimit:12 } }
          }
        }
      });

      chartsInit = true;
    }

    try{
      await refresh();
      await initCharts();
      setInterval(async ()=>{ try{ await refresh(); }catch{} }, 2000);
    }catch(e){
      toast(`Status failed: ${e.message}`);
    }
  }

  function initWifi(){
    if (document.body.dataset.page !== "wifi") return;

    const ssidInput = $("#ssid");
    $$(".ssid-row").forEach(r => {
      r.addEventListener("click", () => {
        const v = r.getAttribute("data-ssid") || "";
        if (ssidInput){
          ssidInput.value = v;
          ssidInput.focus();
          toast(`SSID selected: ${v}`);
        }
      });
    });

    const filter = $("#ssidFilter");
    if (filter){
      filter.addEventListener("input", () => {
        const q = filter.value.toLowerCase().trim();
        $$(".ssid-row").forEach(r => {
          const v = (r.getAttribute("data-ssid") || "").toLowerCase();
          r.style.display = (!q || v.includes(q)) ? "" : "none";
        });
      });
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    initTabs();
    initDashboard();
    initWifi();
  });
})();
