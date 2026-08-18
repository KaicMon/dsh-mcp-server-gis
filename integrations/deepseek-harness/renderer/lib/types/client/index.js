import { GisToolCard } from "./GisToolCard.js";
/** Names exported by the `gis` MCP server after server qualification. */
const GIS_TOOLS = [
    'geometry_validate', 'coordinate_transform', 'point_in_polygon',
    'geometry_distance', 'geometry_area', 'geometry_buffer', 'geocode',
    'nearby_search', 'provider_route', 'route_plan', 'nearest_road', 'map_match',
];
export const inject = ['slots'];
/** Register one renderer for every GIS wire Tool without changing Harness core. */
export function apply(ctx) {
    ctx.slots.inject('tool.call.toolview', function* () {
        for (const tool of GIS_TOOLS) {
            yield ctx.slots.register({ name: 'tool.call.toolview', key: `mcp__gis__${tool}` }, GisToolCard);
        }
    });
}
//# sourceMappingURL=index.js.map