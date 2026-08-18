import { useState } from 'react'
import type { ToolCallViewProps } from '@deepseek-ai/dsh-client-ui-tool/client'
import { envelopeFromBlock } from './envelope.ts'

const cardStyle = {
  border: '1px solid rgba(127,127,127,.25)', borderRadius: 10, overflow: 'hidden',
  background: 'var(--dsh-color-bg-secondary, rgba(127,127,127,.06))',
  // Tool rows are updated from a short running state to a much taller map.
  // Give the settled card an independent layout/paint boundary so a following
  // assistant message cannot be projected over the iframe while row heights
  // are being reconciled.
  display: 'block', position: 'relative', isolation: 'isolate', zIndex: 1,
  // Keep layout containment for row measurement, but do not use paint
  // containment: an iframe is a separate compositor surface and Harness's
  // Tool-row face could otherwise be painted above the clipped map surface.
  contain: 'layout', width: '100%', boxSizing: 'border-box',
  marginBottom: 12,
} as const

const mapFrameStyle = {
  display: 'block', width: '100%', height: '100%', border: 0,
} as const

const mapContainerStyle = {
  position: 'relative', display: 'block', width: '100%', height: 280,
  zIndex: 2,
  overflow: 'hidden', borderTop: '1px solid rgba(127,127,127,.18)',
  borderBottom: '1px solid rgba(127,127,127,.18)',
} as const

/** Self-contained Leaflet document used when no result-service URL exists. */
function leafletDocument(geojson: Record<string, unknown>): string {
  // Escape '<' so user-provided GeoJSON properties cannot terminate script.
  const data = JSON.stringify(geojson).replaceAll('<', '\\u003c')
  return `<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>html,body,#map{height:100%;margin:0}</style></head><body><div id="map"></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\/script>
<script>const map=L.map('map');L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);const layer=L.geoJSON(${data}).addTo(map);const bounds=layer.getBounds();bounds.isValid()?map.fitBounds(bounds.pad(.15)):map.setView([32.06,118.79],11);<\/script>
</body></html>`
}

/** Replay-stable GIS card derived solely from the durable Tool result. */
export function GisToolCard({ block, toolName, inspect }: ToolCallViewProps) {
  const envelope = envelopeFromBlock(block)
  const [expanded, setExpanded] = useState(true)
  const running = !('kind' in block)
  if (running) {
    return <div style={{ ...cardStyle, padding: 12 }}>GIS · {toolName} · running…</div>
  }
  if (envelope === null) {
    return <div style={{ ...cardStyle, padding: 12 }}>GIS · {toolName} · result format unavailable</div>
  }
  const mapUrl = envelope.visualization.mapUrl
  const inlineGeoJson = envelope.visualization.geojson
  const provider = typeof envelope.meta.provider === 'string' ? envelope.meta.provider : 'unknown'
  return (
    <section style={cardStyle} data-tool="gis" data-state={block.isError ? 'error' : 'ok'}>
      <button type="button" onClick={() => setExpanded(value => !value)}
        style={{ width: '100%', border: 0, padding: 12, textAlign: 'left', cursor: 'pointer', background: 'transparent' }}>
        <strong>{envelope.tool}</strong> · {envelope.summary}
      </button>
      <div style={{ padding: '0 12px 10px', opacity: .7, fontSize: 12 }}>
        provider: {provider} · trace: {envelope.traceId}
      </div>
      {expanded && mapUrl !== null ? (
        <div style={mapContainerStyle} data-gis-map-preview>
          <iframe title={`${envelope.tool} map`} src={mapUrl}
            loading="lazy" style={mapFrameStyle} />
        </div>
      ) : null}
      {expanded && mapUrl === null && inlineGeoJson !== null ? (
        <div style={mapContainerStyle} data-gis-map-preview>
          <iframe title={`${envelope.tool} inline map`} srcDoc={leafletDocument(inlineGeoJson)}
            sandbox="allow-scripts" loading="lazy" style={mapFrameStyle} />
        </div>
      ) : null}
      <div style={{ display: 'flex', gap: 8, padding: 10, alignItems: 'center',
        minHeight: 24, position: 'relative', zIndex: 2,
        background: 'var(--dsh-color-bg-secondary, rgba(127,127,127,.06))' }}>
        {mapUrl !== null ? (
          <a href={mapUrl} target="_blank" rel="noreferrer"
            style={{ overflowWrap: 'anywhere' }} data-gis-map-url>
            Open full map: {mapUrl}
          </a>
        ) : null}
        {inspect !== undefined ? <button type="button" onClick={inspect}>Inspect</button> : null}
      </div>
    </section>
  )
}
