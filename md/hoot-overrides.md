# Hoot Catalog Overrides

`hoot-overrides.xml` augments imported Hoot catalogs without editing their
upstream XML files. The loader applies it after the base catalog and all child
lists have been read.

The default lookup order is:

1. `hoot-overrides.xml` in the current directory.
2. `hoot-overrides.xml` beside the selected catalog.

Set `HOOT_OVERRIDE_XML` to use an explicit path. A configured path must exist.
Games absent from the selected catalog are ignored, allowing one override file
to cover multiple catalogs.

## Voice banks

An override can match a game by `archive`, `id`, or both. Named voice banks are
additional required members of the game's pack archive. Track codes then select
which bank is mapped before playback starts.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<hoot-overrides>
  <game archive="xak68snd">
    <asset file="SYOKI2.BGM" transform="opmdrv-compact-voice"/>
    <voicebank id="B" file="SYOKI2_B.BGM" offset="0x20000"
               transform="opmdrv-compact-voice"/>
    <track code="0x16" voicebank="B"/>
  </game>
</hoot-overrides>
```

Malformed matched entries, unknown track codes, and missing declared archive
members are errors. Tracks without a `voicebank` assignment retain the bank
loaded by the original Hoot XML.

`<option name="..." value="..."/>` adds or replaces a numeric driver option.
For example, `reset_on_track=1` asks the X68000 generic runtime to restart the
emulated machine for every selection. Without it, track changes preserve the
resident driver and issue the upstream catalog's `stop` command first.

`<asset>` applies a transform to an asset already declared by the upstream
catalog. `opmdrv-compact-voice` expands compact OPMDRV voice definitions into
the NUL-free textual form expected by Hoot pack bootstraps that use C-string
writes. The same transform is accepted on a named `<voicebank>`.
