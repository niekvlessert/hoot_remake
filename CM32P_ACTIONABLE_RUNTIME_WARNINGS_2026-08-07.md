# CM-32P / CM-64 actionable runtime warnings — 2026-08-07

## Scope

Improve the pack-dependent CM-64/CM-32P/SN-U110 runtime warnings so a user can act on them without first reading the configuration file.

## Behaviour

All messages are in English and are only emitted when the selected Hoot configuration actually requires the corresponding hardware/ROM data.

### CM-64 with missing CM-32P PCM ROMs

The warning identifies the required CM-32P PCM dumps as IC18, IC19 and IC20, states that all three are 512 KiB dumps, gives `roms/cm32p` as an example directory, and tells the user to configure:

```ini
[midi]
cm32p_rom_path=roms/cm32p
```

It also states that playback is falling back to CM-32L/LA only and that CM-32P PCM instruments are missing, making playback less authentic.

### CM-64 configuration requiring SN-U110-07 or SN-U110-10

When the catalogue/title names an SN-U110 card, the warning also identifies the exact card file and corresponding setting. For example, SN-U110-10:

```ini
[midi]
cm32p_rom_path=roms/cm32p
cm32p_card_rom_10=roms/cm32p/SN-U110-10.bin
```

The warning explains that CM-32P and expansion-card PCM sounds are absent while the LA-only fallback is active.

### CM-32P available but requested SN-U110 card missing

If the base CM-32P PCM ROMs are available but a named SN-U110 card ROM is missing, the warning only asks for that card. Example:

```ini
[midi]
cm32p_card_rom_10=roms/cm32p/SN-U110-10.bin
```

Playback continues without the expansion-card sounds.

### Unaffected configurations

Ordinary MT-32 and CM-32L configurations do not receive CM-32P/SN-U110 warnings. A fully configured CM-64/CM-32P configuration also remains warning-free.

## Implementation

Updated both PC-98 DOS and X68000 generic drivers. The messages were deliberately kept within the existing `HOOT_TRACK_WARNING_MAX=256` ABI field rather than enlarging the public track-info structure.

The PC-98 MIDI integration test now separately verifies:

- ordinary CM-64 fallback contains `cm32p_rom_path=roms/cm32p` and points to `hootplay.ini`;
- CM-64 + SN-U110-10 contains `cm32p_card_rom_10=roms/cm32p/SN-U110-10.bin`;
- ordinary MT-32 remains free of these warnings.

## Validation

Release test suite: **14/14 passed**.

No Roland ROM images, music packs, or build products are included in the source package.
