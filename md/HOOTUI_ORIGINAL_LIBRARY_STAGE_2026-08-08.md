# Hoot UI original-style Library stage — 2026-08-08

The SDL `Library` placeholder is now functional and is deliberately based on the original
Windows Hoot source (`ssSoundDriverManager::MakeFolders`, `ssFolder`, `FolderSelect`, and
`TitleSelect`).

Hierarchy: root -> `- all -` / driver major type -> `- all -` / driver subtype -> game -> tracks.
Folder cursor/home positions are cached independently, as in original Hoot. Enter loads or
plays; Space on tracks plays and advances; Backspace/Escape ascends. The SDL implementation
adds mouse/double-click navigation and UTF-8 search without changing the catalog hierarchy.

Pack availability is indexed from the configured pack directory plus the GUI's remembered
Open/last-used directory. Both roots are scanned recursively, so platform/category subdirectories are supported. Missing packs stay in the catalog and are visibly marked. Known MIDI
hardware variants are annotated, while `HootTrackInfo.warning` remains the authoritative
runtime requirement/warning source after a game is loaded.
