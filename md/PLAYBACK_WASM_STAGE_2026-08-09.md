# Playback menu and WebAssembly stage — 2026-08-09

## Playback menu

The SDL `Playback` menu is functional and shares the same actions with keyboard shortcuts:

- Play / Pause — Space
- Stop — Ctrl+S
- Restart Track — Ctrl+R
- Previous Track — Left / P
- Next Track — Right / N
- Mute All / Unmute All — M
- Record WAV / Stop WAV Recording — Ctrl+Shift+R

Stop clears queued SDL audio. Play after Stop restarts the selected track. Master mute affects output only; replay/visual state continues to advance. The WAV recorder captures 16-bit stereo PCM before output mute and patches the RIFF sizes when recording stops. Native builds use the SDL save-file dialog. Browser builds persist the recording and then offer it as a download.

## WebAssembly

The Emscripten frontend is the same SDL UI and replay core, not a separate JavaScript player.

- SDL3, SDL3_ttf, SQLite and zlib are Emscripten ports.
- A Japanese-capable font is mandatory at configure time and is preloaded into the bundle.
- The shipped `catalog/hoot.sqlite.zst` is expanded on the build host; the browser preloads ordinary `/catalog/hoot.sqlite` and opens it directly.
- `/hoot` is mounted as IDBFS before C++ `main()` and contains persistent `hootplay.ini`, UI state, uploaded packs, catalog overrides and recordings.
- Open from the HTML toolbar, SDL Open button/key, multi-file picker, and drag/drop all import ZIPs into `/hoot/packs` and refresh the normal recursive Library index.
- Browser audio is resumed after a user gesture.
- Browser Ctrl+S/Ctrl+R/Ctrl+Shift+R defaults are suppressed while the SDL canvas has focus so Hoot's Playback accelerators work.
- Native dynamic MIDI/plugin loaders (Munt, FluidSynth, Vermouth, Nuked-SC55, mdxmini) do not attempt `dlopen` in WebAssembly. They fail cleanly into the existing hardware/backend warning path until dedicated static/WASM backends exist.

## CI / validation

- Native suite: 28 tests (new `wav_recorder` test included).
- Web CMake path has a dedicated GitHub Actions job using Emscripten.
- CI builds `hootweb.html/js/wasm/data`, validates the preloaded Japanese UTF-8 production catalog and checks IDBFS/pack/font/catalog wiring.
- The source distribution does not include a font file; CI provides a Japanese-capable font to `HOOT_GUI_FONT_FILE` during the web build.
