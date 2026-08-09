# CM-32P / SN-U110 pack-requirement warnings — 2026-08-07

## Goal

Make optional Roland ROM requirements visible only when the selected Hoot
configuration actually targets hardware that needs them. A user should be able
to distinguish "playback works but is less authentic" from a hard synth
failure, without seeing irrelevant ROM warnings on ordinary MT-32/CM-32L
entries.

## Runtime behavior

Both the PC-98 DOS MIDI host and the X68000 generic MIDI host now derive the
CM-32P expansion-card requirement from the selected catalog entry.

### CM-64 without CM-32P PCM ROMs

When `midiout_type=3` selects CM-64, the full CM-64 backend is attempted first.
If Munt/CM-32L is available but the CM-32P PCM section cannot open, `auto`
continues with the existing LA-only fallback and reports:

`This pack targets CM-64. Install the CM-32P IC18/IC19/IC20 PCM ROMs for authentic playback; using CM-32L/LA-only fallback, so CM-32P PCM parts are missing.`

This warning is emitted only for a CM-64 configuration. MT-32 and normal
CM-32L paths are unaffected.

### Named SN-U110 card variants

The existing catalog-title detection for `SN-U110-07` and `SN-U110-10` is now
also used for user-facing diagnostics.

If the selected CM-64 variant names a card and the entire PCM section is
missing, the fallback warning includes both requirements, for example:

`This pack targets CM-64 and expects SN-U110-10. Install CM-32P IC18/IC19/IC20 plus the SN-U110-10 PCM card ROM for authentic playback; using CM-32L/LA-only fallback, so PCM/card sounds are missing.`

If IC18/19/20 are present and CM-32P opens, but the requested card ROM itself
is not installed, playback continues with the base CM-32P sounds and reports:

`This pack expects Roland SN-U110-10. Install its PCM card ROM (midi.cm32p_card_rom_10) for authentic playback; expansion-card sounds are unavailable.`

If the requested card ROM is installed, no warning is emitted.

## Implementation

`Cm32pMidiSynth` now exposes diagnostic-only card state (`card_requested`,
`card_loaded`, and `card_model`). `Cm64MidiSynth` forwards the corresponding
PCM-side state. These accessors do not alter rendering or MIDI behavior.

Both platform drivers retain the existing automatic fallback policy, but
replace the low-level ROM-path fallback message with a pack-aware explanation
of the audible consequence.

`hootplay.ini` documents that these notices are selection-dependent.

## Validation

Release build: successful.

CTest: 14/14 passed.

Coverage includes:

- CM-32P base ROMs with no requested card;
- requested SN-U110-10 absent while base CM-32P remains usable;
- requested SN-U110-10 present;
- synthetic PC-98 CM-64 + SN-U110-10 falling back to CM-32L and requiring the
  explicit authenticity warning;
- ordinary PC-98 MT-32/Munt playback requiring an empty warning string.
