import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// This dependency-free contract test mirrors the TS parser and can run before
// the pinned Harness workspace has installed its frontend dependencies.
const marker = '[gis-result-envelope]\n'
const parse = text => {
  const at = text.lastIndexOf(marker)
  if (at < 0) return null
  try {
    const value = JSON.parse(text.slice(at + marker.length).trim())
    return value?.schemaVersion === '1.0' && typeof value?.tool === 'string' ? value : null
  } catch { return null }
}

const fixture = {
  schemaVersion: '1.0', tool: 'geometry_buffer', traceId: 'mcp_test', summary: 'ok', data: {},
  visualization: { kind: 'geojson', inline: true, geojson: { type: 'Point', coordinates: [118.7, 32.0] }, resultId: 'gis_1', mapUrl: 'http://127.0.0.1:8088/results/gis_1' },
  meta: { provider: 'local-gdal' },
}
assert.deepEqual(parse(`ok\n\n${marker}${JSON.stringify(fixture)}`), fixture)
assert.equal(parse('{"schemaVersion":"1.0"}'), null)
assert.equal(parse(`${marker}{broken`), null)
const rendererSource = readFileSync(new URL('../src/client/index.ts', import.meta.url), 'utf8')
assert.match(rendererSource, /mcp__gis__/)
assert.match(rendererSource, /key: ''/)
assert.match(rendererSource, /envelopeFromBlock\(props\.block\)/)
console.log('GIS renderer envelope contract passed')
