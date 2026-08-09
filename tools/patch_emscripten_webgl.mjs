import { readFileSync, writeFileSync } from "node:fs";

const gluePath = process.argv[2];
if (!gluePath) {
  throw new Error("Usage: node patch_emscripten_webgl.mjs <hootweb.js>");
}

const resizableMemoryGlue =
  "function getMemoryBuffer(){try{var b=wasmMemory.toResizableBuffer();return b}catch{}return wasmMemory.buffer}";
const fixedMemoryGlue =
  "function getMemoryBuffer(){return wasmMemory.buffer}";

const source = readFileSync(gluePath, "utf8");

if (source.includes(fixedMemoryGlue)) {
  process.exit(0);
}

// Older Emscripten versions already use a fixed ArrayBuffer view and need no
// rewrite. A newer but unknown resizable-memory pattern must fail loudly so a
// silently crash-prone browser bundle is never produced.
if (!source.includes("wasmMemory.toResizableBuffer()")) {
  process.exit(0);
}

const matches = source.split(resizableMemoryGlue).length - 1;
if (matches !== 1) {
  throw new Error(
    `Expected one known resizable WASM memory glue block, found ${matches}.`,
  );
}

writeFileSync(gluePath, source.replace(resizableMemoryGlue, fixedMemoryGlue));
console.log("Patched resizable Wasm memory view for Chrome WebGL.");
