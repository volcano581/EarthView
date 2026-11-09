/* Recent JavaScript built-ins that current CesiumJS uses but older Qt WebEngine Chromium
   versions lack (Qt 6.4 ships Chromium 102). Each one is added only when missing. */
'use strict';
(function () {
  const define = (target, name, value) => {
    if (!(name in target)) Object.defineProperty(target, name, { value, writable: true, configurable: true });
  };
  const typed = Object.getPrototypeOf(Int8Array.prototype);
  for (const proto of [Array.prototype, typed]) {
    const copy = (a) => (Array.isArray(a) ? a.slice() : new a.constructor(a));
    define(proto, 'toSorted', function (compare) { return copy(this).sort(compare); });
    define(proto, 'toReversed', function () { return copy(this).reverse(); });
    define(proto, 'with', function (index, value) {
      const out = copy(this);
      out[index < 0 ? this.length + index : index] = value;
      return out;
    });
    define(proto, 'findLast', function (fn, thisArg) {
      for (let i = this.length - 1; i >= 0; i--) if (fn.call(thisArg, this[i], i, this)) return this[i];
      return undefined;
    });
    define(proto, 'findLastIndex', function (fn, thisArg) {
      for (let i = this.length - 1; i >= 0; i--) if (fn.call(thisArg, this[i], i, this)) return i;
      return -1;
    });
  }
  define(Array.prototype, 'toSpliced', function (...args) {
    const out = this.slice();
    out.splice(...args);
    return out;
  });
  define(Object, 'groupBy', function (items, fn) {
    const out = Object.create(null);
    let i = 0;
    for (const item of items) (out[fn(item, i++)] ||= []).push(item);
    return out;
  });
  define(Map, 'groupBy', function (items, fn) {
    const out = new Map();
    let i = 0;
    for (const item of items) {
      const key = fn(item, i++);
      if (!out.has(key)) out.set(key, []);
      out.get(key).push(item);
    }
    return out;
  });
  define(Promise, 'withResolvers', function () {
    let resolve;
    let reject;
    const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
    return { promise, resolve, reject };
  });
})();
