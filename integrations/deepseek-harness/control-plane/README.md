# DSH MCP Control Plane

This installable Cordis plugin observes one `@deepseek-ai/dsh-mcp-client`
namespace through the public Harness Tool registry. It publishes
`ctx.mcpControlPlane.snapshot()` for trusted Host plugins and logs tool-set
generation changes caused by startup, reconnect, or
`notifications/tools/list_changed`.

It intentionally does not create an MCP client, execute shell commands, or
install dynamic libraries. The official MCP client plugin remains the sole
owner of the STDIO process and MCP transport.

Load it after the MCP client in a Cordis patch:

```yaml
- id: mcp-control-plane
  name: '@local/dsh-mcp-control-plane'
  config:
    serverName: gis
    logTransitions: true
```

Build from this repository with:

```bash
bash integrations/deepseek-harness/build-control-plane.sh
```
