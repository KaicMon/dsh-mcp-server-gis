//#region src/index.ts
const name = "mcp-rag-bridge";
const inject = ["tools"];
function serverName(value) {
	if (!/^[A-Za-z0-9_-]{1,32}$/.test(value)) throw new Error("mcp-rag-bridge: serverName must match [A-Za-z0-9_-]{1,32}");
	return value;
}
/** Convert an MCP schema to a deterministic compact document for later retrieval. */
function buildCatalog(tools, prefix) {
	return tools.filter((tool) => tool.name.startsWith(prefix)).map((tool) => {
		const description = typeof tool.description === "string" ? tool.description : "";
		const inputSchema = stableJson(tool.inputSchema ?? {});
		const bytes = Buffer.byteLength(`${tool.name}\n${description}\n${inputSchema}`, "utf8");
		return Object.freeze({
			name: tool.name,
			description,
			inputSchema,
			bytes
		});
	}).sort((left, right) => left.name.localeCompare(right.name));
}
/**
* Deterministic lexical baseline. Its API mirrors the future embedding sidecar
* so callers and acceptance scripts will not change when vector ranking lands.
*/
function rankCatalog(query, catalog, topK) {
	const terms = new Set(tokenize(query));
	if (terms.size === 0) return [];
	return catalog.map((document) => {
		const haystack = `${document.name} ${document.description} ${document.inputSchema}`.toLowerCase();
		let score = 0;
		for (const term of terms) if (haystack.includes(term)) score += 1;
		return Object.freeze({
			name: document.name,
			description: document.description,
			score
		});
	}).filter((candidate) => candidate.score > 0).sort((left, right) => right.score - left.score || left.name.localeCompare(right.name)).slice(0, Math.max(1, topK));
}
function tokenize(value) {
	return value.toLowerCase().match(/[\p{L}\p{N}_-]{2,}/gu) ?? [];
}
function stableJson(value) {
	if (Array.isArray(value)) return `[${value.map(stableJson).join(",")}]`;
	if (value && typeof value === "object") {
		const object = value;
		return `{${Object.keys(object).sort().map((key) => `${JSON.stringify(key)}:${stableJson(object[key])}`).join(",")}}`;
	}
	return JSON.stringify(value);
}
var McpRagBridge = class {
	ctx;
	generation = 0;
	prefix;
	topK;
	logTransitions;
	current;
	constructor(ctx, config = {}) {
		this.ctx = ctx;
		if (config.selectionEnabled) throw new Error("mcp-rag-bridge: dynamic selection is not implemented; keep selectionEnabled: false");
		const configuredServer = serverName(config.serverName ?? "gis");
		this.prefix = `mcp__${configuredServer}__`;
		this.topK = Math.max(1, Math.floor(config.topK ?? 6));
		this.logTransitions = config.logTransitions ?? true;
		this.current = this.capture(configuredServer);
		ctx.on("tools/change", () => this.refresh());
	}
	snapshot() {
		return Object.freeze({
			...this.current,
			catalog: Object.freeze([...this.current.catalog])
		});
	}
	/** Preview candidates only. Model-visible schemas are intentionally untouched. */
	preview(query, topK = this.topK) {
		return rankCatalog(query, this.current.catalog, topK);
	}
	capture(configuredServer) {
		const catalog = buildCatalog(this.ctx.tools.schemas(), this.prefix);
		const schemaBytes = catalog.reduce((total, tool) => total + tool.bytes, 0);
		return Object.freeze({
			serverName: configuredServer,
			toolPrefix: this.prefix,
			state: catalog.length === 0 ? "waiting-for-tools" : "ready",
			generation: this.generation,
			observedAt: (/* @__PURE__ */ new Date()).toISOString(),
			catalog: Object.freeze(catalog),
			schemaBytes,
			estimatedSchemaTokens: Math.ceil(schemaBytes / 4),
			selectionEnabled: false
		});
	}
	refresh() {
		const previous = this.current;
		this.generation += 1;
		this.current = this.capture(previous.serverName);
		if (this.logTransitions && previous.schemaBytes !== this.current.schemaBytes) this.ctx.logger.info(`mcp-rag-bridge(${this.current.serverName}): generation ${this.current.generation}, ${this.current.catalog.length} catalogued tool(s), ${this.current.schemaBytes} schema bytes`);
	}
};
function apply(ctx, config = {}) {
	const bridge = new McpRagBridge(ctx, config);
	ctx.effect(() => ctx.provide("mcpRagBridge", bridge), "mcp-rag-bridge.service");
}
//#endregion
export { McpRagBridge, apply, buildCatalog, inject, name, rankCatalog };
