/**
 * Read-only control plane for one DSH-managed MCP namespace.
 *
 * `@deepseek-ai/dsh-mcp-client` remains the sole owner of the MCP transport
 * and child process. This plugin observes the Harness Tool registry instead
 * of opening a second STDIO connection, which would otherwise start another
 * C++ server and make lifecycle state ambiguous.
 */

import type { Context } from '@deepseek-ai/cordis'
import type {} from '@deepseek-ai/dsh-tools'

/** Cordis loader name. */
export const name = 'mcp-control-plane'

/** The registry is the public boundary exposed by the official MCP bridge. */
export const inject = ['tools']

export interface Config {
  /** Namespace configured on the paired dsh-mcp-client instance. */
  serverName?: string
  /** Emit one concise transition record when the synchronized Tool set changes. */
  logTransitions?: boolean
}

export interface McpControlPlaneSnapshot {
  readonly serverName: string
  readonly toolPrefix: string
  readonly state: 'ready' | 'waiting-for-tools'
  readonly generation: number
  readonly observedAt: string
  readonly tools: readonly string[]
}

declare module '@deepseek-ai/cordis' {
  interface Context {
    /** Read-only MCP namespace status for other trusted Cordis extensions. */
    mcpControlPlane: McpControlPlane
  }
}

function validateServerName(value: string): string {
  if (!/^[A-Za-z0-9_-]{1,32}$/.test(value)) {
    throw new Error('mcp-control-plane: serverName must match [A-Za-z0-9_-]{1,32}')
  }
  return value
}

/**
 * A snapshot service deliberately limited to observation. Starting, stopping,
 * installing, or replacing C++ plugins is a privileged operation and will be
 * added only behind an explicit approval and a server-side management API.
 */
export class McpControlPlane {
  private readonly ctx: Context
  private readonly toolPrefix: string
  private readonly logTransitions: boolean
  private generation = 0
  private current: McpControlPlaneSnapshot

  constructor(ctx: Context, config: Config = {}) {
    this.ctx = ctx
    const serverName = validateServerName(config.serverName ?? 'gis')
    this.toolPrefix = `mcp__${serverName}__`
    this.logTransitions = config.logTransitions ?? true
    this.current = this.capture(serverName)

    // `dsh-mcp-client` emits this after initial discovery, reconnects, and
    // MCP notifications/tools/list_changed. Cordis scopes the subscription to
    // this plugin and disposes it automatically on HMR/uninstall.
    ctx.on('tools/change', () => this.refresh())
  }

  /** Return an immutable copy so consumers cannot mutate control-plane state. */
  snapshot(): McpControlPlaneSnapshot {
    return Object.freeze({ ...this.current, tools: Object.freeze([...this.current.tools]) })
  }

  private capture(serverName: string): McpControlPlaneSnapshot {
    const tools = ctxToolNames(this.ctx, this.toolPrefix)
    return Object.freeze({
      serverName,
      toolPrefix: this.toolPrefix,
      state: tools.length === 0 ? 'waiting-for-tools' : 'ready',
      generation: this.generation,
      observedAt: new Date().toISOString(),
      tools: Object.freeze(tools),
    })
  }

  private refresh(): void {
    const previous = this.current
    this.generation += 1
    this.current = this.capture(previous.serverName)
    if (this.logTransitions && previous.tools.join('\u0000') !== this.current.tools.join('\u0000')) {
      this.ctx.logger.info(
        `mcp-control-plane(${this.current.serverName}): generation ${this.current.generation}, `
        + `${this.current.tools.length} synchronized tool(s)`,
      )
    }
  }
}

/** Use the public ToolRuntime schema projection; no private bridge internals. */
function ctxToolNames(ctx: Context, prefix: string): string[] {
  return ctx.tools.schemas()
    .map(tool => tool.name)
    .filter(name => name.startsWith(prefix))
    .sort((left, right) => left.localeCompare(right))
}

/** Construct and publish the read-only service into the Cordis container. */
export function apply(ctx: Context, config: Config = {}): void {
  const plane = new McpControlPlane(ctx, config)
  // Do not import Cordis at runtime: this keeps the package installable from
  // both the published DSH bundle and the unbuilt pinned source checkout.
  ctx.effect(() => ctx.provide('mcpControlPlane', plane), 'mcp-control-plane.service')
}
