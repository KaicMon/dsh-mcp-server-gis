import { clientBundle } from '../../../build_dev/vendor/deepseek-harness/packages/client/tsdown.client.ts'

// Build directly from authored TS. The pinned workspace's normal full build
// first emits lib/types; this out-of-tree package intentionally avoids forcing
// a 239-package type build merely to produce its two runtime artifacts.
export default clientBundle('@local/gis-mcp-harness-renderer', ['src/index.ts'])
