# MSA-Kit developer guide

**English** | [简体中文](guide.zh-CN.md)

This guide connects [the README's alignment example](../README.md#first-result) to the C++ implementation. Follow the same three records as they become input objects, aligned rows and an output file. The explanations below introduce each interface term where it is used.

MSA-Kit 0.3.0 is a reusable DNA/RNA multiple-sequence alignment template. Keep the CLI, input/output and validation layers and replace the algorithm in `src/align.cpp`. It uses C++17 and the pinned HAlign-5 kseq parser. See the [design](design.md) for the full contract and [validation](validation.md) for measured checks.

## 1. Responsibilities and data flow

```text
CLI -> kseq records, cleaning and input checks -> alignment core
    -> result validation -> FASTA/gzip output
```

The **CLI** interprets command-line arguments such as `-i` and `-o`. **kseq** is the library that turns FASTA/FASTQ bytes into records; **zlib** handles gzip compression. The **core** computes the alignment. The **support library** is the compiled shared code for the surrounding layers, used by both the program and its tests.

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Minimal entry point. |
| `src/cli.cpp` | Arguments, logging, execution flow and exit codes. |
| `src/input.cpp` | File/stdin lifecycle, strict gzip, kseq, cleaning and counts. |
| `src/output.cpp` | File identity, formatting, temporary files and atomic publication. |
| `src/validation.cpp` | Algorithm-independent output checks. |
| `src/align.cpp` | Replaceable reference star core. |
| `src/internal.hpp` | Shared internal types, errors and checked arithmetic. |

The executable and tests link the same support library. kseq is instantiated only in input code. The core uses the standard library, without CLI11, spdlog or zlib headers.

## 2. Core interface and ownership

```cpp
namespace msakit {
struct SequenceRecord { std::string header; std::string sequence; };
struct Alignment { std::vector<std::string> rows; };
Alignment align(const std::vector<SequenceRecord>& input);
}
```

The declaration is in [core.hpp](../include/msakit/core.hpp). Records are nonempty and gap-free after cleaning. Vector position is identity: duplicate titles and duplicate sequences remain separate records. The framework owns the vector throughout the call; the const reference does not copy it. Do not modify input or retain references after returning. Output strings own their storage, and `rows[i]` must correspond to `input[i]`.

### Follow one alignment through the interface

For `examples/tiny.fasta`, the input vector is a list of three records. The C++ index starts at zero:

| Position | `input[i].header` | `input[i].sequence` | Reference result `rows[i]` |
| --- | --- | --- | --- |
| 0 | `center` | `AC` | `A--C` |
| 1 | `q1` | `ATC` | `AT-C` |
| 2 | `q2` | `AGGC` | `AGGC` |

`SequenceRecord` packages a name and a sequence. `Alignment` packages the resulting list of aligned strings; it contains no FASTA `>` lines. The framework retains the names, checks these strings, and writes the final FASTA. Thus the algorithm works with sequences instead of implementing another FASTA parser.

`const` makes the incoming list read-only; `&` passes a reference to the framework's existing list rather than copying it. `rows[i]` uses the same index so the writer can reunite the right name with the right aligned sequence. The table describes this example's computed result; your implementation must compute rows for every input, not return these fixed strings.

Before editing, identify these three points in [align.cpp](../src/align.cpp): `align()` receives the records, `align_pair()` builds pairwise alignments, and the insertion-slot merge constructs the final rows. Start by tracing AC/ATC/AGGC through them. Keep reading and writing in the surrounding modules while you change the calculation.

The core does not parse arguments, access files, handle headers, or write stdout. Throw an exception on failure; the application maps core/allocation/contract failures to exit 5. New algorithm settings belong in the CLI and explicit configuration, not an independent argument parser inside the core.

### Migration from 0.2.0

Replace `std::span<const SequenceRecord>` with `const std::vector<SequenceRecord>&` in declarations, definitions and callbacks; remove unused `<span>` and adapt span-specific operations. Calls remain `align(records)`. Rebuild framework, core and tests together in a fresh directory. Record fields, CLI, ordering, exit codes and reference scoring are unchanged; `--version` reports `0.3.0`.

## 3. Input preparation

Files and stdin accept plain/gzip FASTA, FASTQ and mixed records recognized by the pinned kseq. Compression uses magic bytes, independent of extension. The strict decoder checks truncation, CRC, lengths, concatenated members and trailing garbage. FASTQ qualities are checked for length and discarded.

Titles are kseq `name` plus nonempty `comment`, with a joining space when both exist. This may change original separator whitespace. Marker, preamble, CR and EOF behavior follows the pinned parser; consult [the input contract](design.md#4-input-contract), not the old strict FASTA rules.

After parsing, reject `-` and `.`. Preserve `ACGTUN`, uppercase `acgtun`, and map every other returned sequence byte to N. T/U remain distinct; whitespace and other IUPAC symbols that survive parsing become N. Count uppercase conversions and unknown-byte replacements separately. Result validation uses these cleaned sequences, not the original bytes.

For example, `acgRyUn` becomes `ACGNNUN`. Headers and qualities do not undergo sequence cleaning. Empty returned titles/sequences or no records fail with code 3; physical line numbers are not promised.

## 4. Build and use

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
./build/msakit -i examples/tiny.fasta -o build/tiny.aligned.fasta
python3 -c 'from pathlib import Path; assert Path("examples/tiny.expected.fasta").read_text() == Path("build/tiny.aligned.fasta").read_text(); print("Example: PASS")'
```

Linux, a C++17 compiler, CMake >=3.18 and system zlib development files are required. Bundled headers are not downloaded. Tests need Python >=3.8; `BUILD_TESTING=OFF` builds the executable without Python. Installation is optional with `cmake --install build --prefix <prefix>`.

Input/output paths are required; `-` selects stdin/stdout. Output ending in `.gz` is compressed; stdout is plain FASTA. Default width is 80, `--line-width 0` disables wrapping. `--quiet` retains errors; `--force` allows replacement, but never input/output identity, including existing links. The destination parent must exist. Existing named output is protected on failure; stdout cannot be rolled back. Use `set -o pipefail` when piping and inspect the status.

| Exit | Meaning |
| --- | --- |
| 0 | Completed output or successful help/version. |
| 2 | Parameter error, including same input/output identity. |
| 3 | Invalid returned records, gaps or quality length. |
| 4 | Read/decompression/write error, overwrite refusal or missing parent. |
| 5 | Core, resource allocation or result-contract failure. |

## 5. Replacing the algorithm

1. Build and test the unchanged template.
2. Implement `msakit::align` in `src/align.cpp`, preserving identity and ownership.
3. Keep file/CLI behavior in the surrounding layers.
4. Run the contract tests; add algorithm-specific scoring and edge-case tests.
5. Update or replace reference-only tests if their exact gap layout no longer applies.

For website submission, follow the separate [Benchmark integration tutorial](../benchmark/README.md). Its adapter restores original titles/IUPAC symbols; the normal CLI contract remains the cleaned-sequence contract.

## 6. Reference star alignment

The source is marked **Reference star alignment — replaceable core**. Sequence 0 is the center. Each remaining sequence is globally aligned to it with Needleman–Wunsch. Match scores +1, mismatch -1, each gap -1. Any pair containing N scores 0, including N/N; T/U match for scoring while their output characters stay unchanged.

Tie order is diagonal, consume center only, then consume query only. A center of length L has L+1 insertion slots, including prefix/suffix. Each slot takes the maximum insertion length across queries. Insertions are left-aligned and right-padded; the center receives gaps in slots. Insertions are not realigned to each other.

```fasta
>center
A--C
>q1
AT-C
>q2
AGGC
```

This is the output for center `AC`, q1 `ATC`, q2 `AGGC` in [tiny.fasta](../examples/tiny.fasta). One input returns unchanged. Pairwise time is `O(L × Σmᵢ)` plus construction/validation. Full DP is allocated per query and released; insertion data and the final matrix also occupy memory. Dimensions, bytes, score ranges and output lengths are checked. This reference is for small examples, not a claim of optimal MSA or HAlign-5 scale.

## 7. Checks after replacement

```bash
(cd build && ctest -L contract --output-on-failure)
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

All rows must have the input count and order, equal positive widths, only `ACGTUN-`, exact cleaned residues after removing gaps, and no all-gap column. Different valid algorithms may choose different gaps. `reference` tests constrain only the supplied algorithm; plain CTest runs both groups.

doctest tests modules and reference scoring. Python tests real subprocesses, files, compression and pipes. [Input cases](../tests/input_cases.py) store independent expected values and stable IDs; [the testing guide](testing.md) explains extension and reproduction. Tests include faulty cores, file publication races, read/write/close/allocation faults, kseq limits, all bytes and deterministic short-pair optimal scoring. Fault seams are internal, without production CLI/environment switches. Transport tests compare records and cleaned residues, independently decompressing gzip; they do not require every core to choose identical gaps.

## 8. Sources and licensing

The baseline is [HAlign-5 b0d5608811d44e30bc323da54f0583ceaaf684d2](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2). MSA-Kit adopts its CMake/CLI organization, cleaning-table approach and kseq lifecycle/title reconstruction, with explicit gzip, identity and publication guarantees. WFA2, KSW2, MinHash, profiles and external MSA workflows are omitted. See [third-party notices](../THIRD_PARTY_NOTICES.md) for revisions and licenses. Preserve MIT permissions, upstream copyrights and dependency notices when copying the template.
