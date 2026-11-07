/* Renders OpenMapTiles vector tiles (from the host's /mbtiles route) into 512 px bitmaps for
   Cesium, off the main thread. Includes a minimal protobuf / Mapbox Vector Tile decoder. */
'use strict';

// ---- protobuf + MVT decoding ------------------------------------------------------------

class Pbf {
  constructor(bytes) {
    this.buf = bytes;
    this.pos = 0;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  }
  varint() {
    let result = 0;
    let shift = 0;
    let b;
    do {
      b = this.buf[this.pos++];
      result += (b & 0x7f) * 2 ** shift;
      shift += 7;
    } while (b & 0x80);
    return result;
  }
  svarint() {
    const n = this.varint();
    return n % 2 === 1 ? (n + 1) / -2 : n / 2;
  }
  string() {
    const end = this.varint() + this.pos;
    const s = new TextDecoder().decode(this.buf.subarray(this.pos, end));
    this.pos = end;
    return s;
  }
  packed() {
    const end = this.varint() + this.pos;
    const out = [];
    while (this.pos < end) out.push(this.varint());
    return out;
  }
  skip(wire) {
    if (wire === 0) this.varint();
    else if (wire === 1) this.pos += 8;
    else if (wire === 2) this.pos += this.varint();
    else if (wire === 5) this.pos += 4;
    else throw new Error(`unsupported protobuf wire type ${wire}`);
  }
}

function decodeValue(p, end) {
  let value = null;
  while (p.pos < end) {
    const tag = p.varint();
    const field = tag >> 3;
    if (field === 1) value = p.string();
    else if (field === 2) { value = p.view.getFloat32(p.pos, true); p.pos += 4; }
    else if (field === 3) { value = p.view.getFloat64(p.pos, true); p.pos += 8; }
    else if (field === 4 || field === 5) value = p.varint();
    else if (field === 6) value = p.svarint();
    else if (field === 7) value = p.varint() !== 0;
    else p.skip(tag & 7);
  }
  return value;
}

function decodeFeature(p, end) {
  const f = { type: 0, tags: [], geometry: [] };
  while (p.pos < end) {
    const tag = p.varint();
    const field = tag >> 3;
    if (field === 2) f.tags = p.packed();
    else if (field === 3) f.type = p.varint();
    else if (field === 4) f.geometry = p.packed();
    else p.skip(tag & 7);
  }
  return f;
}

function decodeLayer(p, end) {
  const layer = { name: '', extent: 4096, keys: [], values: [], features: [] };
  while (p.pos < end) {
    const tag = p.varint();
    const field = tag >> 3;
    if (field === 1) layer.name = p.string();
    else if (field === 2) { const e = p.varint() + p.pos; layer.features.push(decodeFeature(p, e)); p.pos = e; }
    else if (field === 3) layer.keys.push(p.string());
    else if (field === 4) { const e = p.varint() + p.pos; layer.values.push(decodeValue(p, e)); p.pos = e; }
    else if (field === 5) layer.extent = p.varint();
    else p.skip(tag & 7);
  }
  return layer;
}

/** Tile bytes -> { layerName: { extent, features: [{ type, props, geometry }] } }. */
function decodeTile(bytes) {
  const p = new Pbf(bytes);
  const layers = {};
  while (p.pos < bytes.length) {
    const tag = p.varint();
    if (tag >> 3 === 3 && (tag & 7) === 2) {
      const end = p.varint() + p.pos;
      const layer = decodeLayer(p, end);
      p.pos = end;
      layers[layer.name] = {
        extent: layer.extent,
        features: layer.features.map((f) => {
          const props = {};
          for (let i = 0; i + 1 < f.tags.length; i += 2) props[layer.keys[f.tags[i]]] = layer.values[f.tags[i + 1]];
          return { type: f.type, props, geometry: decodeGeometry(f.geometry) };
        }),
      };
    } else {
      p.skip(tag & 7);
    }
  }
  return layers;
}

/** MVT command stream -> array of flat [x0, y0, x1, y1, ...] parts (rings, lines or points). */
function decodeGeometry(cmds) {
  const parts = [];
  let x = 0;
  let y = 0;
  let current = null;
  const zigzag = (n) => (n >>> 1) ^ -(n & 1);
  for (let i = 0; i < cmds.length;) {
    const cmd = cmds[i++];
    const id = cmd & 7;
    const count = cmd >> 3;
    if (id === 1 || id === 2) {
      for (let k = 0; k < count; k++) {
        x += zigzag(cmds[i++]);
        y += zigzag(cmds[i++]);
        if (id === 1) {
          if (current) parts.push(current);
          current = [x, y];
        } else {
          current.push(x, y);
        }
      }
    } else if (id === 7 && current) {
      current.push(current[0], current[1]);
    }
  }
  if (current) parts.push(current);
  return parts;
}

// ---- style (OpenMapTiles schema, Google-Maps-like palette) -------------------------------

const COLORS = {
  land: '#f4f1ea', water: '#a9d3f2', park: '#c9e7c1', building: '#e3dfd8', buildingEdge: '#d4cfc6',
  boundary: '#9e8fb5', label: '#3c4043', labelMinor: '#5f6368', halo: '#ffffff',
};
const LANDCOVER = { wood: '#cde6c2', forest: '#cde6c2', grass: '#e0efd4', farmland: '#efeee0', sand: '#f3e8c8',
  rock: '#e4dfd6', ice: '#ffffff', wetland: '#d5ebe2' };
const LANDUSE = { residential: '#ece8e1', commercial: '#f2e5e6', retail: '#f2e5e6', industrial: '#ebe5ef',
  military: '#f3dada', cemetery: '#dbecd5', hospital: '#f9e3e3', school: '#f4efd8', stadium: '#d7efd0', pitch: '#d7efd0' };
const ROADS = [ // drawn bottom to top
  { classes: ['path', 'track'], minzoom: 14, fill: '#ffffff', casing: null, widths: [[14, 0.8], [18, 2.5]], dash: [4, 3] },
  { classes: ['service'], minzoom: 14, fill: '#ffffff', casing: '#dcdcdc', widths: [[14, 1.5], [18, 7]] },
  { classes: ['minor'], minzoom: 12, fill: '#ffffff', casing: '#d9d9d9', widths: [[12, 1], [14, 3], [18, 12]] },
  { classes: ['tertiary'], minzoom: 9, fill: '#ffffff', casing: '#d2d2d2', widths: [[9, 0.6], [12, 2], [14, 5], [18, 16]] },
  { classes: ['secondary'], minzoom: 8, fill: '#ffffff', casing: '#cfcfcf', widths: [[8, 0.6], [11, 2], [14, 6], [18, 18]] },
  { classes: ['primary', 'trunk'], minzoom: 6, fill: '#fde293', casing: '#f0c66c', widths: [[6, 0.6], [10, 2.2], [14, 7], [18, 20]] },
  { classes: ['motorway'], minzoom: 4, fill: '#f8c967', casing: '#e2a843', widths: [[4, 0.6], [10, 3], [14, 8], [18, 24]] },
];
const RAIL = { minzoom: 10, color: '#b9b9b9', widths: [[10, 0.6], [18, 2]], dash: [6, 4] };
const PLACES = { // class -> [minzoom, maxzoom, font, colour, priority]
  country: [2, 6, '600 14px', COLORS.label, 0],
  state: [4, 8, 'italic 12px', COLORS.labelMinor, 1],
  city: [5, 22, '600 14px', COLORS.label, 2],
  town: [9, 22, '13px', COLORS.label, 3],
  village: [12, 22, '12px', COLORS.labelMinor, 4],
  suburb: [13, 22, '11px', COLORS.labelMinor, 5],
  hamlet: [14, 22, '11px', COLORS.labelMinor, 6],
};

function widthAt(z, stops) {
  if (z <= stops[0][0]) return stops[0][1];
  for (let i = 1; i < stops.length; i++) {
    if (z <= stops[i][0]) {
      const [z0, w0] = stops[i - 1];
      const [z1, w1] = stops[i];
      return w0 + ((w1 - w0) * (z - z0)) / (z1 - z0);
    }
  }
  return stops[stops.length - 1][1];
}

// ---- tile cache and rendering ------------------------------------------------------------

const cache = new Map();
const CACHE_MAX = 96;

function loadTile(z, x, y) {
  const key = `${z}/${x}/${y}`;
  let entry = cache.get(key);
  if (entry) {
    cache.delete(key);
    cache.set(key, entry);
    return entry;
  }
  entry = (async () => {
    const r = await fetch(`mbtiles/${z}/${x}/${y}`);
    if (r.status === 404) return null;
    if (!r.ok) throw new Error(`tile ${key}: HTTP ${r.status}`);
    let bytes = new Uint8Array(await r.arrayBuffer());
    if (bytes[0] === 0x1f && bytes[1] === 0x8b) { // OpenMapTiles stores gzip
      const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
      bytes = new Uint8Array(await new Response(stream).arrayBuffer());
    }
    return decodeTile(bytes);
  })();
  entry.catch(() => cache.delete(key));
  cache.set(key, entry);
  if (cache.size > CACHE_MAX) cache.delete(cache.keys().next().value);
  return entry;
}

function pathOf(parts, path = new Path2D()) {
  for (const part of parts) {
    path.moveTo(part[0], part[1]);
    for (let i = 2; i < part.length; i += 2) path.lineTo(part[i], part[i + 1]);
  }
  return path;
}

function fillLayer(g, layer, colorOf) {
  if (!layer) return;
  const paths = new Map();
  for (const f of layer.features) {
    if (f.type !== 3) continue;
    const color = colorOf(f.props);
    if (!color) continue;
    if (!paths.has(color)) paths.set(color, new Path2D());
    pathOf(f.geometry, paths.get(color));
  }
  for (const [color, path] of paths) {
    g.fillStyle = color;
    g.fill(path, 'evenodd');
  }
}

function strokePath(g, path, color, widthPx, k, dash) {
  g.strokeStyle = color;
  g.lineWidth = widthPx / k;
  g.setLineDash(dash ? dash.map((d) => d / k) : []);
  g.stroke(path);
}

function drawRoads(g, layer, z, k) {
  if (!layer) return;
  const byGroup = ROADS.map(() => new Path2D());
  const rail = new Path2D();
  for (const f of layer.features) {
    if (f.type !== 2) continue;
    const cls = f.props.class;
    if (cls === 'rail' || cls === 'transit') { if (z >= RAIL.minzoom) pathOf(f.geometry, rail); continue; }
    const group = ROADS.findIndex((r) => r.classes.includes(cls));
    if (group >= 0 && z >= ROADS[group].minzoom) pathOf(f.geometry, byGroup[group]);
  }
  g.lineCap = 'round';
  g.lineJoin = 'round';
  if (z >= RAIL.minzoom) strokePath(g, rail, RAIL.color, widthAt(z, RAIL.widths), k, RAIL.dash);
  ROADS.forEach((r, i) => {
    if (r.casing && z >= 10) strokePath(g, byGroup[i], r.casing, widthAt(z, r.widths) + 1.6, k, null);
  });
  ROADS.forEach((r, i) => strokePath(g, byGroup[i], r.fill, widthAt(z, r.widths), k, r.dash));
  g.setLineDash([]);
}

function drawLabels(g, layer, z, toPixel, size) {
  if (!layer) return;
  const items = [];
  for (const f of layer.features) {
    if (f.type !== 1) continue;
    const style = PLACES[f.props.class];
    if (!style || z < style[0] || z > style[1]) continue;
    const name = f.props['name:latin'] || f.props.name_en || f.props.name_int || f.props.name;
    if (!name) continue;
    const [px, py] = toPixel(f.geometry[0][0], f.geometry[0][1]);
    items.push({ name, px, py, style, rank: f.props.rank ?? 99 });
  }
  items.sort((a, b) => a.style[4] - b.style[4] || a.rank - b.rank);
  const placed = [];
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.lineJoin = 'round';
  for (const it of items) {
    g.font = `${it.style[2]} "Segoe UI", Roboto, Arial, sans-serif`;
    const w = g.measureText(it.name).width;
    const box = { x0: it.px - w / 2 - 3, x1: it.px + w / 2 + 3, y0: it.py - 10, y1: it.py + 10 };
    if (box.x0 < 0 || box.y0 < 0 || box.x1 > size || box.y1 > size) continue; // would be cut at the tile edge
    if (placed.some((b) => b.x0 < box.x1 && box.x0 < b.x1 && b.y0 < box.y1 && box.y0 < b.y1)) continue;
    placed.push(box);
    g.strokeStyle = COLORS.halo;
    g.lineWidth = 3;
    g.strokeText(it.name, it.px, it.py);
    g.fillStyle = it.style[3];
    g.fillText(it.name, it.px, it.py);
  }
}

async function render({ x, y, z, minzoom, maxzoom, size }) {
  const canvas = new OffscreenCanvas(size, size);
  const g = canvas.getContext('2d');
  // Past the file's max zoom, draw a part of the deepest tile at a larger scale (stays sharp).
  const sourceZ = Math.min(z, maxzoom);
  const d = z - sourceZ;
  const scale = 2 ** d;
  const tile = z < minzoom ? null : await loadTile(sourceZ, x >> d, y >> d);
  if (!tile) return canvas.transferToImageBitmap(); // transparent: the base map shows through
  const subX = x - (x >> d) * scale;
  const subY = y - (y >> d) * scale;

  g.fillStyle = COLORS.land;
  g.fillRect(0, 0, size, size);
  const extent = (tile.water || tile.transportation || Object.values(tile)[0] || { extent: 4096 }).extent;
  const k = (size * scale) / extent;
  g.setTransform(k, 0, 0, k, -subX * size, -subY * size);

  fillLayer(g, tile.landcover, (p) => LANDCOVER[p.class] || LANDCOVER[p.subclass]);
  fillLayer(g, tile.landuse, (p) => (z >= 10 ? LANDUSE[p.class] : null));
  fillLayer(g, tile.park, () => COLORS.park);
  fillLayer(g, tile.water, () => COLORS.water);
  fillLayer(g, tile.aeroway, () => '#e8e6ef');
  if (tile.waterway) {
    const path = new Path2D();
    for (const f of tile.waterway.features) if (f.type === 2) pathOf(f.geometry, path);
    strokePath(g, path, COLORS.water, widthAt(z, [[8, 0.5], [14, 2.5], [18, 6]]), k, null);
  }
  if (z >= 13 && tile.building) {
    fillLayer(g, tile.building, () => COLORS.building);
  }
  drawRoads(g, tile.transportation, z, k);
  if (tile.boundary) {
    const countries = new Path2D();
    const states = new Path2D();
    for (const f of tile.boundary.features) {
      if (f.type !== 2 || f.props.maritime) continue;
      if (f.props.admin_level <= 2) pathOf(f.geometry, countries);
      else if (f.props.admin_level <= 4 && z >= 5) pathOf(f.geometry, states);
    }
    strokePath(g, states, COLORS.boundary, widthAt(z, [[5, 0.6], [12, 1.2]]), k, [3, 2]);
    strokePath(g, countries, COLORS.boundary, widthAt(z, [[2, 0.8], [10, 2]]), k, null);
  }

  g.setTransform(1, 0, 0, 1, 0, 0);
  g.setLineDash([]);
  drawLabels(g, tile.place, z, (vx, vy) => [vx * k - subX * size, vy * k - subY * size], size);
  return canvas.transferToImageBitmap();
}

if (typeof self !== 'undefined' && typeof self.postMessage === 'function' && typeof module === 'undefined') {
  self.onmessage = async ({ data }) => {
    try {
      const bitmap = await render(data);
      self.postMessage({ id: data.id, bitmap }, [bitmap]);
    } catch (e) {
      self.postMessage({ id: data.id, error: String((e && e.message) || e) });
    }
  };
}

if (typeof module !== 'undefined') module.exports = { decodeTile, decodeGeometry, widthAt };
