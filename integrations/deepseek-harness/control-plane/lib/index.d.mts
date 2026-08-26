import { Context } from "@deepseek-ai/cordis";

//#region src/index.d.ts
/** Cordis loader name. */
declare const name = "mcp-control-plane";
/** The registry is the public boundary exposed by the official MCP bridge. */
declare const inject: string[];
interface Config {
  /** Namespace configured on the paired dsh-mcp-client instance. */
  serverName?: string;
  /** Emit one concise transition record when the synchronized Tool set changes. */
  logTransitions?: boolean;
}
interface McpControlPlaneSnapshot {
  readonly serverName: string;
  readonly toolPrefix: string;
  readonly state: 'ready' | 'waiting-for-tools';
  readonly generation: number;
  readonly observedAt: string;
  readonly tools: readonly string[];
}
declare module '@deepseek-ai/cordis' {
  interface Context {
    /** Read-only MCP namespace status for other trusted Cordis extensions. */
    mcpControlPlane: McpControlPlane;
  }
}
/**
 * A snapshot service deliberately limited to observation. Starting, stopping,
 * installing, or replacing C++ plugins is a privileged operation and will be
 * added only behind an explicit approval and a server-side management API.
 */
declare class McpControlPlane {
  private readonly ctx;
  private readonly toolPrefix;
  private readonly logTransitions;
  private generation;
  private current;
  constructor(ctx: Context, config?: Config);
  /** Return an immutable copy so consumers cannot mutate control-plane state. */
  snapshot(): McpControlPlaneSnapshot;
  private capture;
  private refresh;
}
/** Construct and publish the read-only service into the Cordis container. */
declare function apply(ctx: Context, config?: Config): void;
//#endregion
export { Config, McpControlPlane, McpControlPlaneSnapshot, apply, inject, name };
//# sourceMappingURL=index.d.mts.map