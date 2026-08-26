// Use the same builder version as the pinned Harness checkout. Runtime Cordis
// imports are deliberately type-only, so the bundle is host-layout agnostic.
import { defineConfig } from '../../../build_dev/vendor/deepseek-harness/node_modules/tsdown/dist/index.mjs'

export default defineConfig({
  entry: ['src/index.ts'],
  outDir: 'lib',
  format: ['esm'],
  fixedExtension: true,
  platform: 'node',
  target: 'es2024',
  dts: true,
  clean: true,
  external: ['@deepseek-ai/cordis', '@deepseek-ai/dsh-tools'],
})
