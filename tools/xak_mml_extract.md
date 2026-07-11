# Xak X68000 MML extraction

`xak_mml_extract.py` reconstructs textual OPMDRV MML from tokenized `.BGM`
files on the original Xak X68000 HDM disks.

```sh
python3 tools/xak_mml_extract.py \
  "/path/to/Xak - The Art of Visual Stage (Disk A).hdm" \
  TOWN1.BGM \
  --xak-x /path/to/XAK.X \
  --output packs/xak_mml_extract/TOWN1.BGM
```

`--xak-x` is optional. It validates that the executable is the Human68k Xak
program which loads `SYOKI2.BGM`; the token expansion itself is deliberately
implemented in the script so the conversion remains reproducible and auditable.

The extractor currently supports the complete token set used by the original
TOWN1 sequence. Unsupported tokens fail with their exact input offset instead
of silently producing questionable MML.
