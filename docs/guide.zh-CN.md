# MSA-Kit 中文开发指南

[English](guide.md) | **简体中文**

本指南把 [README 的比对示例](../README.zh-CN.md#first-result) 与 C++ 实现连起来：跟踪同样三条记录如何变成输入对象、对齐字符串和输出文件，在使用接口的地方解释相关术语。

MSA-Kit 0.3.0 是可复制的 DNA/RNA 多序列比对程序模板：复用输入输出、命令行和结果验证代码，把开发工作集中在比对算法上。本版采用 HAlign-5 固定版本的 `kseq.h` 读取 FASTA/FASTQ，`src` 按功能组织为七个文件，派生开发者主要编辑 `src/align.cpp`。

仓库已包含 C++17 实现、参考星比对、小型示例和测试。实际执行的构建与测试结果见 [验证记录](validation.zh-CN.md)。

完整接口定义见 [设计规范](design.zh-CN.md)，项目概览见 [README](../README.zh-CN.md)。本指南解释如何理解框架和替换核心，不另行定义一套规则。

## 1. 程序分成哪些部分

先认识这里的模块名：**CLI** 解释 `-i`、`-o` 等命令行参数；**kseq** 把 FASTA/FASTQ 字节读成记录；**zlib** 处理 gzip 压缩；**核心**计算对齐。**外围支持库**把这些共用外围代码编译后提供给程序与测试使用。

数据依次经过五个部分：

```text
命令行参数解析
    ↓
kseq 读取 FASTA/FASTQ、字符清洗与输入校验
    ↓
可替换的比对核心
    ↓
统一结果验证
    ↓
FASTA / gzip 写出
```

| 部分 | 负责什么 | 替换算法时的处理 |
|---|---|---|
| 命令行层 | 参数、帮助、版本、日志和退出码 | 复用 |
| 输入层 | 普通文件、gzip、管道、标题保存和字符清洗 | 复用 |
| 比对核心 | 接收序列，生成对齐行 | 替换这里 |
| 结果验证层 | 检查行数、顺序、等长和碱基保留 | 复用 |
| 输出层 | 标题恢复、折行、压缩和写入完成检查 | 复用 |

核心拿到的是已准备好的内存数据，不需要知道序列来自文件还是管道，也不需要决定输出格式。

源码布局如下：

```text
include/msakit/core.hpp    # 公共核心接口
src/
├── main.cpp              # 极简程序入口
├── cli.cpp               # 参数、日志、调用流程和异常处理
├── input.cpp             # 文件/stdin、gzip 解压、kseq 解析和清洗
├── output.cpp            # FASTA/gzip 写出、路径保护和原子发布
├── validation.cpp        # 比对结果的通用合法性检查
├── align.cpp             # 可替换的参考星比对核心
└── internal.hpp          # 共用类型、声明、错误和尺寸检查
extern/kseq/kseq.h        # 固定来源并带安全补丁的读取依赖
```

`main.cpp` 调用命令行模块中的 `run_application()`，后者按顺序完成读取与清洗、输出路径检查、核心比对、结果验证和输出。命令行、输入、输出、验证各自独立编译，再组成 `msakit_support` 库，供程序、模块测试和替代核心测试共同使用；`msakit_core` 保留独立算法目标。共用声明集中在一个 `internal.hpp` 中，按职责分段。该头文件通过前向声明引用 zlib 流类型，因此算法编译不必包含 zlib、CLI11 或日志库头文件。

后续开发可以按任务定位文件：

| 要修改的功能 | 实现位置 |
| --- | --- |
| 参数、日志配置、程序执行步骤 | [cli.cpp](../src/cli.cpp) |
| 输入格式解析、字符清洗、转换统计 | [input.cpp](../src/input.cpp) |
| 输出格式、折行、压缩和文件保护 | [output.cpp](../src/output.cpp) |
| 所有核心都必须满足的结果规则 | [validation.cpp](../src/validation.cpp) |
| 比对算法 | [align.cpp](../src/align.cpp) |

0.3.0 保留上述七文件结构，将最低语言标准改为 C++17，并将核心参数改为只读 `vector` 引用。输入行为、算法、命令行参数和退出码均沿用 0.2.0 的约定。

## 2. 核心接收和返回什么

公开接口位于 [include/msakit/core.hpp](../include/msakit/core.hpp)：

```cpp
namespace msakit {

struct SequenceRecord {
    std::string header;
    std::string sequence;
};

struct Alignment {
    std::vector<std::string> rows;
};

Alignment align(const std::vector<SequenceRecord>& input);

}
```

`header` 由 kseq 的 `name` 和 `comment` 重建：保留 `name`，追加非空 `comment`，两者均非空时在中间加一个空格。例如 `>sample\tinfo` 得到 `sample info`。描述内容保留，但不再保证原始标题分隔空白逐字节不变。输入集合至少包含一条记录；每条 `sequence` 已完成清洗，非空且没有 gap。`const std::vector<SequenceRecord>&` 表示通过只读引用接收整个输入集合，调用时不复制容器或记录。数据由框架持有，在调用期间有效；核心不能修改记录，也不能在返回后继续持有其引用。

返回值中的 `rows[i]` 对应 `input[i]`。例如第 3 条输入必须仍然对应第 3 条输出；核心内部即使排序或分组，也必须恢复这个对应关系。

### 跟踪一次比对经过接口的过程

对 `examples/tiny.fasta`，输入 vector 就是包含三条记录的列表。C++ 下标从 0 开始：

| 位置 | `input[i].header` | `input[i].sequence` | 参考结果 `rows[i]` |
| --- | --- | --- | --- |
| 0 | `center` | `AC` | `A--C` |
| 1 | `q1` | `ATC` | `AT-C` |
| 2 | `q2` | `AGGC` | `AGGC` |

SequenceRecord 把名称和序列放在一起；Alignment 保存对齐后的字符串列表，不含 FASTA 的 `>` 标题行。框架保留名称、检查这些字符串，再写出完整 FASTA，因此算法处理序列即可，不需要再写一个 FASTA 解析器。

const 让传入列表只读，`&` 表示引用框架已有列表而不复制。rows[i] 与 input[i] 使用相同下标，输出层就能把正确名称与对齐序列重新组合。表格描述这次示例计算出的结果；算法必须为实际输入计算，不能固定返回这几个字符串。

修改前先在 [align.cpp](../src/align.cpp) 找到三处：align() 接收记录、align_pair() 做两两对齐、插入槽合并构造最终行。先跟踪 AC/ATC/AGGC 如何经过它们，再替换计算部分；读取和写出继续使用外围模块。

记录位置是内部身份，标题可以重复。不要用标题作为唯一键，否则同名记录可能被覆盖。输出层使用重建后保存的标题，因此核心只需返回对齐后的序列字符串。

核心不能自行解析参数、读写文件或输出 FASTA。失败时抛出异常，由统一入口记录错误并返回退出码；不要返回不完整的结果作为成功输出。

### 已基于 0.2.0 开发的项目如何迁移

将自定义核心的声明和定义都改为上面的 `const std::vector<SequenceRecord>&` 签名。自定义核心回调或辅助函数若使用旧的 `std::span` 参数，也要同步调整，移除不再使用的 `<span>` 头文件，并改写依赖 span 的操作。框架内部的核心函数指针、验证及输出接口也采用相同的只读引用。已有 `align(records)` 调用在传入框架的输入 vector 时无需增加转换。

在新的构建目录中重新编译框架、自定义核心及测试，再运行 `contract` 组。不要混用旧接口编译的对象文件。`SequenceRecord` 与 `Alignment` 的字段、输入位置对应关系和结果约定保持不变；CLI 用法及数据处理行为也保持不变，`--version` 更新为 `0.3.0`。编译器验收范围见 [测试说明](testing.zh-CN.md#c17-build-compatibility)，具体执行结果见 [验证记录](validation.zh-CN.md)。

## 3. 进入核心前，序列会怎样变化

本版面向 DNA 和 RNA，采用 HAlign 风格的字符清洗。下表只作用于 **kseq 返回的序列字节**；标题和 FASTQ 质量值不参与序列清洗。

| 序列内容 | 处理方式 |
|---|---|
| `A/C/G/T/U/N` | 保留 |
| `a/c/g/t/u/n` | 转为对应大写字符 |
| 其他字符，包括其他 IUPAC 模糊碱基 | 转为 `N` |
| `-` 或 `.` | 报错，要求输入未比对序列 |

kseq 负责记录与行的解析，保留下来的空格、制表符等字节会转为 `N`。多行序列拼接后仍须非空。读取器直接调用 `kseq_init()`、`kseq_read()` 和 `kseq_destroy()`，通过读取回调接收普通或解压后的字节，不先用另一套解析器重新组织输入。

例如 `acguR` 清洗后是 `ACGUN`。日志会汇总大小写转换数量和其他字符转为 `N` 的数量；已有 `N` 或小写 `n` 不计入未知字符替换数量。

输入支持 FASTA、FASTQ 及固定版本 kseq 能识别的混合记录。FASTQ 可以有多行序列和质量值；质量长度须与序列长度一致，质量值解析后丢弃，不传入核心，也不写入结果。输出始终为 aligned FASTA。重复标题、重复序列与输入顺序继续保留。URL 下载、蛋白质及已对齐 profile 不在支持范围内。

以下输入行为由 0.2.0 相比 0.1.0 引入，0.3.0 继续沿用：

| 场景 | 本版行为 |
|---|---|
| 标题中的名称与描述分隔符 | 按 `name` 与 `comment` 重建；如一个制表符变为连接空格，描述中剩余的空白继续保留 |
| 开头有注释、BOM 或其他前导内容 | kseq 寻找下一条记录时跳到 `>` 或 `@`，该标记不必位于行首 |
| 序列行开头出现 `>` 或 `@` | 结束当前序列并开始下一条记录 |
| 序列行开头出现 `+` | 开始质量区，包括以 `>` 开始的记录 |
| 序列行内部出现 `>`、`@` 或 `+` | 保留为序列字节，随后各自转为 `N` |
| 文件末尾序列为 `AC\r` | kseq 去掉末尾 CR，得到 `AC` |
| 首个碱基前有一个空 CRLF 行 | 初始 CR 会保留，清洗后贡献一个 `N`；LF 空行则忽略 |
| 完整记录后只有一个尾随 `>` | kseq 可以直接报告 EOF，没有返回一条新的空记录 |

CR 的规则取决于 kseq 字段累计长度：按行读取的字段长度大于 1 时去掉末尾 CR，即使该 CR 后没有 LF。因此不再承诺旧版“只删除真正 CRLF”的行为。更完整的例子见 [输入规范](design.zh-CN.md#4-input-contract)。本版不增加 Unicode 解释或自动格式修复。

读取不到任何记录、返回的标题为空或仅含 ASCII 空白、返回空序列、已有 gap，以及 kseq 报告的质量长度错误，均返回内容错误 `3`。错误保留来源和能够确定的记录号，不再承诺精确物理行号。部分旧版拒绝的异常输入会被 kseq 接受或跳过；校验对象是 kseq 实际返回的记录。

kseq 带有必要的分配、扩容、清理和长度检查补丁。由于其返回长度类型为 `int`，单个解析字段上限为 `INT_MAX` 字节。分配或长度失败返回 `5`；底层读取或解压失败返回 `4`。这些补丁不改变正常记录的解析规则。

这里的关键是：**结果去 gap 后，必须等于进入核心的清洗后序列。** 对上例而言，校验目标是 `ACGUN`，而不是原来的 `acguR`。

T 与 U 始终保留各自字符。参考核心仅在评分时把它们视为同类，不把 RNA 的 U 写成 T，也不把 T 写成 U。

## 4. 构建与使用

本版支持 Linux，需要 C++17 编译器、CMake ≥ 3.18 和系统 zlib 开发文件。CLI11、spdlog、doctest 和 kseq 已随仓库固定来源附带，正常配置和构建不会下载依赖。启用测试时还需要 Python ≥ 3.8；运行 `msakit` 不依赖 Python。依赖版本、来源和许可见 [第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)。

在仓库根目录构建并运行测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
```

生成的程序位于 `build/msakit`。以下命令使用仓库中的 [小型合成输入](../examples/tiny.fasta)，并把结果写到标准输出，可以重复运行：

```bash
./build/msakit --version
./build/msakit -i examples/tiny.fasta -o -
```

以下文件输出示例使用已有的 `build` 目录，并显式允许替换同名示例结果：

```bash
./build/msakit -i examples/tiny.fasta -o build/tiny.aligned.fasta --force
./build/msakit -i examples/tiny.fasta -o build/tiny.aligned.fasta.gz --force
```

普通输入和 gzip 输入均可通过管道传入。以下 Bash 示例的第二条管道需要系统 `gzip` 工具：

```bash
set -o pipefail
cat examples/tiny.fasta | ./build/msakit -i - -o -
gzip -c examples/tiny.fasta | ./build/msakit -i - -o -
```

如果只构建程序、不运行测试，可以关闭测试，此时不需要 Python：

```bash
cmake -S . -B build-app -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-app --parallel 2
./build-app/msakit -i examples/tiny.fasta -o -
```

输入是否压缩根据 gzip 文件头识别，不依赖扩展名；标准输入也遵循该规则。支持串接的 gzip 成员，成员内容连续组成交给 kseq 的字节流；截断、CRC/长度错误或末尾非 gzip 数据仍会报错。输出文件名以小写 `.gz` 结尾时压缩，`-o -` 始终输出普通 FASTA。需要压缩管道结果时，可在程序之后连接压缩工具。

FASTQ 使用相同的输入参数，例如 `./build/msakit -i reads.fastq.gz -o aligned.fasta`，不需要额外的格式选项。

程序只把对齐结果写到运行时的标准输出，日志和错误写到标准错误，避免日志混入 FASTA。

首版参数为 `-i/--input`、`-o/--output`、`--line-width`、`--force`、`--quiet`、`-h/--help` 和 `-v/--version`。输入和输出参数必填；帮助和版本可独立运行，不读取数据或创建输出。

输出默认每行 80 个字符，统一 LF，并保留文件末尾换行；`--line-width` 接受非负十进制整数，`0` 表示不折行。`--quiet` 关闭常规日志，但保留错误。参考核心为单线程固定参数，首版不提供线程、参考序列或评分矩阵选项。

已有输出文件默认受到保护，只有 `--force` 允许替换；输入和输出指向同一文件时始终按参数冲突报错，返回 `2`，包括符号链接和硬链接别名。输出父目录须已存在；拒绝覆盖已有文件和父目录缺失均返回 `4`。文件结果先写入同目录独占临时文件，写入、压缩结束和关闭成功后再发布到目标路径。默认通过原子无覆盖操作保护目标；`--force` 也只用完整结果替换目标。失败会清理本次创建的临时文件并保留原目标，不承诺断电后的持久化恢复。

标准输出不能回滚，管道写入失败时可能已经产生部分字节，调用方必须检查退出码。在 Bash 中连接多个程序时，先设置 `set -o pipefail`，避免末端压缩程序的成功状态掩盖比对程序失败。管道失败仍可能留下重定向创建的部分文件，不能仅凭文件存在判断成功；同目录临时文件保护适用于通过 `-o` 指定的文件输出。

| 退出码 | 含义 |
|---|---|
| `0` | 成功，或成功显示帮助/版本 |
| `2` | 命令行参数错误 |
| `3` | 输入内容不合法 |
| `4` | 文件读取、解压或写入失败 |
| `5` | 核心执行、资源分配或结果约定检查失败 |

## 5. 如何替换成自己的算法

1. 复制或克隆 MSA-Kit，阅读英文设计中的输入、核心和输出约定。
2. 打开 [include/msakit/core.hpp](../include/msakit/core.hpp)，确认 `SequenceRecord`、`Alignment` 和 `align` 的含义。
3. 在 [src/align.cpp](../src/align.cpp) 中找到标注为 **Reference star alignment — replaceable core** 的实现入口，用自己的算法替换 `msakit::align` 的实现。保持文件位置和函数签名时，无需调整外围代码；拆分或重命名源文件时，相应更新 CMake 的 `msakit_core` 目标。
4. 保持输入只读，并按输入位置填充 `Alignment::rows`。算法内部可以使用不同的数据结构、评分或中心选择方式。
5. 保留通用约定测试，为新算法补充小型正确性测试，并调整参考算法专属断言。参考算法的最优分数、固定 gap 布局和并列规则测试分布在 [核心测试](../tests/test_core.cpp) 和 [集成测试](../tests/integration.py)，新算法不必复现这些布局；重新构建与测试的命令见第 4、7 节。
6. 更新项目名称、算法说明和适用范围，保留实际复用代码的许可及来源声明。

如果确实需要新增参数，应通过命令行层增加明确的配置并传入核心，而不是在核心内部重新读取命令行。这样的扩展属于派生项目的接口变化，需要同步文档和测试。

## 6. 参考星比对具体做什么

参考实现使用第一条输入作为中心，逐条执行 Needleman–Wunsch 全局比对，完整消费中心和查询序列。

评分为匹配 `+1`、错配 `-1`、每个 gap 字符 `-1`。只要一侧为 `N` 就记 `0`，包括 `N/N`；这条规则先于字符相等判断。T/U 按匹配计分，但回溯结果保留原字符。

并列回溯按固定顺序选择：同时消费中心和查询；仅消费中心、查询填 gap；仅消费查询、中心填 gap。这个顺序让参考结果可重复。

长度为 L 的中心具有 L+1 个插入槽，包括第一个碱基之前、相邻碱基之间和最后一个碱基之后。每个槽取所有查询在该处插入长度的最大值，插入串左对齐，右侧补 gap；中心在插入槽中全部填 gap。

例如，仓库中的 [examples/tiny.fasta](../examples/tiny.fasta) 包含以下合成数据：

```fasta
>center
AC
>q1
ATC
>q2
AGGC
```

参考核心按上述规则输出：

```fasta
>center
A--C
>q1
AT-C
>q2
AGGC
```

中心的 A 和 C 之间需要容纳最长的两个插入字符；q1 的一个插入字符 T 在这个槽中左对齐。槽内插入串不会再互相比对，因此该规则只是易读、确定的参考合并方法，不保证最优多序列比对。

两两计算时间为 `O(L × Σmᵢ)`，还需构造并验证最终矩阵。动态规划表逐条查询分配并释放，其峰值取决于中心与最长查询的长度；最终对齐矩阵和插入信息也占用内存。计算和分配前检查尺寸、字节数、评分范围与输出长度溢出。该核心用于小型演示和接口验证，不承诺 HAlign-5 的处理规模。

## 7. 替换后如何确认没有破坏约定

通用接口测试与参考算法测试有独立的 CTest 标签。替换核心后，可先运行：

```bash
(cd build && ctest -L contract --output-on-failure)
```

`reference` 标签用于星比对的特定评分、结果和回溯检查；替换算法时应更新这组测试。未指定标签的 `ctest` 会运行两组。

框架在输出前检查以下条件：

- 输出行数与输入记录数相同，行与输入位置对应。
- 所有对齐行等长且宽度大于零。
- 每行去掉 `-` 后，逐字符等于对应的清洗后输入。
- 输出只包含允许的碱基字符及 `-`，没有全 gap 列。

通用测试覆盖 FASTA、FASTQ、混合记录、质量值忽略及长度错误、标题重建、kseq 的标记和 CR 语义、gzip、管道、重复标题、清洗规则、已有 gap 拒绝、覆盖保护和写入失败。测试核心故意返回缺行、异长、改写碱基等错误结果或抛出异常，检查框架能否拒绝；内部文件操作替身用于模拟写入、关闭、发布失败和无覆盖竞态。kseq 的独立测试还检查初始化、扩容失败和长度边界。生产程序没有这些测试替身的公开参数或环境变量入口。

参考核心另有小型合成测试，覆盖单条序列、重复序列、前缀/内部/后缀插入、删除、N、T/U、不同插入长度和并列回溯。独立穷举路径用于检查短序列两两最优分数，生成的小型多序列数据用于检查结果属性与可重复性。具体 gap 布局只用于参考核心测试，不能作为所有新算法的唯一正确答案。

CTest 的 `unit` 入口运行 doctest 模块测试，`integration` 入口通过 Python 标准库运行真实子进程、压缩、管道和文件生命周期测试。实际执行结果见 [验证记录](validation.zh-CN.md)。

输入处理用例集中保存在 [输入用例表](../tests/input_cases.py)，每例都有稳定编号、原始字节和独立的预期结果。新增一种输入情况时，可按 [测试矩阵与扩展指南](testing.zh-CN.md) 添加用例，复用文件、标准输入及 gzip 通道检查。失败报告包含用例编号、输入输出通道和错误信息；生成测试使用固定种子，便于复现。

通道一致性检查关注标题、清洗后序列、转换计数和结果约定。不同核心的合法 gap 布局可以不同；压缩输出先独立解压再检查。内部读取操作替身还用于稳定模拟短读、中断和读取、关闭失败，生产命令行没有对应的测试开关。

替换核心后，先运行第 4 节的 Release 构建和完整测试，再运行地址和未定义行为检查：

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

## 8. HAlign-5 来源与许可

设计固定参考 [HAlign-5 提交 b0d5608](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2)，完整提交号为 `b0d5608811d44e30bc323da54f0583ceaaf684d2`。

MSA-Kit 沿用其 CMake、CLI11、字符查表清洗及缓冲读写思路，并采用相同的 kseq 记录读取与 `name/comment` 标题重建方式。本项目从 0.3.0 起使用 C++17。固定版本的 `kseq.h` 保留原 MIT 声明，并记录原始校验值及本地安全补丁。输入 gap 拒绝、位置身份、严格 gzip 校验、标准错误日志和输出完成检查属于 MSA-Kit 明确规定的行为；具体来源和改写范围见 [设计规范](design.zh-CN.md)。

HAlign-5 的 WFA2、KSW2、MinHash、外部 MSA 命令和参考 profile 流程没有进入首版基础模板。HAlign-5 及依赖的版本、来源和许可说明见 [第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)。

MSA-Kit 采用 [MIT 许可](../LICENSE)。复制模板开发新项目时，应保留实际复用内容及第三方依赖的版权、许可和来源声明。
