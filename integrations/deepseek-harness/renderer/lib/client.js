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
			background: "var(--dsh-color-bg-secondary, rgba(127,127,127,.06))",
			display: "block",
			position: "relative",
			width: "100%",
			boxSizing: "border-box",
			marginBottom: 12
		};
		/** Compact chat summary; maps render only in the session-level 地图展示 tab. */
		function GisToolCard({ block, toolName, inspect }) {
			const envelope = envelopeFromBlock(block);
			if (!("kind" in block)) return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
				style: {
					...cardStyle,
					padding: 12
				},
				children: [
					"GIS 工具 ",
					toolName,
					" 正在执行…"
				]
			});
			if (envelope === null) return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
				style: {
					...cardStyle,
					padding: 12
				},
				children: [
					"GIS 工具 ",
					toolName,
					" 的结果格式不可用"
				]
			});
			const provider = typeof envelope.meta.provider === "string" ? envelope.meta.provider : "unknown";
			return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("section", {
				style: cardStyle,
				"data-tool": "gis",
				"data-state": block.isError ? "error" : "ok",
				children: [
					/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
						style: { padding: 12 },
						children: [
							/* @__PURE__ */ (0, react_jsx_runtime.jsx)("strong", { children: envelope.tool }),
							" — ",
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
							envelope.traceId,
							" · 地图请在“地图展示”标签页查看"
						]
					}),
					/* @__PURE__ */ (0, react_jsx_runtime.jsx)("div", {
						style: {
							display: "flex",
							gap: 8,
							padding: 10,
							minHeight: 24
						},
						children: inspect !== void 0 ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("button", {
							type: "button",
							onClick: inspect,
							children: "Inspect"
						}) : null
					})
				]
			});
		}
		//#endregion
		//#region src/client/GisPanels.tsx
		/**
		* The GIS Tool profile deliberately exposes this fixed public surface. Keep
		* this catalog next to the renderer registrations so the panel and tool-card
		* renderer cannot drift apart. Runtime call history is not used as a source:
		* an uncalled tool is still available to the agent and belongs in this view.
		*/
		const GIS_MCP_TOOLS = [
			{
				name: "geometry_validate",
				group: "本地 GIS 分析",
				description: "校验 GeoJSON 几何对象与拓扑。"
			},
			{
				name: "coordinate_transform",
				group: "本地 GIS 分析",
				description: "在 EPSG:4326、EPSG:3857 等坐标系间转换坐标。"
			},
			{
				name: "point_in_polygon",
				group: "本地 GIS 分析",
				description: "判断点是否位于 Polygon 或 MultiPolygon 内。"
			},
			{
				name: "geometry_distance",
				group: "本地 GIS 分析",
				description: "计算两个 GeoJSON 几何对象之间的最短距离。"
			},
			{
				name: "geometry_area",
				group: "本地 GIS 分析",
				description: "计算 Polygon 或 MultiPolygon 的面积。"
			},
			{
				name: "geometry_buffer",
				group: "本地 GIS 分析",
				description: "生成以米为单位的 GeoJSON 缓冲区。"
			},
			{
				name: "dataset_info",
				group: "本地 GIS 分析",
				description: "查看配置目录中本地矢量数据集的元数据。"
			},
			{
				name: "features_within",
				group: "本地 GIS 分析",
				description: "查询数据集中与指定面相交的要素。"
			},
			{
				name: "geocode",
				group: "地点与 POI",
				description: "将中文地址或地名解析为 WGS84 候选坐标。"
			},
			{
				name: "reverse_geocode",
				group: "地点与 POI",
				description: "将 WGS84 坐标解析为标准化中文地址。"
			},
			{
				name: "poi_search",
				group: "地点与 POI",
				description: "按关键词、城市和分类搜索 POI。"
			},
			{
				name: "nearby_search",
				group: "地点与 POI",
				description: "以坐标为中心查询附近 POI。"
			},
			{
				name: "search_nearby_by_place",
				group: "地点与 POI",
				description: "以地名为中心一站式查询附近 POI。"
			},
			{
				name: "administrative_search",
				group: "地点与 POI",
				description: "查询中国行政区划及其 WGS84 中心点。"
			},
			{
				name: "provider_route",
				group: "路径与路网",
				description: "通过自动、高德或本地 OSM 服务规划步行/驾车路线。"
			},
			{
				name: "route_plan",
				group: "路径与路网",
				description: "使用本地 OSM 路网规划驾车路径。"
			},
			{
				name: "nearest_road",
				group: "路径与路网",
				description: "查找坐标附近的有向道路路段。"
			},
			{
				name: "map_match",
				group: "路径与路网",
				description: "将 GPS 轨迹匹配到本地 OSM 路网。"
			}
		];
		function rowsFromNodes(nodes) {
			const rows = [];
			for (const node of nodes.values()) {
				const root = node?.data?.root;
				if (typeof root !== "object" || root === null) continue;
				const record = root;
				const envelope = envelopeFromBlock(root);
				const name = typeof record.name === "string" ? record.name : typeof record.call?.name === "string" ? record.call.name : "unknown tool";
				if (!name.startsWith("mcp__gis__") && envelope === null) continue;
				rows.push({
					callId: typeof record.callId === "string" ? record.callId : String(rows.length),
					name,
					state: !("kind" in record) ? "执行中" : record.isError === true ? "失败" : "成功",
					envelope
				});
			}
			return rows;
		}
		function useGisRows(useSession) {
			const nodes = useSession((snapshot) => snapshot.chat.nodes);
			return (0, react.useMemo)(() => rowsFromNodes(nodes), [nodes]);
		}
		const pageStyle = {
			height: "100%",
			overflow: "auto",
			padding: 20,
			boxSizing: "border-box"
		};
		const rowStyle = {
			border: "1px solid rgba(127,127,127,.25)",
			borderRadius: 8,
			padding: 12,
			marginBottom: 8
		};
		/** Standalone Leaflet document for small inline GeoJSON results. */
		function leafletDocument(geojson) {
			return `<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"><style>html,body,#map{height:100%;margin:0}</style></head><body><div id="map"></div><script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\\/script><script>const map=L.map('map');L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);const layer=L.geoJSON(${JSON.stringify(geojson).replaceAll("<", "\\u003c")}).addTo(map);const bounds=layer.getBounds();bounds.isValid()?map.fitBounds(bounds.pad(.15)):map.setView([32.06,118.79],11);<\\/script></body></html>`;
		}
		/** Plugin-installed tab: the complete current GIS MCP public Tool surface. */
		function McpToolsPanel(_) {
			const groups = /* @__PURE__ */ new Map();
			for (const tool of GIS_MCP_TOOLS) {
				const tools = groups.get(tool.group) ?? [];
				tools.push(tool);
				groups.set(tool.group, tools);
			}
			return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("section", {
				style: pageStyle,
				"data-mcp-tools-panel": true,
				children: [
					/* @__PURE__ */ (0, react_jsx_runtime.jsx)("h2", {
						style: { marginTop: 0 },
						children: "MCP 工具面板"
					}),
					/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("p", {
						style: { opacity: .7 },
						children: [
							"当前 GIS MCP Profile 已注册的 ",
							GIS_MCP_TOOLS.length,
							" 个可用工具；这里不显示本次会话的调用记录。"
						]
					}),
					[...groups].map(([group, tools]) => /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("div", {
						style: { marginBottom: 20 },
						children: [/* @__PURE__ */ (0, react_jsx_runtime.jsx)("h3", { children: group }), tools.map((tool) => /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("article", {
							style: rowStyle,
							children: [/* @__PURE__ */ (0, react_jsx_runtime.jsxs)("strong", { children: ["mcp__gis__", tool.name] }), /* @__PURE__ */ (0, react_jsx_runtime.jsx)("div", {
								style: {
									marginTop: 6,
									opacity: .75
								},
								children: tool.description
							})]
						}, tool.name))]
					}, group))
				]
			});
		}
		/** Plugin-installed session tab: maps are intentionally outside agent messages. */
		function GisMapPanel({ useSession }) {
			const rows = useGisRows(useSession).filter((row) => row.envelope !== null);
			const [selected, setSelected] = (0, react.useState)(null);
			const current = rows.find((row) => row.callId === selected) ?? rows.at(-1) ?? null;
			const envelope = current?.envelope ?? null;
			return /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("section", {
				style: pageStyle,
				"data-gis-map-panel": true,
				children: [/* @__PURE__ */ (0, react_jsx_runtime.jsx)("h2", {
					style: { marginTop: 0 },
					children: "地图展示"
				}), envelope === null ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("p", { children: "调用路径规划、POI、缓冲区等 GIS 工具后，地图会显示在这里。" }) : /* @__PURE__ */ (0, react_jsx_runtime.jsxs)(react_jsx_runtime.Fragment, { children: [
					/* @__PURE__ */ (0, react_jsx_runtime.jsx)("select", {
						value: current.callId,
						onChange: (event) => setSelected(event.target.value),
						children: rows.map((row) => /* @__PURE__ */ (0, react_jsx_runtime.jsxs)("option", {
							value: row.callId,
							children: [
								row.envelope?.tool,
								" — ",
								row.envelope?.summary
							]
						}, row.callId))
					}),
					/* @__PURE__ */ (0, react_jsx_runtime.jsx)("p", { children: envelope.summary }),
					envelope.visualization.mapUrl !== null ? /* @__PURE__ */ (0, react_jsx_runtime.jsxs)(react_jsx_runtime.Fragment, { children: [/* @__PURE__ */ (0, react_jsx_runtime.jsx)("iframe", {
						title: `${envelope.tool} map`,
						src: envelope.visualization.mapUrl,
						loading: "lazy",
						style: {
							display: "block",
							width: "100%",
							height: 560,
							border: "1px solid rgba(127,127,127,.25)",
							borderRadius: 8
						}
					}), /* @__PURE__ */ (0, react_jsx_runtime.jsx)("p", { children: /* @__PURE__ */ (0, react_jsx_runtime.jsx)("a", {
						href: envelope.visualization.mapUrl,
						target: "_blank",
						rel: "noreferrer",
						children: "在新窗口打开完整地图"
					}) })] }) : envelope.visualization.geojson !== null ? /* @__PURE__ */ (0, react_jsx_runtime.jsx)("iframe", {
						title: `${envelope.tool} inline map`,
						srcDoc: leafletDocument(envelope.visualization.geojson),
						sandbox: "allow-scripts",
						loading: "lazy",
						style: {
							display: "block",
							width: "100%",
							height: 560,
							border: "1px solid rgba(127,127,127,.25)",
							borderRadius: 8
						}
					}) : /* @__PURE__ */ (0, react_jsx_runtime.jsx)("p", { children: "该结果没有可展示的空间数据。" })
				] })]
			});
		}
		//#endregion
		//#region src/client/index.ts
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
			ctx.slots.inject("conversation.view", function* () {
				yield ctx.slots.register({
					name: "conversation.view",
					id: "mcp-tools",
					order: 20,
					label: () => "MCP工具面板"
				}, McpToolsPanel);
				yield ctx.slots.register({
					name: "conversation.view",
					id: "gis-map",
					order: 30,
					label: () => "地图展示"
				}, GisMapPanel);
			});
			ctx.slots.inject("tool.call.toolview", function* () {
				for (const tool of GIS_MCP_TOOLS) yield ctx.slots.register({
					name: "tool.call.toolview",
					key: `mcp__gis__${tool.name}`
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