import { useMemo, useState } from 'react'
import type { ConvViewProps } from '@deepseek-ai/dsh-client-ui-conversation/client'
import { envelopeFromBlock, type GisEnvelope } from './envelope.ts'

interface ToolRow { callId: string; name: string; state: string; envelope: GisEnvelope | null }

/**
 * The GIS Tool profile deliberately exposes this fixed public surface. Keep
 * this catalog next to the renderer registrations so the panel and tool-card
 * renderer cannot drift apart. Runtime call history is not used as a source:
 * an uncalled tool is still available to the agent and belongs in this view.
 */
export const GIS_MCP_TOOLS = [
  { name: 'geometry_validate', group: '本地 GIS 分析', description: '校验 GeoJSON 几何对象与拓扑。' },
  { name: 'coordinate_transform', group: '本地 GIS 分析', description: '在 EPSG:4326、EPSG:3857 等坐标系间转换坐标。' },
  { name: 'point_in_polygon', group: '本地 GIS 分析', description: '判断点是否位于 Polygon 或 MultiPolygon 内。' },
  { name: 'geometry_distance', group: '本地 GIS 分析', description: '计算两个 GeoJSON 几何对象之间的最短距离。' },
  { name: 'geometry_area', group: '本地 GIS 分析', description: '计算 Polygon 或 MultiPolygon 的面积。' },
  { name: 'geometry_buffer', group: '本地 GIS 分析', description: '生成以米为单位的 GeoJSON 缓冲区。' },
  { name: 'dataset_info', group: '本地 GIS 分析', description: '查看配置目录中本地矢量数据集的元数据。' },
  { name: 'features_within', group: '本地 GIS 分析', description: '查询数据集中与指定面相交的要素。' },
  { name: 'geocode', group: '地点与 POI', description: '将中文地址或地名解析为 WGS84 候选坐标。' },
  { name: 'reverse_geocode', group: '地点与 POI', description: '将 WGS84 坐标解析为标准化中文地址。' },
  { name: 'poi_search', group: '地点与 POI', description: '按关键词、城市和分类搜索 POI。' },
  { name: 'nearby_search', group: '地点与 POI', description: '以坐标为中心查询附近 POI。' },
  { name: 'search_nearby_by_place', group: '地点与 POI', description: '以地名为中心一站式查询附近 POI。' },
  { name: 'administrative_search', group: '地点与 POI', description: '查询中国行政区划及其 WGS84 中心点。' },
  { name: 'provider_route', group: '路径与路网', description: '通过自动、高德或本地 OSM 服务规划步行/驾车路线。' },
  { name: 'route_plan', group: '路径与路网', description: '使用本地 OSM 路网规划驾车路径。' },
  { name: 'nearest_road', group: '路径与路网', description: '查找坐标附近的有向道路路段。' },
  { name: 'map_match', group: '路径与路网', description: '将 GPS 轨迹匹配到本地 OSM 路网。' },
] as const

function rowsFromNodes(nodes: ReadonlyMap<string, unknown>): ToolRow[] {
  const rows: ToolRow[] = []
  for (const node of nodes.values()) {
    const root = (node as { data?: { root?: unknown } })?.data?.root
    if (typeof root !== 'object' || root === null) continue
    const record = root as { callId?: unknown; name?: unknown; call?: { name?: unknown }; isError?: unknown }
    const envelope = envelopeFromBlock(root)
    const name = typeof record.name === 'string' ? record.name
      : typeof record.call?.name === 'string' ? record.call.name : 'unknown tool'
    if (!name.startsWith('mcp__gis__') && envelope === null) continue
    rows.push({ callId: typeof record.callId === 'string' ? record.callId : String(rows.length), name,
      state: !('kind' in record) ? '执行中' : record.isError === true ? '失败' : '成功', envelope })
  }
  return rows
}

function useGisRows(useSession: ConvViewProps['useSession']): ToolRow[] {
  const nodes = useSession(snapshot => snapshot.chat.nodes)
  return useMemo(() => rowsFromNodes(nodes), [nodes])
}

const pageStyle = { height: '100%', overflow: 'auto', padding: 20, boxSizing: 'border-box' } as const
const rowStyle = { border: '1px solid rgba(127,127,127,.25)', borderRadius: 8, padding: 12, marginBottom: 8 } as const

/** Standalone Leaflet document for small inline GeoJSON results. */
function leafletDocument(geojson: Record<string, unknown>): string {
  const data = JSON.stringify(geojson).replaceAll('<', '\\u003c')
  return `<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"><style>html,body,#map{height:100%;margin:0}</style></head><body><div id="map"></div><script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\\/script><script>const map=L.map('map');L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);const layer=L.geoJSON(${data}).addTo(map);const bounds=layer.getBounds();bounds.isValid()?map.fitBounds(bounds.pad(.15)):map.setView([32.06,118.79],11);<\\/script></body></html>`
}

/** Plugin-installed tab: the complete current GIS MCP public Tool surface. */
export function McpToolsPanel(_: ConvViewProps) {
  const groups = new Map<string, typeof GIS_MCP_TOOLS[number][]>()
  for (const tool of GIS_MCP_TOOLS) {
    const tools = groups.get(tool.group) ?? []
    tools.push(tool)
    groups.set(tool.group, tools)
  }
  return <section style={pageStyle} data-mcp-tools-panel>
    <h2 style={{ marginTop: 0 }}>MCP 工具面板</h2>
    <p style={{ opacity: .7 }}>当前 GIS MCP Profile 已注册的 {GIS_MCP_TOOLS.length} 个可用工具；这里不显示本次会话的调用记录。</p>
    {[...groups].map(([group, tools]) => <div key={group} style={{ marginBottom: 20 }}>
      <h3>{group}</h3>
      {tools.map(tool => <article key={tool.name} style={rowStyle}>
        <strong>mcp__gis__{tool.name}</strong>
        <div style={{ marginTop: 6, opacity: .75 }}>{tool.description}</div>
      </article>)}
    </div>)}
  </section>
}

/** Plugin-installed session tab: maps are intentionally outside agent messages. */
export function GisMapPanel({ useSession }: ConvViewProps) {
  const rows = useGisRows(useSession).filter(row => row.envelope !== null)
  const [selected, setSelected] = useState<string | null>(null)
  const current = rows.find(row => row.callId === selected) ?? rows.at(-1) ?? null
  const envelope = current?.envelope ?? null
  return <section style={pageStyle} data-gis-map-panel>
    <h2 style={{ marginTop: 0 }}>地图展示</h2>
    {envelope === null ? <p>调用路径规划、POI、缓冲区等 GIS 工具后，地图会显示在这里。</p> : <>
      <select value={current.callId} onChange={event => setSelected(event.target.value)}>
        {rows.map(row => <option key={row.callId} value={row.callId}>{row.envelope?.tool} — {row.envelope?.summary}</option>)}
      </select>
      <p>{envelope.summary}</p>
      {envelope.visualization.mapUrl !== null ? <>
        <iframe title={`${envelope.tool} map`} src={envelope.visualization.mapUrl} loading="lazy"
          style={{ display: 'block', width: '100%', height: 560, border: '1px solid rgba(127,127,127,.25)', borderRadius: 8 }} />
        <p><a href={envelope.visualization.mapUrl} target="_blank" rel="noreferrer">在新窗口打开完整地图</a></p>
      </> : envelope.visualization.geojson !== null ? <iframe title={`${envelope.tool} inline map`}
        srcDoc={leafletDocument(envelope.visualization.geojson)} sandbox="allow-scripts" loading="lazy"
        style={{ display: 'block', width: '100%', height: 560, border: '1px solid rgba(127,127,127,.25)', borderRadius: 8 }} />
        : <p>该结果没有可展示的空间数据。</p>}
    </>}
  </section>
}
