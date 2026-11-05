# MSA-Kit 0.3.0 验证记录

[English](validation.md) | **简体中文**

本记录对应 2026-10-27 在 Linux 服务器实际执行的 C++17 迁移验收，起点提交为 `a880fa7787e5a603c7b5db62924a972d8681e03e`。公开核心参数改为 const vector 引用，派生项目需同步签名并重建；七文件结构和输入输出行为不变。这里记录历史执行结果，本次文档本地化不表示重新执行了这些实验。

## 环境与源码

| 项目 | 观察值 |
| --- | --- |
| 平台 | Linux 6.8.0-136-generic，x86_64，glibc 2.35 |
| 编译器 | GCC 13.1.0、GCC 9.5.0 |
| 语言 | `-std=c++17`，标准必需、禁用扩展 |
| CMake | 3.26.4 |
| 系统 zlib | 1.2.11 |
| 测试 Python | 3.10.12 |
| 依赖 | CLI11 2.6.1、spdlog 1.16.0、doctest 2.4.12、固定 kseq 及安全补丁 |
| 安装版本 | `msakit 0.3.0` |

HAlign-5 基线 `b0d5608811d44e30bc323da54f0583ceaaf684d2`。[依赖清单](../extern/provenance.json) 的 138 个头文件/许可证哈希已核对，含原始/分发 kseq 校验值与修改说明。

src 恰为 main、cli、input、output、validation、align 六个 cpp 加 internal.hpp。核心、回调、验证和输出使用不复制记录的 const vector 引用；其余兼容编辑为前后缀函数和常量表达式数组填充的 C++17 写法。评分、回溯、槽合并、依赖、输入夹具及算法断言未改变；测试编辑仅涉及签名、前缀检查和版本预期。

三个测试构建各 11 个编译单元在 compile_commands.json 中均恰使用 `-std=c++17`，runtime-only 另查，核心无第三方 include 目录。实际验证只覆盖上述两版 GCC，不推断任意 C++17 编译器。

## 已完成构建与测试

| CTest | 用例/方法数 | GCC 13.1 Release | GCC 13.1 Debug+ASan/UBSan | GCC 9.5 Release |
| --- | ---: | --- | --- | --- |
| `unit.contract` | 29 | 通过 | 通过 | 通过 |
| `unit.reference` | 8 | 通过 | 通过 | 通过 |
| `integration.contract` | 63 | 通过 | 通过 | 通过 |
| `integration.reference` | 2 | 通过 | 通过 | 通过 |
| 合计 | **102** | **4/4** | **4/4** | **4/4** |

每构建 37 个 doctest 共 8,863 断言：contract 3,888、reference 4,975。Python 方法含参数化子场景，无跳过；doctest 的 skipped 是互补标签过滤，完整运行已覆盖所有用例。

CTest 时长依次为 8.21、39.36、8.58 秒，是测试套件时长而非比对性能。均新目录、零失败、首次编译/测试通过，Sanitizer 无报错；保留既有行为断言和对齐答案。

其他实际检查：contract 2/2；右补 gap 的替代核心通过全部 63 Python contract 方法；GCC 13.1 `BUILD_TESTING=OFF` 不查 Python，完成隔离前缀安装、版本、小型 FASTA，FASTQ stdin `@one description\nacTu\n+\n!!!!\n` 得 `>one description\nACTU\n`。

三个构建各测七个短输出名 `x`、`gz`、`.g`、`.gz`、`x.gz`、`x.GZ`、`x.gzx`，21 场景均通过，仅 `.gz`、`x.gz` 压缩并独立解压，检查后缀大小写、输入不变、quiet、临时清理；不计入 CTest 用例数。另查链接、依赖哈希、七文件和本地/服务器副本。

## 输入行为覆盖

- 32 合法目录例×四输入×三输出为 384 组合；含 FASTA/FASTQ/混合、多行、标题、前导内容、CR/EOF、重复、清洗。
- 22 内容错误×四输入为 88；所选失败另加 80 命名输出生命周期组合；检查新目标无、force 旧目标及输入不变、stdout 空、无临时残留。
- FASTQ 专项另有 16 质量不变、36 gzip 字段切分、12 大字段边界。
- gzip 保留 8 合法、13 损坏、26 截断前缀；识别后的 gzip 错误 4，完整解压但内容错 3；零/一字节魔数按普通输入。
- 模块枚举 256 字节；结构标记/CR 单测；80 个 16 KiB/64 KiB 缓冲边界；FASTA/FASTQ 短读、EINTR、读错和关闭故障。
- 六个 kseq 专项检查部分初始化、长 FASTQ 每次分配、失败 realloc 所有权、长度/算术、每字节读错、短读；资源映射 5，不做巨型分配。
- 输入生成保留 seed 20261026、32 组；CLI、输出发布、链接/同文件、错误核心、参考算法持续运行。

13 组 MSA：六固定各六条，七生成 seed 20261027、最多 128 条；127 通道/重排/折行场景在三构建及替代核心均通过。独立检查标题、位置、数量、清洗后残基、非空等宽、合法字符、无全 gap 列；检查器自测接受两种合法布局，拒绝 16 错误和 2 折行错误。最大评分表 263,682 格，约 2.1 MB；大字节输入单序列，避免大 DP。这些是合成软件测试。

## 保留的解析器比较

此前 kseq 迁移用原始和补丁头对相同输入比较：seed 20261026 生成 100,000 短字节流，各以 1、3、17、16,384 字节块读取，400,000 次返回值/name/comment/sequence/quality 均一致，涵盖前导内容、标记、空白、CR、NUL、高位字节。C++17 迁移未重跑这一历史比较，因为解析器未改；新模块套件仍跑分配、读错和字节清洗。结论仅覆盖已测输入。

比较源码、原始/补丁头、日志在服务器和本地忽略目录 `build-kseq-review/`。

## 复现命令

以下命令从仓库根目录运行。编译器绝对路径须对应本机已有版本；不修改系统软件。

```bash
cmake -S . -B build-cpp17-gcc13-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DBUILD_TESTING=ON
cmake --build build-cpp17-gcc13-release --parallel 2
(cd build-cpp17-gcc13-release && ctest --output-on-failure)

cmake -S . -B build-cpp17-gcc13-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-cpp17-gcc13-sanitize --parallel 2
(cd build-cpp17-gcc13-sanitize && ctest --output-on-failure)

cmake -S . -B build-cpp17-gcc9-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-9 -DBUILD_TESTING=ON
cmake --build build-cpp17-gcc9-release --parallel 2
(cd build-cpp17-gcc9-release && ctest --output-on-failure)

(cd build-cpp17-gcc13-release && ctest -L contract --output-on-failure)

cmake -S . -B build-cpp17-runtime -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DBUILD_TESTING=OFF
cmake --build build-cpp17-runtime --parallel 2
cmake --install build-cpp17-runtime --prefix "$PWD/build-cpp17-runtime/install"
./build-cpp17-runtime/install/bin/msakit --version
```

```bash
mkdir -p build-cpp17-evidence
printf '#!/bin/sh\nexec "%s/build-cpp17-gcc13-release/msakit_test_driver" valid "$@"\n' "$PWD" \
  > build-cpp17-evidence/alternate-core
chmod +x build-cpp17-evidence/alternate-core
python3 tests/integration.py --binary build-cpp17-evidence/alternate-core \
  --test-driver build-cpp17-gcc13-release/msakit_test_driver --suite contract
```

记录时另外使用观察版 CMake 支持的 `--output-junit cpp17-test-results.xml`，并开 CMAKE_EXPORT_COMPILE_COMMANDS。报告选项不改变声明的 CMake 3.18 最低版本；CXX_STANDARD_REQUIRED=ON、CXX_EXTENSIONS=OFF。替代核心命令也包含在上方。

完整原始日志在 `build-cpp17-evidence/gcc13-release-full.log`、`gcc13-sanitize-full.log`、`gcc9-release-full.log`，不被后续标签运行覆盖。同目录还有 run_validation.py、构建/CTest/contract/替代核心/runtime 日志、commands.json、环境、编译标准、短输出及前后哈希。JUnit 在各构建目录，本地忽略目录保留证据副本。

## Sanitizer 与历史

仅 Sanitizer 保留 `-fno-pie`/`-no-pie`。0.1.0 阶段空 PIE 检查程序曾复现间歇启动崩溃，non-PIE 对照未复现；Release 保留正常布局。ASan 泄漏和立即失败、UBSan 立即失败和堆栈保持启用，无抑制列表或主机配置修改。

0.1.0 输入/MSA 日志保留在 `build-input-tests-evidence/`、`build-msa-evidence/`，后者含 13 数据集的早期输入输出与宽度表；kseq 和模块拆分记录在 `build-kseq-evidence/`、`build-modular-evidence/`。C++17 验收使用本记录的新运行结果，不用历史结果替代。

## 解释与限制

验证软件约定和有界参考核心正确性，不测生物准确率或大规模性能。kseq 原生语义与 0.1.0 的差异见 [设计](design.zh-CN.md#4-input-contract)。蛋白/profile/并行/非 Linux 不在范围；声明最低 CMake 3.18，实际使用 3.26.4。此次历史实现交付没有提交、推送、标签或发行；本地化未改变该历史状态。
