# CI and Termux builds

## Desktop CI

`.github/workflows/ci.yml` builds and tests Hoot on:

- Linux x64 (`ubuntu-latest`)
- Windows x64 (`windows-latest`)
- macOS arm64 (`macos-15`)

The desktop matrix installs zlib, SQLite, zstd, SDL3 and SDL3_ttf through
vcpkg. `HOOT_BUILD_GUI=ON` is mandatory, and CMake itself now fails if the
SDL3_ttf-backed Unicode renderer cannot be built. Linux CI additionally installs
Noto CJK. After the normal CTest suite each desktop job runs
`hootui --check-font`, which uses the exact production font locator and verifies
that a font containing Hiragana and Kanji can really be opened. Then
`ci/unicode_smoke.py` launches `hootplay --list` against the production catalogue
and decodes output strictly as UTF-8; it explicitly checks Japanese catalogue
text including `スレイヤーズ`.

The install tree from every desktop build is uploaded as a GitHub Actions
artifact. These are CI build artifacts, not fully self-contained redistributable
application bundles; system/runtime library packaging and code signing are a
separate release concern.

## Termux

The same workflow has a best-effort `termux-aarch64` job. It intentionally uses
the official `termux/termux-packages` build infrastructure rather than treating
a regular Linux build as Android-compatible. A temporary local X11 package
recipe is generated for Hoot and built for Android/Bionic aarch64. Dependencies
are downloaded from the Termux repositories with `build-package.sh -I`.

The Termux GUI build depends on the X11 channel's `sdl3` and `sdl3-ttf`
packages. At runtime a graphical `hootui` therefore needs a working Termux X11
environment. The CLI remains usable without opening the UI.

The Termux job is `continue-on-error` because the upstream rolling Termux
package repository can change independently of this project; Linux, Windows and
macOS remain the blocking CI gates.

## Japanese fonts

No copyrighted or third-party font binary is committed or shipped by Hoot.
Native `hootui` uses SDL3_ttf and discovers a Japanese-capable system font, or
the user can select one with `--font` / `[gui] font` in `~/.hoot/hootplay.ini`.
CI validates the Unicode code path, Japanese metadata and native Japanese font
discovery, but does not bundle a font into artifacts.
