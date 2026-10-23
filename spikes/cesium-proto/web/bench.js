/* Like-for-like engine benchmark: deck.gl (+ MapLibre basemap) vs CesiumJS.
   Same host, same binary unit frames over the same long-poll connection, same window, same
   scripted camera path, same layers (heading icons, labels, trails) and the same metrics.
   Query: engine=deck|cesium n view=2d|tilt labels trails basemap terrain warm measure pick verify
   See README.md ("Engine benchmark"). */
'use strict';

const P = new URLSearchParams(location.search);
const num = (key, fallback) => (P.has(key) ? Number(P.get(key)) : fallback);
const CFG = {
  engine: P.get('engine') || 'cesium',
  n: num('n', 300),
  hz: num('hz', 20),
  view: P.get('view') || '2d',
  labels: num('labels', 0),
  trails: num('trails', 0),
  trailLen: num('trailLen', 60),
  basemap: num('basemap', 1),
  terrain: num('terrain', 0),
  warm: num('warm', 10),
  measure: num('measure', 15),
  pick: num('pick', 1),
  pickSecs: num('pickSecs', 6),
  verify: num('verify', 0),
};
CFG.labels = Math.min(CFG.labels, CFG.n);
CFG.trails = Math.min(CFG.trails, CFG.n);

const R = 6371000;
const MPP0 = 12; // metres per pixel at the middle of the zoom cycle
const rad = (d) => (d * Math.PI) / 180;
const deg = (r) => (r * 180) / Math.PI;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const container = document.getElementById('map');
const statusEl = document.getElementById('status');

// ---- plumbing -------------------------------------------------------------------------------

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(`cannot load ${src}`));
    document.head.append(s);
  });
}

function loadCss(href) {
  const l = document.createElement('link');
  l.rel = 'stylesheet';
  l.href = href;
  document.head.append(l);
}

const post = (path, params) => fetch(`${path}?${new URLSearchParams(params)}`).then((r) => r.text());
const postResult = (obj) => post('bench/result', { json: JSON.stringify(obj) });

async function fatal(e) {
  console.error(e && e.stack ? e.stack : String(e));
  statusEl.textContent = `ERROR ${e && e.message ? e.message : e}`;
  try { await postResult({ cfg: CFG, error: String(e && e.message ? e.message : e) }); } catch (_) { /* host gone */ }
}
window.addEventListener('error', (ev) => fatal(ev.error || ev.message));
window.addEventListener('unhandledrejection', (ev) => fatal(ev.reason));

function percentile(values, p) {
  if (!values.length) return 0;
  const sorted = Float64Array.from(values).sort();
  return sorted[Math.min(sorted.length - 1, Math.floor(p * (sorted.length - 1)))];
}

const round = (v, d = 2) => Math.round(v * 10 ** d) / 10 ** d;

// ---- scripted camera: pan on a circle, zoom in and out, rotate; one cycle = 10 s ------------

function camAt(t, centre) {
  const ph = (2 * Math.PI * t) / 10;
  const east = 4000 * Math.cos(ph);
  const north = 4000 * Math.sin(ph);
  return {
    lat: centre[1] + deg(north / R),
    lon: centre[0] + deg(east / (R * Math.cos(rad(centre[1])))),
    mpp: MPP0 * 2 ** (0.8 * Math.sin(ph)), // ground metres per CSS pixel at the screen centre
    bearing: 30 * Math.sin(ph + Math.PI / 2),
  };
}

// ---- frames from the host: [f64 t][u32 count][u32 seq] f64 lon,lat x count; f32 h,hdg,speed x count

function parseFrame(buffer) {
  const dv = new DataView(buffer);
  const count = dv.getUint32(8, true);
  return {
    t: dv.getFloat64(0, true), count, seq: dv.getUint32(12, true),
    lonLat: new Float64Array(buffer, 16, count * 2),
    dyn: new Float32Array(buffer, 16 + count * 16, count * 3),
  };
}

/** Track trails sampled at 2 Hz, oldest to newest, as one flat lon/lat array. */
class TrailRing {
  constructor(count, length) {
    this.count = count;
    this.length = length;
    this.ring = new Float64Array(count * length * 2);
    this.head = 0;
    this.started = false;
  }

  sample(lonLat) {
    const { count, length, ring } = this;
    if (!this.started) {
      for (let i = 0; i < count; i++)
        for (let k = 0; k < length; k++) {
          ring[(i * length + k) * 2] = lonLat[i * 2];
          ring[(i * length + k) * 2 + 1] = lonLat[i * 2 + 1];
        }
      this.started = true;
    }
    this.head = (this.head + 1) % length;
    for (let i = 0; i < count; i++) {
      ring[(i * length + this.head) * 2] = lonLat[i * 2];
      ring[(i * length + this.head) * 2 + 1] = lonLat[i * 2 + 1];
    }
    const ordered = new Float64Array(count * length * 2);
    for (let i = 0; i < count; i++)
      for (let k = 0; k < length; k++) {
        const src = (i * length + ((this.head + 1 + k) % length)) * 2;
        const dst = (i * length + k) * 2;
        ordered[dst] = ring[src];
        ordered[dst + 1] = ring[src + 1];
      }
    return ordered;
  }
}

function arrowCanvas() {
  const c = document.createElement('canvas');
  c.width = c.height = 64;
  const g = c.getContext('2d');
  g.fillStyle = '#fff';
  g.beginPath();
  g.moveTo(32, 4); g.lineTo(54, 58); g.lineTo(32, 46); g.lineTo(10, 58);
  g.closePath();
  g.fill();
  return c;
}

// ---- deck.gl (+ MapLibre basemap) -----------------------------------------------------------

class DeckEngine {
  constructor(cfg, meta) {
    this.cfg = cfg;
    this.meta = meta;
    this.cameraInPreRender = false;
    this.fc = 0;
    this.trails = cfg.trails ? new TrailRing(cfg.trails, cfg.trailLen) : null;
    this.trailData = null;
    this.iconData = null;
    this.positions = null;
    this.seq = 0;
    this.labelIndex = Array.from({ length: cfg.labels }, (_, i) => i);
    this.labelText = this.labelIndex.map((i) => `TRK${i}`);
    this.onAfterRender = () => {};
  }

  async init() {
    await loadScript('vendor/deck.gl.min.js');
    this.atlas = arrowCanvas().toDataURL();
    const start = camAt(0, this.meta.center);
    if (this.cfg.basemap) {
      loadCss('vendor/maplibre-gl.css');
      await loadScript('vendor/maplibre-gl.js');
      const base = location.origin + location.pathname.replace(/[^/]*$/, '');
      this.map = new maplibregl.Map({
        container,
        style: {
          version: 8,
          sources: { base: { type: 'raster', tiles: [`${base}base/{z}/{x}/{y}.png`], tileSize: 256, maxzoom: 18 } },
          layers: [
            { id: 'bg', type: 'background', paint: { 'background-color': '#202428' } },
            { id: 'base', type: 'raster', source: 'base' },
          ],
        },
        center: [start.lon, start.lat], zoom: 12, interactive: false, attributionControl: false,
        fadeDuration: 0, pixelRatio: 1, maxPitch: 85, renderWorldCopies: false,
      });
      this.overlay = new deck.MapboxOverlay({
        interleaved: false, layers: [], useDevicePixels: false, onAfterRender: () => this.onAfterRender(),
      });
      this.map.addControl(this.overlay);
      await new Promise((resolve) => this.map.on('load', resolve));
      this.version = `deck.gl ${deck.VERSION || '9.1'} + MapLibre ${maplibregl.getVersion ? maplibregl.getVersion() : '4.7'}`;
    } else {
      this.deck = new deck.Deck({
        parent: container, views: new deck.MapView({ repeat: false }), controller: false, layers: [],
        viewState: { longitude: start.lon, latitude: start.lat, zoom: 12, pitch: 0, bearing: 0 },
        useDevicePixels: false, onAfterRender: () => this.onAfterRender(),
      });
      this.version = `deck.gl ${deck.VERSION || '9.1'}`;
    }
  }

  applyCamera(c) {
    const zoom = Math.log2((78271.517 * Math.cos(rad(c.lat))) / c.mpp);
    const pitch = this.cfg.view === 'tilt' ? 55 : 0;
    if (this.map) {
      this.map.jumpTo({ center: [c.lon, c.lat], zoom, bearing: c.bearing, pitch });
    } else {
      this.deck.setProps({ viewState: { longitude: c.lon, latitude: c.lat, zoom, bearing: c.bearing, pitch } });
    }
  }

  update(frame) {
    const { cfg } = this;
    const n = frame.count;
    const angles = new Float32Array(n); // new identity each update so deck re-uploads it
    for (let i = 0; i < n; i++) angles[i] = -frame.dyn[i * 3 + 1];
    this.positions = frame.lonLat;
    this.seq = frame.seq;
    this.iconData = { length: n, attributes: {
      getPosition: { value: frame.lonLat, size: 2 }, getAngle: { value: angles, size: 1 } } };
    if (this.trails && this.fc++ % Math.max(1, Math.round(cfg.hz / 2)) === 0) {
      const ordered = this.trails.sample(frame.lonLat);
      this.trailData = { length: cfg.trails, startIndices: this.trailStarts || (this.trailStarts =
        Uint32Array.from({ length: cfg.trails }, (_, i) => i * cfg.trailLen)),
      attributes: { getPath: { value: ordered, size: 2 } } };
    }
    const layers = [];
    if (this.trailData) layers.push(new deck.PathLayer({
      id: 'trails', data: this.trailData, positionFormat: 'XY', _pathType: 'open',
      getWidth: 1.5, widthUnits: 'pixels', getColor: [255, 170, 40, 160],
    }));
    layers.push(new deck.IconLayer({
      id: 'units', data: this.iconData, pickable: true, iconAtlas: this.atlas,
      iconMapping: { arrow: { x: 0, y: 0, width: 64, height: 64, mask: true } },
      getIcon: () => 'arrow', // a constant string is silently ignored by IconLayer: nothing draws
      billboard: true, sizeUnits: 'pixels', getSize: 16, getColor: [25, 195, 255, 255],
    }));
    if (cfg.labels) {
      const pos = frame.lonLat;
      layers.push(new deck.TextLayer({
        id: 'labels', data: this.labelIndex, getPosition: (i) => [pos[i * 2], pos[i * 2 + 1]],
        getText: (i) => this.labelText[i], getSize: 11, getPixelOffset: [0, -16], getColor: [225, 232, 240, 255],
        fontFamily: 'Consolas, monospace', characterSet: 'TRK0123456789', fontSettings: { sdf: true },
        outlineWidth: 3, outlineColor: [0, 0, 0, 220], updateTriggers: { getPosition: frame.seq },
      }));
    }
    (this.overlay || this.deck).setProps({ layers });
  }

  pick(x, y) {
    return (this.overlay || this.deck).pickObject({ x, y, radius: 4, layerIds: ['units'] });
  }

  project(lon, lat) {
    if (this.map) { const p = this.map.project([lon, lat]); return [p.x, p.y]; }
    return Array.from(this.deck.getViewports()[0].project([lon, lat]));
  }
}

// ---- CesiumJS --------------------------------------------------------------------------------

class CesiumEngine {
  constructor(cfg, meta) {
    this.cfg = cfg;
    this.meta = meta;
    this.cameraInPreRender = true;
    this.fc = 0;
    this.onAfterRender = () => {};
    this.driveCamera = () => {};
    this.trails = cfg.trails ? new TrailRing(cfg.trails, cfg.trailLen) : null;
  }

  async init() {
    window.CESIUM_BASE_URL = 'cesium/';
    loadCss('cesium/Widgets/widgets.css');
    await loadScript('cesium/Cesium.js');
    const { cfg } = this;
    const credits = document.createElement('div');
    credits.style.display = 'none';
    document.body.append(credits);
    const terrain = cfg.terrain
      ? new Cesium.CustomHeightmapTerrainProvider({
        width: 65, height: 65,
        callback: (x, y, level) => fetch(`terrain/${level}/${x}/${y}`).then((r) => r.arrayBuffer()).then((b) => new Float32Array(b)),
      })
      : new Cesium.EllipsoidTerrainProvider();
    this.widget = new Cesium.CesiumWidget(container, {
      baseLayer: false, terrainProvider: terrain,
      sceneMode: cfg.view === 'tilt' ? Cesium.SceneMode.SCENE3D : Cesium.SceneMode.SCENE2D,
      mapProjection: new Cesium.WebMercatorProjection(), mapMode2D: Cesium.MapMode2D.ROTATE,
      msaaSamples: 1, skyBox: false, skyAtmosphere: false, creditContainer: credits, requestRenderMode: false,
    });
    const scene = this.scene = this.widget.scene;
    this.cam = scene.camera;
    scene.screenSpaceCameraController.enableInputs = false;
    scene.globe.baseColor = Cesium.Color.fromCssColorString('#202428');
    scene.globe.showGroundAtmosphere = false;
    scene.globe.depthTestAgainstTerrain = false;
    scene.fog.enabled = false;
    scene.postProcessStages.fxaa.enabled = false;
    scene.highDynamicRange = false;
    if (scene.sun) scene.sun.show = false;
    if (scene.moon) scene.moon.show = false;
    if (cfg.basemap) {
      this.widget.imageryLayers.addImageryProvider(new Cesium.UrlTemplateImageryProvider({
        url: 'base/{z}/{x}/{y}.png', tilingScheme: new Cesium.WebMercatorTilingScheme(), maximumLevel: 18,
      }));
    }
    const arrow = arrowCanvas();
    const color = Cesium.Color.fromBytes(25, 195, 255, 255);
    this.bills = scene.primitives.add(new Cesium.BillboardCollection({ blendOption: Cesium.BlendOption.TRANSLUCENT }));
    this.items = new Array(cfg.n);
    for (let i = 0; i < cfg.n; i++) {
      const b = this.bills.add({ id: i, width: 16, height: 16, color, disableDepthTestDistance: Number.POSITIVE_INFINITY });
      b.setImage('arrow', arrow);
      this.items[i] = b;
    }
    this.labelItems = [];
    if (cfg.labels) {
      this.labelCollection = scene.primitives.add(new Cesium.LabelCollection());
      for (let i = 0; i < cfg.labels; i++) {
        this.labelItems.push(this.labelCollection.add({
          text: `TRK${i}`, font: '11px Consolas, monospace', fillColor: Cesium.Color.fromCssColorString('#e1e8f0'),
          outlineColor: Cesium.Color.BLACK, outlineWidth: 3, style: Cesium.LabelStyle.FILL_AND_OUTLINE,
          verticalOrigin: Cesium.VerticalOrigin.BOTTOM, pixelOffset: new Cesium.Cartesian2(0, -16),
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        }));
      }
    }
    if (cfg.trails) {
      this.polylines = scene.primitives.add(new Cesium.PolylineCollection());
      this.trailMaterial = Cesium.Material.fromType('Color', { color: Cesium.Color.fromBytes(255, 170, 40, 160) });
      this.trailItems = [];
      for (let i = 0; i < cfg.trails; i++)
        this.trailItems.push(this.polylines.add({ width: 1.5, material: this.trailMaterial }));
    }
    this.scratch = new Cesium.Cartesian3();
    scene.preRender.addEventListener(() => this.driveCamera());
    scene.postRender.addEventListener(() => this.onAfterRender());
    this.version = `CesiumJS ${Cesium.VERSION}`;
    // Same vertical field of view as MapLibre (36.87 degrees, camera at 1.5 x screen height).
    if (cfg.view === 'tilt') {
      const aspect = container.clientWidth / container.clientHeight;
      this.cam.frustum.fov = 2 * Math.atan(Math.tan(Math.atan(1 / 3)) * aspect);
    }
  }

  applyCamera(c) {
    const W = container.clientWidth;
    const H = container.clientHeight;
    const lon = rad(c.lon);
    const lat = rad(c.lat);
    const heading = rad(c.bearing);
    if (this.cfg.view === 'tilt') {
      this.cam.lookAt(Cesium.Cartesian3.fromRadians(lon, lat, 0),
        new Cesium.HeadingPitchRange(heading, rad(-35), 1.5 * H * c.mpp));
    } else {
      this.cam.setView({
        destination: Cesium.Cartesian3.fromRadians(lon, lat, (c.mpp * W) / Math.cos(lat)),
        orientation: { heading, pitch: -Math.PI / 2, roll: 0 },
      });
    }
  }

  update(frame) {
    const { cfg } = this;
    const { lonLat, dyn } = frame;
    const n = frame.count;
    const s = this.scratch;
    const ellipsoid = Cesium.Ellipsoid.WGS84;
    const lift = cfg.terrain;
    for (let i = 0; i < n; i++) {
      Cesium.Cartesian3.fromDegrees(lonLat[i * 2], lonLat[i * 2 + 1], lift ? dyn[i * 3] + 1.5 : 0, ellipsoid, s);
      const b = this.items[i];
      b.position = s;
      b.rotation = -rad(dyn[i * 3 + 1]); // Cesium rotates counter-clockwise; heading is clockwise
      if (i < cfg.labels) this.labelItems[i].position = s;
    }
    if (this.trails && this.fc++ % Math.max(1, Math.round(cfg.hz / 2)) === 0) {
      const ordered = this.trails.sample(lonLat);
      const len = cfg.trailLen * 2;
      for (let i = 0; i < cfg.trails; i++)
        this.trailItems[i].positions = Cesium.Cartesian3.fromDegreesArray(ordered.subarray(i * len, (i + 1) * len));
    }
  }

  pick(x, y) {
    return this.scene.pick(new Cesium.Cartesian2(x, y), 4, 4);
  }

  project(lon, lat) {
    const p = Cesium.SceneTransforms.worldToWindowCoordinates(this.scene, Cesium.Cartesian3.fromDegrees(lon, lat, 0));
    return p ? [p.x, p.y] : [NaN, NaN];
  }
}

// ---- harness -----------------------------------------------------------------------------------

const M = { dt: [], apply: [], cam: [], lat: [], pick: [], pickDt: [], recv: 0, dropped: 0, renders: 0 };
let phase = 'init';
let lastSeq = 0;
let pendingT = 0;
let haveFrame = false;
let camOverride = null;
let t0 = 0;
let engine = null;
let meta = null;

function driveCamera() {
  const c = camOverride || camAt((performance.now() - t0) / 1000, meta.center);
  const a = performance.now();
  engine.applyCamera(c);
  if (phase === 'measure') M.cam.push(performance.now() - a);
}

function onFrame(frame) {
  if (frame.count !== CFG.n) throw new Error(`host sends ${frame.count} units but the page expects ${CFG.n} (--units)`);
  if (lastSeq && frame.seq > lastSeq + 1 && phase === 'measure') M.dropped += frame.seq - lastSeq - 1;
  lastSeq = frame.seq;
  const a = performance.now();
  engine.update(frame);
  if (phase === 'measure') { M.apply.push(performance.now() - a); M.recv++; }
  pendingT = frame.t;
  haveFrame = true;
}

async function pollFrames() {
  let after = 0;
  for (;;) {
    try {
      const buffer = await (await fetch(`${meta.frameUrl}?after=${after}`)).arrayBuffer();
      const frame = parseFrame(buffer);
      after = frame.seq;
      onFrame(frame);
    } catch (e) {
      if (String(e).includes('expects')) throw e;
      await sleep(300);
    }
  }
}

let lastRaf = 0;
function loop(now) {
  if (lastRaf) {
    if (phase === 'measure') M.dt.push(now - lastRaf);
    if (phase === 'pick') M.pickDt.push(now - lastRaf);
  }
  lastRaf = now;
  if (!engine.cameraInPreRender) driveCamera();
  if (phase === 'pick') {
    const a = now / 700;
    const t = performance.now();
    engine.pick(container.clientWidth / 2 + Math.cos(a) * container.clientWidth / 4,
                container.clientHeight / 2 + Math.sin(a) * container.clientHeight / 4);
    M.pick.push(performance.now() - t);
  }
  requestAnimationFrame(loop);
}

function summarise(measuredSecs, pickSecs) {
  const total = M.dt.reduce((a, b) => a + b, 0);
  const longFrames = (ms) => M.dt.filter((d) => d > ms).length;
  const pickTotal = M.pickDt.reduce((a, b) => a + b, 0);
  return {
    frames: {
      fps: round(M.dt.length / (total / 1000), 1),
      dt_p50: round(percentile(M.dt, 0.5), 1), dt_p95: round(percentile(M.dt, 0.95), 1),
      dt_p99: round(percentile(M.dt, 0.99), 1), dt_max: round(Math.max(0, ...M.dt), 1),
      over33: longFrames(33.4), over50: longFrames(50), over100: longFrames(100),
      count: M.dt.length, renders_per_s: round(M.renders / measuredSecs, 1),
    },
    updates: {
      recv_per_s: round(M.recv / measuredSecs, 1), dropped: M.dropped,
      apply_p50: round(percentile(M.apply, 0.5), 2), apply_p95: round(percentile(M.apply, 0.95), 2),
      apply_max: round(Math.max(0, ...M.apply), 2),
      camera_p50: round(percentile(M.cam, 0.5), 2), camera_p95: round(percentile(M.cam, 0.95), 2),
      latency_p50: round(percentile(M.lat, 0.5), 1), latency_p95: round(percentile(M.lat, 0.95), 1),
    },
    pick: M.pick.length ? {
      p50: round(percentile(M.pick, 0.5), 2), p95: round(percentile(M.pick, 0.95), 2),
      fps_during: round(M.pickDt.length / (pickTotal / 1000), 1), samples: M.pick.length,
    } : null,
  };
}

function glRenderer() {
  const gl = document.createElement('canvas').getContext('webgl');
  const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
  return ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : 'unknown';
}

/** Screen positions of fixed ground points, to check both engines show the same view. */
function parityPoints(c) {
  const out = [];
  for (const [e, n] of [[0, 0], [3000, 0], [0, 3000], [-3000, -2000], [2000, -3000]]) {
    const lat = c.lat + deg(n / R);
    const lon = c.lon + deg(e / (R * Math.cos(rad(c.lat))));
    out.push(engine.project(lon, lat).map((v) => round(v, 1)));
  }
  return out;
}

/** Eight units in a row with headings 0, 45, ... 315 (north-up view: 0 = up, 90 = right). */
function syntheticFrame() {
  const n = 8;
  const lonLat = new Float64Array(n * 2);
  const dyn = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) {
    lonLat[i * 2] = meta.center[0] + deg(((i - 3.5) * 400) / (R * Math.cos(rad(meta.center[1]))));
    lonLat[i * 2 + 1] = meta.center[1];
    dyn[i * 3 + 1] = i * 45;
  }
  return { t: Date.now(), count: n, seq: 1, lonLat, dyn };
}

async function verify() {
  const result = { cfg: CFG, engine: engine.version, parity: {} };
  statusEl.textContent = `verify ${CFG.engine}`;
  engine.update(syntheticFrame());
  const north = { lat: meta.center[1], lon: meta.center[0], mpp: 8, bearing: 0 };
  camOverride = north;
  phase = 'warm';
  requestAnimationFrame(loop);
  await sleep(5000);
  result.parity.north_up = parityPoints(north);
  const moving = camAt(1.0, meta.center);
  camOverride = moving;
  await sleep(5000);
  result.parity.moving = { camera: moving, points: parityPoints(moving) };
  await sleep(1000);
  await postResult(result);
}

async function main() {
  meta = await (await fetch('meta.json')).json();
  engine = CFG.engine === 'deck' ? new DeckEngine(CFG, meta) : new CesiumEngine(CFG, meta);
  engine.driveCamera = driveCamera;
  engine.onAfterRender = () => {
    if (phase === 'measure') M.renders++;
    if (pendingT) {
      if (phase === 'measure') M.lat.push(Date.now() - pendingT);
      pendingT = 0;
    }
  };
  await engine.init();
  if (CFG.verify) { await verify(); return; }

  pollFrames().catch(fatal);
  for (let i = 0; i < 400 && !haveFrame; i++) await sleep(50);
  if (!haveFrame) throw new Error('no unit frame arrived');
  t0 = performance.now();
  phase = 'warm';
  requestAnimationFrame(loop);
  const ticker = setInterval(() => { statusEl.textContent = `${CFG.engine} n=${CFG.n} ${CFG.view} ${phase}`; }, 1000);

  await sleep(CFG.warm * 1000);
  await post('bench/mark', { phase: 'measure_start' });
  const startedAt = performance.now();
  phase = 'measure';
  await sleep(CFG.measure * 1000);
  const measuredSecs = (performance.now() - startedAt) / 1000;
  phase = 'idle';
  await post('bench/mark', { phase: 'measure_end' });
  let pickSecs = 0;
  if (CFG.pick) {
    phase = 'pick';
    pickSecs = CFG.pickSecs;
    await sleep(pickSecs * 1000);
  }
  phase = 'done';
  clearInterval(ticker);
  const gl = glRenderer();
  await postResult({
    cfg: CFG, engine: engine.version,
    env: { dpr: window.devicePixelRatio, width: container.clientWidth, height: container.clientHeight, gl,
           canvases: [...container.querySelectorAll('canvas')].map((c) => `${c.width}x${c.height}`),
           jsHeapMb: performance.memory ? round(performance.memory.usedJSHeapSize / 1048576, 1) : null },
    measuredSecs: round(measuredSecs, 2),
    ...summarise(measuredSecs, pickSecs),
  });
}

main().catch(fatal);
