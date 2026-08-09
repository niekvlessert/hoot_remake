# HootUI Edit / Help menu stage — 2026-08-08

The SDL UI now mirrors the useful parts of the original 2001 Hoot menu layout:

- No File menu in the cross-platform UI.
- Edit -> Fullscreen, with the original Alt+Enter accelerator.
- Help -> About hoot...

The About dialog preserves the original resource information:

- `hoot... - Sound Hardware Emulator`
- `Original Hoot copyright (C) 1999-2001 DMP SOFT.`
- `Original site: http://dmpsoft.virtualave.net/`

and adds:

- `Cross platform port & update: Niek Vlessert`

The About dialog is an SDL modal overlay and therefore works on all native UI platforms without relying on OS-specific dialog resources.
