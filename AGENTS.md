# 项目协作指南

## 项目概览

本项目是 MCP（Model Context Protocol）的 C++20 实现，使用 CMake 3.20+ 构建：

- `mcp_server/`：MCP 服务端、STDIO/SSE/HTTP Stream 传输及动态插件。
- `mcp_client/`：MCP 客户端、工具管理、Agent 集成和 RAG 工具检索。
- `common/`：公共类型与日志组件。
- `examples/`：客户端及 RAG-MCP 集成示例。
- `docs/`：插件开发、RAG 使用和学习记录。

核心调用链：`Agent -> MCPClient -> Transport/JSON-RPC -> MCP Server -> Plugin -> Result`；RAG 在调用前检索并筛选候选工具。

## 开发约定

- 优先阅读相关接口、实现和 CMake 文件，再做修改；保持现有 C++20 风格与模块边界。
- 不覆盖用户已有改动，不直接清理已有 `build/`；需要重建时使用新的构建目录。
- 常规构建：`cmake -S . -B build-local && cmake --build build-local -j`。
- 基础验证：运行 `./test_all.sh`，或按改动范围执行 `mcp_server/test/` 下的测试。
- 修改协议、Transport、插件 ABI 或 RAG 检索逻辑时，同步检查示例和对应文档。

## `docs/learn` 学习模式

- `docs/learn/学习计划.md` 只维护长期路线、阶段进度、当前单元和下一里程碑。
- 每次学习使用 `docs/learn/每日记录/YYYY-MM-DD.md`；同一天继续追加，不重复新建。
- 按“目标 -> 概念 -> 源码入口 -> 调用链 -> 动手验证 -> 回顾”推进。
- 每日记录至少包含：今日目标、已完成、关键文件/调用链、实验结果、疑问、待学习、自测题、整理状态。
- 先记录可验证的事实、命令和结果；推测要明确标注，结论应能追溯到源码或实验。
- 学习结束后整理当日笔记，并同步更新学习计划中的勾选项、当前阶段和最后更新日期。
- 遇到构建失败时记录环境、命令、关键报错和判断，不删除或覆盖现有构建产物来“验证”。
