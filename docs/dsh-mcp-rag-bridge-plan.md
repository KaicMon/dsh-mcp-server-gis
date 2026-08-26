# DSH MCP RAG Bridge plan

## Goal

Let DeepSeek Harness reduce the MCP Tool schemas injected into a model request,
without changing MCP discovery, execution, or server-side authorization.

## Phase 1: catalog and baseline (implemented, disabled)

`@local/dsh-mcp-rag-bridge` observes `ctx.tools.schemas()` after the official
`@deepseek-ai/dsh-mcp-client` discovers `mcp__gis__*` tools. It stores stable
Tool documents (name, description, normalized input schema), exposes lexical
Top-K previews, and reports catalog bytes plus `ceil(bytes / 4)` as an
approximate schema-token baseline. It never opens another STDIO client and
never modifies the model-visible schemas.

`selectionEnabled: false` is a hard rollout gate. Setting it to true fails
fast, rather than silently producing partial filtering.

## Phase 2: retrieval implementation

Replace the lexical `rankCatalog()` adapter with a local API over the existing
C++ `ToolRetriever` / embedding cache. Keep lexical retrieval as deterministic
fallback and record the catalog generation used for every selection.

## Phase 3: per-turn selection

Add an Agent Loop extension before the LLM request is assembled. It will rank
the current user query, preserve mandatory recovery/meta-tools, then inject
only the selected schemas for that turn. The selected names and catalog
generation must be stored in the session trace; tool execution remains with
the official MCP client and server profile remains the authorization boundary.

## Evaluation and rollout

Compare the same fixed scenario suite with selection disabled and enabled:

| Metric | Required reading |
| --- | --- |
| Schema bytes / estimated tokens | catalog snapshot and selected subset |
| Provider token usage | DashScope/Harness session usage |
| Tool recall@K | required tool appears in candidates |
| Task success | human-reviewed answer and trace |
| Extra calls / latency | durable `tool/call` events and elapsed time |

Enable the feature only after recall and task-success meet the agreed baseline;
keep a configuration switch for immediate fallback to full-schema injection.
