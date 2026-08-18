# 插件热更新运行手册

## 启动

热更新默认关闭。只使用启动时 staged 加载：

```bash
./build_dev/phase6-debug/mcp_server/mcp_server \
  --plugins ./build_dev/phase6-debug/mcp_server/plugins \
  --plugin-staging ./build_dev/runtime/plugin-staging
```

启用 Linux inotify 热更新：

```bash
./build_dev/phase6-debug/mcp_server/mcp_server \
  --plugins ./build_dev/phase6-debug/mcp_server/plugins \
  --plugin-hot-reload \
  --plugin-staging ./build_dev/runtime/plugin-staging \
  --plugin-debounce-ms 300 \
  --plugin-delete-grace-ms 500 \
  --plugin-retain-generations 2
```

关闭 `--plugin-hot-reload` 即可快速回退；启动时仍会执行 ABI、Schema 和路由校验。

## 安全部署插件

不要直接原地覆盖正在写入的 `.so`。推荐先生成同目录临时文件，再执行同文件系统 rename：

```bash
cp libcalculator.so plugins/libcalculator.so.next
mv plugins/libcalculator.so.next plugins/libcalculator.so
```

Watcher 收到原子移入事件后会去抖并执行以下事务：

```text
SHA-256 → 唯一 Staging 副本 → ABI/Initialize → 元数据/路由校验
→ 原子发布 Registry → list_changed（仅能力变化时）→ 延迟回收旧版
```

候选失败时旧 Registry 继续服务。不要删除 staging 中仍被进程占用的 Generation。

## 验证

```bash
cmake -S . -B build_dev/phase6-debug \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build_dev/phase6-debug -j
ctest --test-dir build_dev/phase6-debug --output-on-failure

cmake -S . -B build_dev/phase6-asan \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DMCP_ENABLE_ASAN=ON
cmake --build build_dev/phase6-asan -j
ctest --test-dir build_dev/phase6-asan --output-on-failure
```

重点日志字段：`reload_id`、`stage`、`source`、`path` 和 `generation`。候选失败应没有后续 `stage=published`。

## 限制

- 第一版 Watcher 仅在 Linux 使用 inotify；其他平台需实现 `IPluginWatcher`。
- 插件必须遵守 ABI v2，并在 `Shutdown()` 返回前停止和 join 自己的线程。
- 不支持插件之间的动态依赖图或插件内部状态自动迁移。
- 当前 WSL 环境的 ThreadSanitizer 在程序启动前报告 `unexpected memory mapping`；应在原生 Linux CI 再运行 TSan。
