window.__ModuleLoader__.load({
	id: "@local/gis-mcp-harness-renderer",
	factory: (require) => {
		var module = { exports: {} };
		var exports = module.exports;
		Object.defineProperty(exports, Symbol.toStringTag, { value: "Module" });
		let react = require("react");
		let react_jsx_runtime = require("react/jsx-runtime");
		//#region src/client/envelope.ts
		const ENVELOPE_MARKER = "[gis-result-envelope]\n";
		function record(value) {
			return typeof value === "object" && value !== null && !Array.isArray(value);
		}
		/** Parse only the explicitly tagged durable projection; unrelated JSON output is ignored. */
		function parseEnvelope(text) {
			const marker = text.lastIndexOf(ENVELOPE_MARKER);
			if (marker < 0) return null;
			try {
				const value = JSON.parse(text.slice(marker + 22).trim());
				if (!record(value) || value.schemaVersion !== "1.0" || typeof value.tool !== "string" || typeof value.traceId !== "string" || typeof value.summary !== "string" || !record(value.data) || !record(value.meta) || !record(value.visualization)) return null;
				if (typeof value.visualization.inline !== "boolean") return null;
				return value;
			} catch {
				return null;
			}
		}
		/** Flatten the frozen Harness result blocks without consulting live MCP state. */
		function envelopeFromBlock(block) {
			if (!record(block) || block.kind !== "tool-result" || !Array.isArray(block.content)) return null;
			return parseEnvelope(block.content.filter(record).filter((item) => item.type === "text" && typeof item.text === "string").map((item) => item.text).join("\n"));
		}
		//#endregion
		//#region src/client/GisToolCard.tsx
		const cardStyle = {
			border: "1px solid rgba(127,127,127,.25)",
			borderRadius: 10,
			overflow: "hidden",
			background: "var(--dsh-color-bg-secondary, rgba(127,127,127,.06))",
			display: "block",
			position: "relative",
			isolation: "isolate",
			zIndex: 1,
			contain: "layout",
			width: "100%",
			boxSizing: "border-box",
			marginBottom: 12
		};
		const mapFrameStyle = {
			display: "block",
			width: "100%",
			height: "100%",
			border: 0
		};
		const mapContainerStyle = {
			position: "relative",
			display: "block",
			width: "100%",
			height: 280,
			zIndex: 2,
			overflow: "hidden",
			borderTop: "1px solid rgba(127,127,127,.18)",
			borderBottom: "1px solid rgba(127,127,127,.18)"
		};
		/** Self-contained Leaflet document used when no result-service URL exists. */
		function leafletDocument(geojson) {
			return `<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>html,body,#map{height:100%;margin:0}</style></head><body><div id="map"></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\/script>
<script>const map=L.map('map');L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);const layer=L.geoJSON(${JSON.stringify(geojson).replaceAll("<", "\\u003c")}).addTo(map);const bounds=layer.getBounds();bounds.isValid()?map.fitBounds(bounds.pad(.15)):map.setView([32.06,118.79],11);<\/script>
</body></html>`;
		}
		/** Replay-stable GIS card derived solely from the durable Tool result. */
		function GisToolCard({ block, toolName, inspect }) {
			const envelope = envelopeFromBlock(block);
			const [expanded, setExpanded] = (0, react.useState)(true);
			if (!("kind" in block)) return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
				style: {
					...cardStyle,
					padding: 12
				},
				children: [
					"GIS · ",
					toolName,
					" · running…"
				]
			});
			if (envelope === null) return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
				style: {
					...cardStyle,
					padding: 12
				},
				children: [
					"GIS · ",
					toolName,
					" · result format unavailable"
				]
			});
			const mapUrl = envelope.visualization.mapUrl;
			const inlineGeoJson = envelope.visualization.geojson;
			const provider = typeof envelope.meta.provider === "string" ? envelope.meta.provider : "unknown";
			return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("section", {
				style: cardStyle,
				"data-tool": "gis",
				"data-state": block.isError ? "error" : "ok",
				children: [
					/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("button", {
						type: "button",
						onClick: () => setExpanded((value) => !value),
						style: {
							width: "100%",
							border: 0,
							padding: 12,
							textAlign: "left",
							cursor: "pointer",
							background: "transparent"
						},
						children: [
							/* @__PURE__ */ (0, react_jsx_runtime.jsx)("strong", { children: envelope.tool }),
							" · ",
							envelope.summary
						]
					}),
					/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
						style: {
							padding: "0 12px 10px",
							opacity: .7,
							fontSize: 12
						},
						children: [
							"provider: ",
							provider,
							" · trace: ",
							envelope.traceId
						]
					}),
					expanded && mapUrl !== null ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("div", {
						style: mapContainerStyle,
						"data-gis-map-preview": true,
						children: /* @__PURE__ */ (0, react_jsx_runtime.jsx)("iframe", {
							title: `${envelope.tool} map`,
							src: mapUrl,
							loading: "lazy",
							style: mapFrameStyle
						})
					}) : null,
					expanded && mapUrl === null && inlineGeoJson !== null ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("div", {
						style: mapContainerStyle,
						"data-gis-map-preview": true,
						children: /* @__PURE__ */ (0, react_jsx_runtime.jsx)("iframe", {
							title: `${envelope.tool} inline map`,
							srcDoc: leafletDocument(inlineGeoJson),
							sandbox: "allow-scripts",
							loading: "lazy",
							style: mapFrameStyle
						})
					}) : null,
					/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
						style: {
							display: "flex",
							gap: 8,
							padding: 10,
							alignItems: "center",
							minHeight: 24,
							position: "relative",
							zIndex: 2,
							background: "var(--dsh-color-bg-secondary, rgba(127,127,127,.06))"
						},
						children: [mapUrl !== null ? /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("a", {
							href: mapUrl,
							target: "_blank",
							rel: "noreferrer",
							style: { overflowWrap: "anywhere" },
							"data-gis-map-url": true,
							children: ["Open full map: ", mapUrl]
						}) : null, inspect !== void 0 ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("button", {
							type: "button",
							onClick: inspect,
							children: "Inspect"
						}) : null]
					})
				]
			});
		}
		//#endregion
		//#region src/client/index.ts
		/** Names exported by the `gis` MCP server after server qualification. */
		const GIS_TOOLS = [
			"geometry_validate",
			"coordinate_transform",
			"point_in_polygon",
			"geometry_distance",
			"geometry_area",
			"geometry_buffer",
			"geocode",
			"dataset_info",
			"features_within",
			"reverse_geocode",
			"poi_search",
			"nearby_search",
			"search_nearby_by_place",
			"administrative_search",
			"provider_route",
			"route_plan",
			"nearest_road",
			"map_match"
		];
		const inject = ["slots"];
		/**
		* DashScope may omit a Tool call id in continuation chunks. Older Harness
		* releases then lose the Tool name when freezing the result and request the
		* renderer registered under an empty key. Recover GIS identity from the
		* durable, explicitly tagged result instead of relying on transient UI state.
		*/
		function EmptyCallIdResult(props) {
			if (envelopeFromBlock(props.block) !== null) return (0, react.createElement)(GisToolCard, props);
			return (0, react.createElement)("section", { style: {
				border: "1px solid rgba(127,127,127,.25)",
				borderRadius: 10,
				padding: 12
			} }, (0, react.createElement)("strong", null, "Tool call"), (0, react.createElement)("div", { style: {
				marginTop: 8,
				opacity: .7
			} }, "Result available in Inspect."), props.inspect === void 0 ? null : (0, react.createElement)("button", {
				type: "button",
				onClick: props.inspect,
				style: { marginTop: 8 }
			}, "Inspect"));
		}
		/** Register one renderer for every GIS wire Tool without changing Harness core. */
		function apply(ctx) {
			ctx.slots.inject("tool.call.toolview", function* () {
				for (const tool of GIS_TOOLS) yield ctx.slots.register({
					name: "tool.call.toolview",
					key: `mcp__gis__${tool}`
				}, GisToolCard);
				yield ctx.slots.register({
					name: "tool.call.toolview",
					key: ""
				}, EmptyCallIdResult);
			});
		}
		//#endregion
		exports.apply = apply;
		exports.inject = inject;
		return module.exports;
	}
});

//# sourceMappingURL=client.js.map