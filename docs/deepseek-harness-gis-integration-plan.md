# DeepSeek Harness 与 GIS/LBS MCP 平台集成规划

## 1. 文档目的

本文档定义 DeepSeek Harness 作为本项目 Agent 调用端和 Web 交互入口的集成方案。它记录双方系统边界、MCP 接入方式、GIS 结果展示协议、Qwen 模型配置、测试方法及实施阶段。

本文档与以下规划配套使用：

- `docs/gis-lbs-mcp-platform-plan.md`：GIS/LBS、GDAL、MapProvider 和 Tool 建设；
- `docs/osm-routing-mcp-plugin-plan.md`：OSM 路由、空间索引和地图匹配；
- 本文档：Agent、MCP Client、Web UI、地图 Renderer 和最终演示。

原计划中的 Cherry Studio 集成由 DeepSeek Harness **完全替代**，Cherry Studio 不再属于正式交付或验收范围。底层 MCP 协议仍通过自动化契约测试验证，不依赖某个桌面客户端。

最后更新：2026-08-15。

## 2. 已确认的设计决策

| 设计项 | 决策 |
|---|---|
| Harness 定位 | 两阶段：先作为独立 Agent 客户端，后开发原生 GIS Renderer |
| MCP 传输 | 本地先用 STDIO，生产化阶段增加 Streamable HTTP |
| 地图结果 | 小结果内联 GeoJSON；大结果返回 `resultId + mapUrl` |
| Tool 数量控制 | 当前插件少，默认全部暴露；保留 Profile，暂不启用动态 RAG |
| RAG-MCP | 保留现有实现；Tool 足够多且有评测数据后迁移设计到 Harness 插件 |
| 任务编排 | 原子 Tool + 少量高频复合 Tool；首轮验收必须验证原子 Tool 连续调用 |
| 错误恢复 | 服务端处理技术性重试/降级，Agent 处理语义性决策 |
| 大结果存储 | `ResultStore` 抽象；开发用内存，生产可切 Redis |
| Tool 输出 | 所有 GIS/LBS Tool 使用版本化统一结果信封 |
| 地图组件 | Harness 原生 Chat Renderer，内部复用 Leaflet |
| Harness 依赖方式 | 固定版本 + 当前仓库内维护外置插件，不修改 Harness 核心 |
| Tool 权限 | 默认自动执行；管理类和修改类 Tool 暂不向 Harness 暴露 |
| 部署范围 | 第一版本机优先，同时预留远程部署接口 |
| 模型测试 | 自动化使用 Mock；最终验收使用真实 Qwen |
| 模型 Provider | 配置层通用，第一轮使用 DashScope OpenAI-compatible API |
| Trace | 增加跨 Harness、MCP、插件、Provider 和结果仓库的统一 Trace |
| 验收范围 | 七类代表性场景 + 基础量化指标 |
| Cherry Studio | 完全替换，不再维护该集成 |

## 3. 系统定位与边界

DeepSeek Harness 不是单纯静态前端，而是包含 Web UI、Agent Loop、模型适配、Tool Registry、MCP Client、会话和权限能力的 Agent Harness。

```text
浏览器
  |
  v
DeepSeek Harness Web UI
  |
  v
Harness Agent Loop + Qwen
  |
  v
@deepseek-ai/dsh-mcp-client
  |
  +-- STDIO（本地开发和演示）
  +-- Streamable HTTP（后续远程部署）
  |
  v
C++ MCP Server
  |
  +-- osm-routing plugin
  +-- gis-analysis plugin
  +-- map-services plugin
  |
  +-- routing_core
  +-- gis_core / GDAL / OGR / GEOS
  +-- MapProvider / Redis
```

边界约束：

- C++ MCP Server 不依赖 Harness 源码或 Node.js；
- Harness 不访问 `routing_core`、`gis_core` 或插件 ABI，只使用 MCP；
- Renderer 不读取 C++ 内存对象，只消费统一结果信封和 HTTP 结果 API；
- 模型 Provider 可替换，MCP Server 不感知上游模型类型；
- Harness 故障不影响 MCP Server 的独立测试和服务能力。

## 4. 两阶段前端策略

### 4.1 第一阶段：独立 Agent 客户端

Harness 承担：

- Qwen 对话；
- Tool Schema 注入；
- Tool 选择和连续调用；
- Tool 结果摘要展示；
- MCP 连接、重连和工具列表同步。

现有 Leaflet 页面承担：

- 路线 Geometry 展示；
- POI 标记和候选点展示；
- Buffer、Polygon 和地图匹配轨迹展示；
- 大结果的独立地图查看。

第一阶段 Harness 通过 Tool 结果中的 `mapUrl` 打开完整地图，不改动 Harness 核心 UI。

### 4.2 第二阶段：原生 GIS Renderer

在当前仓库开发 Harness 外置插件。Renderer 识别 GIS 统一结果信封，并在对话中显示：

- 紧凑 Leaflet 地图卡片；
- 路线、起终点、POI 和 Buffer 图层；
- 距离、预计时间、Provider 等摘要；
- 展开地图和打开独立页面操作；
- 过期结果和加载失败状态。

现有独立 Leaflet 页面继续保留，作为调试、大图和降级入口。

## 5. 仓库目录规划

```text
integrations/deepseek-harness/
├── README.md
├── VERSION                     # 固定 Harness commit/tag
├── cordis.patch.yml            # MCP Server 和模型配置层
├── schemas/
│   └── gis-result-envelope.schema.json
├── renderer/
│   ├── package.json
│   ├── src/
│   ├── test/
│   └── cordis.patch.yml
├── e2e/
│   ├── fixtures/
│   ├── mock-llm/
│   ├── scenarios/
│   └── reports/
└── patches/                    # 仅在上游缺少必要扩展点时使用
```

约束：

- 固定并记录验证过的 Harness 版本；
- Renderer 作为标准 Cordis/Harness 插件，不直接修改上游核心；
- 升级 Harness 前运行 MCP、Mock LLM 和 Renderer 兼容测试；
- 必须修改上游时，仅保存最小补丁并记录原因和移除条件；
- Node 构建产物放在集成目录的忽略目录中，C++ 构建仍全部放在 `build_dev/`。

## 6. MCP 连接设计

### 6.1 本地 STDIO

第一版由 Harness 启动 C++ MCP Server。配置草案：

```yaml
- id: mcp-gis-platform
  name: '@deepseek-ai/dsh-mcp-client'
  config:
    serverName: gis
    transport: stdio
    command: /home/humr/agan-projects/mcp/build_dev/release/mcp_server/mcp_server
    args: []
    cwd: /home/humr/agan-projects/mcp/build_dev/release/mcp_server
    env:
      MCP_ROUTING_NETWORK: /home/humr/agan-projects/mcp/routing/data/generated/nanjing-v1.route
    toolCallTimeoutMs: 120000
    failOnStartupError: true
    reconnect:
      enabled: true
      initialDelayMs: 500
      maxDelayMs: 30000
      maxAttempts: 10
```

最终路径由正式构建布局决定，不在业务代码中硬编码。

Harness 对工具使用服务器限定名称：

```text
mcp__gis__route_plan
mcp__gis__geometry_buffer
mcp__gis__nearby_search
```

Harness 与 Server 的兼容测试必须覆盖：

```text
initialize
-> notifications/initialized
-> tools/list（包括分页边界）
-> tools/call
-> notifications/tools/list_changed
-> 重新 tools/list
-> shutdown / EOF
```

### 6.2 后续 Streamable HTTP

远程模式启用前必须验证：

- 当前 C++ HTTP Transport 与 Harness MCP SDK 的协议兼容性；
- Session 建立、关联、过期和并发隔离；
- 初始化握手和断线恢复；
- Bearer Token 或其他服务鉴权；
- TLS、反向代理、CORS 和请求大小限制；
- 多客户端 Tool 通知分发；
- 服务健康检查和优雅停止。

远程模式不是第一阶段交付阻塞项。

## 7. Tool 暴露与 RAG 策略

当前预计只有以下主要插件：

- `osm-routing`
- `gis-analysis`
- `map-services`

因此第一版默认暴露全部业务 Tool，避免过早引入 Tool Gateway。配置层保留以下逻辑 Profile：

```text
gis-full       所有业务 Tool，默认
routing        路径规划、邻近道路、地图匹配
gis-analysis   本地几何和坐标运算
map-services   地理编码、POI、周边和在线 Provider
```

现有 C++ RAG-MCP 位于自研 `mcp_client` 调用链中，Harness 使用自己的 MCP Client，因此它不会自动生效。本阶段：

- 不删除或重写现有 RAG-MCP；
- 不建立 `search_tools + execute_tool` 两阶段 Gateway；
- 记录每次请求注入的 Tool Schema Token 和选择正确率；
- Tool 数量明显增长且已有评测集后，复用现有索引字段和检索思想开发 Harness Tool 检索插件。

是否进入动态检索阶段由数据决定，而非只按固定数量触发。重点观察：

- Tool 选择错误率；
- 平均 Schema Token；
- Tool 描述相似度；
- Qwen 首次选中正确 Tool 的比例；
- Top-K 方案的 Recall@K。

## 8. 原子 Tool 与复合 Tool

原子 Tool 是平台基础，例如：

```text
geocode
nearby_search
geometry_buffer
point_in_polygon
route_plan
```

第一轮 E2E 必须验证 Qwen 能完成：

```text
geocode
-> nearby_search
-> 候选选择
-> route_plan
-> 结果汇总
```

后续可为稳定高频流程增加少量复合 Tool，例如 `find_nearby_and_route`，但必须满足：

- 不替代原子 Tool；
- 不把所有业务堆入一个万能 Tool；
- 明确输入语义和错误边界；
- 分别统计自主编排与复合 Tool 的成功率、延迟和调用成本。

## 9. 统一 GIS 结果信封

所有 GIS/LBS Tool 的 `structuredContent` 使用同一版本化信封。第一版草案：

```json
{
  "schemaVersion": "1.0",
  "tool": "nearby_search",
  "traceId": "trace_...",
  "summary": "找到 5 个地铁站，最近站点距离 820 米",
  "data": {},
  "visualization": {
    "kind": "geojson",
    "inline": true,
    "geojson": {},
    "resultId": null,
    "mapUrl": null
  },
  "meta": {
    "coordinateSystem": "EPSG:4326",
    "provider": "local-osm",
    "durationMs": 12,
    "cacheHit": false,
    "fallbackUsed": false,
    "attempts": 1
  }
}
```

字段规则：

- `schemaVersion`：Renderer 与 Tool 的兼容依据；
- `tool`：MCP 原始 Tool 名；
- `traceId`：跨系统调用关联标识；
- `summary`：供模型直接阅读，不重复完整 Geometry；
- `data`：Tool 自有结构化业务结果；
- `visualization`：统一地图渲染入口；
- `meta`：坐标、Provider、缓存、降级和性能信息。

错误也使用统一结构，并至少包含：

```json
{
  "schemaVersion": "1.0",
  "tool": "geocode",
  "traceId": "trace_...",
  "summary": "地点存在歧义",
  "error": {
    "code": "AMBIGUOUS_LOCATION",
    "message": "找到多个同名地点",
    "retryable": false,
    "details": {}
  },
  "meta": {}
}
```

MCP 外层仍正确设置 `isError`，不能只依赖信封中的 `error` 字段。

## 10. GeoJSON 内联与外置策略

采用双阈值决策：

- Geometry 点数；
- JSON UTF-8 序列化字节数。

小结果：

```json
{
  "inline": true,
  "geojson": { "type": "FeatureCollection", "features": [] }
}
```

大结果：

```json
{
  "inline": false,
  "geojson": null,
  "resultId": "gis_...",
  "mapUrl": "http://127.0.0.1:18096/results/gis_..."
}
```

初始阈值在实现阶段通过 Token 和浏览器性能测试确定，不写死在协议中。配置必须设定合理默认值和硬上限。

## 11. ResultStore 设计

```text
ResultStore
├── MemoryResultStore   开发和本机演示默认
└── RedisResultStore    生产、多实例和共享结果
```

公共职责：

- 保存序列化结果；
- 生成不可预测 `resultId`；
- TTL 过期；
- 按 ID 查询；
- 容量和大小限制；
- 过期与不存在的稳定错误；
- 为 HTTP 地图接口提供数据。

`MemoryResultStore` 至少限制：

- 单结果最大字节数；
- 最大 Geometry 点数；
- 最大结果数量；
- 总内存容量；
- 默认和最大 TTL；
- LRU 或等价淘汰策略。

切换到 Redis 不改变 Tool Schema、`resultId` 语义和 Renderer 接口。

## 12. Qwen 模型接入

第一轮真实模型使用阿里云 DashScope OpenAI-compatible API。MCP Server 不绑定模型，Harness 配置层保持多 Provider 能力。

配置草案：

```yaml
providers:
  qwen:
    displayName: Qwen
    api: openai-completions
    baseURL: https://dashscope.aliyuncs.com/compatible-mode/v1
    apiKeyEnv: DASHSCOPE_API_KEY
    models:
      - id: qwen-plus
```

`qwen-plus` 只是初始候选，最终实施时从 DashScope 实际可用模型中选择并固定。验收报告必须记录：

- Provider；
- 完整模型 ID；
- Harness 版本；
- Tool Calling 模式；
- 关键模型参数；
- 测试日期。

不得把 `DASHSCOPE_API_KEY` 写入仓库、Tool Schema、日志或测试快照。

模型兼容探测包括：

- 单 Tool 选择和参数生成；
- 连续调用两个以上 Tool；
- 使用前一 Tool 输出生成后一 Tool 参数；
- Tool 错误后的参数修正；
- 中文地名处理；
- 经纬度顺序保持；
- 流式 `tool_calls` 聚合；
- 大结果摘要而非复制 GeoJSON。

DeepSeek 可作为未来对照模型，但不是第一版硬依赖。

## 13. 分层错误恢复

### 服务端负责技术性恢复

- 连接和读取超时；
- HTTP 5xx；
- Provider 临时限流；
- 短暂网络故障；
- 明确可重试的幂等请求；
- 主备 Provider 切换。

服务端必须实施有限重试、指数退避和抖动，不能无限重试。参数错误、鉴权失败和确定性空结果不得盲目换 Provider。

### Agent 负责语义性决策

- 地址存在歧义；
- 是否扩大周边搜索半径；
- 是否选择其他候选地点；
- 是否切换步行/驾车等路线模式；
- 是否接受更远的 POI。

结果元数据必须提供 `provider`、`attempts`、`fallbackUsed` 和稳定错误码。

## 14. 自动执行与安全边界

Harness 中的业务 Tool 默认自动执行，避免复合任务中反复确认。

同时执行以下约束：

- 只向 Harness 暴露查询和本地计算类 Tool；
- 路网版本切换、缓存清空、Provider 配置修改等管理 Tool 暂不暴露；
- Provider API Key 只由服务端配置读取；
- 限制批量数量、搜索半径、输出大小和调用 Deadline；
- Provider 层保留限流和成本保护；
- 日志对密钥、认证头和可配置的精确位置字段脱敏；
- 防止模型循环调用导致无限外部请求；
- 未来新增有副作用 Tool 时重新评估审批机制。

## 15. Trace 与可观测性

每次用户任务生成或传播 `traceId`，关联：

```text
Harness session/turn
-> model request
-> public Tool name
-> MCP request ID
-> raw Tool name
-> plugin generation
-> Provider / cache / fallback
-> ResultStore resultId
-> duration / error
```

第一阶段写入结构化日志，不开发可视化调用时间线。建议字段：

- `timestamp`
- `traceId`
- `sessionId`
- `mcpRequestId`
- `tool`
- `plugin`
- `pluginGeneration`
- `provider`
- `cacheHit`
- `fallbackUsed`
- `attempts`
- `durationMs`
- `resultId`
- `errorCode`

Tool 参数只记录脱敏摘要，禁止记录 API Key 和完整认证头。

## 16. 插件热更新协同

当 C++ 插件新增、删除或修改 Tool Schema：

```text
PluginRuntime staging + 校验
-> 原子发布插件注册表快照
-> notifications/tools/list_changed
-> Harness 重新 tools/list
-> 原子替换 ctx.tools 中该服务器的工具世代
```

验证要求：

- 更新前正在执行的请求使用旧插件快照安全完成；
- Harness 不出现重复或半套 Tool；
- Tool 名称不变时保持稳定；
- Schema 变化后新会话/请求使用新定义；
- 更新失败时 Harness 继续看到上一可用世代；
- Tool 列表刷新满足量化时延指标。

## 17. 本地部署与远程预留

第一版组件默认监听本机：

```text
Harness Web UI       127.0.0.1:3080
Routing HTTP         127.0.0.1:8088
GIS Result HTTP      127.0.0.1:18096
C++ MCP              STDIO child process
MemoryResultStore    GIS Result HTTP 进程内
```

需要配置化：

- Web UI 地址；
- 地图服务基址；
- MCP Server 命令和工作目录；
- 路网快照路径；
- ResultStore 类型和 TTL；
- Provider URL 和凭据变量名。

不得在 Tool 业务实现中硬编码端口或绝对路径。

## 18. 测试分层

### 18.1 C++ 单元测试

- GIS Geometry 和 CRS；
- 路由、空间索引和地图匹配；
- ResultStore TTL、容量和并发；
- Provider 归一化、超时和降级；
- 统一结果信封序列化。

### 18.2 MCP 契约测试

- 初始化生命周期；
- Tool 列表和 Schema；
- Tool 成功/业务错误/协议错误；
- `isError` 与错误信封一致；
- `structuredContent`；
- Tool 列表变更通知；
- 断开和重连。

### 18.3 Harness + Mock LLM

固定模型输出，验证：

- Tool 注册名称；
- 连续调用顺序；
- 参数传递；
- 错误修正分支；
- 小/大结果处理；
- Renderer 数据入口；
- 插件热更新后的工具替换。

### 18.4 Harness + 真实 Qwen

真实模型测试不作为普通提交的确定性回归测试，只用于受控验收并生成报告。

## 19. 代表性验收场景

### 场景 A：周边地铁与步行路线

> 帮我查找南京师范大学附近三公里内的地铁站，并规划到最近地铁站的步行路线。

验证 `geocode -> nearby_search -> 候选选择 -> route_plan -> 汇总/地图`。

### 场景 B：本地 GIS 分析

输入 GeoJSON，依次执行合法性检查、米制 Buffer 和点在多边形内判断。

### 场景 C：路线比较

为两个候选地点规划路线，由 Agent 比较距离和预计时间并给出选择依据。

### 场景 D：Provider 降级

模拟主 Provider 临时失败，验证服务端有限重试和备用 Provider，Agent 不重复进行技术性重试。

### 场景 E：模糊地点

返回多个候选地址，验证 Qwen 能基于上下文选择或向用户说明歧义。

### 场景 F：大 Geometry

构造超过内联阈值的路线或 FeatureCollection，确保模型只得到摘要和 `resultId + mapUrl`。

### 场景 G：插件热更新

更新一个 Tool Schema，验证 C++ 原子发布、通知、Harness 工具刷新和后续调用。

## 20. 初始量化指标

| 指标 | 初始目标 |
|---|---|
| MCP 协议契约测试成功率 | 100% |
| Harness + Mock LLM 回归成功率 | 100% |
| 真实 Qwen 代表性任务完成率 | 不低于 80% |
| 普通本地 GIS Tool P95 | 不高于 100 ms |
| 本地路线查询 | 沿用并单独记录 routing benchmark |
| Provider 默认请求超时 | 10 秒，可配置 |
| 超阈值大结果进入模型上下文 | 0 次 |
| API Key 泄漏 | 0 次 |
| Tool 热更新至 Harness 刷新 | 不高于 5 秒 |
| 复合任务可由 traceId 完整追踪 | 100% |

首次完整基准后允许调整指标，但必须记录调整原因和前后数据。

## 21. 实施阶段

### H0：兼容性探测

- 固定 Harness commit/tag；
- 使用当前 C++ Server 验证 STDIO 初始化、工具发现和调用；
- 验证 `tools/list_changed`；
- 配置 DashScope/Qwen 并验证单 Tool Calling；
- 记录发现的协议兼容问题。

**完成标准**：Qwen 能通过 Harness 调用一个现有 MCP Tool，且调用链可在双方日志确认。

### H1：统一结果信封与 Trace

- 建立 JSON Schema；
- 在 GIS/LBS Tool 中统一 `structuredContent`；
- 增加 traceId 传播和结构化日志；
- 添加信封契约测试。

**完成标准**：所有纳入 Harness 的 GIS/LBS Tool 返回兼容的成功或错误信封。

### H2：ResultStore 与独立地图入口

- 定义 `ResultStore`；
- 实现 `MemoryResultStore`；
- 增加结果 HTTP API 和 TTL；
- 实现 GeoJSON 双阈值；
- 让 Leaflet 页面按 `resultId` 加载结果。

**完成标准**：大 Geometry 不进入模型上下文，可通过 `mapUrl` 展示。

### H3：Mock Harness 自动化

- 建立 Harness 固定版本集成目录；
- 配置 MCP Client；
- 构建 Mock LLM；
- 覆盖连续调用、错误、大结果和热更新。

**完成标准**：确定性 Mock 回归全部通过。

### H4：真实 Qwen 验收

- 固定实际 Qwen 模型；
- 执行七类代表性场景；
- 收集完成率、Token、延迟和 Trace；
- 调整 Tool 描述和 Schema，但不为测试样例硬编码模型逻辑。

**完成标准**：达到初始量化指标并生成验收报告。

### H5：Harness GIS Renderer

- 开发当前仓库内的外置 Renderer；
- 复用 Leaflet；
- 支持内联和外置结果；
- 增加地图卡片、展开和错误状态测试。

**完成标准**：对话内可直接显示路线、POI 和 Buffer，独立页面仍可用。

### H6：远程部署能力

- 验证 Streamable HTTP；
- 增加鉴权、TLS/代理部署指导；
- 实现 RedisResultStore；
- 验证多 Session 和多客户端隔离。

**完成标准**：Harness 可通过受保护的远程 MCP URL 和结果 API 完成相同代表性任务。

### H7：按数据决定是否启用 Harness Tool RAG

- 分析 Tool 数量、Token 和错误选择数据；
- 构建 Tool 检索评测集；
- 若收益明确，复用现有 RAG-MCP 设计实现 Harness 插件；
- 对比全量/Profile/Top-K。

**完成标准**：只有在保持目标 Recall@K 和任务完成率的前提下，才启用动态检索。

## 22. 非目标

- 第一阶段不修改或 Fork Harness 核心；
- 第一阶段不实现 Streamable HTTP 生产部署；
- 当前插件数量较少时不强制启用 RAG；
- 不把所有业务包装成一个万能复合 Tool；
- 不把完整大 Geometry 注入模型上下文；
- 不向 Harness 暴露管理和数据修改 Tool；
- 不同时维护 Cherry Studio 集成；
- 不把 Qwen/DashScope 依赖写入 C++ MCP Server。

## 23. 主要风险

- Harness 仍处于快速迭代阶段，存在破坏性变更；
- Qwen 不同模型版本的 Tool Calling 能力不一致；
- 当前 C++ HTTP Transport 可能不完全兼容现代 Streamable HTTP；
- Tool Schema 过大或相似会降低选择准确率；
- 大 GeoJSON 可能影响模型上下文、网络和浏览器性能；
- 自动执行在线 Tool 可能产生意外 API 成本；
- 精确位置和查询内容可能涉及隐私；
- Renderer 需要兼顾 Harness 插件 API 与上游版本变化。

缓解措施包括固定版本、契约测试、Mock LLM、结果外置、调用上限、Trace 脱敏和升级门禁。

## 24. 当前状态

| 项目 | 状态 |
|---|---|
| 关键设计问答 | 已完成 |
| 集成规划文档 | 已完成 |
| Harness 版本固定 | 已完成（源码 commit 与发布版 Web 包分别固定并记录） |
| H0 兼容性探测 | 已完成（STDIO、工具发现/调用/刷新及真实 Qwen 均有调用链证据） |
| H1 统一信封与 Trace | 已完成（全部 GIS/LBS Tool 与契约测试） |
| H2 ResultStore | 已完成第一阶段范围（MemoryResultStore、双阈值、结果 API 和 Leaflet 页面） |
| H3 Mock Harness 测试 | 已完成（连续调用、错误修正、大结果、数据集链路和会话内热更新） |
| H4 Qwen 验收 | 已完成（`qwen-plus` + Amap，七类场景人工复核 7/7 通过） |
| H5 GIS Renderer | 已完成（构建、契约、Web manifest/bundle、对话卡片及外置 Leaflet 页面均已验证） |
| H6 远程部署 | 后续阶段，属于第一阶段非目标 |
| H7 Tool RAG 决策 | 已决定当前使用 Profile，待 Tool 数量和评测数据足够后再启用 RAG |
