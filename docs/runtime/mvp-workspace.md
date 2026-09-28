# MVP workspace and persistence

Date: 2026-09-27. Code: `engine/include/market_classifier/ui/{workspace,workspace_codec,workspace_controller}.hpp`,
`bridge/src/persistence.ts`.

## Model

- Five built-in presets (packet §7), read-only, always generated from code (never
  persisted): Overview, Tape Reader, Footprint, Liquidity, Derivatives. First run opens
  Overview. Each preset's dock arrangement is built with ImGui DockBuilder (first panel
  large on the left, the rest stacked on the right).
- User layouts (≤ 32): name (1–64 printable bytes), `uid`, panel list (≤ 32; per-panel
  settings incl. venue selector, interval, bucket, grouping, min trade USD, CVD daily
  reset) and the layout's ImGui docking ini.
- Preferences: UTC/local time. Active layout index spans presets then user layouts.

## ImGui identity scoping

Each layout has a stable `uid`. Panel windows are named `Title###L<uid>_<id>` and the layout
owns the dockspace `RootDockSpace<uid>`. Saved ini is filtered to that layout's
`[Window][L<uid>_…]` sections and its own `DockSpace` node tree, so layouts never share
window geometry and ini text cannot grow with other layouts. Known limit: after an import
whose `uid` matches a layout opened earlier in the same session, that session keeps the
earlier runtime geometry until reload.

## Schema (v1)

```json
{"schema":1,"active":5,"utcTime":false,"layouts":[
  {"uid":6,"name":"Mine","imguiIni":"…","panels":[
    {"id":1,"kind":"Cvd","venue":"Both","intervalMs":60000,"bucket":1,
     "grouping":0,"minTradeUsd":0,"cvdDailyReset":false}]}]}
```

Panel kinds and venues are stable strings. Limits: 256 KiB per document, 64 KiB ini per
layout. Decoding never partially applies: any error returns the defaults plus a
`CodecError` (`TooLarge`, `Malformed`, `UnsupportedVersion`, `OutOfBounds`).

## Migrations

| From | To | Change |
|---|---|---|
| 0 (`"version":0`, pre-release) | 1 | `version` → `schema`; `utcTime` added (default `false`); missing `uid` assigned |

Each step is covered by `engine/tests/test_workspace_codec.cpp` (fixture
`fixtures/mvp/workspace-v0.json`).

## Persistence and fallback

IndexedDB database `market-classifier`, store `workspace`, keys `latest` and `last-good`.

1. Load `latest` → engine import succeeds → use it and copy it to `last-good`.
2. Else load `last-good` → succeeds → use it; banner "Restored last good workspace".
3. Else (records present but unreadable) → defaults; banner "Workspace reset to defaults
   (saved data was unreadable)". Nothing stored → defaults silently (first run).

Autosave: the engine raises a dirty flag on any persisted change (layouts, active layout,
prefs, panel settings, docking geometry of user layouts); the host writes `latest` after 1 s
without further changes.

## Export / import / reset

File menu → Export downloads the workspace JSON; Import opens a file picker. The host checks
size (≤ 256 KiB) and JSON syntax; the engine validates schema, version and bounds and applies
atomically. Rejections are shown with a reason. Layouts → Reset workspace asks for
confirmation in-app, then restores defaults.
