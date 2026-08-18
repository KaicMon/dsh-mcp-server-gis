#!/usr/bin/env node

/**
 * Decode one Harness JSONL session and report its durable token usage.
 *
 * Harness writes independent zstd frames rather than one ordinary compressed
 * stream, so the repository reuses the pinned Harness decoder instead of
 * requiring a system `zstd` package. Only aggregate counters are printed; the
 * conversation content and credentials are never copied into the report.
 */
import { readFile } from 'node:fs/promises'
import { pathToFileURL } from 'node:url'
import { resolve } from 'node:path'

if (process.argv.length !== 4) {
  console.error('Usage: summarize-session.mts <harness-source> <session.jsonl[.zstd]>')
  process.exit(2)
}

const harnessRoot = resolve(process.argv[2]!)
const sessionPath = resolve(process.argv[3]!)
const decoderModule = pathToFileURL(resolve(
  harnessRoot,
  'packages/session/session-persistence-jsonl/src/zstd.ts',
)).href
const { createZstdFrameDecoder, scanZstdFrames } = await import(decoderModule)

const stored = await readFile(sessionPath)
const plaintext = sessionPath.endsWith('.zstd')
  ? Array.from(
      createZstdFrameDecoder().decode(stored, scanZstdFrames(stored).frames),
      (chunk: Uint8Array) => Buffer.from(chunk),
    ).map(chunk => chunk.toString('utf8')).join('')
  : stored.toString('utf8')

type Counters = Record<string, number>
const totals: Counters = {}
const usageByStep = new Map<string, Record<string, unknown>>()
let toolCalls = 0
let mcpToolCalls = 0
const mcpToolNames: string[] = []

function findNamedObject(value: unknown, name: string): Record<string, unknown> | undefined {
  if (!value || typeof value !== 'object') return undefined
  const object = value as Record<string, unknown>
  const direct = object[name]
  if (direct && typeof direct === 'object' && !Array.isArray(direct)) {
    return direct as Record<string, unknown>
  }
  for (const child of Object.values(object)) {
    const found = findNamedObject(child, name)
    if (found) return found
  }
  return undefined
}

function findScalar(value: unknown, name: string): string | number | undefined {
  if (!value || typeof value !== 'object') return undefined
  const object = value as Record<string, unknown>
  const direct = object[name]
  if (typeof direct === 'string' || typeof direct === 'number') return direct
  for (const child of Object.values(object)) {
    const found = findScalar(child, name)
    if (found !== undefined) return found
  }
  return undefined
}

for (const line of plaintext.split('\n')) {
  if (!line.trim()) continue
  const event = JSON.parse(line) as Record<string, unknown>
  if (event.type === 'tool/call') {
    toolCalls += 1
    const name = findScalar(event, 'name')
    if (typeof name === 'string' && name.startsWith('mcp__gis__')) {
      mcpToolCalls += 1
      mcpToolNames.push(name)
    }
  }
  // Persistence envelopes differ slightly between Harness releases. Search
  // recursively, then deduplicate usage by the canonical turn/step identity;
  // both a stream chunk and turn-end event may carry the same counters.
  const usage = findNamedObject(event, 'usage')
  if (!usage) continue
  const turn = findScalar(event, 'turn')
  const step = findScalar(event, 'step')
  const identity = turn !== undefined && step !== undefined
    ? `${turn}/${step}`
    : `event/${usageByStep.size}`
  usageByStep.set(identity, usage)
}

for (const usage of usageByStep.values()) {
  for (const [name, value] of Object.entries(usage)) {
    if (typeof value === 'number' && Number.isFinite(value)) {
      totals[name] = (totals[name] ?? 0) + value
    }
  }
}

const totalTokens = usageByStep.size === 0
  ? null
  : Object.entries(totals)
      .filter(([name]) => name.toLowerCase().includes('token'))
      .reduce((sum, [, value]) => sum + value, 0)

process.stdout.write(JSON.stringify({
  usageEvents: usageByStep.size,
  totalTokens,
  toolCalls,
  mcpToolCalls,
  mcpToolNames,
  counters: totals,
}))
