import { Context } from "@deepseek-ai/cordis";

//#region src/index.d.ts
declare const name = "mcp-rag-bridge";
declare const inject: string[];
interface Config {
  serverName?: string;
  topK?: number;
  /** Reserved rollout gate. It must remain false until Agent Loop support lands. */
  selectionEnabled?: boolean;
  logTransitions?: boolean;
}
interface ToolDocument {
  readonly name: string;
  readonly description: string;
  readonly inputSchema: string;
  readonly bytes: number;
}
interface Candidate {
  readonly name: string;
  readonly score: number;
  readonly description: string;
}
interface McpRagBridgeSnapshot {
  readonly serverName: string;
  readonly toolPrefix: string;
  readonly state: 'ready' | 'waiting-for-tools';
  readonly generation: number;
  readonly observedAt: string;
  readonly catalog: readonly ToolDocument[];
  readonly schemaBytes: number;
  /** Approximation for trend comparison only; it is not provider billing. */
  readonly estimatedSchemaTokens: number;
  readonly selectionEnabled: false;
}
declare module '@deepseek-ai/cordis' {
  interface Context {
    mcpRagBridge: McpRagBridge;
  }
}
type RegistryTool = {
  name: string;
  description?: string;
  inputSchema?: unknown;
};
/** Convert an MCP schema to a deterministic compact document for later retrieval. */
declare function buildCatalog(tools: readonly RegistryTool[], prefix: string): ToolDocument[];
/**
 * Deterministic lexical baseline. Its API mirrors the future embedding sidecar
 * so callers and acceptance scripts will not change when vector ranking lands.
 */
declare function rankCatalog(query: string, catalog: readonly ToolDocument[], topK: number): Candidate[];
declare class McpRagBridge {
  private readonly ctx;
  private generation;
  private readonly prefix;
  private readonly topK;
  private readonly logTransitions;
  private current;
  constructor(ctx: Context, config?: Config);
  snapshot(): McpRagBridgeSnapshot;
  /** Preview candidates only. Model-visible schemas are intentionally untouched. */
  preview(query: string, topK?: number): Candidate[];
  private capture;
  private refresh;
}
declare function apply(ctx: Context, config?: Config): void;
//#endregion
export { Candidate, Config, McpRagBridge, McpRagBridgeSnapshot, ToolDocument, apply, buildCatalog, inject, name, rankCatalog };