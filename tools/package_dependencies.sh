#!/usr/bin/env bash
# Package the Hoot vendored source dependencies into one clean ZIP archive.
#
# Only the canonical dependency checkouts used by CMake are included:
#   third_party/libkss
#   third_party/libvgm
#   third_party/px68k-libretro
#
# Each Git repository is archived from its current HEAD. Nested Git submodules
# are archived recursively at the exact commits pinned by their parent repo.
# This is important for libkss, which has submodules of its own.

set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
OUTPUT="$PROJECT_ROOT/hoot-dependencies-latest.zip"
SYNC_SUBMODULES=1
UPDATE_TOP_LEVEL=0

usage() {
    cat <<USAGE
Usage: $(basename "$0") [options]

Create a clean ZIP containing the current Hoot source dependencies.

Options:
  -o, --output FILE       Output ZIP path.
                          Default: hoot-dependencies-latest.zip
  -t, --third-party DIR   Dependency root.
                          Default: <project>/third_party
      --update            Fast-forward each top-level dependency to its configured upstream.
      --no-sync           Do not run 'git submodule update --init --recursive'.
  -h, --help              Show this help.

The script packages only these canonical directories:
  libkss, libvgm, px68k-libretro

It does not include build trees, ignored/untracked files, .git metadata,
old sibling copies, or existing archives. The working trees must be clean.
USAGE
}

while (($#)); do
    case "$1" in
        -o|--output)
            [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
            OUTPUT=$2
            shift 2
            ;;
        -t|--third-party)
            [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
            THIRD_PARTY_DIR=$2
            shift 2
            ;;
        --update)
            UPDATE_TOP_LEVEL=1
            shift
            ;;
        --no-sync)
            SYNC_SUBMODULES=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for command in git tar zip mktemp; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "error: required command not found: $command" >&2
        exit 1
    }
done

THIRD_PARTY_DIR=$(CDPATH= cd -- "$THIRD_PARTY_DIR" 2>/dev/null && pwd) || {
    echo "error: third-party directory not found: $THIRD_PARTY_DIR" >&2
    exit 1
}

case "$OUTPUT" in
    /*) ;;
    *) OUTPUT="$PWD/$OUTPUT" ;;
esac

DEPENDENCIES=(libkss libvgm px68k-libretro)
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/hoot-dependencies.XXXXXX")
STAGE_DIR="$TMP_DIR/hoot-dependencies"
MANIFEST="$STAGE_DIR/DEPENDENCIES-MANIFEST.txt"

cleanup() {
    rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$STAGE_DIR/third_party"

ensure_clean_repo() {
    local repo=$1
    local dirty

    git -C "$repo" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
        echo "error: not a Git working tree: $repo" >&2
        exit 1
    }

    dirty=$(git -C "$repo" status --porcelain --untracked-files=no)
    if [[ -n "$dirty" ]]; then
        echo "error: dependency has tracked modifications: $repo" >&2
        echo "Commit or stash them before packaging; git archive only exports HEAD." >&2
        exit 1
    fi
}

update_top_level_repo() {
    local repo=$1
    local branch upstream

    ensure_clean_repo "$repo"
    branch=$(git -C "$repo" symbolic-ref --quiet --short HEAD 2>/dev/null || true)
    if [[ -z "$branch" ]]; then
        echo "warning: detached checkout; keeping pinned commit: $repo" >&2
        return 0
    fi

    upstream=$(git -C "$repo" rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null || true)
    if [[ -z "$upstream" ]]; then
        echo "warning: branch has no upstream; keeping current commit: $repo" >&2
        return 0
    fi

    echo "Updating $(basename "$repo") from $upstream..."
    git -C "$repo" fetch --tags --prune
    git -C "$repo" merge --ff-only "$upstream"
}

sync_submodules() {
    local repo=$1

    [[ -f "$repo/.gitmodules" ]] || return 0
    git -C "$repo" submodule sync --recursive
    git -C "$repo" submodule update --init --recursive
}

# Archive one repository and recursively replace each gitlink with the contents
# of the checked-out submodule at the exact pinned commit.
archive_repo_recursive() {
    local repo=$1
    local dest=$2
    local sub_path sub_repo

    ensure_clean_repo "$repo"
    mkdir -p "$dest"
    git -C "$repo" archive --format=tar HEAD | tar -xf - -C "$dest"

    [[ -f "$repo/.gitmodules" ]] || return 0

    while IFS= read -r sub_path; do
        [[ -n "$sub_path" ]] || continue
        sub_repo="$repo/$sub_path"

        if ! git -C "$sub_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
            echo "error: submodule is not initialized: $sub_repo" >&2
            echo "Run without --no-sync, or initialize submodules manually." >&2
            exit 1
        fi

        rm -rf -- "$dest/$sub_path"
        archive_repo_recursive "$sub_repo" "$dest/$sub_path"
    done < <(git -C "$repo" config --file .gitmodules --get-regexp 'submodule\..*\.path' 2>/dev/null | awk '{print $2}')
}

append_repo_manifest() {
    local label=$1
    local repo=$2
    local commit describe branch date

    commit=$(git -C "$repo" rev-parse HEAD)
    describe=$(git -C "$repo" describe --tags --always --dirty 2>/dev/null || printf '%s' "$commit")
    branch=$(git -C "$repo" symbolic-ref --quiet --short HEAD 2>/dev/null || printf '(detached)')
    date=$(git -C "$repo" show -s --format='%cI' HEAD)

    {
        printf '%s\n' "$label"
        printf '  path: %s\n' "$repo"
        printf '  commit: %s\n' "$commit"
        printf '  describe: %s\n' "$describe"
        printf '  branch: %s\n' "$branch"
        printf '  commit-date: %s\n' "$date"
    } >> "$MANIFEST"

    if [[ -f "$repo/.gitmodules" ]]; then
        while IFS= read -r sub_path; do
            [[ -n "$sub_path" ]] || continue
            append_repo_manifest "$label/$sub_path" "$repo/$sub_path"
        done < <(git -C "$repo" config --file .gitmodules --get-regexp 'submodule\..*\.path' 2>/dev/null | awk '{print $2}')
    fi
}

{
    echo "Hoot dependency source package"
    echo "Created: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo
    echo "Only Git-tracked files from the current commits are included."
    echo "Nested submodules are included recursively at their pinned commits."
    echo
} > "$MANIFEST"

for dependency in "${DEPENDENCIES[@]}"; do
    repo="$THIRD_PARTY_DIR/$dependency"

    [[ -d "$repo" ]] || {
        echo "error: missing dependency directory: $repo" >&2
        exit 1
    }

    echo "Preparing $dependency..."
    ensure_clean_repo "$repo"
    if ((UPDATE_TOP_LEVEL)); then
        update_top_level_repo "$repo"
    fi
    if ((SYNC_SUBMODULES)); then
        sync_submodules "$repo"
    fi

    archive_repo_recursive "$repo" "$STAGE_DIR/third_party/$dependency"
    append_repo_manifest "$dependency" "$repo"
    echo >> "$MANIFEST"
done

# Defensive cleanup for generated files that were accidentally committed in a
# dependency repository. Source directories with similar names are untouched.
find "$STAGE_DIR/third_party" -depth -type d \
    \( -name build -o -name 'build-*' -o -name 'cmake-build-*' \
       -o -name CMakeFiles -o -name .cache -o -name '__pycache__' \
       -o -name .pytest_cache -o -name .mypy_cache \) \
    -exec rm -rf -- {} +

find "$STAGE_DIR/third_party" -type f \
    \( -name '*.o' -o -name '*.obj' -o -name '*.a' -o -name '*.lib' \
       -o -name '*.so' -o -name '*.dylib' -o -name '*.dll' \
       -o -name '*.exe' -o -name '*.pyc' -o -name CMakeCache.txt \
       -o -name '*.zip' -o -name '*.7z' -o -name '*.tar' \
       -o -name '*.tar.gz' -o -name '*.tgz' \) \
    -delete

mkdir -p "$(dirname -- "$OUTPUT")"
rm -f -- "$OUTPUT"
(
    cd "$TMP_DIR"
    zip -X -q -y -r "$OUTPUT" hoot-dependencies
)

echo "Created: $OUTPUT"
echo "Size: $(du -h "$OUTPUT" | awk '{print $1}')"
echo "Contents: libkss, libvgm, px68k-libretro (including recursive submodules)"
