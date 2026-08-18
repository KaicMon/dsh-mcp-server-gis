import type { ClientContext } from '@deepseek-ai/dsh-client-runtime/client'
import type { ToolCallViewProps } from '@deepseek-ai/dsh-client-ui-tool/client'
import { createElement } from 'react'
import { GisToolCard } from './GisToolCard.tsx'
import { envelopeFromBlock } from './envelope.ts'

/** Names exported by the `gis` MCP server after server qualification. */
const GIS_TOOLS = [
  'geometry_validate', 'coordinate_transform', 'point_in_polygon',
  'geometry_distance', 'geometry_area', 'geometry_buffer', 'geocode',
  'dataset_info', 'features_within', 'reverse_geocode', 'poi_search',
  'nearby_search', 'search_nearby_by_place', 'administrative_search',
  'provider_route', 'route_plan',
  'nearest_road', 'map_match',
] as const

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
  ctx.slots.inject('tool.call.toolview', function* () {
    for (const tool of GIS_TOOLS) {
      yield ctx.slots.register(
        { name: 'tool.call.toolview', key: `mcp__gis__${tool}` },
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
