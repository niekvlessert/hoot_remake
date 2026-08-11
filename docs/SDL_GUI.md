# SDL3 graphical frontend

`hootui` is a cross-platform SDL3 frontend for `libhoot`. Its layout preserves
the information architecture of the original Hoot graphical player while the
window chrome/menu styling is platform-neutral.

The fixed logical canvas is 1440x900 and SDL logical presentation scales it to
the actual window. The main areas are:

- left: one piano keyboard strip per sound channel, with channel label, centre-origin stereo level meter and current note/tone below it;
- upper right: architecture/driver/CPU/device, stereo spectrum analyzer,
  CPU registers and driver-work hex view;
- bottom: a scrolling NOW PLAYING line for the actual selected track, complete track list and playback status.

Audio uses an SDL3 `SDL_AudioStream` in push mode. The GUI thread fills a small
audio queue from `hoot_render_s16()` and sends the exact same final stereo mix
to the spectrum analyzer. No emulator is called from an asynchronous SDL audio
callback, avoiding UI/audio data races in the initial implementation.

Controls:

- O or click **Open**: native SDL file dialog for a Hoot ZIP
- Space: pause/resume
- Left/P and Right/N: previous/next track
- Up/Down: scroll playlist
- PageUp/PageDown: scroll channel bank
- Q/Escape: quit
- drag a Hoot ZIP onto the window: load that pack

Text is UTF-8 end to end and Japanese rendering is a mandatory GUI feature.
A native `HOOT_BUILD_GUI=ON` configuration now fails at CMake time unless both
SDL3 and SDL3_ttf are present; Hoot no longer produces a silent ASCII-only GUI.
At runtime the renderer searches fonts in this order: `--font`, `[gui] font`, the
`HOOT_UI_FONT` environment variable, a build-time Web font, then Japanese-capable
system fonts. macOS also recursively checks the standard System/Library/User font
roots because Apple has renamed and relocated the Hiragino fonts across releases.
Every candidate is glyph-tested for both Hiragana and Kanji. If no suitable font
exists, `hootui` shows a fatal dialog and exits instead of replacing Japanese text
with question marks. Hoot does not bundle a font. `hootui --check-font` performs
the same headless probe and is used by CI on Linux, Windows and macOS.

Clipping is measured with SDL3_ttf and truncates only on UTF-8 codepoint boundaries,
so a Japanese title can never be cut into an invalid byte sequence. The channel
meters use the visual channel pan to grow from the centre into left/right halves;
attack is immediate and release decays smoothly for roughly half a second, matching
the readable behaviour of the classic Hoot display. The note readout uses the
classic `o4f+` style where note information is available.

## Configuration

`hootui` uses the same `hootplay.ini` format and search order as `hootplay`.
On native first start both programs create `~/.hoot`; a `./catalog` directory and
`./hootplay.ini` are imported there once if present. Normal lookup is then
`--config`, `HOOTPLAY_CONFIG`, `~/.hoot/hootplay.ini`, the current directory, and
finally the executable directory as a legacy fallback. Player catalogue/packs/rate/track settings are applied before
command-line overrides, and backend settings are applied before `libhoot` is
created. This includes SoundFonts, Munt/MT-32 and CM-32L ROM paths, CM-32P and
SN-U110 ROM paths, Nuked-SC55, Vermouth and MXDRV/mdxmini settings.

The GUI-specific font can be selected without affecting the CLI:

```ini
[gui]
font = /path/to/NotoSansCJK-Regular.ttc
```

The same value can be overridden once with `hootui --font /path/to/font.ttc`.
This is intentional: switching from the CLI player to the GUI must not silently
change the configured replay or MIDI synth backend.


## Playback requirement overlay

When the selected track reports additional hardware, ROM, synth-backend or
compatibility requirements through `HootTrackInfo.warning`, `hootui` shows a
dismissible modal overlay instead of relying on terminal output. The dialog
contains the original libhoot warning plus actionable hints. ROM-related
messages point at the relevant default directory under `~/.hoot/roms/`
(`mt32`, `cm32l`, `cm32p` or `sc55`) and provide a direct **Open Settings**
button. **Close**, the title-bar X, Escape or Enter dismiss the notice. The
same exact warning is suppressed for later track changes within that pack,
while a different requirement is shown; loading another pack resets the
suppression. Diagnostic stderr output is retained for logs/terminal use.


## Settings dialog

Click **Settings** in the top bar or press **Ctrl+,**. The modal settings editor
exposes every named setting accepted by `hootplay.ini`, grouped by General,
Player, Interface, MIDI, X68000, PSG/SSG, PC-98 and PC-88. Each setting can be
enabled/disabled independently so catalogue/autodetection defaults remain
intact. Boolean and enumerated fields cycle directly; text, numeric and path
fields support SDL text input. Save performs a parser validation before atomically
replacing the INI. Backend, catalogue, audio-rate and font changes intentionally
take effect on the next start. Existing arbitrary `[environment]` entries are
preserved.

## Library browser

`Library` (or the `L` key) opens the catalog browser. Its hierarchy intentionally
follows original Hoot's `ssSoundDriverManager::MakeFolders()` model:

```
/                         root
  - all -                 every catalog game
  x68k                    driver major type
    - all -               every x68k game
    generic               driver subtype
      game                catalog entry
        track             title list
    mxdrv
  pc98dos
  ...
```

The native browser preserves the original selector semantics:

- Up/Down, PageUp/PageDown, Home/End move the selector.
- Enter descends into a folder, loads a game, or plays a track.
- Space on a track plays it and advances the selector one row, matching classic Hoot.
- Backspace/Escape returns to the parent; Escape at the root closes the Library.
- Each folder remembers its own selection and scroll position for the process lifetime.

Enhancements that do not change the hierarchy:

- Mouse selection and double-click activation.
- `F` / `Ctrl+F` starts UTF-8 search. At the root or a driver-family folder it
  searches games recursively; in game, variant, and track lists it filters the
  current folder. Titles, entry IDs, archive names, and driver names are searchable,
  and Japanese queries are supported.
- Games show `READY` when `<archive>.zip` is present in the configured or last-used pack
  directory, otherwise `missing pack`. Missing games remain visible just like original Hoot.
- Recognizable external MIDI targets (MT-32, CM-64, SC-55, SC-88, Vermouth, etc.) are
  annotated. The normal playback requirement overlay remains authoritative for actual ROM
  or backend requirements after the game is loaded.

Selecting a game loads its driver/pack but keeps the Library open on the game's title list,
so tracks can be auditioned without leaving the browser. Playback continues when navigating
back up the Library tree.


## Channel mute / solo

The channel display is interactive. Hoot keeps the emulated guest driver running
and suppresses only the selected host-side sound voice, so muting a channel does
not stop its timers, sequence state or visual activity.

- left-click a channel row: toggle mute
- right-click, Shift+click or double-click a row: toggle solo
- `1`..`9`, `0`: mute/unmute the first ten rows in the currently visible channel bank
- `Shift+1`..`Shift+0`: solo the corresponding visible row
- `U`: clear every channel mute and solo state
- `M`: master output mute; independent from channel muting

Muted channels remain visible and their activity meters are dimmed rather than
frozen. `Mxx` and `Sxx` markers identify muted and soloed rows. Not every legacy
backend exposes independent voices; when a backend cannot perform a requested
channel mute, Hoot reports that explicitly instead of pretending the channel was
silenced.

## Classic visual behaviour

The channel meters use the classic center-origin stereo presentation: centered
signals extend left and right, while panned signals extend toward their audible
side. They have fast attack, a short peak hold and smooth release. Keyboard
highlights, note/tone text and the scrolling current-track strip remain active
while playback is running. The spectrum analyser uses the same fast-attack,
slow-release principle so quiet bands decay smoothly rather than snapping off.

## Catalog editor

The native Library can edit catalogue entries without modifying the upstream
XML/JSON/SQLite file. Select a game and press `E` or click **Edit entry**.
Changes are stored atomically in `~/.hoot/catalog/user-overrides.json`, which is
also consumed by libhoot/hootplay. The editor covers general metadata, tracks,
options, assets and Hoot MIDI hardware targets, supports reset-to-base and local
variant duplication, and reapplies saved changes immediately.

## Playback menu

The SDL frontend has a functional **Playback** menu rather than a placeholder:

- Play / Pause (`Space`)
- Stop (`Ctrl+S`)
- Restart Track (`Ctrl+R`)
- Previous Track (`Left` / `P`)
- Next Track (`Right` / `N`)
- Mute All / Unmute All (`M`) — master output mute
- Clear Channel Mutes / Solo (`U`)
- Record WAV / Stop WAV Recording (`Ctrl+Shift+R`)

Stop clears queued audio; Play after Stop restarts the selected track. Mute All
is an output mute: the replay engine continues advancing so visual state stays
live. WAV recording captures the unmuted rendered signal before the UI output
mute. Native builds use SDL's save-file dialog; the browser writes to its
persistent `/hoot/recordings` directory and downloads the finalized file.
