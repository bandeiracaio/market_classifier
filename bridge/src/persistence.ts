// Workspace persistence (packet §8): IndexedDB records `latest` and `last-good`, with
// fallback latest -> last-good -> built-in defaults. The engine owns schema validation
// and migrations (mc_workspace_import); this module only stores opaque JSON text.

export const DB_NAME = "market-classifier";
export const STORE_NAME = "workspace";
export const KEY_LATEST = "latest";
export const KEY_LAST_GOOD = "last-good";
export const MAX_WORKSPACE_BYTES = 256 * 1024; // ui::k_max_workspace_json_bytes
export const AUTOSAVE_DEBOUNCE_MS = 1000;

export const NOTICE_LAST_GOOD = "Restored last good workspace";
export const NOTICE_RESET = "Workspace reset to defaults (saved data was unreadable)";

/** Minimal async key/value surface (IndexedDB in the browser, a Map in tests). */
export interface KeyValueStore {
  get(key: string): Promise<string | undefined>;
  put(key: string, value: string): Promise<void>;
}

/** Engine side of the workspace boundary (WASM exports). importJson returns 0 on success. */
export interface WorkspaceTarget {
  importJson(json: string): number;
  exportJson(): string;
  reset(): void;
  takeDirty(): boolean;
}

export type LoadSource = "latest" | "last-good" | "defaults" | "empty";
export interface LoadOutcome {
  source: LoadSource;
  notice: string | null;
}

async function read(store: KeyValueStore, key: string): Promise<string | undefined | null> {
  try {
    return await store.get(key);
  } catch {
    return null; // unreadable store: treated like corrupt data
  }
}

function applies(target: WorkspaceTarget, json: string | undefined | null): json is string {
  return typeof json === "string" && json.length <= MAX_WORKSPACE_BYTES && target.importJson(json) === 0;
}

/** Loads the workspace on startup, telling the user which fallback happened. */
export async function loadWorkspace(store: KeyValueStore, target: WorkspaceTarget): Promise<LoadOutcome> {
  const latest = await read(store, KEY_LATEST);
  if (applies(target, latest)) {
    try {
      await store.put(KEY_LAST_GOOD, latest);
    } catch {
      /* keep running; next save retries */
    }
    return { source: "latest", notice: null };
  }
  const lastGood = await read(store, KEY_LAST_GOOD);
  if (applies(target, lastGood)) {
    return { source: "last-good", notice: NOTICE_LAST_GOOD };
  }
  if (latest === undefined && lastGood === undefined) {
    return { source: "empty", notice: null }; // first run: engine already has defaults
  }
  target.reset();
  return { source: "defaults", notice: NOTICE_RESET };
}

/** Saves `latest` once the workspace has been quiet for `debounceMs` after a change. */
export function createAutosave(store: KeyValueStore, target: WorkspaceTarget, debounceMs = AUTOSAVE_DEBOUNCE_MS) {
  let deadline: number | null = null;
  return {
    async tick(nowMs: number): Promise<void> {
      if (target.takeDirty()) deadline = nowMs + debounceMs;
      if (deadline === null || nowMs < deadline) return;
      deadline = null;
      const json = target.exportJson();
      if (json.length === 0 || json.length > MAX_WORKSPACE_BYTES) return;
      try {
        await store.put(KEY_LATEST, json);
      } catch {
        deadline = nowMs + debounceMs; // retry later
      }
    },
  };
}

export type ImportCheck = { ok: true } | { ok: false; reason: string };

/** Host-side checks before handing an imported file to the engine (which validates schema). */
export function validateImportFile(text: string): ImportCheck {
  if (new TextEncoder().encode(text).length > MAX_WORKSPACE_BYTES) {
    return { ok: false, reason: "File is larger than 256 KiB" };
  }
  try {
    JSON.parse(text);
  } catch {
    return { ok: false, reason: "File is not valid JSON" };
  }
  return { ok: true };
}

/** Human reason for an engine CodecError (ui::CodecError). */
export function codecErrorReason(code: number): string {
  switch (code) {
    case 1:
      return "File is larger than 256 KiB";
    case 2:
      return "File is not a valid workspace";
    case 3:
      return "Unsupported workspace version";
    case 4:
      return "Workspace exceeds limits (too many layouts or panels)";
    default:
      return "Unknown error";
  }
}

/** IndexedDB-backed store: database `market-classifier`, object store `workspace`. */
export function openIndexedDbStore(factory: IDBFactory): Promise<KeyValueStore> {
  return new Promise((resolve, reject) => {
    const request = factory.open(DB_NAME, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains(STORE_NAME)) request.result.createObjectStore(STORE_NAME);
    };
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result;
      const run = <T>(mode: IDBTransactionMode, op: (s: IDBObjectStore) => IDBRequest<T>) =>
        new Promise<T>((res, rej) => {
          const r = op(db.transaction(STORE_NAME, mode).objectStore(STORE_NAME));
          r.onsuccess = () => res(r.result);
          r.onerror = () => rej(r.error);
        });
      resolve({
        get: async (key) => {
          const value = await run<unknown>("readonly", (s) => s.get(key));
          return typeof value === "string" ? value : undefined;
        },
        put: async (key, value) => {
          await run("readwrite", (s) => s.put(value, key));
        },
      });
    };
  });
}
