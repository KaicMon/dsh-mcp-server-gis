# OSM 路网引擎改造为 MCP 插件的设计与实施计划

## 1. 目标与结论

目标是在现有 MCP Server 上提供可被 Agent 调用的真实路网能力，同时保持路由算法、数据构建和存储系统可以脱离 MCP 独立测试与部署。

推荐结论：**改造成“独立路网核心库 + 轻量 MCP Tool 插件 + HTTP 可视化适配层”，而不是把全部 OSM 系统直接写进插件 `.so`。**

```text
Agent
  → MCP tools/call
  → osm-routing-plugin（参数校验、限流、结果格式化）
  → routing_core（图、索引、搜索、地图匹配、版本快照）
  → 本地路网快照

Web 前端（MapLibre GL JS / Leaflet）
  → Routing HTTP API（鉴权、限流、GeoJSON 格式化）
  → routing_core（与 MCP 插件共享同一套查询接口）
  → 本地路网快照

离线构建器
  → OSM PBF/XML
  → 拓扑转换、Dense ID、CSR、空间索引
  → 版本化路网文件

管理与辅助系统
  ├─ PostGIS：Geometry 管理、质检、分析和增量数据准备
  └─ Redis：可选的跨进程热点 OD 结果缓存
```

MCP 和 HTTP 都是查询入口，不是路网计算内核。`routing_core` 不得依赖 MCP 的 JSON-RPC、`PluginAPI`、HTTP 框架或 Server 类型。浏览器不直接依赖 MCP 插件；MCP 面向 Agent，HTTP/GeoJSON 面向地图前端。

## 2. 与当前 MCP 项目的适配分析

### 已经具备

- ABI v2 支持插件侧 `FreeResult` 和 Host 通知上下文：`mcp_server/src/interface/PluginAPI.h`；
- `PluginRegistry` 使用不可变 `shared_ptr` 快照路由 Tool；
- `PluginRuntime` 支持 `.so` 的 Staging、校验、原子发布和失败回滚；
- 旧插件请求完成前不会提前 `dlclose`；
- Tool Schema 变化可发送 `notifications/tools/list_changed`。

### 必须补齐

1. 当前插件 `HandleRequest` 是同步调用。长路径、批量 OD 和 HMM 地图匹配可能阻塞请求读取链，正式接入前必须增加 Server 请求队列和固定大小 Worker Pool。
2. 当前 ABI 没有取消令牌和 Deadline。第一版在插件内部实现超时、最大扩展节点数和最大轨迹点数，后续再评估 ABI v3。
3. 插件代码热更新与路网数据热更新必须分开。前者由 `PluginRuntime` 管理，后者由 `routing_core::RoadNetworkStore` 管理。
4. 返回 JSON 不能携带无限大的 Geometry 或搜索明细，需要输出上限、Polyline 编码和可选字段。

## 3. 模块边界与建议目录

```text
routing/
├── core/
│   ├── include/routing/
│   │   ├── graph.h
│   │   ├── road_network.h
│   │   ├── router.h
│   │   ├── spatial_index.h
│   │   ├── map_matcher.h
│   │   └── network_store.h
│   └── src/
├── builder/
│   ├── osm_parser.cpp
│   ├── topology_builder.cpp
│   ├── csr_builder.cpp
│   ├── index_builder.cpp
│   └── network_validator.cpp
├── storage/
│   ├── network_format.h
│   ├── postgis_repository.cpp
│   └── redis_route_cache.cpp
├── benchmark/
├── test/
├── tools/osm_build.cpp
└── adapters/
    ├── geojson_serializer.cpp
    └── http/
        ├── routing_http_service.cpp
        └── CMakeLists.txt

mcp_server/plugins/osm-routing/
├── OsmRoutingPlugin.cpp
├── OsmRoutingPlugin.h
├── Config.cpp
└── CMakeLists.txt
```

第一版可以把 `routing/` 放在当前仓库；核心稳定后可以拆成独立仓库或独立 Routing Service。

## 4. OSM 到有向路网的拓扑转换

### 4.1 输入语义

需要解析：

- Node：OSM ID、经纬度；
- Way：Node 序列和道路 Tag；
- Relation：至少支持 `type=restriction` 的转向限制；
- 关键 Tag：`highway`、`oneway`、`junction`、`maxspeed`、`access`、`vehicle`、`motor_vehicle`、`surface`、`bridge`、`tunnel`、`toll`。

### 4.2 节点化规则

不是每个 OSM Node 都必须成为搜索图节点。以下位置成为拓扑节点：

1. 可通行 Way 的首尾节点；
2. 被两条或多条可通行 Way 共享的交叉点；
3. 道路属性发生变化的位置，如限速、道路等级或通行权限变化；
4. 转向限制的 `from/via/to` 相关节点；
5. 需要保留的障碍、收费站或其他路由语义点。

同一条 Way 上两个连续拓扑节点之间的 OSM Shape Node 压缩为一条道路边的 Geometry。这样既保留路线形状，又避免把全部 Shape Node 放入搜索状态空间。

### 4.3 有向边生成

- 普通双向道路生成正向和反向两条 Edge；
- `oneway=yes/1/true` 只生成 Way 顺序方向；
- `oneway=-1` 只生成反方向；
- 环岛默认按 OSM Node 顺序单向，除非 Tag 明确覆盖；
- 根据 Profile 过滤不可通行道路；
- 每条 Edge 保存距离、预估时间、道路等级、限速、权限位、Geometry 引用和 OSM Way ID；
- 转向限制不能只存 Edge 属性，应生成 Turn Table，或在搜索状态中引入前驱 Edge。

必须对重复边、零长度边、自环、断裂 Geometry、非法坐标和悬空引用做构建期校验。

## 5. Dense ID 与 CSR 紧凑图

OSM ID 是稀疏 64 位整数，只保留在外部映射和诊断信息中。在线搜索使用连续 Dense ID：

```cpp
using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;

struct CsrGraph {
    std::vector<std::uint64_t> offsets; // node_count + 1
    std::vector<NodeId> targets;        // edge_count
    std::vector<EdgeWeight> weights;
    std::vector<EdgeAttributes> attributes;
};
```

建议采用 Structure of Arrays，避免查询只需要权重时把完整 Edge 对象拉入 Cache Line。坐标可以使用定点整数，例如 `lat/lon * 1e7`。

构建流程：

```text
收集拓扑 OSM Node
  → 排序/映射 OSM ID 到 Dense ID
  → 统计每个节点出度
  → 前缀和生成 offsets
  → 填充 targets/weights/attributes
  → 校验 offsets 单调且末值等于 edge_count
```

如果单区域可能超过 `uint32_t` 上限，再升级为 64 位 ID；不应一开始为极端规模付出双倍索引内存。

## 6. 搜索算法

统一接口：

```cpp
struct RouteOptions {
    TravelProfile profile;
    CostMetric metric;
    AvoidFlags avoid;
    std::chrono::milliseconds timeout;
    std::uint32_t max_settled_nodes;
};

class IRouter {
public:
    virtual RouteResult Route(NodeId source, NodeId target,
                              const RouteOptions&) const = 0;
};
```

### 6.1 算法顺序

1. Dijkstra：正确性基线；
2. A*：默认单次查询算法；
3. 双向 Dijkstra；
4. 双向 A*：明确终止条件并用基线做差分验证；
5. 数据量和延迟确有需要时再评估 CH/MLD，不纳入第一版插件。

距离优化目标可使用 Haversine/球面距离启发函数。时间优化必须使用：

```text
h(n) = great_circle_distance(n, target) / global_max_speed
```

只有启发函数不高估真实剩余代价时，A* 才保持最优性。道路等级估算速度不能直接作为启发函数，除非能够证明 admissible。

每个查询使用独立 SearchContext，路网快照只读，因此多个 Worker 可以无锁共享 Graph。

## 7. 空间索引与起终点吸附

在线路径规划的输入通常是经纬度，不是 Graph NodeId，需要先找到可通行候选 Edge。

第一版建议在内存实现固定空间网格：

- 构建简单、可序列化；
- 查询 Cell 及邻居 Cell；
- Edge 跨越的所有 Cell 均建立引用；
- 候选集再做点到折线的精确投影。

R-Tree 作为可替换实现，用于边界框分布不均或超大区域。PostGIS GiST 适合管理、离线验证和低频分析，不建议成为每次在线 Route 的起终点吸附必经网络调用。

吸附结果至少包含：EdgeId、投影点、沿 Edge 比例、距离、正反向可通行性。路径结果需要补上起点和终点所在 Edge 的部分代价。

## 8. HMM 地图匹配

输入是带时间戳的 GPS 点序列：

1. 空间索引为每个 GPS 点生成 K 个候选投影；
2. 发射概率由 GPS 点到候选道路的距离计算；
3. 转移概率比较相邻候选间的路网距离与 GPS 观测距离/时间；
4. Viterbi 求最可能候选序列；
5. 对过长时间间隔、无候选点和不连通候选做分段；
6. 输出匹配 Geometry、Edge 序列、每点置信度和断点。

必须限制最大 GPS 点数和每点候选数。候选间最短路可使用局部有界搜索和查询内缓存，避免形成 `点数 × K²` 次完整路径搜索。

## 9. PostGIS 与 Redis 的正确位置

### PostGIS

适合：

- 保存道路 Geometry、OSM 标识和版本；
- 使用 GiST 做数据质检、邻近分析和增量影响范围计算；
- 为管理接口和离线构建器提供数据源；
- 对比内存空间索引结果。

不适合：默认在线 Route 每一步都查询 PostGIS。主查询链应使用本地只读 CSR 和空间索引，避免数据库 RTT 成为 P95/P99 的主要组成。

### Redis

缓存键必须包含完整语义：

```text
network_version + profile + cost_metric + avoid_flags
+ snapped_source_edge/position + snapped_target_edge/position
```

原始经纬度先按配置归一化或吸附后再生成 Key。版本进入 Key 后无需在地图切换时全量删除旧 Key，只需设置 TTL。第一版先实现进程内有界 LRU，再根据命中率和多实例部署需求决定是否启用 Redis。

## 10. 路网持久化与版本原子切换

持久化文件应有明确 Header：Magic、格式版本、Endian、构建版本、区域、节点/边数、Section Offset、校验和及 OSM 数据时间。

```text
network-2026-08-15/
├── manifest.json
├── nodes.bin
├── graph.csr
├── edge_attributes.bin
├── geometries.bin
├── spatial_index.bin
└── turn_restrictions.bin
```

运行时使用第二层不可变快照：

```cpp
class RoadNetworkStore {
public:
    std::shared_ptr<const RoadNetwork> Snapshot() const;
    LoadResult LoadAndPublish(const std::filesystem::path& version);
};
```

切换流程：

```text
构建新版本目录
  → 文件校验和与结构校验
  → 加载/预热 Graph 和空间索引
  → 小样本路由自检
  → atomic store 新 RoadNetwork
  → 新查询使用新版
  → 旧查询释放最后引用后回收旧版
```

这里不要调用插件 Reload。插件代码版本和 `RoadNetwork` 数据版本是两个独立 Generation，所有响应都应返回二者。

增量更新第一版采用“增量输入、完整生成新快照”，先保证原子性和可回滚；只有完整重建成本经测量不可接受时，才设计分区级增量 CSR。

## 11. MCP Tool 设计

第一版只提供只读查询 Tool。

### `route_plan`

输入：起终点坐标、Profile、优化目标、避让项、是否返回 Geometry、最大候选路线数。

输出：距离、预计时间、编码 Geometry、道路段摘要、吸附信息、搜索统计、`networkVersion` 和 `pluginVersion`。

### `nearest_road`

输入：坐标、Profile、搜索半径、最大候选数。输出候选道路、投影点、距离、方向和属性。

### `map_match`

输入：GPS 点数组和 Profile。输出匹配 Edge 序列、Geometry、置信度、未匹配区间和版本信息。

### `route_matrix`

第二阶段提供。必须限制坐标数量与总 OD 数，避免一个 Tool 调用占满全部 Worker。

### `routing_status`

返回 Graph 节点/边数、区域、路网版本、索引状态、缓存统计、活动查询数和最近切换结果。

地图导入、删除和版本切换不放进普通 Agent Tool。若需要远程运维，应实现独立、默认禁用且有鉴权边界的 Admin 接口。

## 12. 前端可视化与 HTTP 接口

前端展示不是另写一套路由逻辑，而是 `routing_core` 的第二个薄适配层：

```text
                         ┌─ OsmRoutingPlugin ─ MCP/JSON-RPC ─ Agent
RoutingEngine / Result ──┤
                         └─ RoutingHttpService ─ HTTP/GeoJSON ─ Web 地图
```

### 12.1 核心结果模型

核心层返回结构化结果，不返回预先拼好的 MCP JSON 或 HTTP JSON。`RouteResult` 至少保留：

- 路径的 WGS84 经纬度折线；
- 距离、预计时间、权重和道路段摘要；
- 起终点吸附位置和原始输入位置；
- 算法、扩展节点数和分段耗时；
- `networkVersion` 与构建数据时间。

Geometry 同时支持原始坐标、GeoJSON 和 Encoded Polyline 序列化。对外接口必须配置最大坐标点数和最大响应字节数；用于绘制的简化只作用于响应副本，不得改变搜索结果和距离计算。

### 12.2 HTTP API MVP

| 接口 | 用途 | 主要输出 |
| --- | --- | --- |
| `POST /api/v1/routes` | 路径规划 | GeoJSON `LineString`、距离、时间、吸附点、版本 |
| `GET /api/v1/roads/nearest` | 附近道路候选 | GeoJSON `FeatureCollection`、投影点和道路属性 |
| `POST /api/v1/map-match` | GPS 地图匹配 | 原始轨迹、匹配轨迹、候选点和置信度 |
| `GET /api/v1/status` | 服务状态 | Graph 规模、版本、索引状态和构建时间 |
| `GET /api/v1/benchmarks` | 算法比较 | Dijkstra/A*/双向搜索的 P50/P95/P99 |

所有几何统一使用 GeoJSON 坐标顺序 `[longitude, latitude]` 和 WGS84。错误响应使用稳定错误码；前端不得解析 MCP `content[].text` 获取业务字段。

### 12.3 前端展示范围

第一版使用 MapLibre GL JS 或 Leaflet，展示：

- 起点、终点、吸附点与最终路线；
- 原始 GPS 轨迹和 HMM 匹配轨迹；
- 当前路网版本、算法、距离、时间和查询耗时；
- Dijkstra、A*、双向搜索 Benchmark 对比；
- 可选的访问节点/边调试图层，仅在受限调试接口中返回。

南京全量路网不得作为单个 GeoJSON 一次发送到浏览器。底图优先使用现成瓦片；若要展示自有路网，后续增加按视口/缩放级别裁剪或生成矢量瓦片的独立流程。

### 12.4 共享契约验收

对同一请求，MCP 与 HTTP 适配层必须调用同一个 `RoutingEngine`，并返回相同的路径代价、Geometry、吸附结果和 `networkVersion`。契约测试从固定 `RouteResult` 同时生成 MCP JSON 与 GeoJSON，防止两个入口逐步产生语义偏差。

## 13. 插件生命周期与线程模型

```text
Initialize
  → 读取插件配置
  → 加载指定 RoadNetwork 快照
  → 初始化有界缓存
  → 可选启动路网版本监控线程

HandleRequest
  → 解析并限制输入
  → 获取一个 RoadNetwork shared_ptr 快照
  → 执行查询
  → 复制为有界 JSON 结果

Shutdown
  → 拒绝新内部任务
  → 停止并 join 路网监控线程
  → 等待插件内部任务完成
  → 释放活动 RoadNetwork
```

Server Worker Pool 负责请求级并发；插件不得为每次调用创建线程。Graph 和空间索引发布后只读，SearchContext、Viterbi 状态和结果缓冲区为请求私有。

## 14. Benchmark 与指标

数据集至少分为小型单元图、城市级和目标规模三档。Route 样本必须包含短、中、长距离以及不可达 OD，使用固定种子生成并保存样本集。

记录：

- Dijkstra、A*、双向算法的 P50/P95/P99；
- 扩展节点数、访问边数、堆操作数；
- 峰值 RSS 和每 Node/Edge 字节数；
- 冷启动、路网加载和版本切换耗时；
- 空间候选查询延迟；
- HMM 每 GPS 点耗时和匹配准确率；
- 缓存命中率及命中/未命中延迟；
- MCP JSON 解析、核心计算和序列化的分段耗时。

正确性采用差分测试：A* 和双向搜索必须与 Dijkstra 返回相同最优代价；地图版本切换期间每个请求只能观察一个版本。

## 15. 分阶段实施计划

### 阶段 0：需求与基线

- [ ] 冻结车辆 Profile、Cost、Avoid 和返回格式；
- [ ] 选定一个城市级 PBF 和可提交的小型测试图；
- [ ] 建立算法正确性与内存基线；
- [ ] 明确 P50/P95/P99、RSS、启动时间目标。

验收：测试数据、期望路径和性能测量方法可重复。

### 阶段 1：拓扑构建器

- [ ] 解析 Node/Way/Restriction Relation；
- [ ] 实现节点化和双向/单向 Edge 生成；
- [ ] 实现道路等级、速度、权限和 Geometry；
- [ ] 输出拓扑诊断与非法数据统计。

验收：小型手工 OSM 图逐边符合预期，单行道和转向限制测试通过。

### 阶段 2：Dense ID、CSR 与持久化

- [ ] 实现 OSM ID 到 Dense ID 映射；
- [ ] 构建 SoA CSR；
- [ ] 定义版本化二进制格式与校验和；
- [ ] 支持 mmap 或顺序加载；
- [ ] 对比哈希邻接表的 RSS 和遍历性能。

验收：百万级数据可稳定构建和加载，CSR 内存与随机查询指标优于基线。

### 阶段 3：路径搜索

- [ ] Dijkstra 正确性基线；
- [ ] A* 与 admissible 启发函数；
- [ ] 双向 Dijkstra/A*；
- [ ] Deadline 和搜索资源上限；
- [ ] P50/P95/P99 benchmark 与差分测试。

验收：所有优化算法与 Dijkstra 最优代价一致，无环路、不可达和有向图错误。

### 阶段 4：空间索引与地图匹配

- [ ] 网格索引及序列化；
- [ ] 点到道路折线投影和起终点吸附；
- [ ] HMM 发射/转移概率与 Viterbi；
- [ ] 候选间路由缓存和轨迹分段；
- [ ] 准确率与性能数据集。

验收：候选道路查询达到目标延迟，地图匹配在标注轨迹上达到约定准确率。

### 阶段 5：路网版本与外部存储

- [ ] `RoadNetworkStore` 原子快照；
- [ ] 新版本构建、校验、预热、发布和回滚；
- [ ] PostGIS Geometry 管理与 GiST 查询；
- [ ] 进程内 LRU，并根据测量决定 Redis；
- [ ] 版本进入响应和缓存 Key。

验收：阻塞旧查询期间切换版本，新旧请求均完成且各自只观察一个版本。

### 阶段 6：MCP 插件 MVP

- [ ] 新增 `osm-routing` ABI v2 Tool 插件；
- [ ] 实现 `route_plan`、`nearest_road`、`routing_status`；
- [ ] 参数 Schema、输入上限和错误模型；
- [ ] 接入插件代码热更新和路网数据快照；
- [ ] Agent 端到端调用测试。

验收：Agent 能完成自然语言约束到确定性路线结果的闭环，坏插件候选和坏地图候选都不影响当前服务。

### 阶段 7：并发与地图匹配 Tool

- [ ] Server 请求队列、Worker Pool 和有序响应关联；
- [ ] `map_match` Tool；
- [ ] 并发路由、更新、通知与 Shutdown 压力测试；
- [ ] ASan/UBSan/TSan；
- [ ] 超时、过载和返回大小控制。

验收：并发下无竞态、死锁和版本混用；P99 与资源占用达到目标。

### 阶段 8：HTTP 与前端可视化

- [ ] 从核心结果模型生成标准 GeoJSON 和 Encoded Polyline；
- [ ] 实现只读 Routing HTTP API、参数上限和稳定错误模型；
- [ ] 实现路线、吸附点、地图匹配和版本状态地图页面；
- [ ] 实现算法 Benchmark 图表和受限搜索调试图层；
- [ ] 增加 MCP/HTTP 共享语义契约测试；
- [ ] 验证南京数据下的响应大小、绘制耗时和坐标简化效果。

验收：浏览器能规划并绘制南京路线；同一 OD 经 MCP 与 HTTP 查询得到相同路径代价、Geometry 和路网版本；前端不需要了解 MCP 插件 ABI。

### 阶段 9：生产化决策

- [ ] 评估 CH/MLD、分区和批量 Matrix；
- [ ] 根据实测决定 Redis；
- [ ] 根据内存、隔离和扩容需求决定是否拆成独立 Routing Service；
- [ ] 补齐部署、监控、数据更新和回滚手册。

## 16. 关键风险与决策门槛

| 风险 | 对策/门槛 |
| --- | --- |
| 把所有 OSM Node 放进搜索图 | 通过交叉点节点化压缩 Shape Node |
| A* 时间启发函数高估 | 使用球面距离除以全局最大速度，并与 Dijkstra 差分 |
| 双向搜索在有向图提前终止 | 实现有证明的终止条件，随机图差分测试 |
| 在线查询依赖 PostGIS | 在线主链使用内存 CSR 和空间索引 |
| Redis Key 忽略路网版本 | Key 强制包含 network version 和全部路由语义 |
| 插件 Reload 同时切地图 | 两类 Generation 独立，响应同时返回两者 |
| 长调用阻塞 MCP Server | MCP 接入前完成 Worker Pool，所有查询有资源上限 |
| 插件后台线程跨越 `dlclose` | `Shutdown()` 必须停止并 join，Sanitizer 压测 |
| Geometry 返回过大 | Polyline 编码、字段开关、点数和响应字节上限 |
| 浏览器一次加载南京全量路网 | 使用底图瓦片；自有路网按视口裁剪或生成矢量瓦片 |
| MCP 与 HTTP 返回语义漂移 | 共用核心 Result 类型和序列化契约测试 |
| 增量 CSR 过早复杂化 | 第一版增量输入但完整生成新快照，以实测决定分区更新 |

## 17. 推荐的第一里程碑

第一里程碑只做一条有价值的完整链路：

```text
城市级 OSM PBF
  → 拓扑节点化
  → Dense ID + CSR
  → 持久化并加载
  → 空间索引吸附起终点
  → A* 路径
  → RouteResult（坐标、距离、时间、吸附和版本）
  ├─ route_plan MCP Tool → Agent
  └─ HTTP GeoJSON → Web 地图绘制
```

暂缓 HMM、PostGIS 在线查询、Redis、增量 CSR 和 CH。先证明核心图结构、算法正确性、MCP 并发模型、GeoJSON 可视化与端到端业务价值，再增加复杂度。
