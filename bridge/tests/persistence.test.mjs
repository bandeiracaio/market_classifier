import assert from "node:assert/strict";
import test from "node:test";
import {
  NOTICE_LAST_GOOD,
  NOTICE_RESET,
  createAutosave,
  loadWorkspace,
  validateImportFile,
  MAX_WORKSPACE_BYTES,
} from "../.build/persistence.js";

/** Map-backed stand-in for the tiny IndexedDB subset persistence uses. */
function memoryStore(initial = {}) {
  const map = new Map(Object.entries(initial));
  return {
    map,
    writes: [],
    async get(key) {
      return map.get(key);
    },
    async put(key, value) {
      this.writes.push(key);
      map.set(key, value);
    },
  };
}

/** Engine stand-in: accepts JSON that parses and has schema 1, like mc_workspace_import. */
function fakeTarget() {
  return {
    imported: [],
    resets: 0,
    dirty: false,
    current: '{"schema":1,"layouts":[]}',
    importJson(json) {
      try {
        const v = JSON.parse(json);
        if (v.schema !== 1) return 3;
        this.imported.push(json);
        this.current = json;
        return 0;
      } catch {
        return 2;
      }
    },
    exportJson() {
      return this.current;
    },
    reset() {
      this.resets++;
    },
    takeDirty() {
      const d = this.dirty;
      this.dirty = false;
      return d;
    },
  };
}

const good = '{"schema":1,"layouts":[{"name":"Mine"}]}';
const older = '{"schema":1,"layouts":[{"name":"Older"}]}';

test("valid latest record is applied and promoted to last-good", async () => {
  const store = memoryStore({ latest: good });
  const target = fakeTarget();
  const outcome = await loadWorkspace(store, target);
  assert.deepEqual(outcome, { source: "latest", notice: null });
  assert.deepEqual(target.imported, [good]);
  assert.equal(store.map.get("last-good"), good);
});

test("corrupt latest falls back to last-good with a notice", async () => {
  const store = memoryStore({ latest: "{garbage", "last-good": older });
  const target = fakeTarget();
  const outcome = await loadWorkspace(store, target);
  assert.deepEqual(outcome, { source: "last-good", notice: NOTICE_LAST_GOOD });
  assert.equal(NOTICE_LAST_GOOD, "Restored last good workspace");
  assert.deepEqual(target.imported, [older]);
  assert.equal(store.map.get("last-good"), older); // untouched
});

test("both corrupt resets to defaults with a notice", async () => {
  const store = memoryStore({ latest: "{bad", "last-good": '{"schema":99}' });
  const target = fakeTarget();
  const outcome = await loadWorkspace(store, target);
  assert.deepEqual(outcome, { source: "defaults", notice: NOTICE_RESET });
  assert.equal(NOTICE_RESET, "Workspace reset to defaults (saved data was unreadable)");
  assert.equal(target.resets, 1);
});

test("first run with nothing stored uses defaults silently", async () => {
  const outcome = await loadWorkspace(memoryStore(), fakeTarget());
  assert.deepEqual(outcome, { source: "empty", notice: null });
});

test("a store that throws is treated as unreadable, not fatal", async () => {
  const store = {
    async get() {
      throw new Error("blocked");
    },
    async put() {},
  };
  const outcome = await loadWorkspace(store, fakeTarget());
  assert.equal(outcome.source, "defaults");
});

test("autosave debounces dirty changes for one second", async () => {
  const store = memoryStore();
  const target = fakeTarget();
  const autosave = createAutosave(store, target, 1000);
  target.dirty = true;
  await autosave.tick(0);
  assert.equal(store.writes.length, 0);
  await autosave.tick(999);
  assert.equal(store.writes.length, 0);
  target.dirty = true; // more edits push the deadline
  await autosave.tick(500);
  await autosave.tick(1499);
  assert.equal(store.writes.length, 0);
  await autosave.tick(1500);
  assert.deepEqual(store.writes, ["latest"]);
  assert.equal(store.map.get("latest"), target.current);
});

test("import validation rejects oversize and non-JSON files with a reason", () => {
  assert.deepEqual(validateImportFile("x".repeat(MAX_WORKSPACE_BYTES + 1)), {
    ok: false,
    reason: "File is larger than 256 KiB",
  });
  assert.deepEqual(validateImportFile("not json"), { ok: false, reason: "File is not valid JSON" });
  assert.deepEqual(validateImportFile(good), { ok: true });
});
