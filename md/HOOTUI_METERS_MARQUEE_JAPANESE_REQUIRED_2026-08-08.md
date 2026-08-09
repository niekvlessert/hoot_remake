# hootui Japanese requirement + classic channel display stage — 2026-08-08

## Japanese UI is mandatory

- `HOOT_BUILD_GUI=ON` requires SDL3 >= 3.2 and SDL3_ttf >= 3.0 at CMake configure time.
- Native builds no longer silently compile the SDL debug-font ASCII fallback.
- The runtime font locator validates a representative Hiragana glyph (`あ`) and Kanji glyph (`日`).
- macOS scans the standard system, local and user font roots after trying known Hiragino paths, which makes it resilient to Apple font path/name changes.
- If no Japanese-capable font is available, the native UI displays a fatal SDL dialog and exits instead of showing broken metadata.
- `hootui --check-font` performs the same font probe without creating the player window.
- Desktop GitHub CI executes this probe on Linux, Windows and macOS. Linux installs Noto CJK for the probe.
- Web builds require `HOOT_GUI_FONT_FILE`; no font files are bundled in this project.

## Channel rows

The channel view now follows the classic Hoot visual hierarchy more closely:

1. keyboard strip on top;
2. chip/channel description underneath at the left;
3. stereo activity meter underneath the keyboard, starting at the centre;
4. current note/tone at the right using classic notation such as `o4f+`.

Pan controls the two meter halves: centre produces equal left/right bars, hard-left only extends left and hard-right only extends right. Meter attack follows the current visual level immediately, while release decays smoothly at roughly 1.82 full-scale units per second (~0.55 s from full scale to zero). This is intentionally visual-only and does not alter audio.

The meter now has its own baseline and no longer crosses the channel description glyphs.

## Now-playing marquee

The line at the top of the playlist is now a clipped moving `NOW PLAYING` marquee. It uses the selected track's title rather than only the pack/game title and retains the game title in brackets when useful. Movement is frame-time based rather than tied to audio callback cadence.
