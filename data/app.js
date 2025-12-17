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

  async function withRelayGuard(id, action, opts={}){
    const controls = [
      $(`#seg-${id}-auto`),
      $(`#seg-${id}-man`),
      $(`#tog-${id}`),
    ].filter(Boolean);

    const prevDisabled = controls.map(el => el.disabled);
    controls.forEach(el => {
      el.disabled = true;
      el.setAttribute("aria-busy", "true");
    });

    try{
      const result = await action();
      if (opts.successMessage) toast(opts.successMessage);
      return result;
    }catch(e){
      const prefix = opts.errorMessage || "Request failed";
      toast(`${prefix}: ${e.message}`);
      throw e;
    }finally{
      controls.forEach((el, idx) => {
        el.disabled = prevDisabled[idx];
        el.removeAttribute("aria-busy");
      });
    }
  }

  function setText(id, v){
    const el = $(id);
    if (el) el.textContent = v;
  }

  function setAppliedProfileBanner(label){
    const banner = $("#appliedProfileBanner");
    if (!banner) return;
    if (label && label.length){
      banner.textContent = `Applied profile: ${label}`;
      banner.dataset.label = label;
      banner.style.display = "";
    } else {
      banner.textContent = "";
      banner.dataset.label = "";
      banner.style.display = "none";
    }
  }

  function parseChamberDatasetValue(raw, preferId = false){
    if (raw == null) return null;
    const str = String(raw).trim();
    if (!/^[0-9]+$/.test(str)) return null;
    const val = Number(str);
    if (preferId){
      if (val >= 1 && val <= 2){
        return { chamberIdx: val - 1, chamberId: val, requestValue: val };
      }
      return null;
    }
    if (val === 0 || val === 1){
      return { chamberIdx: val, chamberId: val + 1, requestValue: val };
    }
    if (val === 2){
      return { chamberIdx: 1, chamberId: 2, requestValue: 2 };
    }
    return null;
  }

  function resolveChamberDataset(dataset){
    return parseChamberDatasetValue(dataset?.chamberId, true)
        || parseChamberDatasetValue(dataset?.chamber, false)
        || { chamberIdx: 0, chamberId: 1, requestValue: 0 };
  }

  function deriveChamberLabels(chambers){
    const list = Array.isArray(chambers) ? chambers : [];
    return [1, 2].map(idx => {
      const entry = list.find(c => Number(c?.id) === idx) || list.find(c => Number(c?.idx) === (idx - 1));
      const name = (entry && typeof entry.name === "string") ? entry.name.trim() : "";
      return name || `Chamber ${idx}`;
    });
  }

  const datasetInt = (el, key) => {
    if (!el || !el.dataset || !(key in el.dataset)) return null;
    const v = Number.parseInt(el.dataset[key], 10);
    return Number.isFinite(v) ? v : null;
  };

  const datasetBool = (el, key) => {
    if (!el || !el.dataset || !(key in el.dataset)) return null;
    const raw = String(el.dataset[key]).toLowerCase();
    return raw === "1" || raw === "true" || raw === "yes";
  };

  const datasetStr = (el, key) => (el && el.dataset && (key in el.dataset)) ? String(el.dataset[key]) : "";

  function readProfileOption(selectEl, chamberIdx){
    if (!selectEl) return null;
    const opt = selectEl.selectedOptions?.[0] || (selectEl.options ? selectEl.options[selectEl.selectedIndex] : null);
    if (!opt) return null;

    const lightKey = chamberIdx === 0 ? "l1" : "l2";
    const soilKey  = chamberIdx === 0 ? "c1" : "c2";

    return {
      label: datasetStr(opt, "label") || opt.textContent || "Preset",
      soilDry: datasetInt(opt, `${soilKey}Dry`),
      soilWet: datasetInt(opt, `${soilKey}Wet`),
      lightOn: datasetStr(opt, `${lightKey}On`),
      lightOff: datasetStr(opt, `${lightKey}Off`),
      lightAuto: datasetBool(opt, `${lightKey}Auto`),
      autoFan: datasetBool(opt, "autoFan"),
      autoPump: datasetBool(opt, "autoPump"),
      setsAutoFan: datasetBool(opt, "setAutoFan"),
      setsAutoPump: datasetBool(opt, "setAutoPump"),
    };
  }

  function renderChamberPreview(selectEl, previewEl, chamberIdx){
    if (!previewEl) return;
    const chamberName = previewEl.dataset.chamberName || `Chamber ${chamberIdx + 1}`;
    const lightLabel  = previewEl.dataset.lightLabel || `Light ${chamberIdx + 1}`;
    const data = readProfileOption(selectEl, chamberIdx);

    const soilEl  = $(".pv-soil", previewEl);
    const lightEl = $(".pv-light", previewEl);
    const modeEl  = $(".pv-mode", previewEl);

    if (!data){
      if (soilEl) soilEl.textContent = "Select a preset";
      if (lightEl) lightEl.textContent = `${lightLabel}: —`;
      if (modeEl) modeEl.textContent = "Light mode: —";
      return;
    }

    const soilText = Number.isFinite(data.soilDry) && Number.isFinite(data.soilWet)
      ? `${data.soilDry}% dry / ${data.soilWet}% wet`
      : "Select a preset";

    const on  = data.lightOn || "—";
    const off = data.lightOff || "—";
    const mode = data.lightAuto === null ? "—" : (data.lightAuto ? "AUTO" : "MAN");

    if (soilEl) soilEl.textContent = soilText;
    if (lightEl) lightEl.textContent = `${lightLabel}: ${on} – ${off}`;
    if (modeEl) modeEl.textContent = `${lightLabel} mode: ${mode}`;

    previewEl.dataset.previewLabel = chamberName;
  }

  function buildChamberConfirmMessage(profile, chamberName, lightLabel){
    const chamber = chamberName || "this chamber";
    if (!profile){
      return `Apply preset to ${chamber}?`;
    }

    const soil = Number.isFinite(profile.soilDry) && Number.isFinite(profile.soilWet)
      ? `${profile.soilDry}% dry / ${profile.soilWet}% wet`
      : "—";
    const on  = profile.lightOn || "—";
    const off = profile.lightOff || "—";
    const mode = profile.lightAuto === null ? "—" : (profile.lightAuto ? "AUTO" : "MAN");

    return `Apply "${profile.label}" to ${chamber}?\n\nSoil: ${soil}\n${lightLabel || "Light"}: ${on} – ${off} (${mode})`;
  }

  const sparkData = {
    temp: [],
    hum: [],
    s1: [],
    s2: [],
  };

  const sparkInvalid = {
    temp: 0,
    hum:  0,
    s1:   0,
    s2:   0,
  };

  function pushSpark(key, arr, v){
    if (v == null || Number.isNaN(v)){
      sparkInvalid[key] = (sparkInvalid[key] || 0) + 1;
      if (sparkInvalid[key] >= 3) arr.length = 0;
      return;
    }
    sparkInvalid[key] = 0;
    arr.push(v);
    if (arr.length > 60) arr.shift();
  }

  function formatTimeLabel(dt, timezone){
    const baseOpts = { hour: "2-digit", minute:"2-digit" };

    if (timezone){
      try{
        return dt.toLocaleTimeString([], { ...baseOpts, timeZone: timezone });
      }catch{}
    }

    return dt.toLocaleTimeString([], baseOpts);
  }

  function resolveTimezone(status){
    const tzIana = (status && typeof status.timezone_iana === "string") ? status.timezone_iana.trim() : "";
    if (tzIana) return tzIana;

    const label = (status && typeof status.timezone === "string") ? status.timezone.trim() : "";
    if (label.includes("/")) return label;

    return "";
  }

  function prepareHistoryDatasets(points, timezone, chamberLabels=[]){
    const pts = Array.isArray(points) ? points : [];
    const labelNames = Array.isArray(chamberLabels) && chamberLabels.length >= 2
      ? chamberLabels
      : ["Chamber 1", "Chamber 2"];

    const safeVal = v => (v == null || Number.isNaN(v)) ? null : v;
    const soilVal = (p, keyShort, keyLong) => {
      const raw = (keyLong in p) ? p[keyLong] : p[keyShort];
      return Number.isFinite(raw) ? raw : null;
    };

    const labels = pts.map((p, idx) => {
      if (p && Number.isFinite(p.t) && p.t > 0){
        const dt = new Date(p.t * 1000);
        return formatTimeLabel(dt, timezone);
      }
      return String(idx);
    });

    const lightVal = v => (v ? 1 : 0);

    return {
      labels,
      temps: pts.map(p => safeVal(p?.temp)),
      hums:  pts.map(p => safeVal(p?.hum)),
      light1: pts.map(p => lightVal(p?.l1)),
      light2: pts.map(p => lightVal(p?.l2)),
      soil1: pts.map(p => soilVal(p || {}, "s1", "soil1")),
      soil2: pts.map(p => soilVal(p || {}, "s2", "soil2")),
      chamberLabels: labelNames,
    };
  }

  function drawSpark(id, data, color, opts={}){
    const canvas = $(id);
    if (!canvas || !data.length) return;
    const ctx = canvas.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || 120;
    const h = canvas.clientHeight || 38;
    if (w < 10 || h < 10) return;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const min = Number.isFinite(opts.min) ? opts.min : Math.min(...data);
    const max = Number.isFinite(opts.max) ? opts.max : Math.max(...data);
    const clamp = v => Math.min(max, Math.max(min, v));
    const range = Math.max(1e-3, max - min);

    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    data.forEach((v, idx) => {
      const cv = clamp(v);
      const x = (idx / Math.max(1, data.length - 1)) * (w - 6) + 3;
      const y = h - ((cv - min) / range) * (h - 6) - 3;
      if (idx === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }

  function setBadge(id, on, auto){
    const bState = $(`#b-${id}`);
    const bMode  = $(`#m-${id}`);
    const segA   = $(`#seg-${id}-auto`);
    const segM   = $(`#seg-${id}-man`);
    const tog    = $(`#tog-${id}`);

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
    if (segA && segM){
      segA.classList.toggle("active", !!auto);
      segM.classList.toggle("active", !auto);
      segA.setAttribute("aria-pressed", auto ? "true" : "false");
      segM.setAttribute("aria-pressed", auto ? "false" : "true");
    }
    if (tog){
      tog.disabled = !!auto;
      tog.textContent = on ? "Turn OFF" : "Turn ON";
    }
  }

  async function initDashboard(){
    if (document.body.dataset.page !== "dashboard") return;

    const styles = getComputedStyle(document.documentElement);
    const accent = styles.getPropertyValue("--accent").trim() || "#12a150";
    const muted  = styles.getPropertyValue("--muted").trim()  || "#6b7c85";
    let statusTimezone = "";
    const relayStates = {};
    const pollIntervalMs = 2000;
    const staleAfterMs = 10000;
    let lastOkTs = Date.now();
    let consecutiveErrors = 0;
    let chamberLabels = ["Chamber 1", "Chamber 2"];

    function setStaleState(on){
      document.body.classList.toggle("stale", !!on);
      if (on) setText("#top-conn", "reconnecting…");
    }

    async function refresh(){
      const s = await apiGet(`/api/status?ts=${Date.now()}`);

      lastOkTs = Date.now();
      consecutiveErrors = 0;
      setStaleState(false);
      statusTimezone = resolveTimezone(s);
      chamberLabels = deriveChamberLabels(s.chambers);

      const tzLabel = s.timezone ? ` (${s.timezone})` : "";
      setText("#top-time", s.time_synced ? `${s.time}${tzLabel}` : "syncing…");

      if (s.wifi?.connected){
        setText("#top-conn", `${s.wifi.ssid} (${s.wifi.rssi} dBm) · ${s.wifi.ip}`);
      } else {
        setText("#top-conn", s.wifi?.mode === "AP" ? "AP mode" : "not connected");
      }

      setText("#lbl-s1", chamberLabels[0]);
      setText("#lbl-s2", chamberLabels[1]);

      const temp = (s.sensors?.temp_c == null) ? "N/A" : s.sensors.temp_c.toFixed(1);
      const hum  = (s.sensors?.hum_rh == null) ? "N/A" : Math.round(s.sensors.hum_rh).toString();
      setText("#v-temp", temp);
      setText("#v-hum", hum);
      setText("#v-s1",  (s.sensors?.soil1 ?? 0).toString());
      setText("#v-s2",  (s.sensors?.soil2 ?? 0).toString());
      setText("#ctl-light1-name", chamberLabels[0]);
      setText("#ctl-light2-name", chamberLabels[1]);

      pushSpark("temp", sparkData.temp,  s.sensors?.temp_c);
      pushSpark("hum",  sparkData.hum,   s.sensors?.hum_rh);
      pushSpark("s1",   sparkData.s1,    s.sensors?.soil1);
      pushSpark("s2",   sparkData.s2,    s.sensors?.soil2);

      drawSpark("#spark-temp", sparkData.temp, accent, { min: 0,  max: 50 });
      drawSpark("#spark-hum",  sparkData.hum,  muted,  { min: 0,  max: 100 });
      drawSpark("#spark-s1",   sparkData.s1,   accent, { min: 0,  max: 100 });
      drawSpark("#spark-s2",   sparkData.s2,   muted,  { min: 0,  max: 100 });

      for (const id of ["light1","light2","fan","pump"]){
        const r = s.relays?.[id];
        if (!r) continue;
        relayStates[id] = !!r.state;
        setBadge(id, r.state, r.auto);
        const sched = $(`#sched-${id}`);
        if (sched && r.schedule) sched.textContent = r.schedule;
      }
    }

    // Wire controls
    ["light1","light2","fan","pump"].forEach(id => {
      const segAuto = $(`#seg-${id}-auto`);
      const segMan  = $(`#seg-${id}-man`);
      const tog     = $(`#tog-${id}`);

      if (segAuto){
        segAuto.addEventListener("click", async () => {
          try{
            const res = await withRelayGuard(
              id,
              async () => apiGet(`/api/mode?id=${encodeURIComponent(id)}&auto=1`),
              { errorMessage:"Mode change failed" }
            );
            if (res?.changed){
              toast("Mode updated");
              await refresh();
            }
          }catch{}
        });
      }
      if (segMan){
        segMan.addEventListener("click", async () => {
          try{
            const res = await withRelayGuard(
              id,
              async () => apiGet(`/api/mode?id=${encodeURIComponent(id)}&auto=0`),
              { errorMessage:"Mode change failed" }
            );
            if (res?.changed){
              toast("Mode updated");
              await refresh();
            }
          }catch{}
        });
      }
      if (tog){
        tog.addEventListener("click", async () => {
          if (tog.disabled) return;
          if (id === "pump" && relayStates.pump === false){
            const proceed = confirm("Turn pump ON? This will start water flow.");
            if (!proceed) return;
          }
          try{
            const res = await withRelayGuard(
              id,
              async () => apiGet(`/api/toggle?id=${encodeURIComponent(id)}`),
              { errorMessage:"Toggle failed" }
            );
            if (res?.changed){
              toast("Toggle sent");
              await refresh();
            } else if (res?.reason === "AUTO"){
              toast("Switch to MAN to toggle");
            }
          }catch{}
        });
      }
    });

    // History charts (once)
    let chartsInit = false;
    async function initCharts(){
      if (chartsInit) return;
      const c1 = $("#tempHumChart");
      const c2 = $("#lightChart");
      const c3 = $("#soilChart");
      if (!c1 || !c2 || !window.Chart) return;

      const d = await apiGet(`/api/history?ts=${Date.now()}`);
      const pts = d.points || [];
      if (!pts.length) return;

      const history = prepareHistoryDatasets(pts, statusTimezone, chamberLabels);
      const labels = history.labels;
      const temps  = history.temps;
      const hums   = history.hums;
      const l1     = history.light1;
      const l2     = history.light2;
      const s1     = history.soil1;
      const s2     = history.soil2;

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

      if (c3){
        new Chart(c3.getContext("2d"), {
          type:"line",
          data:{ labels, datasets:[
            { label:`${history.chamberLabels[0]} soil`, data:s1, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)", tension:0.2, spanGaps:true },
            { label:`${history.chamberLabels[1]} soil`, data:s2, borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)", tension:0.2, spanGaps:true },
          ]},
          options:{
            responsive:true,
            interaction:{ mode:"index", intersect:false },
            scales:{
              y:{ min:0, max:100, title:{ display:true, text:"Soil moisture (%)" } },
              x:{ ticks:{ maxTicksLimit:12 } }
            }
          }
        });
      }

      new Chart(c2.getContext("2d"), {
        type:"line",
        data:{ labels, datasets:[
          { label:(chamberLabels[0] || "Chamber 1"), data:l1, stepped:true, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)" },
          { label:(chamberLabels[1] || "Chamber 2"), data:l2, stepped:true, borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)" }
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

    const poll = async () => {
      try{
        await refresh();
        await initCharts();
      }catch(e){
        consecutiveErrors++;
        if (consecutiveErrors % 3 === 0) toast(`Status failed: ${e.message}`);
        setStaleState(true);
      }
      if (Date.now() - lastOkTs > staleAfterMs) setStaleState(true);
    };

    try{ await poll(); }catch{}
    setInterval(poll, pollIntervalMs);
  }

  async function initConfig(){
    if (document.body.dataset.page !== "config") return;

    try{
      const s = await apiGet(`/api/status?ts=${Date.now()}`);
      const tzLabel = s.timezone ? ` (${s.timezone})` : "";
      setText("#cfg-time", s.time_synced ? `${s.time}${tzLabel}` : "syncing…");

      const banner = $("#appliedProfileBanner");
      if (banner && banner.dataset.label){
        setAppliedProfileBanner(banner.dataset.label);
      }
    }catch(e){
      console.warn("Config status fetch failed", e);
    }

    const renderPreview = idx => {
      const select = $(`#prof-ch${idx + 1}`);
      const preview = document.querySelector(`[data-preview='ch${idx + 1}']`);
      renderChamberPreview(select, preview, idx);
    };

    renderPreview(0);
    renderPreview(1);

    [0, 1].forEach(idx => {
      const select = $(`#prof-ch${idx + 1}`);
      if (select){
        select.addEventListener("change", () => renderPreview(idx));
      }
    });

    $$(".apply-profile").forEach(btn => {
      btn.addEventListener("click", async () => {
        const { chamberIdx, chamberId, requestValue } = resolveChamberDataset(btn.dataset || {});
        const select = $(`#prof-ch${chamberIdx + 1}`);
        const profileId = select ? Number(select.value) : 0;
        const preview = document.querySelector(`[data-preview='ch${chamberId}']`);
        const chamberName = btn.dataset.chamberName || preview?.dataset.chamberName || `Chamber ${chamberId}`;
        const lightLabel = btn.dataset.lightLabel || preview?.dataset.lightLabel || `Light ${chamberId}`;
        const profileData = readProfileOption(select, chamberIdx);
        const confirmMsg = buildChamberConfirmMessage(profileData, chamberName, lightLabel);

        if (!window.confirm(confirmMsg)){
          return;
        }

        btn.disabled = true;
        btn.setAttribute("aria-busy", "true");
        try{
          const res = await apiGet(`/api/grow/apply?chamber=${encodeURIComponent(requestValue)}&profile=${encodeURIComponent(profileId)}`);
          if (res?.ok){
            const appliedName = res.applied_profile || "Custom";
            const resChamberId = Number(res.chamber_id);
            const chamberName = res.chamber_name || `Chamber ${Number.isFinite(resChamberId) && resChamberId > 0 ? resChamberId : chamberId}`;
            const label = res.label || `${appliedName} -> ${chamberName}`;
            setAppliedProfileBanner(label);
            toast("Preset applied");
          }
        }catch(e){
          toast(`Apply failed: ${e.message}`);
        }finally{
          btn.disabled = false;
          btn.removeAttribute("aria-busy");
          renderPreview(chamberIdx);
        }
      });
    });
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
    initConfig();
    initWifi();
  });

  if (typeof window !== "undefined"){
    window.__app = Object.assign({}, window.__app, {
      withRelayGuard,
      formatTimeLabel,
      pushSpark,
      deriveChamberLabels,
      resolveTimezone,
      readProfileOption,
      renderChamberPreview,
      buildChamberConfirmMessage,
      prepareHistoryDatasets,
    });
  }
})();
