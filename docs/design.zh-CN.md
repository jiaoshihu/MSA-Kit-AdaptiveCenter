# MSA-Kit 设计规范

[English](design.md) | **简体中文**

本文用于查询精确行为。第一次上手请先读 [README 入门内容](../README.zh-CN.md#what-is-msa)，准备修改算法时再读 [开发指南](guide.zh-CN.md)。

本规范对应 0.3.0 已实现的接口。参见 [项目首页](../README.zh-CN.md)、[开发指南](guide.zh-CN.md) 和 [实际验证记录](validation.zh-CN.md)。

这里的**约定（contract）**是任何替代算法都必须满足的规则，例如保留清洗后输入的全部残基；**参考算法**是项目附带的这一个星比对实现。它的评分和 gap 选择描述该实现，公共输入输出规则则约束所有核心。下文的模块划分说明各项职责由哪里实现。

## 1. 状态与范围

MSA-Kit 是 Linux、C++17、CMake >=3.18 的 DNA／RNA 多序列比对模板。包含程序、固定依赖、参考星比对、合成示例及测试；正常构建不下载依赖。输入为文件或 stdin 的未对齐 FASTA／FASTQ，可为 gzip；输出 aligned FASTA，可写 gzip 文件。比对数据驻留内存。

基础模板不包括蛋白、profile、URL 下载、动态插件、多线程执行、外部比对命令，也不继承 HAlign-5 的规模或性能。来源固定为 [b0d5608811d44e30bc323da54f0583ceaaf684d2](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2)。

## 2. 模块划分

```text
参数解析 -> 读取/清洗/输入校验 -> 核心 -> 结果验证 -> FASTA/gzip
```

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 调用共享应用入口。 |
| `src/cli.cpp` | 参数、日志、`run_application()`、核心注入与异常映射。 |
| `src/input.cpp` | 文件/stdin、严格 gzip、kseq、清洗、统计。 |
| `src/output.cpp` | 文件身份、序列化、受保护写入和原子发布。 |
| `src/validation.cpp` | 行身份、数量、宽度、字符与列校验。 |
| `src/align.cpp` | 可替换的内存算法。 |
| `src/internal.hpp` | 共用声明、故障替身、错误、尺寸计算。 |
| `include/msakit/core.hpp` | 公开数据结构和入口。 |

`src` 共七个文件。外围库独立编译 CLI、输入、输出、验证，程序与测试链接同一实现。kseq 只在输入模块实例化；zlib 专属辅助函数位于输入/输出。内部头用标准库类型和 `z_stream_s` 前向声明，核心不依赖 zlib、CLI11、spdlog。每个实现显式包含自身依赖。没有运行时插件加载器，也不建立 HAlign 风格工作目录树或原始输入副本。

<a id="3-core-interface"></a>
## 3. 核心接口

```cpp
#include <string>
#include <vector>
namespace msakit {
struct SequenceRecord { std::string header; std::string sequence; };
struct Alignment { std::vector<std::string> rows; };
Alignment align(const std::vector<SequenceRecord>& input);
}
```

位置是身份，标题不是唯一键；重复标题、重复序列必须保留。输入至少一条，序列为非空、无 gap 的清洗结果。框架在调用期间持有输入，const 引用不复制记录；核心不能修改输入、返回后保留引用、读写文件、解析参数或写 stdout。返回字符串独立持有存储，`rows[i]` 对应 `input[i]`，不得增删或重排。

核心用异常报告失败。核心异常、分配失败、结果不合法统一为退出 5；不得截断结果或静默切换算法。输出前检查：行数相同、等长且宽度大于零、每行去掉 `-` 精确等于相应清洗序列、仅含 `ACGTUN-`、没有全 gap 列。这是结构约定，不是生物学准确率保证。

<a id="migrating-from-020"></a>
### 从 0.2.0 迁移

将 `std::span<const SequenceRecord>` 改为 `const std::vector<SequenceRecord>&`，同步声明、定义、回调，移除 `<span>` 并适配专属操作。普通调用仍是 `align(records)`。在新目录共同重建核心、框架和测试，运行 contract。字段、顺序、CLI、退出码、算法不变，版本输出为 `0.3.0`。

<a id="4-input-contract"></a>
## 4. 输入约定

输入路径或 `-`。gzip 按前两个魔数字节识别，与扩展名无关，stdin 同样支持。探测前缀不能丢失。严格校验截断、CRC、长度、多成员及空成员；尾随非 gzip 字节返回 4。少于两个魔数字节仍按普通输入识别。

解码后的字节通过回调直接交给固定 kseq，调用 `kseq_init/read/destroy`，RAII 清理。不另设预解析器或重编码记录。支持解析器识别的 FASTA、FASTQ 和混合记录；质量值检查长度后丢弃，不进入核心或清洗计数。

标题由 `name` 加非空 `comment` 重建，仅两者均非空时插入一个空格；不做序列清洗，不保证原分隔空白逐字节保留。

| 上下文 | kseq 清洗前行为 |
| --- | --- |
| `>sample\tinfo` | 重建为 `sample info`。 |
| `>sample  info` | 第二个空格属于 comment，重建保留两个空格。 |
| `> sample` | name 为空，comment 为 sample，标题成为 `sample`。 |
| `>sample\rinfo` | CR 分隔 name，重建为 `sample info`。 |
| `>sample info\rmore` | comment 内的 CR 保留在标题。 |
| 前导内容、BOM、缩进 | 扫描到 `>` 或 `@`，标记不必在行首。 |
| 序列行首 `>`／`@` | 结束当前序列，开始下一记录。 |
| 序列行首 `+` | 开始质量字段，包括在 FASTA 标题后。 |
| 序列内部 `A>+@C` | 五个字节均为序列，清洗为 `ANNNC`。 |
| 多行序列和质量 | 拼接；质量长度必须匹配序列。 |
| EOF 处 `AC\r` | 去掉尾 CR，得到 AC。 |
| 首个残基前的 CRLF 空行 | 初始 CR 可能保留，随后清洗为 N。 |
| 完整记录后的单独尾 `>` | 可直接 EOF 而不返回空记录。 |

累积字段长度大于一时，行读取器移除尾 CR，包括 EOF；LF 空序列行跳过。剩余空格、制表符按字节清洗。不做 Unicode 解释或格式修复。没有返回记录、返回标题仅 ASCII 空白、空序列、质量长度错误返回 3；定位包含来源及可确定的一基记录号，不承诺物理行号。仅检查 kseq 实际返回的记录。

安全补丁检查初始化和扩容、部分清理、临时指针 realloc、长度及读取错误。每个字段限制为 `INT_MAX`；资源/长度失败为 5，读取/解压失败为 4，正常记录语义不变。

清洗先拒绝 `-`、`.`，再保留 `ACGTUN`，将 `acgtun` 大写，其余返回字节转 N。其他 IUPAC、数字、空白、高位字节也转 N。U 不转 T。合法小写转换与其他转 N 分别计数，已有 N 不计，n 只计小写转换；quiet 隐藏常规统计但保留错误。例如 `acgRyUn` 得到 `ACGNNUN`，后者才是结果保真基准。

## 5. 输出约定

按输入顺序写保存的重建标题和全部记录，质量不写出；新增 gap 用 `-`。输出 LF 并保留末尾换行；默认宽度 80，0 不折行，其他非负值限制序列行长度，标题不折行。只有大小写精确匹配后缀 `.gz` 的命名输出压缩，stdout 始终普通 FASTA。

先验证完整结果，再在目标同目录创建独占临时文件，检查写入、刷新、关闭及 gzip 终结后发布；析构清理不能代替完成检查。父目录必须存在。无 force 时通过原子无覆盖操作避免竞态；force 也只替换完整结果。失败保留旧目标，只清理本次临时文件。不保证断电恢复。

输入输出不可同文件，包括符号/硬链接，即使 force 也拒绝。stdin/stdout 无法可靠识别 Shell 重定向别名，不要重定向覆盖输入。stdout 不能回滚，断管返回非零；结果之外的日志、进度、错误只到 stderr。

## 6. CLI 与退出码

| 参数 | 约定 |
| --- | --- |
| `-i, --input PATH` | 必填，路径或 `-`。 |
| `-o, --output PATH` | 必填，路径或 `-`。 |
| `--line-width N` | 非负整数，默认 80，0 不折行。 |
| `--force` | 允许完整结果替换已有文件。 |
| `--quiet` | 隐藏常规日志，保留错误。 |
| `-h, --help` | stdout 帮助，成功退出。 |
| `-v, --version` | stdout 名称和版本，成功退出。 |

帮助/版本不要求输入输出，不读取数据、不创建文件。未知/缺少参数、负数或溢出行宽为参数错误。无线程或隐藏算法选项。日志不污染结果流。

| 退出码 | 含义 |
| --- | --- |
| 0 | 写出成功或帮助/版本成功。 |
| 2 | 参数错误或输入输出同文件。 |
| 3 | 返回记录不合法、无记录、已有 gap、质量长度错误。 |
| 4 | 读取/解压/写入错误、文件不可读、父目录缺失、拒绝覆盖。 |
| 5 | 核心、资源分配或结果约定失败。 |

CLI11 自带错误码转换为 2；文件不存在属于 4。使用管道时设置 `set -o pipefail` 并检查状态，存在输出文件不表示成功；Shell 重定向不享受 `-o` 命名输出事务保护。

## 7. 参考星比对

标记 **Reference star alignment — replaceable core**。第一条为中心，其他逐条与同一中心进行 Needleman–Wunsch 全局对齐。单条直接返回。评分先判断任一为 N 则 0（含 N/N），再判断相同或 T/U 为 +1，否则 -1；每 gap -1。并列优先对角、只消费中心、只消费查询。输出保留各自原字符。

中心长度 L 对应 L+1 个插入槽，包含前后缀。每槽宽度取所有查询插入长度最大值，插入左对齐、右补 gap，中心在槽内全 gap。中心 AC、查询 ATC/AGGC 得到 A--C、AT-C、AGGC。槽内插入不进一步互比，不保证最优 MSA。

两两时间 `O(L × Σmᵢ)`，构造/验证 `O(n × W)`。逐查询释放完整 DP；峰值表 `O((L+1) × (max(mᵢ)+1))`，输入、插入信息和最终 `O(n × W)` 矩阵另占内存。评分用有符号类型；乘法、字节、评分范围和输出长度分配前检查溢出。资源不足明确失败，不截断或改算法。

## 8. 替换核心

先构建原模板，阅读接口，替换 `src/align.cpp`；可新增私有算法文件并更新 CMake。保留外围职责，跑 contract、添加算法测试并维护依赖声明。可改变中心、评分或策略，但必须保留位置与清洗后字符。reference 的具体 gap 仅约束参考算法。新公开参数或输入语义需明确扩展公共规范。

<a id="9-verification-plan"></a>
## 9. 验证方案

doctest 负责模块，Python 标准库负责真实进程/压缩/管道/文件生命周期；全部小型合成数据。覆盖 kseq记录、身份、清洗、gzip完整性、覆盖保护、短读/中断/关闭/发布/分配故障、故意错误的替代核心、参考评分回溯、边界和独立穷举短序列最优分数。测试故障接口不进入生产 CLI 或环境开关。参见 [测试矩阵](testing.zh-CN.md) 和 [实际结果](validation.zh-CN.md)。

构建目标 `msakit_support`、`msakit_core`、`msakit`。Release 与 Debug+ASan/UBSan 使用同一测试；`BUILD_TESTING=OFF` 不查找 Python。C++17 禁用扩展；已执行 GCC 13.1 Release、GCC 13.1 Sanitizer、GCC 9.5 Release，不据此推断所有旧编译器兼容。命令见 [开发指南](guide.zh-CN.md)。

## 10. HAlign-5 来源和依赖

所有来源固定到前述提交。CMakeLists 的组织保留但改 C++17/小依赖/CTest；config.hpp 提供 CLI11/日志组织思路；utils.h 的清洗表被改写并增加 gap 拒绝和位置身份；seq_io.cpp 的 kseq 生命周期及 name/comment 重建沿用，配以严格 gzip 和完整写入。kseq.h 保留许可并补安全检查；preprocess.cpp 仅借鉴准备阶段划分，省略共识选择、下载和工作目录树；align.h 被内存 API 替代，省略 WFA2/KSW2、sketch、profile、SAM。原读写测试仅借鉴小型模式，没有携带全部测试工作量。

附带 CLI11 2.6.1、spdlog 1.16.0、doctest 2.4.12、固定 kseq；zlib 必需但不随仓库附带。核心仅标准库，不需要外部 MSA、OpenMP、WFA2、KSW2、MinHash、cereal、libcurl。来源、许可证与每个文件 SHA-256 在 [provenance.json](../extern/provenance.json)。MSA-Kit 使用 MIT，保留 HAlign-5 `Copyright (c) 2026 HAlign-5 contributors` 和其他依赖许可；详见 [第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)。
