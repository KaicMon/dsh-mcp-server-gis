import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

const plugin = await import('../lib/index.mjs')
assert.equal(plugin.name, 'mcp-control-plane')
assert.deepEqual(plugin.inject, ['tools'])
assert.equal(typeof plugin.apply, 'function')
assert.equal(typeof plugin.McpControlPlane, 'function')

const source = readFileSync(new URL('../src/index.ts', import.meta.url), 'utf8')
assert.match(source, /mcp__\$\{serverName\}__/)
assert.match(source, /ctx\.tools\.schemas\(\)/)
assert.doesNotMatch(source, /child_process|spawn\(|exec\(|dlopen/)
console.log('DSH MCP control-plane plugin contract passed')
