import { jsxs as _jsxs, jsx as _jsx } from "react/jsx-runtime";
import { useState } from 'react';
import { envelopeFromBlock } from "./envelope.js";
const cardStyle = {
    border: '1px solid rgba(127,127,127,.25)', borderRadius: 10, overflow: 'hidden',
    background: 'var(--dsh-color-bg-secondary, rgba(127,127,127,.06))',
};
/** Self-contained Leaflet document used when no result-service URL exists. */
function leafletDocument(geojson) {
    // Escape '<' so user-provided GeoJSON properties cannot terminate script.
    const data = JSON.stringify(geojson).replaceAll('<', '\\u003c');
    return `<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>html,body,#map{height:100%;margin:0}</style></head><body><div id="map"></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\/script>
<script>const map=L.map('map');L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);const layer=L.geoJSON(${data}).addTo(map);const bounds=layer.getBounds();bounds.isValid()?map.fitBounds(bounds.pad(.15)):map.setView([32.06,118.79],11);<\/script>
</body></html>`;
}
/** Replay-stable GIS card derived solely from the durable Tool result. */
export function GisToolCard({ block, toolName, inspect }) {
    const envelope = envelopeFromBlock(block);
    const [expanded, setExpanded] = useState(true);
    const running = !('kind' in block);
    if (running) {
        return _jsxs("div", { style: { ...cardStyle, padding: 12 }, children: ["GIS \u00B7 ", toolName, " \u00B7 running\u2026"] });
    }
    if (envelope === null) {
        return _jsxs("div", { style: { ...cardStyle, padding: 12 }, children: ["GIS \u00B7 ", toolName, " \u00B7 result format unavailable"] });
    }
    const mapUrl = envelope.visualization.mapUrl;
    const inlineGeoJson = envelope.visualization.geojson;
    const provider = typeof envelope.meta.provider === 'string' ? envelope.meta.provider : 'unknown';
    return (_jsxs("section", { style: cardStyle, "data-tool": "gis", "data-state": block.isError ? 'error' : 'ok', children: [_jsxs("button", { type: "button", onClick: () => setExpanded(value => !value), style: { width: '100%', border: 0, padding: 12, textAlign: 'left', cursor: 'pointer', background: 'transparent' }, children: [_jsx("strong", { children: envelope.tool }), " \u00B7 ", envelope.summary] }), _jsxs("div", { style: { padding: '0 12px 10px', opacity: .7, fontSize: 12 }, children: ["provider: ", provider, " \u00B7 trace: ", envelope.traceId] }), expanded && mapUrl !== null ? (_jsx("iframe", { title: `${envelope.tool} map`, src: mapUrl, style: { display: 'block', width: '100%', height: 360, border: 0 } })) : null, expanded && mapUrl === null && inlineGeoJson !== null ? (_jsx("iframe", { title: `${envelope.tool} inline map`, srcDoc: leafletDocument(inlineGeoJson), sandbox: "allow-scripts", style: { display: 'block', width: '100%', height: 360, border: 0 } })) : null, _jsxs("div", { style: { display: 'flex', gap: 8, padding: 10 }, children: [mapUrl !== null ? _jsx("a", { href: mapUrl, target: "_blank", rel: "noreferrer", children: "Open full map" }) : null, inspect !== undefined ? _jsx("button", { type: "button", onClick: inspect, children: "Inspect" }) : null] })] }));
}
//# sourceMappingURL=GisToolCard.js.map