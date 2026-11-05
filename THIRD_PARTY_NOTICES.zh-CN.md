# 来源与第三方声明

[English](THIRD_PARTY_NOTICES.md) | **简体中文**

MSA-Kit 使用 [MIT 许可](LICENSE)。本文件说明复用内容；具有法律效力的上游版权、许可和免责原文保留在所链接文件中，不以中文说明替换。

## HAlign-5

来源固定为 [pinglu-zhang/HAlign-5 b0d5608811d44e30bc323da54f0583ceaaf684d2](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2)。`src/input.cpp` 的清洗表改写自 include/utils.h，保留 **Copyright (c) 2026 HAlign-5 contributors**；完整许可及免责在 [LICENSE](LICENSE)。

读写组织、CLI 和准备/核心分离提供设计依据。kseq 生命周期和 name/comment 重建、FASTQ 质量忽略沿用 src/utils/seq_io.cpp；严格 gzip、输出事务、星比对按本项目约定实现。详见 [设计](docs/design.zh-CN.md)。未附带其其他算法、数据或外部软件工作流。

## 随仓库提供的头文件依赖

CLI11、spdlog、doctest 头文件来自相同固定快照，保持原内容；版本宏标明版本。快照缺少的许可证从对应 release tag 获取，Git blob 身份另记。

| 组件 | 版本 | 原始许可与署名 |
| --- | --- | --- |
| [CLI11](https://github.com/CLIUtils/CLI11/tree/v2.6.1) | 2.6.1 | [BSD-3-Clause](extern/CLI11/LICENSE)，University of Cincinnati and contributors |
| [spdlog](https://github.com/gabime/spdlog/tree/v1.16.0) | 1.16.0 | [MIT](extern/spdlog/LICENSE)，Gabi Melman and contributors |
| spdlog 附带 fmt | 固定快照版本 | [fmt 原许可](extern/spdlog/include/spdlog/fmt/bundled/fmt.license.rst) |
| [doctest](https://github.com/doctest/doctest/tree/v2.4.12) | 2.4.12 | [MIT](extern/doctest/LICENSE.txt)，Viktor Kirilov |
| [kseq](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/kseq.h) | 原头日期 30JAN2023，加安全补丁 | [头内 MIT](extern/kseq/kseq.h)，Attractive Chaos，2008、2009、2011 |

[provenance.json](extern/provenance.json) 记录来源提交、版本、许可来源身份、每个头及许可证 SHA-256；构建不获取更新版本。doctest 仅测试启用时使用。

### kseq 本地安全补丁

原始 include/kseq.h SHA-256：`9279bfe0f8bfcfe507d2852b838d2156d793f70b76ae9c63e3f6d381239ba901`。清单分别记录原始哈希/Git blob 和补丁文件哈希。原 MIT 覆盖上游代码，本地维护采用 MSA-Kit MIT。

补丁检查初始化/扩容失败，清理部分初始化，realloc 失败保留旧指针，长度加法和窄化前检查 INT_MAX，所有解析阶段（含质量）传播读错。返回 -4 表示分配/长度失败，框架映射退出 5；负/非法读取回调返回 -3，映射 4。编译期分配钩子仅用于模块测试，无生产参数/环境开关。宏空白排版变化不改变正常记录语义。

读取适配从严格 gzip 解码器提供字节，不用 gzread 放宽完整性，也不在 kseq 前重解析/重编码。

## 系统依赖

zlib 是必需系统库，未附带，使用其[自身许可](https://zlib.net/zlib_license.html)。核心可执行程序不依赖 Python，测试及独立网站适配器使用 Python 3。MSA-Kit 许可不替代任何第三方许可。
