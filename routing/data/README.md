# OSM 数据目录

本目录只提交微型测试图、数据来源清单和说明。真实 `.osm.pbf`、构建后的 CSR、空间索引和 Benchmark 原始结果不提交 Git。

```text
data/
├── test/        可提交的手工 OSM XML 与预期结果
├── raw/         下载的真实 OSM/PBF（忽略）
├── generated/   构建产物（忽略）
└── datasets.yaml
```

所有 OSM 数据使用前应记录来源、下载日期、SHA-256、边界框和 ODbL 许可信息。
