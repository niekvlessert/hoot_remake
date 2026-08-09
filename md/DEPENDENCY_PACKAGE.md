# Clean dependency source package

Run from anywhere inside or outside the project:

```sh
./tools/package_dependencies.sh --update
```

`--update` fast-forwards each top-level dependency to its configured upstream.
Without it, the script packages the currently checked-out revisions.

The default output is `hoot-dependencies-latest.zip` in the project root. The
archive contains only the canonical dependency checkouts used by CMake:

- `third_party/libkss`
- `third_party/libvgm`
- `third_party/px68k-libretro`

The script uses `git archive`, so local build products, ignored files, `.git`
metadata, old sibling copies and previously created archives are not included.
All dependency working trees must be clean.

## libkss submodules

Before packaging, the script runs:

```sh
git submodule sync --recursive
git submodule update --init --recursive
```

It then archives every nested submodule recursively at the commit pinned by its
parent repository. It deliberately does not pull each libkss submodule to an
independent latest branch, because that could create an incompatible mixture.

## Options

```text
-o, --output FILE       Choose the ZIP path
-t, --third-party DIR   Choose another third_party directory
    --update            Fast-forward top-level dependencies first
    --no-sync           Do not initialize/update pinned submodules
```

The ZIP contains `DEPENDENCIES-MANIFEST.txt` with the exact commit, tag/describe
value, branch and commit date for every dependency and nested submodule.
