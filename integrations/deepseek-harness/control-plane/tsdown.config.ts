// Resolve through the pinned Harness checkout so this repository-owned plugin
// uses the exact same builder as the DSH host, without vendoring Node modules.
import { defineConfig } from '../../../build_dev/vendor/deepseek-harness/node_modules/tsdown/dist/index.mjs'

/** Node-only Cordis extension: no browser bundle is needed in the first slice. */
export default defineConfig({
  entry: ['src/index.ts'],
  outDir: 'lib',
  format: ['esm'],
  fixedExtension: true,
  platform: 'node',
  target: 'es2024',
  dts: true,
  clean: true,
  external: ['@deepseek-ai/cordis', '@deepseek-ai/dsh-tools', '@deepseek-ai/schemastery'],
})
