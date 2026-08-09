#!/usr/bin/env bash
# Restore the source-only dependencies intentionally excluded by .gitignore.
#
# CI normally reaches the pinned public repositories directly. If a local or
# checked-in hoot-dependencies-latest.zip is present, use it instead; this
# keeps offline/source-package builds reproducible as well.

set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
DEPENDENCY_ROOT="$PROJECT_ROOT/third_party"
ARCHIVE="$PROJECT_ROOT/hoot-dependencies-latest.zip"

REQUIRED_SOURCES=(
  "$DEPENDENCY_ROOT/libkss/modules/kmz80/kmz80.c"
  "$DEPENDENCY_ROOT/libkss/modules/kmz80/kmz80c.c"
  "$DEPENDENCY_ROOT/libkss/modules/kmz80/kmz80t.c"
  "$DEPENDENCY_ROOT/libkss/modules/kmz80/kmevent.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/ay8910.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/fmopn.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/ym2151.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/nukedopm.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/ymdeltat.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/fmopl.c"
  "$DEPENDENCY_ROOT/libvgm/emu/cores/ymf262.c"
  "$DEPENDENCY_ROOT/libvgm/emu/logging.c"
  "$DEPENDENCY_ROOT/px68k-libretro/m68000/musashi/m68kcpu.c"
  "$DEPENDENCY_ROOT/px68k-libretro/m68000/musashi/m68kops.c"
  "$DEPENDENCY_ROOT/px68k-libretro/m68000/musashi/softfloat/softfloat.c"
)

sources_present() {
  local source
  for source in "${REQUIRED_SOURCES[@]}"; do
    [[ -f "$source" ]] || return 1
  done
}

if sources_present; then
  echo "Hoot vendored dependency sources are already present."
  exit 0
fi

if [[ -d "$DEPENDENCY_ROOT" ]] && find "$DEPENDENCY_ROOT" -mindepth 1 -print -quit | grep -q .; then
  echo "error: third_party exists but is incomplete; refusing to overwrite it: $DEPENDENCY_ROOT" >&2
  exit 1
fi

if [[ -f "$ARCHIVE" ]]; then
  command -v unzip >/dev/null 2>&1 || {
    echo "error: unzip is required to restore $ARCHIVE" >&2
    exit 1
  }
  archive_tmp=$(mktemp -d "${TMPDIR:-/tmp}/hoot-dependencies.XXXXXX")
  trap 'rm -rf -- "$archive_tmp"' EXIT INT TERM HUP
  unzip -q "$ARCHIVE" -d "$archive_tmp"
  archive_root="$archive_tmp/hoot-dependencies/third_party"
  sources_present_from_archive=1
  for source in "${REQUIRED_SOURCES[@]}"; do
    relative=${source#"$DEPENDENCY_ROOT/"}
    [[ -f "$archive_root/$relative" ]] || sources_present_from_archive=0
  done
  if ((sources_present_from_archive)); then
    mkdir -p "$DEPENDENCY_ROOT"
    cp -a "$archive_root/." "$DEPENDENCY_ROOT/"
    echo "Restored Hoot vendored dependencies from $ARCHIVE."
    exit 0
  fi
  echo "error: $ARCHIVE does not contain all required Hoot dependency sources" >&2
  exit 1
fi

mkdir -p "$DEPENDENCY_ROOT"

clone_at_revision() {
  local name=$1
  local url=$2
  local revision=$3
  local destination="$DEPENDENCY_ROOT/$name"

  git clone --filter=blob:none --no-checkout "$url" "$destination"
  git -C "$destination" fetch --no-tags --depth 1 origin "$revision"
  git -C "$destination" checkout --detach "$revision"
}

# These revisions match the dependency package described in
# md/DEPENDENCY_PACKAGE.md. Pinning avoids an upstream update silently changing
# the source used by the platform builds.
clone_at_revision libkss \
  https://github.com/digital-sound-antiques/libkss.git \
  751cd56267ba4eaca6b0e1b45f9184473620aebe
git -C "$DEPENDENCY_ROOT/libkss" submodule update --init --recursive

clone_at_revision libvgm \
  https://github.com/ValleyBell/libvgm.git \
  867223e7c33d63de115d1ab955f784c44f19040a

clone_at_revision px68k-libretro \
  https://github.com/libretro/px68k-libretro.git \
  45dfd4005434d1199b01fb74a5371ec9bc513164

if ! sources_present; then
  echo "error: dependency restore completed, but required source files are missing" >&2
  exit 1
fi

echo "Restored Hoot vendored dependencies from pinned upstream revisions."
