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
    return parseChamberDatasetValue(dataset?.chamber, false)
        || parseChamberDatasetValue(dataset?.chamberId, true)
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

  const defaultChartScales = {
    tempMin: 10,
    tempMax: 40,
    humMin:  0,
    humMax:  100,
  };

  function resolveChartScales(raw){
    const clamp = (v, lo, hi) => {
      const num = Number(v);
      if (!Number.isFinite(num)) return null;
      if (num < lo) return lo;
      if (num > hi) return hi;
      return num;
    };

    const tempMin = clamp(raw?.temp_min ?? raw?.tempMin, -40, 120);
    const tempMax = clamp(raw?.temp_max ?? raw?.tempMax, -40, 120);
    const humMin  = clamp(raw?.hum_min ?? raw?.humMin,   0,   100);
    const humMax  = clamp(raw?.hum_max ?? raw?.humMax,   0,   100);

    const scales = {
      tempMin: tempMin ?? defaultChartScales.tempMin,
      tempMax: tempMax ?? defaultChartScales.tempMax,
      humMin:  humMin  ?? defaultChartScales.humMin,
      humMax:  humMax  ?? defaultChartScales.humMax,
    };

    if (scales.tempMax <= scales.tempMin){
      scales.tempMin = defaultChartScales.tempMin;
      scales.tempMax = defaultChartScales.tempMax;
    }
    if (scales.humMax <= scales.humMin){
      scales.humMin = defaultChartScales.humMin;
      scales.humMax = defaultChartScales.humMax;
    }

    return scales;
  }

  const HISTORY_RANGE_KEY       = "historyRangeDays";
  const HISTORY_MIN_DAYS        = 1;
  const HISTORY_MAX_DAYS        = 7;
  const HISTORY_INTERVAL_MINUTES = 10;
  const HISTORY_SAMPLES_PER_DAY = Math.max(1, Math.round((24 * 60) / HISTORY_INTERVAL_MINUTES));

  const clampHistoryDays = (v) => {
    const num = Number(v);
    if (!Number.isFinite(num)) return HISTORY_MIN_DAYS;
    return Math.min(HISTORY_MAX_DAYS, Math.max(HISTORY_MIN_DAYS, Math.round(num)));
  };

  function filterHistoryPoints(points, days = HISTORY_MIN_DAYS){
    const pts = Array.isArray(points) ? points : [];
    if (!pts.length) return [];

    const rangeDays = clampHistoryDays(days);
    const maxSamples = HISTORY_SAMPLES_PER_DAY * rangeDays;

    let newestTs = 0;
    pts.forEach(p => {
      const ts = Number(p?.t);
      if (Number.isFinite(ts) && ts > newestTs) newestTs = ts;
    });

    if (newestTs > 0){
      const cutoff = newestTs - (rangeDays * 24 * 60 * 60);
      const filtered = pts.filter((p, idx) => {
        const ts = Number(p?.t);
        if (Number.isFinite(ts) && ts > 0) return ts >= cutoff;
        return (pts.length - idx) <= maxSamples;
      });
      return filtered.length > maxSamples ? filtered.slice(filtered.length - maxSamples) : filtered;
    }

    return pts.slice(-maxSamples);
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
  const MINUTES_PER_DAY = 24 * 60;

  function minutesToClock(mins){
    if (!Number.isFinite(mins)) return "—";
    const total = ((mins % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;
    const h = Math.floor(total / 60);
    const m = Math.floor(total % 60);
    return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
  }

  function clockStringToMinutes(str){
    if (typeof str !== "string") return null;
    const match = str.trim().match(/^(\d{1,2}):(\d{2})(?::(\d{2}))?/);
    if (!match) return null;
    const h = Number(match[1]);
    const m = Number(match[2]);
    const s = match[3] ? Number(match[3]) : 0;
    if (!Number.isFinite(h) || !Number.isFinite(m) || !Number.isFinite(s)) return null;
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return null;
    return h * 60 + m + (s / 60);
  }

  function normalizeMinutes(mins){
    if (!Number.isFinite(mins)) return null;
    const mod = mins % MINUTES_PER_DAY;
    return mod < 0 ? mod + MINUTES_PER_DAY : mod;
  }

  function nearestSignedDelta(targetMinutes, nowMinutes){
    const target = normalizeMinutes(targetMinutes);
    const now = normalizeMinutes(nowMinutes);
    if (target == null || now == null) return null;
    const forward = (target - now + MINUTES_PER_DAY) % MINUTES_PER_DAY;
    const backward = forward === 0 ? 0 : forward - MINUTES_PER_DAY;
    return (Math.abs(backward) < Math.abs(forward)) ? backward : forward;
  }

  function formatHoursValue(hours){
    if (!Number.isFinite(hours)) return "";
    const rounded = hours >= 10 ? Math.round(hours) : Math.round(hours * 10) / 10;
    const safe = Math.max(0.1, rounded);
    return Number.isInteger(safe) ? `${safe}` : safe.toFixed(1);
  }

  function formatDeltaHours(deltaMinutes, label){
    if (deltaMinutes == null) return "";
    if (Math.abs(deltaMinutes) < 0.5) return `${label} now`;
    const hours = Math.abs(deltaMinutes) / 60;
    const hoursLabel = formatHoursValue(hours);
    const dir = deltaMinutes > 0 ? "in" : "ago";
    return dir === "in"
      ? `${label} in ${hoursLabel} h`
      : `${label} ${hoursLabel} h ago`;
  }

  function splitScheduleString(str){
    if (typeof str !== "string") return { on:"", off:"" };
    const parts = str.split(/–|-/);
    return {
      on: (parts[0] || "").trim(),
      off: (parts[1] || "").trim(),
    };
  }

  function renderAutomationPreview(container, data){
    if (!container) return;
    container.innerHTML = "";
    if (!data || (!data.setsAutoFan && !data.setsAutoPump)){
      container.textContent = "—";
      return;
    }

    const pills = document.createDocumentFragment();
    const pill = (label, isAuto) => {
      const span = document.createElement("span");
      span.className = `mode-pill ${isAuto ? "mode-auto" : "mode-manual"}`;
      span.textContent = `${label}: ${isAuto ? "AUTO" : "MANUAL"}`;
      return span;
    };

    if (data.setsAutoFan) pills.appendChild(pill("Fan", !!data.autoFan));
    if (data.setsAutoPump) pills.appendChild(pill("Pump", !!data.autoPump));
    container.appendChild(pills);
  }

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
    const autoEl  = $(".pv-auto", previewEl);

    if (!data){
      if (soilEl) soilEl.textContent = "Select a preset";
      if (lightEl) lightEl.textContent = `${lightLabel}: —`;
      if (modeEl) modeEl.textContent = "Light mode: —";
      if (autoEl) autoEl.textContent = "—";
      return;
    }

    const soilText = Number.isFinite(data.soilDry) && Number.isFinite(data.soilWet)
      ? `${data.soilDry}% dry / ${data.soilWet}% wet`
      : "Select a preset";

    const on  = data.lightOn || "—";
    const off = data.lightOff || "—";
    const mode = data.lightAuto === null ? "—" : (data.lightAuto ? "AUTO" : "MAN");
    const onMinutes  = clockStringToMinutes(on);
    const offMinutes = clockStringToMinutes(off);
    const scheduleText = buildScheduleLabel(onMinutes, offMinutes, {
      timezoneLabel: deviceClock.timezoneLabel,
      nowMinutes: deviceClock.minutes,
      baseLabel: `${lightLabel}: ${on} – ${off}`,
      onLabel: on,
      offLabel: off,
      timeSynced: deviceClock.timeSynced,
    });

    if (soilEl) soilEl.textContent = soilText;
    if (lightEl) lightEl.textContent = scheduleText;
    if (modeEl) modeEl.textContent = `${lightLabel} mode: ${mode}`;
    if (autoEl) renderAutomationPreview(autoEl, data);

    previewEl.dataset.previewLabel = chamberName;
  }

  function renderPresetSchedules(nowMinutesOverride=null){
    const nowMinutes = Number.isFinite(nowMinutesOverride) ? nowMinutesOverride
      : (deviceClock.timeSynced ? deviceClock.minutes : null);
    const tzLabel = deviceClock.timezoneLabel;

    $$(".preset-schedules").forEach(cell => {
      const row = cell.closest(".preset-row");
      if (!row) return;

      const l1OnStr  = datasetStr(row, "l1On");
      const l1OffStr = datasetStr(row, "l1Off");
      const l2OnStr  = datasetStr(row, "l2On");
      const l2OffStr = datasetStr(row, "l2Off");

      const l1On  = clockStringToMinutes(l1OnStr);
      const l1Off = clockStringToMinutes(l1OffStr);
      const l2On  = clockStringToMinutes(l2OnStr);
      const l2Off = clockStringToMinutes(l2OffStr);

      const sched1 = buildScheduleLabel(l1On, l1Off, {
        timezoneLabel: tzLabel,
        nowMinutes,
        baseLabel: `L1 ${l1OnStr || minutesToClock(l1On)}–${l1OffStr || minutesToClock(l1Off)}`,
        onLabel: l1OnStr,
        offLabel: l1OffStr,
        timeSynced: deviceClock.timeSynced,
      });
      const sched2 = buildScheduleLabel(l2On, l2Off, {
        timezoneLabel: "",
        nowMinutes,
        baseLabel: `L2 ${l2OnStr || minutesToClock(l2On)}–${l2OffStr || minutesToClock(l2Off)}`,
        onLabel: l2OnStr,
        offLabel: l2OffStr,
        timeSynced: deviceClock.timeSynced,
      });

      cell.textContent = `${sched1} · ${sched2}`;
    });
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

  function timezoneLabelFromStatus(status){
    const label = (status && typeof status.timezone === "string") ? status.timezone.trim() : "";
    const resolved = resolveTimezone(status);
    return label || resolved;
  }

  let deviceClock = {
    minutes: null,
    timezone: "",
    timezoneLabel: "",
    timezoneIana: "",
    timeSynced: false,
    rawTime: "",
  };

  let statusTimezone = "";

  function updateDeviceClockFromStatus(status){
    const tz = resolveTimezone(status);
    deviceClock = {
      minutes: clockStringToMinutes(status?.time),
      timezone: tz,
      timezoneLabel: timezoneLabelFromStatus(status),
      timezoneIana: (status && typeof status.timezone_iana === "string") ? status.timezone_iana.trim() : "",
      timeSynced: !!(status && status.time_synced),
      rawTime: typeof status?.time === "string" ? status.time : "",
    };
    statusTimezone = tz;
    return deviceClock;
  }

  function buildScheduleLabel(onMinutes, offMinutes, opts={}){
    const {
      timezoneLabel = deviceClock.timezoneLabel,
      nowMinutes = deviceClock.timeSynced ? deviceClock.minutes : null,
      baseLabel,
      onLabel,
      offLabel,
      timeSynced = deviceClock.timeSynced,
    } = opts;

    const baseOn  = onLabel || minutesToClock(onMinutes);
    const baseOff = offLabel || minutesToClock(offMinutes);
    const base = baseLabel || `${baseOn}–${baseOff}`;
    const tzText = timezoneLabel ? ` (${timezoneLabel})` : "";

    const deltas = [];
    if (timeSynced && Number.isFinite(nowMinutes)){
      const onDelta = nearestSignedDelta(onMinutes, nowMinutes);
      if (onDelta != null) deltas.push(formatDeltaHours(onDelta, "on"));
      const offDelta = nearestSignedDelta(offMinutes, nowMinutes);
      if (offDelta != null) deltas.push(formatDeltaHours(offDelta, "off"));
    }
    const deltaText = deltas.filter(Boolean).join(" · ");
    return `${base}${tzText}${deltaText ? ` · ${deltaText}` : ""}`;
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
    const relayStates = {};
    const pollIntervalMs = 2000;
    const staleAfterMs = 10000;
    let lastOkTs = Date.now();
    let consecutiveErrors = 0;
    let chamberLabels = ["Chamber 1", "Chamber 2"];
    let chartScales = defaultChartScales;

    function setStaleState(on){
      document.body.classList.toggle("stale", !!on);
      if (on) setText("#top-conn", "reconnecting…");
    }

    async function refresh(){
      const s = await apiGet(`/api/status?ts=${Date.now()}`);

      lastOkTs = Date.now();
      consecutiveErrors = 0;
      setStaleState(false);
      updateDeviceClockFromStatus(s);
      chamberLabels = deriveChamberLabels(s.chambers);

      const tzLabel = deviceClock.timezoneLabel ? ` (${deviceClock.timezoneLabel})` : "";
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

      const profileLabel = (entry) => {
        const raw = (entry && typeof entry.profile_label === "string") ? entry.profile_label.trim() : "";
        const pid = Number(entry?.profile_id);
        if (raw) return raw;
        if (Number.isFinite(pid) && pid >= 0) return "Custom/manual";
        return "Not set";
      };

      const ch1Entry = Array.isArray(s.chambers) ? s.chambers[0] : null;
      const ch2Entry = Array.isArray(s.chambers) ? s.chambers[1] : null;
      setText("#profile-ch1", `Profile: ${profileLabel(ch1Entry)}`);
      setText("#profile-ch2", `Profile: ${profileLabel(ch2Entry)}`);

      chartScales = resolveChartScales(s.chart_scales);

      pushSpark("temp", sparkData.temp,  s.sensors?.temp_c);
      pushSpark("hum",  sparkData.hum,   s.sensors?.hum_rh);
      pushSpark("s1",   sparkData.s1,    s.sensors?.soil1);
      pushSpark("s2",   sparkData.s2,    s.sensors?.soil2);

      drawSpark("#spark-temp", sparkData.temp, accent, { min: chartScales.tempMin,  max: chartScales.tempMax });
      drawSpark("#spark-hum",  sparkData.hum,  muted,  { min: chartScales.humMin,   max: chartScales.humMax });
      drawSpark("#spark-s1",   sparkData.s1,   accent, { min: 0,  max: 100 });
      drawSpark("#spark-s2",   sparkData.s2,   muted,  { min: 0,  max: 100 });

      for (const id of ["light1","light2","fan","pump"]){
        const r = s.relays?.[id];
        if (!r) continue;
        relayStates[id] = !!r.state;
        setBadge(id, r.state, r.auto);
        const sched = $(`#sched-${id}`);
        if (sched && r.schedule){
          const scheduleParts = splitScheduleString(r.schedule);
          const onMinutes = Number.isFinite(r.on_minutes) ? Number(r.on_minutes) : clockStringToMinutes(scheduleParts.on);
          const offMinutes = Number.isFinite(r.off_minutes) ? Number(r.off_minutes) : clockStringToMinutes(scheduleParts.off);
          const tz = deviceClock.timezoneLabel || s.timezone || s.timezone_iana || "";
          sched.textContent = buildScheduleLabel(onMinutes, offMinutes, {
            timezoneLabel: tz,
            nowMinutes: deviceClock.minutes,
            baseLabel: r.schedule,
            onLabel: scheduleParts.on,
            offLabel: scheduleParts.off,
            timeSynced: deviceClock.timeSynced,
          });
        }
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

    const historyRangeSelect = $("#historyRange");
    let historyRangeDays = clampHistoryDays(localStorage.getItem(HISTORY_RANGE_KEY));
    const syncHistoryRangeSelect = () => {
      if (historyRangeSelect){
        historyRangeSelect.value = String(historyRangeDays);
      }
    };
    syncHistoryRangeSelect();
    if (historyRangeSelect){
      historyRangeSelect.addEventListener("change", () => {
        const chosen = clampHistoryDays(historyRangeSelect.value);
        historyRangeDays = chosen;
        localStorage.setItem(HISTORY_RANGE_KEY, String(chosen));
        initCharts({ force:true });
      });
    }

    // History charts (with selectable range)
    let chartsInit = false;
    let chartsLoading = false;
    let lastHistoryFetch = 0;
    let tempHumChart = null;
    let soilChart = null;
    const historyRefreshMs = 60 * 1000;

    async function initCharts(opts={}){
      if (chartsLoading) return;
      const force = opts.force === true;
      const tempHumCanvas = $("#tempHumChart");
      if (!tempHumCanvas || !window.Chart) return;

      const now = Date.now();
      if (chartsInit && !force && (now - lastHistoryFetch) < historyRefreshMs) return;

      const soilCanvas = $("#soilChart");
      chartsLoading = true;
      try{
        const d = await apiGet(`/api/history?days=${historyRangeDays}&ts=${now}`);
        lastHistoryFetch = now;
        const pts = filterHistoryPoints(d.points || [], historyRangeDays);
        if (!pts.length) return;

        const { labels, temps, hums, soil1, soil2, chamberLabels: soilLabels } =
          prepareHistoryDatasets(pts, statusTimezone, chamberLabels);

        if (!chartsInit){
          tempHumChart = new Chart(tempHumCanvas.getContext("2d"), {
            type:"line",
            data:{ labels, datasets:[
              { label:"Temperature (°C)", data:temps, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)", tension:0.2, yAxisID:"y" },
              { label:"Humidity (%)",     data:hums,  borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)", tension:0.2, yAxisID:"y1" }
            ]},
            options:{
              responsive:true,
              interaction:{ mode:"index", intersect:false },
              scales:{
                y:{ position:"left",  title:{ display:true, text:"Temperature (°C)" }, min: chartScales.tempMin, max: chartScales.tempMax },
                y1:{ position:"right", title:{ display:true, text:"Humidity (%)" }, grid:{ drawOnChartArea:false }, min: chartScales.humMin, max: chartScales.humMax }
              }
            }
          });

          if (soilCanvas){
            soilChart = new Chart(soilCanvas.getContext("2d"), {
              type:"line",
              data:{ labels, datasets:[
                { label:`${soilLabels[0]} soil`, data:soil1, borderColor:accent, backgroundColor:"rgba(18,161,80,0.10)", tension:0.2, spanGaps:true },
                { label:`${soilLabels[1]} soil`, data:soil2, borderColor:muted,  backgroundColor:"rgba(107,124,133,0.10)", tension:0.2, spanGaps:true },
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

          chartsInit = true;
          return;
        }

        if (tempHumChart){
          tempHumChart.data.labels = labels;
          tempHumChart.data.datasets[0].data = temps;
          tempHumChart.data.datasets[1].data = hums;
          if (typeof tempHumChart.update === "function") tempHumChart.update();
        }

        if (soilChart){
          soilChart.data.labels = labels;
          soilChart.data.datasets[0].data = soil1;
          soilChart.data.datasets[1].data = soil2;
          if (typeof soilChart.update === "function") soilChart.update();
        }
      }finally{
        chartsLoading = false;
      }
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

    const setApplyStatus = (target, text) => {
      const el = document.querySelector(`.apply-status[data-apply-status='${target}']`);
      if (el){
        el.textContent = text || "";
        el.hidden = !text;
      }
    };

    try{
      const s = await apiGet(`/api/status?ts=${Date.now()}`);
      updateDeviceClockFromStatus(s);
      const tzLabel = deviceClock.timezoneLabel ? ` (${deviceClock.timezoneLabel})` : "";
      setText("#cfg-time", deviceClock.timeSynced ? `${deviceClock.rawTime || s.time}${tzLabel}` : "syncing…");
      renderPresetSchedules();

      const banner = $("#appliedProfileBanner");
      if (banner && banner.dataset.label){
        setAppliedProfileBanner(banner.dataset.label);
      }
    }catch(e){
      console.warn("Config status fetch failed", e);
    }
    renderPresetSchedules();

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

    const applyAllBtn = $("#applyProfileAllBtn");
    if (applyAllBtn){
      applyAllBtn.addEventListener("click", async ev => {
        ev.preventDefault();
        const select = $("#globalPresetSelect");
        const id = Number(select?.value || 0);
        if (Number.isNaN(id)) return;
        const ok = window.confirm(
          "Apply preset to both chambers and environment?\n\nThis will overwrite soil thresholds, light schedules and automation defaults for both chambers and the shared environment."
        );
        if (!ok) return;

        applyAllBtn.disabled = true;
        applyAllBtn.setAttribute("aria-busy", "true");
        try{
          const res = await fetch(`/api/grow/apply_all?profile=${encodeURIComponent(id)}`, { method:"POST" });
          if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
          const json = await res.json();
          const label = json?.applied_profile || select?.selectedOptions?.[0]?.dataset?.label || "Preset";
          setAppliedProfileBanner(label);
          setApplyStatus("all", "Applied just now");
          setApplyStatus("ch1", "Applied just now");
          setApplyStatus("ch2", "Applied just now");
          toast("Preset applied to both chambers and environment.");
        }catch(e){
          toast(`Apply failed: ${e.message}`);
        }finally{
          applyAllBtn.disabled = false;
          applyAllBtn.removeAttribute("aria-busy");
        }
      });
    }

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
            setApplyStatus(`ch${chamberId}`, "Applied just now");
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

    const rebootBtn = $("#rebootBtn");
    if (rebootBtn){
      const confirmMsg = rebootBtn.dataset.confirm || "Reboot the device now?";
      const idleLabel = rebootBtn.textContent;
      rebootBtn.addEventListener("click", async () => {
        if (!window.confirm(confirmMsg)) return;

        rebootBtn.disabled = true;
        rebootBtn.setAttribute("aria-busy", "true");
        rebootBtn.textContent = "Rebooting…";
        try{
          const res = await fetch("/api/reboot", { method:"POST" });
          if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
          let msg = "Rebooting…";
          try{
            const data = await res.json();
            if (data && typeof data.message === "string" && data.message.trim()){
              msg = data.message.trim();
            }
          }catch{}
          toast(msg);
        }catch(e){
          toast(`Reboot failed: ${e.message}`);
          rebootBtn.disabled = false;
          rebootBtn.removeAttribute("aria-busy");
          rebootBtn.textContent = idleLabel;
        }
      });
    }
  }

  function initWifi(){
    const page = document.body.dataset.page;
    if (page !== "wifi" && page !== "config") return;

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
      buildScheduleLabel,
      clockStringToMinutes,
      minutesToClock,
      nearestSignedDelta,
      formatDeltaHours,
      readProfileOption,
      renderChamberPreview,
      renderPresetSchedules,
      updateDeviceClockFromStatus,
      buildChamberConfirmMessage,
      prepareHistoryDatasets,
      filterHistoryPoints,
      resolveChartScales,
      defaultChartScales,
    });
  }
})();
