# 输入处理测试指南

[English](testing.md) | **简体中文**

本指南把 [README](../README.zh-CN.md#check-result) 中的结果检查写成可复用的软件测试。contract 表示所有核心都必须满足的程序约定，reference 表示附带算法的特定评分和布局。

## 把测试断言理解为一个比对问题

对 AC/ATC/AGGC 三条输入，比较下面两种布局：

```text
                 center    q1      q2
参考核心         A--C      AT-C    AGGC
另一种布局       A--C      A-TC    AGGC
```

两种布局都有四列，逐行去掉 gap 都能还原输入，也没有全 gap 列。因此 contract 应允许两种结果；参考核心测试按它固定的规则期待第一种。如果 q1 变成 AC，就丢失了一个碱基，即使把所有行补成等长，也必须拒绝。

所以测试要分别检查数据是否保留、某算法是否遵循自己的评分和回溯规则。生物学质量还需用合适证据或参考数据另行评估。

测试使用小型合成字节和序列，描述输入含义、清洗/拒绝及输出文件状态。[设计](design.zh-CN.md) 定义行为，[验证记录](validation.zh-CN.md) 记录已执行结果。

## 测试组织

- [input_cases.py](../tests/input_cases.py)：稳定编号、原始字节、独立预期标题/序列/计数或错误记录号。
- [msa_cases.py](../tests/msa_cases.py)：六组固定 FASTA、七组有界生成多序列数据。
- [integration.py](../tests/integration.py)：运行真实程序，独立解析结果，检查管道、压缩与文件生命周期。
- [test_io.cpp](../tests/test_io.cpp)：完整字节、精确边界和确定性 I/O 故障；解析器测试另检查资源与长度。
- contract 检查外围约定，reference 检查星比对评分/具体布局。期望值不能通过生产读取器生成。

<a id="c17-build-compatibility"></a>
## C++17 构建兼容性

0.3.0 生产/测试均 C++17、禁用扩展，核心、替代核心、验证和输出均接受 const vector 引用。迁移旧 span 见 [设计](design.zh-CN.md#migrating-from-020)。分别用 GCC 13.1 Release、GCC 13.1 Debug+ASan/UBSan、GCC 9.5 Release 跑四个 CTest 入口，并检查替代核心及 `BUILD_TESTING=OFF` 构建、安装、版本、FASTA 和 FASTQ stdin。

切换编译器要用独立目录，首次配置选 `-DCMAKE_CXX_COMPILER=/path/to/g++`。可开 `CMAKE_EXPORT_COMPILE_COMMANDS` 检查 `-std=c++17`，不得变成 GNU 扩展模式。保留既有行为答案、13 组 MSA、清洗、短输出名称和大小写敏感 `.gz`、临时清理、顺序与输入只读检查。矩阵是要求，不能替代实际结果；语言选项不表示所有旧编译器可用。

## 输入矩阵与稳定编号

| 行为 | 用例族 | 预期及层级 |
| --- | --- | --- |
| 大小写、N、T/U | `INPUT-VALID-*`, `INPUT-BYTES-*` | 保留大写和 U，六种合法小写大写化，计数互斥；模块/集成。 |
| IUPAC、数字、空白、NUL、高位字节 | 同上 | kseq 返回的其他字节逐字节转 N；模块/集成。 |
| `-`、`.` | `INPUT-ERROR-*`, `INPUT-BYTES-*` | 内容错误 3，不掩盖为 N。 |
| 重建标题、重复、内部特殊字符、长标题 | `INPUT-VALID-004/005/009`, `KSEQ-COMPAT-005/006/007`, `HEADER-LONG` | name/comment 重建、位置身份、标题不折行。 |
| 多行、LF/CRLF、空行、EOF | `INPUT-VALID-007/008`, `KSEQ-COMPAT-008/009`, `INPUT-STRUCTURE-001`, `INPUT-BOUNDARY-001` | 固定 kseq CR/行语义。 |
| 空返回记录/无记录 | `INPUT-ERROR-*`, `KSEQ-ERROR-005/008/009/010`, `INPUT-KSEQ-002` | 3，来源及可确定记录号，不绑定行号。 |
| 前导内容、BOM、缩进 | `KSEQ-COMPAT-001/002/003/004` | 扫描至首标记，旧拒绝用例现在成功。 |
| FASTQ、混合、多行质量 | `KSEQ-FASTQ-001` 至 `006`, `INPUT-KSEQ-001` | 顺序读取，质量忽略，输出 FASTA。 |
| 质量中的 gap/标记/NUL/高位字节 | `KSEQ-FASTQ-004/006`、质量不变性方法 | 不进入核心或清洗统计。 |
| 质量缺失/过短/过长 | `KSEQ-ERROR-001/002/003/004/007` | 3，无部分输出。 |
| 行首 @/+、内部标记、末尾裸标记 | `KSEQ-COMPAT-010` 至 `014` | 遵循固定解析器，不加严格预解析器。 |
| 扩展名不匹配内容 | 所有合法/内容错误族 | 按魔数，四输入通道一致。 |
| gzip 可选头、头 CRC、空/多成员 | `GZIP-VALID-*`、字段切分方法 | 解码为连续字节流，成员边界不影响解析。 |
| gzip 头/负载/CRC/长度/后续成员/尾随损坏 | `GZIP-ERROR-*`, `GZIP-PREFIX-*` | 已识别 gzip 返回 4。 |
| 零/一字节魔数前缀 | `GZIP-PREFIX-*` | 普通输入识别，返回 3。 |
| 16/64 KiB 边界 | `INPUT-BOUNDARY-*`、FASTQ 边界方法 | 探测偏移/分块不改记录及统计。 |
| 短读、EINTR、读取/关闭失败 | `INPUT-READ-*` | 恢复中断、失败 4；模块。 |
| 初始化/扩容失败 | `KSEQ-RESOURCE-001/002/003` | 5，部分对象清理，失败 realloc 保留所有者。 |
| 字段/算术上限 | `KSEQ-LIMIT-001` | 分配/窄化前检查 int 长度，5。 |
| 解析各阶段负回调 | `KSEQ-READ-001/002` | 4，不能误报 EOF/部分成功。 |
| 生成大小写/折行/换行/gzip 切分 | `GENERATED-*` | 独立预期记录和对齐性质。 |
| 失败的文件副作用 | 输入/gzip 错误族 | stdout 空，新目标无、旧目标/输入不变，无临时残留。 |
| 目录作为输入 | `PATH-INPUT-DIRECTORY` | 4，目录及旧输出不变。 |

固定目录有 32 个合法例（12 输入、14 kseq、6 FASTQ）、22 内容错误、8 合法 gzip、13 损坏 gzip、26 截断前缀。合法例为 384 通道组合，内容错误为 88 输入路径组合及 80 个所选输出生命周期组合；FASTQ 专项另有 16 质量不变、36 成员字段切分、12 大字段边界。它们是方法内子场景，不是独立 CTest 入口。目录检查 ID 唯一及生命周期引用。

## 0.2.0 引入、0.3.0 沿用的行为变化

`INPUT-ERROR-010` 至 `013` 退役，相同字节改为成功的 `KSEQ-COMPAT-001` 至 `004`；旧 `INPUT-ERROR-014` 改为 `KSEQ-FASTQ-001`。`INPUT-VALID-004/005` 检查标题重建，空 name 后首分隔消耗、其他 comment 空白保留。`INPUT-VALID-007` 的末尾 `Tu\r` 得 TU；`KSEQ-COMPAT-008` 的初始 CRLF 空行可得 N。010/011 检查行首标记，012 检查内部字节，013/014 检查末尾裸 `>`/`@` 无下一记录。诊断绑定来源、类别及记录，不绑定物理行号。

## 多序列合法性

固定六组见 [数据说明](../tests/data/msa/README.zh-CN.md)：001 DNA indel/替换，002 RNA/TU/N，003 重复及清洗，004 分歧/N 中心，005 长度 129/1/2/9/31/64，006 长度 79/80/81/159/160/161。生成 001–006 分别 2/3/8/16/64/128 条、长 16–128；007 为四条长 512/513/510/512。MSA 种子 20261027，均为软件合成数据而非生物基准。

四个端到端方法共 127 子场景：72 固定通道、28 生成、12 逆序/轮转、15 折行。行宽 0/1/7/80/81，跨 stdout/普通/gzip；0 每序列单行。独立检查标题、顺序、数量、去 gap 后清洗序列、等长非空、`ACGTUN-`、无全 gap 列。输入重排可改变中心及布局，不能要求布局相同。检查器自测接受两种合法布局，拒绝 16 种错误及 2 种折行错误，不调用生产验证器。

最大两两表 513×514，即 263,682 格、2,109,456 字节评分，逐表释放；250,000 字符单序列另测 I/O，不触发大 DP。观察宽度不是所有核心的标准答案。

## 通道、边界和生成

代表性有效例通过普通文件、gzip 文件、普通 stdin、gzip stdin，分别输出 stdout、普通文件、gzip 文件；路径含空格/非 ASCII，扩展名可误导。独立解压后检查结构；标题中的合法孤立 CR 不能一概当作换行错误。故障不发布部分结果，不改变旧目标或输入。

完整 256 字节放在受控序列上下文；LF/CR/行首标记另作结构检查。80 个模块边界组合覆盖 16 KiB kseq、64 KiB 解码和两个探测字节。FASTQ 序列/质量长度为 16,383/16,384/16,385 和 65,535/65,536/65,537；大字节例单序列。内部替身确定性控制短读、EINTR、读取与关闭错误，不依赖 sleep/管道调度；关闭替身先释放真实描述符一次，符合 Linux 所有权假设。

解析器逐初始化分配、长 FASTQ 分配及字符串增长注入失败，检查 realloc 所有者、字节及析构无泄漏；INT_MAX 边界不真的分配多 GiB。读取故障覆盖标题/序列/质量和 EOF 区分。故障控制不向生产公开。

输入生成种子 20261026，共 32 组，每组 1–6 条、每条 1–32 字符；先定期望记录，再变大小写、折行、LF/CRLF、多 gzip 成员。失败报告 seed/ID。可选 gzip 头和 CRC 独立构造并用 zlib 检查；逐截断点遵循魔数识别规则。

## 运行和扩展

构建、Release、Sanitizer 命令见 [开发指南](guide.zh-CN.md#7-替换后如何确认没有破坏约定)。常用专项：

```bash
(cd build && ctest -L contract --output-on-failure)
python3 tests/integration.py --binary build/msakit \
  --test-driver build/msakit_test_driver --suite contract
./build/msakit_unit_tests --test-case='*INPUT-*'
./build/msakit_unit_tests --test-case='*KSEQ-*'
```

新增用例：选择未用稳定 ID，保留旧 ID；给出原始字节和独立期望或错误类别/位置；断言单一故障，不绑定整段系统/zlib文本；通道复用共享矩阵，字节边界留模块，gzip 留对应族，生命周期取代表场景。先专项再完整 Release/Sanitizer，更新矩阵和实际验证记录。

```python
InputCase("INPUT-VALID-013", b">example\nacR\n",
          (b"example",), (b"ACN",), uppercase=2, unknown=1)
```

这是新增方式示例，不是已加入夹具。加入 `VALID_CASES`、`KSEQ_COMPAT_CASES`、`KSEQ_FASTQ_CASES` 后由 `ALL_VALID_CASES` 参与通道矩阵。无需真实数据、下载、额外测试包或修改系统；测试软件约定，不测生物准确性/规模性能。
