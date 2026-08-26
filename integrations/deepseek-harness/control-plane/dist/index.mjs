import { Service } from "@deepseek-ai/cordis";
import z from "@deepseek-ai/schemastery";
//#region src/index.ts
/**
* Read-only control plane for one DSH-managed MCP namespace.
*
* `@deepseek-ai/dsh-mcp-client` remains the sole owner of the MCP transport
* and child process. This plugin observes the Harness Tool registry instead
* of opening a second STDIO connection, which would otherwise start another
* C++ server and make lifecycle state ambiguous.
*/
/** Cordis loader name. */
const name = "mcp-control-plane";
/** The registry is the public boundary exposed by the official MCP bridge. */
const inject = ["tools"];
const Config = z.object({
	serverName: z.string().default("gis"),
	logTransitions: z.boolean().default(true)
});
function validateServerName(value) {
	if (!/^[A-Za-z0-9_-]{1,32}$/.test(value)) throw new Error("mcp-control-plane: serverName must match [A-Za-z0-9_-]{1,32}");
	return value;
}
/**
* A snapshot service deliberately limited to observation. Starting, stopping,
* installing, or replacing C++ plugins is a privileged operation and will be
* added only behind an explicit approval and a server-side management API.
*/
var McpControlPlane = class extends Service {
	toolPrefix;
	logTransitions;
	generation = 0;
	current;
	constructor(ctx, config = {}) {
		super(ctx, "mcpControlPlane");
		const serverName = validateServerName(config.serverName ?? "gis");
		this.toolPrefix = `mcp__${serverName}__`;
		this.logTransitions = config.logTransitions ?? true;
		this.current = this.capture(serverName);
		ctx.on("tools/change", () => this.refresh());
	}
	/** Return an immutable copy so consumers cannot mutate control-plane state. */
	snapshot() {
		return Object.freeze({
			...this.current,
			tools: Object.freeze([...this.current.tools])
		});
	}
	capture(serverName) {
		const tools = ctxToolNames(this.ctx, this.toolPrefix);
		return Object.freeze({
			serverName,
			toolPrefix: this.toolPrefix,
			state: tools.length === 0 ? "waiting-for-tools" : "ready",
			generation: this.generation,
			observedAt: (/* @__PURE__ */ new Date()).toISOString(),
			tools: Object.freeze(tools)
		});
	}
	refresh() {
		const previous = this.current;
		this.generation += 1;
		this.current = this.capture(previous.serverName);
		if (this.logTransitions && previous.tools.join("\0") !== this.current.tools.join("\0")) this.ctx.logger.info(`mcp-control-plane(${this.current.serverName}): generation ${this.current.generation}, ${this.current.tools.length} synchronized tool(s)`);
	}
};
/** Use the public ToolRuntime schema projection; no private bridge internals. */
function ctxToolNames(ctx, prefix) {
	return ctx.tools.schemas().map((tool) => tool.name).filter((name) => name.startsWith(prefix)).sort((left, right) => left.localeCompare(right));
}
/** Construct and publish the read-only service into the Cordis container. */
function apply(ctx, config = {}) {
	new McpControlPlane(ctx, config);
}
//#endregion
export { Config, McpControlPlane, apply, inject, name };

//# sourceMappingURL=index.mjs.map