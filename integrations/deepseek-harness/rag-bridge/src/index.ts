/**
 * Read-only retrieval bridge for a DSH MCP namespace.
 *
 * It intentionally observes all schemas registered by dsh-mcp-client and
 * never calls `tools.restrict()` or changes a model request. This lets us
 * establish a stable catalog and baseline token measurements before dynamic
 * Tool injection is enabled in a future Agent Loop extension.
 */
import type { Context } from '@deepseek-ai/cordis'
import type {} from '@deepseek-ai/dsh-tools'

export const name = 'mcp-rag-bridge'
export const inject = ['tools']

export interface Config {
  serverName?: string
  topK?: number
  /** Reserved rollout gate. It must remain false until Agent Loop support lands. */
  selectionEnabled?: boolean
  logTransitions?: boolean
}

export interface ToolDocument {
  readonly name: string
  readonly description: string
  readonly inputSchema: string
  readonly bytes: number
}

export interface Candidate {
  readonly name: string
  readonly score: number
  readonly description: string
}

export interface McpRagBridgeSnapshot {
  readonly serverName: string
  readonly toolPrefix: string
  readonly state: 'ready' | 'waiting-for-tools'
  readonly generation: number
  readonly observedAt: string
  readonly catalog: readonly ToolDocument[]
  readonly schemaBytes: number
  /** Approximation for trend comparison only; it is not provider billing. */
  readonly estimatedSchemaTokens: number
  readonly selectionEnabled: false
}

declare module '@deepseek-ai/cordis' {
  interface Context { mcpRagBridge: McpRagBridge }
}

type RegistryTool = { name: string, description?: string, inputSchema?: unknown }

function serverName(value: string): string {
  if (!/^[A-Za-z0-9_-]{1,32}$/.test(value)) {
    throw new Error('mcp-rag-bridge: serverName must match [A-Za-z0-9_-]{1,32}')
  }
  return value
}

/** Convert an MCP schema to a deterministic compact document for later retrieval. */
export function buildCatalog(tools: readonly RegistryTool[], prefix: string): ToolDocument[] {
  return tools
    .filter(tool => tool.name.startsWith(prefix))
    .map(tool => {
      const description = typeof tool.description === 'string' ? tool.description : ''
      const inputSchema = stableJson(tool.inputSchema ?? {})
      const bytes = Buffer.byteLength(`${tool.name}\n${description}\n${inputSchema}`, 'utf8')
      return Object.freeze({ name: tool.name, description, inputSchema, bytes })
    })
    .sort((left, right) => left.name.localeCompare(right.name))
}

/**
 * Deterministic lexical baseline. Its API mirrors the future embedding sidecar
 * so callers and acceptance scripts will not change when vector ranking lands.
 */
export function rankCatalog(query: string, catalog: readonly ToolDocument[], topK: number): Candidate[] {
  const terms = new Set(tokenize(query))
  if (terms.size === 0) return []
  return catalog
    .map(document => {
      const haystack = `${document.name} ${document.description} ${document.inputSchema}`.toLowerCase()
      let score = 0
      for (const term of terms) if (haystack.includes(term)) score += 1
      return Object.freeze({ name: document.name, description: document.description, score })
    })
    .filter(candidate => candidate.score > 0)
    .sort((left, right) => right.score - left.score || left.name.localeCompare(right.name))
    .slice(0, Math.max(1, topK))
}

function tokenize(value: string): string[] {
  return value.toLowerCase().match(/[\p{L}\p{N}_-]{2,}/gu) ?? []
}

function stableJson(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(stableJson).join(',')}]`
  if (value && typeof value === 'object') {
    const object = value as Record<string, unknown>
    return `{${Object.keys(object).sort().map(key => `${JSON.stringify(key)}:${stableJson(object[key])}`).join(',')}}`
  }
  return JSON.stringify(value)
}

export class McpRagBridge {
  private generation = 0
  private readonly prefix: string
  private readonly topK: number
  private readonly logTransitions: boolean
  private current: McpRagBridgeSnapshot

  constructor(private readonly ctx: Context, config: Config = {}) {
    if (config.selectionEnabled) {
      throw new Error('mcp-rag-bridge: dynamic selection is not implemented; keep selectionEnabled: false')
    }
    const configuredServer = serverName(config.serverName ?? 'gis')
    this.prefix = `mcp__${configuredServer}__`
    this.topK = Math.max(1, Math.floor(config.topK ?? 6))
    this.logTransitions = config.logTransitions ?? true
    this.current = this.capture(configuredServer)
    ctx.on('tools/change', () => this.refresh())
  }

  snapshot(): McpRagBridgeSnapshot {
    return Object.freeze({ ...this.current, catalog: Object.freeze([...this.current.catalog]) })
  }

  /** Preview candidates only. Model-visible schemas are intentionally untouched. */
  preview(query: string, topK = this.topK): Candidate[] {
    return rankCatalog(query, this.current.catalog, topK)
  }

  private capture(configuredServer: string): McpRagBridgeSnapshot {
    const catalog = buildCatalog(this.ctx.tools.schemas() as RegistryTool[], this.prefix)
    const schemaBytes = catalog.reduce((total, tool) => total + tool.bytes, 0)
    return Object.freeze({
      serverName: configuredServer,
      toolPrefix: this.prefix,
      state: catalog.length === 0 ? 'waiting-for-tools' : 'ready',
      generation: this.generation,
      observedAt: new Date().toISOString(),
      catalog: Object.freeze(catalog),
      schemaBytes,
      estimatedSchemaTokens: Math.ceil(schemaBytes / 4),
      selectionEnabled: false,
    })
  }

  private refresh(): void {
    const previous = this.current
    this.generation += 1
    this.current = this.capture(previous.serverName)
    if (this.logTransitions && previous.schemaBytes !== this.current.schemaBytes) {
      this.ctx.logger.info(`mcp-rag-bridge(${this.current.serverName}): generation ${this.current.generation}, ${this.current.catalog.length} catalogued tool(s), ${this.current.schemaBytes} schema bytes`)
    }
  }
}

export function apply(ctx: Context, config: Config = {}): void {
  const bridge = new McpRagBridge(ctx, config)
  ctx.effect(() => ctx.provide('mcpRagBridge', bridge), 'mcp-rag-bridge.service')
}
