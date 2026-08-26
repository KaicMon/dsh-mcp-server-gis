# DSH MCP RAG Bridge

This Cordis Host plugin observes the Tool schemas registered by
`@deepseek-ai/dsh-mcp-client` and maintains a deterministic catalog for one
MCP namespace. `preview(query)` currently uses lexical ranking as a baseline.

It is deliberately **observe-only**: `selectionEnabled` must remain `false`.
The plugin does not open a second MCP connection and does not alter the Tool
schemas sent to Qwen. Its snapshot exposes catalog size, UTF-8 schema bytes,
and a clearly labelled approximate token count for A/B baselines.

The next phase will replace the ranker with the existing C++ embedding sidecar
and add a durable Agent Loop hook that applies a per-turn Tool subset.
