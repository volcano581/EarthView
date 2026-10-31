/* Tactical map prototype on CesiumJS: a flat 2D map with a 3D DTED-terrain view, simulated
   ground units and Google-Maps-style controls. Everything is served by the Qt host (main.cpp)
   from local files; nothing touches the network. */
'use strict';

const $ = (id) => document.getElementById(id);
const toRad = (d) => (d * Math.PI) / 180;
const toDeg = (r) => (r * 180) / Math.PI;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const UNIT_TYPES = ['Infantry', 'Armour', 'Mechanised infantry', 'Reconnaissance', 'Artillery'];
const SIDES = ['Friendly', 'Hostile'];
const EARTH_RADIUS = 6371000;
const EYE_HEIGHT = 1.8;
const UNIT_LABEL_RANGE = 12000; // metres; unit names appear only when zoomed in

function showError(e) {
  const box = $('error');
  box.textContent = 'Error: ' + (e && e.message ? e.message : String(e));
  box.hidden = false;
  console.error(e && e.stack ? e.stack : e);
}
window.addEventListener('error', (ev) => showError(ev.error || ev.message));
window.addEventListener('unhandledrejection', (ev) => showError(ev.reason));

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.json();
}

async function getBuffer(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.arrayBuffer();
}

function percentile(values, p) {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor(p * (sorted.length - 1))];
}

// ---- military symbols (simplified MIL-STD-2525 frames) -----------------------------------

function symbolImage(type, side) {
  const c = document.createElement('canvas');
  c.width = 44;
  c.height = 32;
  const g = c.getContext('2d');
  g.lineWidth = 2;
  g.strokeStyle = '#000';
  g.fillStyle = side === 0 ? 'rgb(128,224,255)' : 'rgb(255,128,128)';
  let box;
  g.beginPath();
  if (side === 0) {
    g.rect(3, 5, 38, 22);
    box = { x: 3, y: 5, w: 38, h: 22 };
  } else {
    g.moveTo(22, 2); g.lineTo(42, 16); g.lineTo(22, 30); g.lineTo(2, 16); g.closePath();
    box = { x: 12, y: 9, w: 20, h: 14 };
  }
  g.fill();
  g.stroke();
  const { x, y, w, h } = box;
  g.beginPath();
  if (type === 0 || type === 2) { // infantry cross
    g.moveTo(x, y); g.lineTo(x + w, y + h); g.moveTo(x + w, y); g.lineTo(x, y + h);
  }
  if (type === 1 || type === 2) { // armour ellipse
    g.moveTo(x + w * 0.82, y + h / 2);
    g.ellipse(x + w / 2, y + h / 2, w * 0.32, h * 0.28, 0, 0, Math.PI * 2);
  }
  if (type === 3) { // reconnaissance diagonal
    g.moveTo(x, y + h); g.lineTo(x + w, y);
  }
  g.stroke();
  if (type === 4) { // artillery dot
    g.beginPath();
    g.arc(x + w / 2, y + h / 2, Math.min(w, h) * 0.2, 0, Math.PI * 2);
    g.fillStyle = '#000';
    g.fill();
  }
  return c.toDataURL();
}

// ---- vector (OpenMapTiles) imagery rendered in workers ----------------------------------

class MvtImageryProvider {
  constructor(meta) {
    this.tilingScheme = new Cesium.WebMercatorTilingScheme();
    this.rectangle = this.tilingScheme.rectangle;
    this.tileWidth = 512;
    this.tileHeight = 512;
    this.minimumLevel = 0;
    this.maximumLevel = 19;
    this.tileDiscardPolicy = undefined;
    this.errorEvent = new Cesium.Event();
    this.credit = undefined;
    this.proxy = undefined;
    this.hasAlphaChannel = true;
    this.ready = true;
    this.readyPromise = Promise.resolve(true);
    this._minzoom = meta.minzoom ?? 0;
    this._maxzoom = meta.maxzoom ?? 14;
    this._pending = new Map();
    this._nextId = 0;
    this._workers = Array.from({ length: 3 }, () => {
      const w = new Worker('mvt-worker.js');
      w.onmessage = (ev) => this._done(ev.data);
      return w;
    });
  }

  _done({ id, bitmap, error }) {
    const p = this._pending.get(id);
    if (!p) return;
    this._pending.delete(id);
    if (error) p.reject(new Error(error));
    else p.resolve(bitmap);
  }

  getTileCredits() { return undefined; }
  pickFeatures() { return undefined; }

  requestImage(x, y, level) {
    const id = ++this._nextId;
    const worker = this._workers[id % this._workers.length];
    return new Promise((resolve, reject) => {
      this._pending.set(id, { resolve, reject });
      worker.postMessage({ id, x, y, z: level, minzoom: this._minzoom, maxzoom: this._maxzoom, size: 512 });
    });
  }
}

// ---- simulated units: binary frames + dead reckoning between them ------------------------

class Units {
  constructor(scene, meta) {
    this.scene = scene;
    this.n = meta.count;
    this.names = meta.units.map((u) => u[0]);
    this.types = meta.units.map((u) => u[1]);
    this.sides = meta.units.map((u) => u[2]);
    this.lon = new Float64Array(this.n);
    this.lat = new Float64Array(this.n);
    this.simHeight = new Float32Array(this.n);
    this.drawHeight = new Float64Array(this.n);
    this.heading = new Float32Array(this.n);
    this.speed = new Float32Array(this.n);
    this.frameT = 0;
    this.seq = 0;
    this.ready = false;
    this.selected = -1;
    this.stats = { recv: 0, dropped: 0, latency: [] };
    this.images = {};
    this.billboards = scene.primitives.add(new Cesium.BillboardCollection());
    this.labels = scene.primitives.add(new Cesium.LabelCollection());
    for (let i = 0; i < this.n; i++) {
      const image = this.imageFor(i);
      this.billboards.add({
        id: i, image, show: false, position: Cesium.Cartesian3.ZERO,
        verticalOrigin: Cesium.VerticalOrigin.CENTER,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      });
      this.labels.add({
        id: i, text: this.names[i], show: false, position: Cesium.Cartesian3.ZERO,
        font: '600 12px "Segoe UI", Roboto, Arial, sans-serif',
        fillColor: Cesium.Color.fromCssColorString('#202124'),
        outlineColor: Cesium.Color.WHITE, outlineWidth: 3,
        style: Cesium.LabelStyle.FILL_AND_OUTLINE,
        verticalOrigin: Cesium.VerticalOrigin.BOTTOM,
        pixelOffset: new Cesium.Cartesian2(0, -17),
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
        distanceDisplayCondition: new Cesium.DistanceDisplayCondition(0, UNIT_LABEL_RANGE),
      });
    }
    this.scratch = new Cesium.Cartesian3();
    this.carto = new Cesium.Cartographic();
    this.symbolScale = 1;
  }

  /** In ground view, symbols farther than @p metres are hidden behind terrain (line of sight). */
  setDepthTestDistance(metres) {
    for (let i = 0; i < this.n; i++) {
      this.billboards.get(i).disableDepthTestDistance = metres;
      this.labels.get(i).disableDepthTestDistance = metres;
    }
  }

  /** Smaller symbols when zoomed out (Cesium's scaleByDistance has no effect in 2D). */
  setSymbolScale(viewWidthMetres) {
    const s = Cesium.Math.clamp(1.15 - Math.log10(Math.max(viewWidthMetres, 1) / 10000) * 0.45, 0.45, 1.0);
    if (Math.abs(s - this.symbolScale) < 0.03) return;
    this.symbolScale = s;
    for (let i = 0; i < this.n; i++) this.billboards.get(i).scale = i === this.selected ? s * 1.4 : s;
  }

  imageFor(i) {
    const key = `${this.types[i]}-${this.sides[i]}`;
    if (!this.images[key]) this.images[key] = symbolImage(this.types[i], this.sides[i]);
    return this.images[key];
  }

  /** Applies one binary frame from the host; returns its sequence number. */
  ingest(buffer) {
    const view = new DataView(buffer);
    const t = view.getFloat64(0, true);
    const count = Math.min(view.getUint32(8, true), this.n);
    const seq = view.getUint32(12, true);
    const lonLat = new Float64Array(buffer, 16, count * 2);
    const dyn = new Float32Array(buffer, 16 + view.getUint32(8, true) * 16, count * 3);
    const in3D = this.scene.mode === Cesium.SceneMode.SCENE3D;
    for (let i = 0; i < count; i++) {
      this.lon[i] = lonLat[i * 2];
      this.lat[i] = lonLat[i * 2 + 1];
      this.simHeight[i] = dyn[i * 3];
      this.heading[i] = dyn[i * 3 + 1];
      this.speed[i] = dyn[i * 3 + 2];
      // Sit on the terrain as drawn (its level of detail), else on the simulation's DTED height.
      let h;
      if (in3D) h = this.scene.globe.getHeight(Cesium.Cartographic.fromDegrees(this.lon[i], this.lat[i], 0, this.carto));
      this.drawHeight[i] = h === undefined ? this.simHeight[i] : h;
    }
    if (this.seq && seq > this.seq + 1) this.stats.dropped += seq - this.seq - 1;
    this.seq = seq;
    this.frameT = t;
    this.stats.recv++;
    this.stats.latency.push(Date.now() - t);
    if (!this.ready) {
      this.ready = true;
      for (let i = 0; i < this.n; i++) {
        this.billboards.get(i).show = true;
        this.labels.get(i).show = true;
      }
    }
    return seq;
  }

  /** Dead-reckoned position of unit @p i now (like DIS: last state + velocity x elapsed). */
  positionOf(i, out) {
    const dt = Math.min(Math.max((Date.now() - this.frameT) / 1000, 0), 1);
    const d = this.speed[i] * dt;
    const h = toRad(this.heading[i]);
    const lat = this.lat[i] + toDeg((d * Math.cos(h)) / EARTH_RADIUS);
    const lon = this.lon[i] + toDeg((d * Math.sin(h)) / (EARTH_RADIUS * Math.cos(toRad(this.lat[i]))));
    out.lon = lon;
    out.lat = lat;
    out.height = this.drawHeight[i];
    return out;
  }

  cartesianOf(i, result) {
    const p = this.positionOf(i, {});
    return Cesium.Cartesian3.fromDegrees(p.lon, p.lat, p.height + 1.5, Cesium.Ellipsoid.WGS84, result);
  }

  update() {
    if (!this.ready) return;
    for (let i = 0; i < this.n; i++) {
      this.cartesianOf(i, this.scratch);
      this.billboards.get(i).position = this.scratch;
      this.labels.get(i).position = this.scratch;
    }
  }

  select(i) {
    if (this.selected >= 0) {
      this.billboards.get(this.selected).scale = this.symbolScale;
      const label = this.labels.get(this.selected);
      label.fillColor = Cesium.Color.fromCssColorString('#202124');
      label.distanceDisplayCondition = new Cesium.DistanceDisplayCondition(0, UNIT_LABEL_RANGE);
    }
    this.selected = i;
    if (i >= 0) {
      this.billboards.get(i).scale = this.symbolScale * 1.4;
      const label = this.labels.get(i);
      label.fillColor = Cesium.Color.fromCssColorString('#1a73e8');
      label.distanceDisplayCondition = undefined;
    }
  }

  boundingSphere() {
    const points = [];
    for (let i = 0; i < this.n; i++) points.push(this.cartesianOf(i, new Cesium.Cartesian3()));
    return Cesium.BoundingSphere.fromPoints(points);
  }
}

async function pollFrames(units, frameUrl) {
  let after = 0;
  for (;;) {
    try {
      after = units.ingest(await getBuffer(`${frameUrl}?after=${after}`)); // long poll
    } catch (e) {
      console.warn(String(e));
      await sleep(500);
    }
  }
}

// ---- the application --------------------------------------------------------------------

class App {
  constructor(meta, simMeta) {
    this.meta = meta;
    this.simMeta = simMeta;
    this.dtedTerrain = new Cesium.CustomHeightmapTerrainProvider({
      width: 65,
      height: 65,
      callback: (x, y, level) => getBuffer(`terrain/${level}/${x}/${y}`).then((b) => new Float32Array(b)),
    });
    this.viewer = new Cesium.Viewer('globe', {
      baseLayer: false,
      terrainProvider: this.dtedTerrain,
      sceneMode: Cesium.SceneMode.SCENE2D,
      mapProjection: new Cesium.WebMercatorProjection(),
      mapMode2D: Cesium.MapMode2D.ROTATE,
      animation: false, timeline: false, baseLayerPicker: false, geocoder: false, homeButton: false,
      sceneModePicker: false, navigationHelpButton: false, fullscreenButton: false,
      infoBox: false, selectionIndicator: false,
      creditContainer: $('credits'),
      msaaSamples: 4,
    });
    this.scene = this.viewer.scene;
    this.camera = this.scene.camera;
    this.scene.globe.baseColor = Cesium.Color.fromCssColorString('#dcdcdc');
    this.scene.globe.depthTestAgainstTerrain = true;
    this.scene.screenSpaceCameraController.minimumZoomDistance = 2;
    this.busy = false;
    this.follow = -1;
    this.ground = null;
    this.scaleUnits = 0; // 0 metric, 1 nautical
    this.fps = { frames: 0 };
    this.hostStats = '';
  }

  async start() {
    this.addImagery();
    this.units = new Units(this.scene, this.simMeta);
    this.places = await this.addPlaces();
    const [lon, lat] = this.simMeta.center;
    this.set2D(toRad(lon), toRad(lat), this.simMeta.radiusKm * 2600, 0);
    this.wireControls();
    this.wirePicking();
    this.scene.preRender.addEventListener(() => this.onPreRender());
    this.scene.postRender.addEventListener(() => this.fps.frames++);
    setInterval(() => this.updateHud(), 1000);
    setInterval(() => this.updateCard(), 250);
    setInterval(() => this.updateScale(), 250);
    this.setBasemap('terrain');
    pollFrames(this.units, this.meta.frameUrl);
  }

  // -- layers --------------------------------------------------------------------------------

  addImagery() {
    const layers = this.viewer.imageryLayers;
    const webMercator = new Cesium.WebMercatorTilingScheme();
    if (this.meta.bordersBase) {
      this.baseLayer = layers.addImageryProvider(new Cesium.UrlTemplateImageryProvider({
        url: 'base/{z}/{x}/{y}.png', tilingScheme: webMercator, maximumLevel: 18,
      }));
    }
    const m = this.meta.mbtiles;
    if (m) {
      const provider = m.format === 'pbf'
        ? new MvtImageryProvider(m)
        : new Cesium.UrlTemplateImageryProvider({
          url: 'mbtiles/{z}/{x}/{y}', tilingScheme: webMercator,
          minimumLevel: m.minzoom ?? 0, maximumLevel: m.maxzoom ?? 18,
        });
      this.vectorLayer = layers.addImageryProvider(provider);
    }
    if (this.meta.dted) {
      const [w, s, e, n] = this.meta.dted.bounds;
      this.reliefLayer = layers.addImageryProvider(new Cesium.UrlTemplateImageryProvider({
        url: 'hillshade/{z}/{x}/{y}.png', tilingScheme: webMercator, hasAlphaChannel: true,
        rectangle: Cesium.Rectangle.fromDegrees(w, s, e, n), maximumLevel: 15,
      }));
    }
  }

  async addPlaces() {
    const labels = this.scene.primitives.add(new Cesium.LabelCollection());
    for (const [name, lon, lat, h, kind] of await getJson('places.json')) {
      labels.add({
        position: Cesium.Cartesian3.fromDegrees(lon, lat, h + 2),
        text: name,
        font: kind === 0 ? '600 14px "Segoe UI", Roboto, Arial, sans-serif' : '12px "Segoe UI", Roboto, Arial, sans-serif',
        fillColor: Cesium.Color.fromCssColorString(kind === 0 ? '#3c4043' : '#5f6368'),
        outlineColor: Cesium.Color.WHITE,
        outlineWidth: 3,
        style: Cesium.LabelStyle.FILL_AND_OUTLINE,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
        distanceDisplayCondition: new Cesium.DistanceDisplayCondition(0, kind === 0 ? 900000 : 70000),
      });
    }
    // Vector map tiles carry their own place names.
    labels.show = !this.meta.mbtiles;
    document.querySelector('[data-overlay="places"]').checked = labels.show;
    return labels;
  }

  setBasemap(name) {
    this.basemap = name;
    if (this.reliefLayer) this.reliefLayer.show = name === 'terrain';
    // The tile shows the style you would switch to, as Google Maps does.
    const other = name === 'terrain' ? 'map' : 'terrain';
    $('basemapLabel').textContent = other === 'terrain' ? 'Terrain' : 'Map';
    const [lon, lat] = this.simMeta.center;
    const z = 8;
    const x = Math.floor(((lon + 180) / 360) * 2 ** z);
    const y = Math.floor(((1 - Math.log(Math.tan(toRad(lat)) + 1 / Math.cos(toRad(lat))) / Math.PI) / 2) * 2 ** z);
    const images = [`url(base/${z}/${x}/${y}.png)`];
    if (other === 'terrain') images.unshift(`url(hillshade/${z}/${x}/${y}.png)`);
    $('basemapTile').style.backgroundImage = images.join(', ');
  }

  setOverlay(name, on) {
    switch (name) {
      case 'units': this.units.billboards.show = on; break;
      case 'unitLabels': this.units.labels.show = on; break;
      case 'places': this.places.show = on; break;
      case 'terrain3d':
        this.viewer.terrainProvider = on ? this.dtedTerrain : new Cesium.EllipsoidTerrainProvider();
        break;
      case 'night': document.body.dataset.theme = on ? 'night' : ''; break;
      case 'hud': $('hud').hidden = !on; break;
      default: break;
    }
  }

  // -- view geometry -------------------------------------------------------------------------

  is2D() { return this.scene.mode === Cesium.SceneMode.SCENE2D; }

  /** Ground point under a window position (CSS pixels), as a Cartographic. */
  pick(windowPosition) {
    if (this.scene.mode === Cesium.SceneMode.SCENE3D) {
      const ray = this.camera.getPickRay(windowPosition);
      const hit = ray && this.scene.globe.pick(ray, this.scene);
      if (hit) return Cesium.Cartographic.fromCartesian(hit);
    }
    const e = this.camera.pickEllipsoid(windowPosition, this.scene.globe.ellipsoid);
    return e ? Cesium.Cartographic.fromCartesian(e) : undefined;
  }

  canvasPoint(fx, fy) {
    const c = this.scene.canvas;
    return new Cesium.Cartesian2(c.clientWidth * fx, c.clientHeight * fy);
  }

  center() { return this.pick(this.canvasPoint(0.5, 0.5)); }

  /** Ground distance (m) across the middle half of the screen, times two. */
  viewWidth() {
    const a = this.pick(this.canvasPoint(0.25, 0.5));
    const b = this.pick(this.canvasPoint(0.75, 0.5));
    if (!a || !b) return undefined;
    return new Cesium.EllipsoidGeodesic(a, b).surfaceDistance * 2;
  }

  async elevation(lonDeg, latDeg) {
    try {
      const r = await getJson(`elevation?lon=${lonDeg}&lat=${latDeg}`);
      return r.ok ? r.h : 0;
    } catch (e) {
      return 0;
    }
  }

  flyToSphere(sphere, offset, duration) {
    return new Promise((resolve) => {
      this.camera.flyToBoundingSphere(sphere, { offset, duration, complete: resolve, cancel: resolve });
    });
  }

  morph(to3D) {
    return new Promise((resolve) => {
      const remove = this.scene.morphComplete.addEventListener(() => { remove(); resolve(); });
      if (to3D) this.scene.morphTo3D(0);
      else this.scene.morphTo2D(0);
      setTimeout(resolve, 1500);
    });
  }

  /** 2D only: put the view centre on lon/lat (radians) with a ground width (m) and heading. */
  set2D(lon, lat, width, heading) {
    this.camera.setView({
      destination: Cesium.Cartesian3.fromRadians(lon, lat, width / Math.cos(lat)),
      orientation: { heading, pitch: -Cesium.Math.PI_OVER_TWO, roll: 0 },
    });
  }

  /** Eased animation driver: step(t) with t in 0..1. */
  animate(duration, step) {
    return new Promise((resolve) => {
      const t0 = performance.now();
      const frame = (now) => {
        const t = Math.min((now - t0) / duration, 1);
        step(t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2);
        if (t < 1) requestAnimationFrame(frame);
        else resolve();
      };
      requestAnimationFrame(frame);
    });
  }

  // -- Google-Maps-style controls ------------------------------------------------------------

  async zoomBy(factor) {
    if (this.busy || this.ground) return;
    const c = this.center();
    const width = this.viewWidth();
    if (!c || !width) return;
    if (this.is2D()) {
      const heading = this.camera.heading;
      await this.animate(280, (k) => this.set2D(c.longitude, c.latitude, width * factor ** -k, heading));
    } else {
      const target = Cesium.Cartographic.toCartesian(c);
      const range = Cesium.Cartesian3.distance(this.camera.positionWC, target);
      await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
        new Cesium.HeadingPitchRange(this.camera.heading, this.camera.pitch, range / factor), 0.35);
    }
  }

  async rotateTo(heading) {
    if (this.busy || this.ground) return;
    const c = this.center();
    if (!c) return;
    const from = this.camera.heading;
    const delta = Cesium.Math.negativePiToPi(heading - from);
    if (this.is2D()) {
      const width = this.viewWidth();
      await this.animate(350, (k) => this.set2D(c.longitude, c.latitude, width, from + delta * k));
    } else {
      const target = Cesium.Cartographic.toCartesian(c);
      const range = Cesium.Cartesian3.distance(this.camera.positionWC, target);
      await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
        new Cesium.HeadingPitchRange(from + delta, this.camera.pitch, range), 0.45);
    }
  }

  /** The 3D button: tilt the flat map into 3D terrain, or flatten it back. */
  async toggle3D() {
    if (this.busy || this.ground) return;
    this.busy = true;
    this.stopFollow();
    try {
      const c = this.center();
      const width = this.viewWidth() || 50000;
      const heading = this.camera.heading;
      if (!c) return;
      if (this.is2D()) {
        await this.morph(true);
        this.camera.setView({
          destination: Cesium.Cartesian3.fromRadians(c.longitude, c.latitude, width * 1.1),
          orientation: { heading, pitch: -Cesium.Math.PI_OVER_TWO, roll: 0 },
        });
        const h = await this.elevation(toDeg(c.longitude), toDeg(c.latitude));
        const target = Cesium.Cartesian3.fromRadians(c.longitude, c.latitude, h);
        await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
          new Cesium.HeadingPitchRange(heading, toRad(-42), width * 0.9), 1.3);
      } else {
        const target = Cesium.Cartographic.toCartesian(c);
        await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
          new Cesium.HeadingPitchRange(heading, toRad(-89.9), width * 1.1), 0.8);
        await this.morph(false);
        this.set2D(c.longitude, c.latitude, width, heading);
      }
    } finally {
      this.busy = false;
      this.updateModeButton();
    }
  }

  updateModeButton() {
    const button = $('mode3d');
    button.textContent = this.is2D() ? '3D' : '2D';
    button.title = this.is2D() ? 'Tilt to 3D' : 'Back to the flat map';
  }

  async locate() {
    if (this.busy || this.ground) return;
    const sphere = this.units.selected >= 0
      ? new Cesium.BoundingSphere(this.units.cartesianOf(this.units.selected, new Cesium.Cartesian3()), 400)
      : this.units.boundingSphere();
    const offset = this.is2D() ? undefined : new Cesium.HeadingPitchRange(this.camera.heading, toRad(-45), 0);
    await this.flyToSphere(sphere, offset, 1.2);
  }

  // -- ground view (Google's pegman) ---------------------------------------------------------

  async enterGroundView(carto) {
    if (this.busy || this.ground || !carto) return;
    this.busy = true;
    this.stopFollow();
    try {
      this.savedView = { was2D: this.is2D(), center: this.center(), width: this.viewWidth() || 50000, heading: this.camera.heading };
      if (this.is2D()) {
        await this.morph(true);
        const c = this.savedView.center || carto;
        this.camera.setView({
          destination: Cesium.Cartesian3.fromRadians(c.longitude, c.latitude, this.savedView.width * 1.1),
          orientation: { heading: this.savedView.heading, pitch: -Cesium.Math.PI_OVER_TWO, roll: 0 },
        });
        this.updateModeButton();
      }
      const lon = toDeg(carto.longitude);
      const lat = toDeg(carto.latitude);
      const h = await this.elevation(lon, lat);
      const heading = this.camera.heading;
      await new Promise((resolve) => this.camera.flyTo({
        destination: Cesium.Cartesian3.fromDegrees(lon, lat, h + EYE_HEIGHT),
        orientation: { heading, pitch: toRad(-3), roll: 0 },
        duration: 2.5, complete: resolve, cancel: resolve,
      }));
      this.ground = { lon, lat, height: h, heading, pitch: toRad(-3), keys: new Set(), last: performance.now(), drag: null };
      this.scene.screenSpaceCameraController.enableInputs = false;
      this.setLineOfSight(true);
      $('groundBanner').hidden = false;
    } finally {
      this.busy = false;
    }
  }

  async exitGroundView() {
    if (!this.ground) return;
    this.ground = null;
    $('groundBanner').hidden = true;
    this.scene.screenSpaceCameraController.enableInputs = true;
    this.setLineOfSight(false);
    const v = this.savedView;
    if (!v || !v.center) return;
    this.busy = true;
    try {
      const target = Cesium.Cartographic.toCartesian(v.center);
      if (v.was2D) {
        await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
          new Cesium.HeadingPitchRange(v.heading, toRad(-89.9), v.width * 1.1), 1.5);
        await this.morph(false);
        this.set2D(v.center.longitude, v.center.latitude, v.width, v.heading);
      } else {
        await this.flyToSphere(new Cesium.BoundingSphere(target, 0),
          new Cesium.HeadingPitchRange(v.heading, toRad(-42), v.width * 0.9), 1.5);
      }
    } finally {
      this.busy = false;
      this.updateModeButton();
    }
  }

  /** Ground view hides symbols and names behind ridges; map views always show them. */
  setLineOfSight(on) {
    const metres = on ? 1500 : Number.POSITIVE_INFINITY;
    this.units.setDepthTestDistance(metres);
    for (let i = 0; i < this.places.length; i++) this.places.get(i).disableDepthTestDistance = metres;
  }

  groundTick() {
    const g = this.ground;
    const now = performance.now();
    const dt = Math.min((now - g.last) / 1000, 0.1);
    g.last = now;
    const forward = (g.keys.has('w') ? 1 : 0) - (g.keys.has('s') ? 1 : 0);
    const strafe = (g.keys.has('d') ? 1 : 0) - (g.keys.has('a') ? 1 : 0);
    if (forward || strafe) {
      const speed = g.keys.has('shift') ? 9 : 1.6;
      const north = (forward * Math.cos(g.heading) - strafe * Math.sin(g.heading)) * speed * dt;
      const east = (forward * Math.sin(g.heading) + strafe * Math.cos(g.heading)) * speed * dt;
      g.lat += toDeg(north / EARTH_RADIUS);
      g.lon += toDeg(east / (EARTH_RADIUS * Math.cos(toRad(g.lat))));
      const h = this.scene.globe.getHeight(Cesium.Cartographic.fromDegrees(g.lon, g.lat));
      if (h !== undefined) g.height = h;
    }
    this.camera.setView({
      destination: Cesium.Cartesian3.fromDegrees(g.lon, g.lat, g.height + EYE_HEIGHT),
      orientation: { heading: g.heading, pitch: g.pitch, roll: 0 },
    });
  }

  // -- follow --------------------------------------------------------------------------------

  startFollow(i) {
    this.follow = i;
    $('cardFollow').classList.add('on');
  }

  stopFollow() {
    if (this.follow < 0) return;
    this.follow = -1;
    $('cardFollow').classList.remove('on');
    if (!this.is2D()) this.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);
  }

  followTick() {
    const target = this.units.cartesianOf(this.follow, new Cesium.Cartesian3());
    if (this.is2D()) {
      const p = this.units.positionOf(this.follow, {});
      const width = this.viewWidth();
      if (width) this.set2D(toRad(p.lon), toRad(p.lat), width, this.camera.heading);
    } else {
      const range = Cesium.Cartesian3.distance(this.camera.positionWC, target);
      this.camera.lookAt(target, new Cesium.HeadingPitchRange(this.camera.heading, this.camera.pitch, range));
    }
  }

  // -- per-frame -------------------------------------------------------------------------------

  onPreRender() {
    this.units.update();
    if (this.ground) this.groundTick();
    else if (this.follow >= 0 && !this.busy) this.followTick();
    $('needle').style.transform = `rotate(${-toDeg(this.camera.heading)}deg)`;
  }

  // -- picking, hover, coordinates -----------------------------------------------------------

  wirePicking() {
    const handler = new Cesium.ScreenSpaceEventHandler(this.scene.canvas);
    handler.setInputAction((click) => {
      if (this.ground) return;
      const picked = this.scene.pick(click.position);
      const id = picked && Number.isInteger(picked.id) ? picked.id : -1;
      this.selectUnit(id);
    }, Cesium.ScreenSpaceEventType.LEFT_CLICK);

    let pending = null;
    let lastElevation = 0;
    handler.setInputAction((move) => {
      if (pending) { pending.position = move.endPosition; return; }
      pending = { position: move.endPosition };
      requestAnimationFrame(() => {
        const at = pending.position;
        pending = null;
        if (!this.ground) {
          const picked = this.scene.pick(at);
          this.scene.canvas.style.cursor = picked && Number.isInteger(picked.id) ? 'pointer' : '';
        }
        const c = this.pick(at);
        if (!c) { $('coords').textContent = ''; return; }
        const lat = toDeg(c.latitude);
        const lon = toDeg(c.longitude);
        const text = `${Math.abs(lat).toFixed(5)}° ${lat >= 0 ? 'N' : 'S'}, ${Math.abs(lon).toFixed(5)}° ${lon >= 0 ? 'E' : 'W'}`;
        $('coords').textContent = text;
        const now = performance.now();
        if (now - lastElevation > 150) {
          lastElevation = now;
          this.elevation(lon, lat).then((h) => { $('coords').textContent = `${text} · ${Math.round(h)} m`; });
        }
      });
    }, Cesium.ScreenSpaceEventType.MOUSE_MOVE);

    // Ground view: drag to look around.
    const canvas = this.scene.canvas;
    canvas.addEventListener('pointerdown', (e) => {
      if (!this.ground) return;
      this.ground.drag = { x: e.clientX, y: e.clientY };
      canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener('pointermove', (e) => {
      const g = this.ground;
      if (!g || !g.drag) return;
      g.heading += (e.clientX - g.drag.x) * 0.004;
      g.pitch = Cesium.Math.clamp(g.pitch - (e.clientY - g.drag.y) * 0.004, toRad(-85), toRad(85));
      g.drag = { x: e.clientX, y: e.clientY };
    });
    canvas.addEventListener('pointerup', () => { if (this.ground) this.ground.drag = null; });
    window.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') {
        if (this.ground) this.exitGroundView();
        else this.selectUnit(-1);
        return;
      }
      if (this.ground) this.ground.keys.add(e.key.toLowerCase());
    });
    window.addEventListener('keyup', (e) => { if (this.ground) this.ground.keys.delete(e.key.toLowerCase()); });
  }

  selectUnit(i) {
    if (i !== this.units.selected) this.stopFollow();
    this.units.select(i);
    $('card').hidden = i < 0;
    if (i >= 0) {
      $('cardSymbol').src = this.units.imageFor(i);
      $('cardName').textContent = this.units.names[i];
      $('cardKind').textContent = `${SIDES[this.units.sides[i]]} · ${UNIT_TYPES[this.units.types[i]]}`;
      this.updateCard();
    }
  }

  updateCard() {
    const i = this.units.selected;
    if (i < 0 || !this.units.ready) return;
    const p = this.units.positionOf(i, {});
    const facts = [
      ['Position', `${p.lat.toFixed(5)}°, ${p.lon.toFixed(5)}°`],
      ['Elevation', `${Math.round(this.units.simHeight[i])} m MSL`],
      ['Speed', `${(this.units.speed[i] * 3.6).toFixed(1)} km/h`],
      ['Heading', `${Math.round(this.units.heading[i])}°`],
    ];
    $('cardFacts').innerHTML = '';
    for (const [k, v] of facts) {
      const dt = document.createElement('dt');
      dt.textContent = k;
      const dd = document.createElement('dd');
      dd.textContent = v;
      $('cardFacts').append(dt, dd);
    }
  }

  updateScale() {
    const a = this.pick(this.canvasPoint(0.5, 0.9));
    const c = this.scene.canvas;
    const b = a && this.pick(new Cesium.Cartesian2(c.clientWidth * 0.5 + 100, c.clientHeight * 0.9));
    if (!a || !b) { $('scale').hidden = true; return; }
    $('scale').hidden = false;
    const metresPerPixel = new Cesium.EllipsoidGeodesic(a, b).surfaceDistance / 100;
    if (!this.ground) this.units.setSymbolScale(metresPerPixel * c.clientWidth);
    const unit = this.scaleUnits === 0 ? 1 : 1852;
    const maxUnits = (metresPerPixel * 110) / unit;
    const pow = 10 ** Math.floor(Math.log10(maxUnits));
    const nice = [5, 2, 1].map((m) => m * pow).find((v) => v <= maxUnits) || pow;
    $('scaleBar').style.width = `${Math.round((nice * unit) / metresPerPixel)}px`;
    $('scaleLabel').textContent = this.scaleUnits === 0
      ? (nice >= 1000 ? `${nice / 1000} km` : `${nice} m`)
      : `${nice} nm`;
  }

  updateHud() {
    const s = this.units.stats;
    const mode = this.ground ? 'ground' : this.is2D() ? '2D' : '3D';
    const text = `${mode}  fps ${this.fps.frames}  units ${this.units.n}  frames/s ${s.recv}  dropped ${s.dropped}\n`
      + `frame latency p50 ${percentile(s.latency, 0.5)} ms  p95 ${percentile(s.latency, 0.95)} ms  `
      + `(sim ${this.meta.simHz} Hz, x${this.simMeta.timeScale})\n`
      + `host: ${this.hostStats}\nCesiumJS ${Cesium.VERSION}`;
    $('hud').textContent = text;
    fetch(`report?s=${encodeURIComponent(text.replace(/\n/g, ' | '))}`)
      .then((r) => r.text()).then((t) => { this.hostStats = t; }).catch(() => {});
    this.fps.frames = 0;
    s.recv = 0;
    s.latency = [];
  }

  wireControls() {
    $('zoomIn').onclick = () => this.zoomBy(2);
    $('zoomOut').onclick = () => this.zoomBy(0.5);
    $('compass').onclick = () => this.rotateTo(0);
    $('rotLeft').onclick = () => this.rotateTo(this.camera.heading - Math.PI / 4);
    $('rotRight').onclick = () => this.rotateTo(this.camera.heading + Math.PI / 4);
    $('mode3d').onclick = () => this.toggle3D();
    $('locate').onclick = () => this.locate();
    $('basemapTile').onclick = () => this.setBasemap(this.basemap === 'terrain' ? 'map' : 'terrain');
    $('scale').onclick = () => { this.scaleUnits = 1 - this.scaleUnits; this.updateScale(); };
    $('hud').onclick = () => $('hud').classList.toggle('collapsed');
    $('cardClose').onclick = () => this.selectUnit(-1);
    $('cardFollow').onclick = () => (this.follow >= 0 ? this.stopFollow() : this.startFollow(this.units.selected));
    $('cardGround').onclick = () => {
      const p = this.units.positionOf(this.units.selected, {});
      this.enterGroundView(Cesium.Cartographic.fromDegrees(p.lon, p.lat));
    };
    $('groundExit').onclick = () => this.exitGroundView();
    document.querySelectorAll('[data-overlay]').forEach((box) => {
      box.onchange = () => this.setOverlay(box.dataset.overlay, box.checked);
    });
    this.wirePegman();
  }

  wirePegman() {
    const pegman = $('pegman');
    let ghost = null;
    pegman.addEventListener('pointerdown', (e) => {
      if (this.ground || this.busy) return;
      e.preventDefault();
      pegman.setPointerCapture(e.pointerId);
      ghost = document.createElement('div');
      ghost.id = 'pegmanGhost';
      ghost.innerHTML = pegman.innerHTML;
      document.body.append(ghost);
      ghost.style.left = `${e.clientX}px`;
      ghost.style.top = `${e.clientY}px`;
    });
    pegman.addEventListener('pointermove', (e) => {
      if (!ghost) return;
      ghost.style.left = `${e.clientX}px`;
      ghost.style.top = `${e.clientY}px`;
    });
    pegman.addEventListener('pointerup', (e) => {
      if (!ghost) return;
      ghost.remove();
      ghost = null;
      const rect = this.scene.canvas.getBoundingClientRect();
      const over = document.elementFromPoint(e.clientX, e.clientY);
      if (over !== this.scene.canvas) return;
      this.enterGroundView(this.pick(new Cesium.Cartesian2(e.clientX - rect.left, e.clientY - rect.top)));
    });
  }
}

(async function main() {
  if (typeof Cesium === 'undefined') throw new Error('CesiumJS is missing: put the Cesium build in web/cesium (see README.md).');
  const [meta, simMeta] = await Promise.all([getJson('meta.json'), getJson('sim/meta')]);
  window.app = new App(meta, simMeta);
  await window.app.start();
})().catch(showError);
