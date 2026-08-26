import type { ClientContext } from '@deepseek-ai/dsh-client-runtime/client'
import type { ToolCallViewProps } from '@deepseek-ai/dsh-client-ui-tool/client'
import type {} from '@deepseek-ai/dsh-client-ui-conversation/client'
import { createElement } from 'react'
import { GisToolCard } from './GisToolCard.tsx'
import { envelopeFromBlock } from './envelope.ts'
import { GIS_MCP_TOOLS, GisMapPanel, McpToolsPanel } from './GisPanels.tsx'

export const inject = ['slots']

/**
 * DashScope may omit a Tool call id in continuation chunks. Older Harness
 * releases then lose the Tool name when freezing the result and request the
 * renderer registered under an empty key. Recover GIS identity from the
 * durable, explicitly tagged result instead of relying on transient UI state.
 */
function EmptyCallIdResult(props: ToolCallViewProps) {
  if (envelopeFromBlock(props.block) !== null) return createElement(GisToolCard, props)

  // Empty ids can also occur for non-GIS tools. Keep a small neutral fallback
  // rather than falsely presenting their output as a map result.
  return createElement('section', {
    style: { border: '1px solid rgba(127,127,127,.25)', borderRadius: 10, padding: 12 },
  },
  createElement('strong', null, 'Tool call'),
  createElement('div', { style: { marginTop: 8, opacity: .7 } }, 'Result available in Inspect.'),
  props.inspect === undefined ? null : createElement('button', {
    type: 'button', onClick: props.inspect, style: { marginTop: 8 },
  }, 'Inspect'))
}

/** Register one renderer for every GIS wire Tool without changing Harness core. */
export function apply(ctx: ClientContext): void {
  ctx.slots.inject('conversation.view', function* () {
    yield ctx.slots.register({ name: 'conversation.view', id: 'mcp-tools', order: 20,
      label: () => 'MCP工具面板' }, McpToolsPanel)
    yield ctx.slots.register({ name: 'conversation.view', id: 'gis-map', order: 30,
      label: () => '地图展示' }, GisMapPanel)
  })
  ctx.slots.inject('tool.call.toolview', function* () {
    for (const tool of GIS_MCP_TOOLS) {
      yield ctx.slots.register(
        { name: 'tool.call.toolview', key: `mcp__gis__${tool.name}` },
        GisToolCard,
      )
    }
    // Compatibility registration for settled Tool results whose correlation
    // id/name was erased by an empty DashScope streaming continuation.
    yield ctx.slots.register(
      { name: 'tool.call.toolview', key: '' },
      EmptyCallIdResult,
    )
  })
}
