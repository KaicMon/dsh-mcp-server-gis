# GIS/LBS MCP 平台新增需求与实施规划

## 1. 文档目的

本文档记录在现有 MCP Server、动态插件系统和 OSM 路由引擎之上新增的 GIS/LBS 平台需求，并作为后续设计、开发和验收依据。

本规划不替代 `docs/osm-routing-mcp-plugin-plan.md`。两者的边界是：

- OSM 路由计划负责路网构建、空间索引、路径搜索、地图匹配、路网快照和路由服务；
- 本计划负责通用 GIS 分析、第三方地图服务适配、LBS Tool、智能工具检索、缓存治理及复合任务验证。

项目目标由“提供若干示例 MCP Tool”扩展为：

> 基于 C++20、MCP、GDAL/OGR 和插件机制构建可扩展的地理空间服务平台，为 Agent 同时提供本地 GIS 计算、OSM 路由及第三方 LBS 服务。

## 2. 建设原则

1. **C++ 用于有明确收益的工作**：大规模路网、批量几何计算、空间数据解析、内存控制、并发查询和原生 GIS 库集成。
2. **MCP 只承担标准化调用入口**：算法和 Provider 核心不依赖 JSON-RPC、Server 或插件 ABI。
3. **按能力和依赖划分插件**：一个插件可以暴露多个相关 Tool，不为每个 Tool 单独创建动态库。
4. **本地能力与在线能力分离**：本地 GIS 分析不应因第三方 API、密钥或网络故障而不可用。
5. **对外统一语义**：统一坐标顺序、坐标系、错误码、分页、超时和结果结构。
6. **先形成可验证闭环，再增加基础设施**：优先完成 GIS Tool 和复合任务，再依据指标引入 Redis、限流和复杂降级策略。

## 3. 当前基础与差距

| 能力 | 当前状态 | 本规划目标 |
|---|---|---|
| JSON-RPC 2.0 与 MCP | 已有请求分发及响应封装 | 增强统一参数校验、错误分类和调用上下文 |
| 动态插件 | 已有 `dlopen/dlsym`、Staging、原子发布 | 复用于 GIS/LBS 插件，保持 ABI 边界安全 |
| OSM 路由 | 已有 CSR、A*、空间索引、地图匹配 | 作为 `LocalOsmProvider` 的路径规划能力 |
| GeoJSON | 已有基础输出 | 增加标准化解析、校验、转换和几何运算 |
| 通用 GIS 分析 | 尚未形成公共模块 | 引入 GDAL/OGR；复杂拓扑能力按需使用 GEOS |
| 在线地图服务 | 未实现 | 建立统一 `MapProvider` 并至少接入一个 Provider |
| Tool 智能检索 | 已有 RAG 基础框架 | 实现真正 Top-K 筛选、评测及降级 |
| Redis 与访问治理 | 未实现 | 对高价值外部查询增加缓存、限流、超时和重试 |
| Agent 复合任务 | 未系统验证 | 使用 DeepSeek Harness + Qwen 完成连续 Tool 调用验收 |

## 4. 总体架构

```text
用户 / Agent / DeepSeek Harness
          |
          v
MCP Server + API Router
  - Method 解析
  - 请求上下文与参数校验
  - Tool 路由
  - 错误与响应封装
          |
          +----------------------+----------------------+
          |                      |                      |
          v                      v                      v
 gis-analysis plugin     map-services plugin     osm-routing plugin
 多个本地 GIS Tool       地理编码/POI/在线路线     本地路线/匹配/邻近道路
          |                      |                      |
          v                      v                      v
     gis_core             MapProvider API          routing_core
   GDAL/OGR/GEOS       Provider + 降级策略       CSR/A*/空间索引
                                 |
                       第三方地图 API / Redis

Tool 元数据
    -> Embedding 索引
    -> Top-K 候选 Tool
    -> LLM 最终选择与连续调用
```

## 5. 模块与目录规划

建议新增以下目录，实际落地前应结合现有 CMake 结构微调：

```text
gis/
├── core/
│   ├── include/gis/
│   │   ├── geometry_engine.h
│   │   ├── coordinate_transformer.h
│   │   ├── geojson.h
│   │   ├── spatial_result.h
│   │   └── error.h
│   └── src/
├── gdal/
│   ├── gdal_runtime.cpp
│   ├── ogr_geometry.cpp
│   └── ogr_coordinate_transformer.cpp
├── providers/
│   ├── include/gis/providers/map_provider.h
│   ├── local_osm_provider.cpp
│   ├── provider_registry.cpp
│   └── fallback_provider.cpp
└── test/

mcp_server/plugins/
├── gis-analysis/
└── map-services/
```

依赖方向必须保持：

```text
MCP plugin -> 项目自有 GIS 接口 -> GDAL/OGR
MCP plugin -> MapProvider -> 具体 Provider
LocalOsmProvider -> routing_core
```

`routing_core`、`gis_core` 和 Provider 实现不得反向依赖 MCP Server。

## 6. MCP 协议与 Tool 路由增强

### 6.1 API Router 职责

- 解析 JSON-RPC `method`、`id` 和 `params`；
- 校验 MCP 生命周期和请求结构；
- 根据 Tool 名称查找不可变插件注册表快照；
- 在插件执行前进行通用约束校验；
- 将插件结果封装为 MCP `content`、`structuredContent` 和 `isError`；
- 将异常转换为稳定错误结构，避免异常跨越插件 ABI；
- 记录 request ID、Tool、Provider、耗时、缓存命中和错误类别。

### 6.2 统一错误分类

至少区分：

- `INVALID_ARGUMENT`：参数、GeoJSON 或坐标不合法；
- `UNSUPPORTED_CRS`：坐标系无法识别或转换；
- `DATA_NOT_FOUND`：没有匹配要素或路线；
- `PROVIDER_UNAVAILABLE`：外部地图服务不可用；
- `RATE_LIMITED`：访问频率超限；
- `TIMEOUT`：超过调用截止时间；
- `INTERNAL_ERROR`：未归类的内部失败。

## 7. GDAL/OGR 集成设计

### 7.1 引入目的

GDAL/OGR 用于：

- GeoJSON、GeoPackage、Shapefile 等矢量格式读取；
- `Point`、`LineString`、`Polygon` 和多几何对象解析；
- Geometry 合法性校验和标准化输出；
- EPSG 坐标系识别与转换；
- Buffer、Contains、Intersects、Distance、Area 等空间运算；
- 本地空间数据集信息读取与要素查询。

现有 CSR 路网、路径搜索、地图匹配和高频道路索引继续使用定制实现，不由 GDAL 替换。

### 7.2 边界与生命周期

- 不在插件 ABI 中暴露 `OGRGeometry*`、`GDALDataset*` 或 C++ STL 对象；
- 使用项目自有 DTO/JSON 作为模块边界；
- 使用 RAII 包装 GDAL 对象，并调用 GDAL/OGR 对应释放函数；
- 集中完成 `GDALAllRegister` 等进程级初始化；
- 构建时检测 GDAL 版本以及是否具备 GEOS 能力；
- 所有新增构建产物仍放入 `build_dev/`。

### 7.3 坐标规范

- MCP 对外默认使用 WGS84，即 `EPSG:4326`；
- 坐标数组固定为 `[longitude, latitude]`；
- 输出必须声明 `coordinateSystem`；
- 以米为单位的 Buffer/Distance 应先转换到适用的投影坐标系计算；
- GCJ-02、BD-09 在具体 Provider 内显式适配，不伪装成普通 EPSG 转换。

## 8. 插件及 Tool 规划

### 8.1 `gis-analysis` 插件

第一批 Tool：

| Tool | 功能 | 优先级 |
|---|---|---|
| `geometry_validate` | 校验 GeoJSON、几何类型和坐标 | P0 |
| `coordinate_transform` | EPSG 坐标转换 | P0 |
| `point_in_polygon` | 判断点与区域的空间关系 | P0 |
| `geometry_distance` | 计算几何距离 | P0 |
| `geometry_buffer` | 生成指定米数的缓冲区 | P1 |
| `geometry_area` | 计算面面积 | P1 |
| `dataset_info` | 查看本地矢量数据集元信息 | P1 |
| `features_within` | 查询区域内的本地要素 | P2 |

Tool 响应优先同时提供：

- `content`：供人和通用 MCP Client 阅读；
- `structuredContent`：供 Agent 连续调用和前端渲染；
- GeoJSON Geometry/Feature/FeatureCollection：供地图直接展示。

### 8.2 `map-services` 插件

计划 Tool：

- `geocode`
- `reverse_geocode`
- `poi_search`
- `nearby_search`
- `search_nearby_by_place`（复合 Tool：地名解析、歧义保护和周边检索）
- `provider_route`
- `administrative_search`

API Key 仅从环境变量或受控配置读取，不进入 Tool Schema、日志和返回值。

### 8.3 `osm-routing` 插件

继续提供现有 Tool：

- `route_plan`
- `nearest_road`
- `routing_status`
- `map_match`

后续按实际需求评估步行 Profile、OD 矩阵和等时圈，不在本轮强制范围内。

## 9. MapProvider 统一适配

核心接口应使用项目自有请求和响应类型：

```cpp
class MapProvider {
public:
    virtual ~MapProvider() = default;

    virtual GeocodeResult Geocode(const GeocodeRequest& request) = 0;
    virtual PoiSearchResult SearchPoi(const PoiSearchRequest& request) = 0;
    virtual RouteResult PlanRoute(const RouteRequest& request) = 0;
    virtual ProviderHealth HealthCheck() = 0;
};
```

Provider 层负责归一化：

- 坐标系和坐标顺序；
- 查询字段、城市编码和 POI 类型；
- HTTP 状态码、业务状态码和错误信息；
- 距离、时间和 Geometry 字段；
- 分页、空结果和配额限制。

`FallbackMapProvider` 根据配置执行主备切换，但仅对明确可重试的错误降级，避免对参数错误重复调用多个 Provider。

## 10. 智能工具检索

索引内容不能只包含 Tool 名称，应至少包括：

- 名称和描述；
- 参数名、类型及说明；
- Tool 分类和典型应用场景；
- 正反例自然语言问题；
- Provider、本地/在线属性及前置条件。

调用流程：

```text
用户问题 -> Embedding -> Top-K Tool -> 规则过滤 -> LLM 最终选择 -> tools/call
```

必须保留以下降级能力：

- Embedding 服务不可用时使用关键词/BM25 或全量候选；
- Tool 数量较少时允许绕过检索；
- 支持查看候选得分，便于调试错误选择。

评测至少记录 `Recall@K`、最终 Tool 选择准确率、平均注入 Token 数和检索延迟。

## 11. 缓存、访问控制与可靠性

### 11.1 Redis 缓存

优先缓存外部且重复率较高的请求：

- 地理编码和逆地理编码；
- POI/周边搜索；
- 行政区查询。

缓存键至少包含版本、Provider、Tool、城市/语言、规范化参数和必要的坐标精度。不同数据类型设置独立 TTL，并对确定性的空结果使用较短负缓存。

在性能数据证明有价值前，不优先缓存大体积路线 Geometry。

### 11.2 调用治理

- 按 Client、Tool 和 Provider 限流；
- 设置连接、读取和总 Deadline；
- 仅对幂等请求及可恢复错误进行有限次数重试；
- 使用指数退避和抖动；
- 设置响应大小、分页数量和 Geometry 点数上限；
- 日志中对 API Key、用户位置和敏感查询脱敏。

## 12. 端到端复合任务

核心验收场景由 DeepSeek Harness + Qwen 执行。原 Cherry Studio 集成计划已取消，详细设计见 `docs/deepseek-harness-gis-integration-plan.md`。

核心验收场景：

> 帮我查找南京师范大学附近三公里内的地铁站，并规划步行路线。

预期调用链：

```text
geocode("南京师范大学")
  -> nearby_search(type="subway_station", radiusMeters=3000)
  -> 候选站点排序或选择
  -> route_plan/profile=walking 或 provider_route
  -> Agent 汇总站点、距离、时间和路线
  -> GeoJSON 在前端地图显示
```

验收点：

1. DeepSeek Harness 能成功初始化并发现相关 Tool；
2. Agent 不需要一次注入全部 Tool；
3. 连续调用的坐标和字段能够直接衔接；
4. 三公里约束真实生效；
5. 路线模式为步行且结果包含距离、时间和 Geometry；
6. 任一外部 Provider 失败时返回可理解错误或按配置降级；
7. 服务端日志可以通过 request ID 追踪完整调用链；
8. 返回的 GeoJSON 可由现有 Web 地图直接展示。

## 13. 分阶段实施计划

### 阶段 0：环境探测和最小设计

- 检查 GDAL 版本、开发头文件、驱动和 GEOS 支持；
- 确定 CMake 查找及可选构建策略；
- 固化坐标、GeoJSON、错误和 DTO 约定；
- 建立最小基准数据和测试样例。

**完成标准**：在 `build_dev/` 中成功链接 GDAL，并通过最小 Geometry 创建/释放测试。

### 阶段 1：`gis_core` 与 GDAL 隔离层

- 实现 GDAL 初始化和 RAII 包装；
- 实现 GeoJSON 解析、输出和合法性检查；
- 实现坐标转换；
- 增加单元测试和异常路径测试。

**完成标准**：`geometry_validate` 和坐标转换的核心接口通过测试，且无跨 ABI 所有权问题。

### 阶段 2：本地 GIS 分析 Tool

- 完成 P0/P1 Geometry 运算；
- 实现 `gis-analysis` 插件和 Tool Schema；
- 增加 MCP 端到端调用测试；
- 验证 GeoJSON 可由浏览器前端渲染。

**完成标准**：至少五个 GIS Tool 可通过 STDIO MCP 调用，成功与失败结果结构一致。

### 阶段 3：MapProvider 与首个在线服务

- 定义统一 Provider DTO 和接口；
- 实现 `LocalOsmProvider`；
- 选择并接入一个在线 Provider；
- 完成坐标、POI 和错误归一化；
- 实现 `map-services` 插件。

**完成标准**：同一 Tool Schema 可在配置切换后调用不同 Provider。

### 阶段 4：缓存与可靠性

- 引入 Redis 可选缓存；
- 实现规范化缓存键和 TTL；
- 增加超时、有限重试、限流和降级；
- 补充监控字段和敏感数据脱敏。

**完成标准**：缓存命中、Provider 超时和主备降级均有自动化验证。

### 阶段 5：智能 Tool 检索

- 扩充 Tool 语义元数据和样例；
- 构建 Embedding 索引；
- 实现 Top-K、规则过滤和降级；
- 建立 GIS/LBS 查询集并评测。

**完成标准**：达到约定 Recall@K，同时显著降低平均 Tool Schema Token 注入量。

### 阶段 6：DeepSeek Harness 与前端验收

- 配置 DeepSeek Harness MCP Client 和 Qwen；
- 执行南京师范大学复合任务；
- 在现有地图页面展示 POI、缓冲区和路线；
- 记录调用链、截图、性能和异常行为。

**完成标准**：自然语言请求能够稳定完成检索、连续调用、结果汇总和地图展示。

## 14. 测试与质量要求

- `gis_core` 使用单元测试覆盖 Geometry、CRS、空值、非法 GeoJSON 和边界坐标；
- 使用已知点和多边形建立 golden tests；
- 对不同 Provider 使用契约测试，确保归一化响应一致；
- MCP 插件测试同时检查 `content`、`structuredContent` 和 `isError`；
- 使用 AddressSanitizer/Valgrind 检查 GDAL 对象生命周期；
- benchmark 区分 GDAL 计算、Provider 网络、Redis 和 MCP 封装耗时；
- 构建及测试目录统一放在 `build_dev/` 下，不污染仓库根目录。

## 15. 非目标与风险

### 本轮非目标

- 自研完整 GIS 数据库；
- 用 GDAL 替换现有路由图和路径算法；
- 同时接入大量第三方地图厂商；
- 在缺少评测数据时实现复杂微服务拆分；
- 将第三方地图数据无条件持久化或混合展示。

### 主要风险

- GDAL/GEOS 版本及构建选项在不同环境不一致；
- 几何对象和字符串跨插件 ABI 释放可能引发内存错误；
- 经纬度直接执行平面距离或 Buffer 会产生错误结果；
- GCJ-02、BD-09 与 WGS84 混用导致位置偏移；
- 第三方 API 配额、服务条款和数据展示许可存在限制；
- Tool 描述过于相似会降低 Agent 选择准确率；
- Redis、重试和降级若过早引入会增加调试复杂度。

## 16. 里程碑与状态

| 里程碑 | 状态 |
|---|---|
| 新增需求分析与规划文档 | 已完成 |
| 阶段 0：GDAL 环境探测 | 已完成（GDAL 3.4.1、GEOS 3.10.2，Debug 冒烟测试通过） |
| 阶段 1：`gis_core` | 已完成（Geometry 校验、坐标转换、Debug 与 ASan 测试通过） |
| 阶段 2：`gis-analysis` 插件 | 已完成（8 个本地 Tool，含 GDAL 数据集查询，插件与 MCP 契约测试通过） |
| 阶段 3：MapProvider 与在线 LBS | 已完成（统一 DTO、Amap/Local OSM、坐标与错误归一化、可恢复故障降级，并由真实 Amap 场景验证） |
| 阶段 4：缓存与可靠性 | 已完成（Memory/Redis 缓存、规范化键、超时、有限重试、限流和降级均有自动化测试） |
| 阶段 5：智能 Tool 检索 | 按已确认决策暂缓；当前使用 `gis-agent` Profile 限制 Tool，待 Tool 数量与评测数据增长后复用 RAG-MCP |
| 阶段 6：端到端验收 | 已完成（Mock、真实 Qwen/高德七类场景、Harness GIS 卡片及外置 Leaflet 页面） |

最后更新：2026-08-16。
