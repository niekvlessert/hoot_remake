# Hoot UI catalog editor stage — 2026-08-08

## Goal

Provide a safe native editor for the Hoot catalogue without making the runtime
SQLite/Zstd database the source of truth. Hoot catalogues may be loaded from
legacy XML, JSON shards/manifests, or SQLite/Zstd; all three now accept the same
per-user editing layer.

## User override layer

Native startup sets `HOOT_USER_OVERRIDES` to:

```
~/.hoot/catalog/user-overrides.json
```

The file is optional and is created only after the first save. Format:

```json
{
  "format": "hoot-user-overrides",
  "version": 1,
  "entries": {
    "entry-id": {
      "title": "edited title",
      "archive": "packname",
      "driver": { "name": "x68k", "type": "generic" },
      "default_sample_rate": 44100,
      "refresh_hz": 60,
      "options": { "midiout_type": 2 },
      "assets": [],
      "tracks": [
        { "code": 1, "title": "Track title", "voice_bank": "" }
      ]
    }
  }
}
```

The layer is applied after the base catalogue and the historical
`hoot-overrides.xml` layer. Existing entry patches may be partial; hootui writes
complete logical entry patches. `hidden=true` removes an entry from the runtime
view, while `create=true` permits a new local entry/variant.

Writes are atomic (`.tmp` followed by rename). A malformed user file fails the
catalogue load rather than silently ignoring edits.

## UI workflow

Open **Library**, select a game and press **E** or click **Edit entry**.

Editor tabs:

- **general** — title, archive, driver family/subtype, sample rate, refresh rate;
- **tracks** — code, Unicode title and voice bank; Insert/Delete are supported;
- **options** — arbitrary Hoot option name/value pairs;
- **assets** — asset type/path/transform/offset;
- **hardware** — friendly `midiout_type` selector for MT-32, CM-64, SC-55,
  Korg M1 compatibility, Vermouth, SC-88 and GM;
- **raw** — documents the actual UTF-8 JSON file and layering model.

`Ctrl+S` saves. **Reset** removes only the local override and reloads the base
catalogue value. **Duplicate** / `D` creates a copied local variant with a unique
id and `create=true`; tracks/assets/options are copied as a unit.

Saving reloads the catalogue immediately. If music was playing, hootui reopens
the previously active entry/track so editing unrelated metadata does not stop
playback.

The Library appends `EDITED` to entries that have a local patch.

## libhoot/API additions

The public discovery API now exposes full catalogue data needed by editors:

- `hoot_get_entry_catalog_track_info()`
- `hoot_get_entry_driver_info()` (preserves driver alias/platform metadata for copied variants)
- `hoot_get_entry_option_count()` / `hoot_get_entry_option_info()`
- `hoot_get_entry_asset_count()` / `hoot_get_entry_asset_info()`
- `HootEntryTrackInfo` exposes track code/title/voice bank without changing the older `HootCatalogTrackInfo` ABI

The user override parser/writer is shared by libhoot and hootui, so the GUI does
not maintain a second interpretation of catalogue structure.

## Validation

- existing core tests remain green;
- new `hoot_user_overrides` test verifies UTF-8 save/load/application and normal
  catalogue-loader layering;
- CLI smoke verifies an edited Japanese Slayers title appears through
  `hootplay --list`;
- SDL frontend sources are syntax-checked in both editor/renderer code paths.
