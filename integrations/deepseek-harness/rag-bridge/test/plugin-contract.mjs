import assert from 'node:assert/strict'

const plugin = await import('../lib/index.mjs')
assert.equal(plugin.name, 'mcp-rag-bridge')
assert.deepEqual(plugin.inject, ['tools'])
assert.equal(typeof plugin.apply, 'function')

const catalog = plugin.buildCatalog([
  { name: 'mcp__gis__nearby_search', description: 'Search nearby places', inputSchema: { properties: { radius: { type: 'number' } }, type: 'object' } },
  { name: 'mcp__other__hidden', description: 'must not be included' },
], 'mcp__gis__')
assert.equal(catalog.length, 1)
assert.equal(catalog[0].name, 'mcp__gis__nearby_search')
assert.deepEqual(plugin.rankCatalog('nearby places', catalog, 3).map(item => item.name), ['mcp__gis__nearby_search'])
console.log('DSH MCP RAG bridge plugin contract passed')
