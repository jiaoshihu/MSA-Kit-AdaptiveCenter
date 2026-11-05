# EasyMSA 网站评测接入

[English](README.md) | **简体中文**

本页把 MSA-Kit 的比对程序接入网站评测：你的程序接收未比对序列、插入 gap、写出 aligned FASTA；网站检查文件并测量程序。[README 示例](../README.zh-CN.md#first-result) 展示同样的本地输入输出过程。

## 先分清自己要提供和拿到的文件

| 阶段 | 示例内容或产物 | 谁负责产生 |
| --- | --- | --- |
| 输入 FASTA | center: AC、q1: ATC、q2: AGGC，各有自己的 FASTA 标题 | 平台数据集或你的本地测试文件。 |
| 对齐 FASTA | 附带核心得到 center: A--C、q1: AT-C、q2: AGGC | 你的一次 run.sh 调用和比对程序。 |
| 评测成绩 | 时间、峰值内存、归一化 SP 和可用的 Q/TC | 平台完成运行、校验和评分后生成。 |

表里的冒号是记录内容的简写，实际文件按 README 所示使用 `>` 标题行。将对齐文件写到脚本第二个参数指定的位置，失败返回非零；不要用成绩表代替 FASTA，也不需要自己编写评分器。

**接口预检**用小输入检查调用方式和输出是否合法；**Benchmark 运行**在数据集集合上测量程序；**评分**评估合法的对齐结果，其中 Q/TC 需要参考比对。这些是不同阶段，排错时先看任务停在哪一步及对应状态。

## 先看清评测流程

```text
你的仓库 -> build.sh -> run.sh 运行小样本 -> 校验输出
         -> 各数据集运行 -> 平台评分 -> 结果页
```

build.sh 只负责一次构建；run.sh 每次收到一个序列文件，写出 aligned FASTA。文件校验和指标计算由平台分别完成。预检通过表示接口在小样本上工作，大型数据仍可能超时或内存不足。

想即时查看结果页，可在 [提交页](https://easymsa.cn/#/benchmark/submit) 选择 MSA-Kit 示例，复用已有成绩，不启动新的评测。

网站入口：[EasyMSA — 提交评测](https://easymsa.cn/#/benchmark/submit)。
提交公开 GitHub 仓库后，网站会固定提交版本，依次执行构建、小样本接口预检和正式数据集评测。
**基于本项目开发时，保留下面四个文件，主要修改 `src/align.cpp` 即可。**

```text
your-repository/
├── CMakeLists.txt
├── src/
├── include/
├── extern/                  # 已随仓库提供的离线依赖
└── benchmark/
    ├── build.sh             # 网站调用的构建入口，必需
    ├── run.sh               # 网站调用的运行入口，必需
    ├── adapter.py           # 本模板的标题、模糊碱基保真适配器
    └── test_adapter.py      # 适配器自测
```

网站要求前两个脚本位于**仓库根目录的 `benchmark/` 下**。`adapter.py` 和测试文件是本模板的实现选择；
自己的程序可以使用其他实现，只要遵守下面相同的接口。所有脚本、源码和依赖都需要提交到 GitHub，不能只存在于本机。

## 1. 构建入口：`benchmark/build.sh`

网站从仓库根目录执行：

```bash
bash benchmark/build.sh
```

本项目的完整脚本如下：

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-benchmark --parallel 2
```

成功后应生成 `build-benchmark/msakit`，退出码为 `0`。网站保存构建产物，供后续每组数据使用，
所以不要在 `run.sh` 中重复编译。构建失败应返回非零，不能用 `|| true` 隐藏错误。

- 构建在 Linux 容器内执行，提供 C++／CMake、Python 和 Bash 工具链；本项目使用 C++17、CMake 和 zlib。
- **构建、运行阶段均断网。** 不在脚本中执行 `apt`、联网 `pip install`、`git clone`，或依赖 CMake 在线下载。
  依赖应由平台镜像提供，或作为普通文件随仓库提交。不要依赖 Git 子模块或 Git LFS 的额外下载。
- 不提交本机生成的可执行文件作为唯一构建方式；不要写死本机目录、用户名或系统专属路径。
- Python 等无需编译的项目也应提供 `build.sh`，可以执行离线准备检查后成功退出。
- Shell 文件使用 LF 换行。平台通过 `bash` 调用，不要求依赖可执行位；保留 shebang 便于本地直接运行。

## 2. 运行入口：`benchmark/run.sh`

网站按下面的**四个位置参数**调用，顺序固定：

```bash
bash benchmark/run.sh INPUT_FASTA OUTPUT_FASTA THREADS SEQUENCE_TYPE
```

| 参数 | 含义 | 脚本应如何使用 |
| --- | --- | --- |
| `$1` | 未比对 FASTA 的绝对路径 | 只读，不修改输入。 |
| `$2` | aligned FASTA 的目标绝对路径 | 将完整结果写到这里；不要写到固定文件名。 |
| `$3` | 允许使用的 CPU／线程额度，正整数 | 多线程程序将其传给线程参数；不得超过额度。 |
| `$4` | `dna` 或 `rna` | 选择对应模式；只声明程序实际支持的类型。 |

**路径由平台提供，不能假定一定是 `/outputs/`。** 例如预检可能传入
`/inputs/input.fasta` 和 `/work/output/aligned.raw.fasta`；这只是路径示例。
始终引用 `"$1"`、`"$2"`，以便正确处理含空格的路径。

本项目的完整脚本如下：

```bash
#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 4 ]; then
  echo 'Usage: benchmark/run.sh INPUT OUTPUT THREADS dna|rna' >&2
  exit 2
fi
cd "$(dirname "$0")/.."
exec python3 benchmark/adapter.py "$1" "$2" "$3" "$4"
```

成功时目标文件必须已经写完并关闭，脚本返回 `0`；失败返回非零。日志写到标准错误，不能混进 FASTA。
不要启动后台进程后提前返回。输入和构建产物按只读使用，运行临时文件应放在平台提供的可写工作区；
本适配器将临时目录建在 `$2` 的父目录，结束后清理。输出是普通、未压缩的 aligned FASTA。

参考星比对是单线程实现，接受 `$3` 作为额度，并不会因此变为多线程。
若替换成其他程序，需在适配器调用该程序时显式传入线程数。

## 3. 输出必须满足什么条件？

网站按**完整 FASTA 标题**匹配记录，不能只保留第一个空格前的名字。
例如 `>sample strain_A description` 和 `>sample\tstrain_B` 是不同记录，
即使第一个单词都为 `sample`，也不能覆盖或合并。建议原样恢复标题的空格、制表符及描述。

| 检查项 | 要求 |
| --- | --- |
| 记录身份 | 输出与输入的完整标题集合一致，每条记录恰好出现一次；不得增删或合并记录。 |
| 对齐形状 | 所有输出序列等长，非空；使用 `-` 表示新增 gap。 |
| 序列保真 | 每条输出去掉 `-` 后与相应输入一致，按大小写不敏感比较。 |
| DNA／RNA 字符 | 保留原始 IUPAC 模糊碱基；不把 `R/Y/...` 永久改成 `N`，不把 `U` 转成 `T`。 |
| 输出格式 | 只有合法 FASTA 标题和序列；不混入日志、统计表或 FASTQ 质量值。 |

平台允许按标题匹配重排后的行；本模板会恢复为原输入顺序，并额外拒绝全 gap 列。
重复序列必须作为各自独立记录保留。MSA-Kit 普通 CLI 允许重复标题，而当前网站输入策略要求完整标题唯一，
不要将两者混为一套规则。

## 4. 为什么 MSA-Kit 需要 `adapter.py`？换算法后改哪里？

普通 MSA-Kit CLI 会将 IUPAC 模糊字符清洗成 `N`，kseq 也可能重新拼接标题中的空白。
直接在运行脚本里调用 `msakit -i "$1" -o "$2"`，可能生成结构合法却无法通过网站原序列保真检查的结果。

已有的 [适配器](adapter.py) 按以下顺序处理：

1. 保存完整原始标题和序列，为各记录生成唯一短 ID。
2. 仅向核心传入短 ID，将 `ACGTUN` 以外的合法 IUPAC 字符暂时映射为 `N`；T／U 保留。
3. 调用 `build-benchmark/msakit`，等待退出。
4. 先核对短 ID、行长及去 gap 后的**清洗后序列**，拒绝核心改写、漏行等错误。
5. 按每个非 gap 字符在原序列中的位置恢复模糊碱基和完整标题，再写出最终文件。

只替换 `src/align.cpp` 时，这四个接入文件通常无需修改。若更改可执行文件名或位置，
同步修改 `build.sh` 和 `adapter.py` 中的 `build-benchmark/msakit`；若改用其他 CLI，
修改适配器 `run()` 中的 `subprocess.run(...)` 参数及线程传递。
保留“先校验核心输出，再恢复原字符”的顺序，不能直接用原序列覆盖错误输出来绕过检查。
这项 IUPAC→N 处理属于算法输入预处理，应在方法说明中写明；能原生保真处理 IUPAC 的程序可自行实现直连接口。

## 5. 提交前在 Linux 本地自测

先从仓库根目录运行：

```bash
# 尤其是从 Windows 编辑后，先统一 Shell 脚本为 LF 换行。
python3 -c 'from pathlib import Path; files = [Path("benchmark/build.sh"), Path("benchmark/run.sh")]; [p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n")) for p in files]'
bash -n benchmark/build.sh benchmark/run.sh
bash benchmark/build.sh
python3 benchmark/test_adapter.py
```

再复制下面整段命令，检查接近网站预检的 DNA／RNA 输入。它包含相同首单词的不同完整标题、
制表符、IUPAC 字符和多行序列；每次使用新的输出路径，避免旧文件造成误判。

```bash
python3 - <<'PY'
from pathlib import Path
import subprocess
import tempfile
from benchmark.adapter import read_records

for kind, base in [('dna', 'T'), ('rna', 'U')]:
    with tempfile.TemporaryDirectory(prefix='msakit-smoke-') as directory:
        directory = Path(directory)
        source, output = directory / 'input.fasta', directory / 'aligned.fasta'
        source.write_text(
            '>sample strain_A description\nACG' + base + '\nRYSWKMBDHVN\n'
            '>sample\tstrain_B\nAGN\n>third record\nACG\n', encoding='utf-8')
        subprocess.run(['bash', 'benchmark/run.sh', str(source), str(output), '1', kind], check=True)
        before = read_records(source)
        after = read_records(output, aligned=True)
        assert [(title, row.replace('-', '')) for title, row in after] == before
        assert len({len(row) for _, row in after}) == 1
        assert not any(all(row[i] == '-' for _, row in after) for i in range(len(after[0][1])))
        print(kind + ': PASS')
PY
```

预期看到适配器测试成功，以及 `dna: PASS`、`rna: PASS`。接着可从网站 Benchmark 的“下载数据”
获取真实 FASTA，先选较小的数据集测试，再逐步增加规模。小样本通过说明接口可用，
不能保证参考星比对在大型数据集上不超时或不耗尽内存。

## 6. 在网站提交与查看结果

1. 将修改后的源码、离线依赖及 `benchmark/` 文件提交并推送到自己的公开 GitHub 仓库。
2. 打开[提交评测](https://easymsa.cn/#/benchmark/submit)，填写仓库链接；在更多设置中选择实际支持的 DNA／RNA，必要时填写版本。
3. 网站解析为固定 commit 后创建任务。后续再推送代码不会改变这次任务；要评测修复后的代码，应另提交新任务。
4. 通过构建和对应类型的小样本预检后，网站运行正式数据集；未声明支持的类型显示“不支持”。
5. 保存结果链接或恢复文件。当前浏览器的“最近评测”可找回任务；邮箱可选，用于接收任务链接。

当前用户评测每组运行一次，CPU 上限为 4、内存上限为 32 GiB，构建和单组比对限时均为 30 分钟；
每个评分器也单独限时 30 分钟。临时空间 8 GiB、单元输出 1 GiB、日志 8 MiB、进程数 512。
实际条件以任务实验详情为准。不要在接入脚本里自行算 Q／TC、下载参考答案或输出评分 JSON；
平台独立校验和评分，你的运行入口只负责生成比对 FASTA。

## 7. 常见失败如何排查

| 现象／错误类别 | 优先检查 |
| --- | --- |
| `missing_entrypoint` | 当前提交中是否真的包含根目录下的 `benchmark/build.sh` 和 `benchmark/run.sh`；注意大小写。 |
| `build_failed` | 构建日志中的首个错误；是否在线下载依赖、依赖本机路径、使用容器不支持的工具链。 |
| `precheck_failed` | 先运行上面的小样本；检查完整标题、IUPAC、RNA 的 U，以及是否写到 `$2`。 |
| `title_mismatch`／`duplicate_title` | 工具是否截断标题、合并记录或更改 ID；检查短 ID 映射及标题恢复。 |
| `sequence_changed` | 是否把模糊碱基改为 N、U 改为 T，或删改了非 gap 字符；检查恢复前的核心输出。 |
| `unequal_lengths`／输出无效 | 是否返回了等长 aligned FASTA，而不是输入原样复制、空文件或部分结果。 |
| `Permission denied`／只读文件系统 | 是否试图修改源码目录、输入目录，或向固定绝对路径写缓存；临时文件改放输出父目录等可写工作区。 |
| `bad interpreter`／`$'\r'` | Shell 脚本可能使用 CRLF；转换为 LF 后重新提交。 |
| 超时／内存超限 | 核对算法复杂度和线程参数，先用较小数据复现；检查是否仍在使用完整 DP 的参考星比对。 |
| 评分失败 | 先确认比对输出校验通过，再查看任务的评分状态；评分阶段失败与程序构建失败分别处理。 |
