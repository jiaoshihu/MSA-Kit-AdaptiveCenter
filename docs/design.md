# MSA-Kit design

**English** | [简体中文](design.zh-CN.md)

This is a reference for exact behavior. For a first hands-on introduction, start with [the README walkthrough](../README.md#what-is-msa), then continue to [the developer guide](guide.md) when you want to modify the algorithm.

This document defines MSA-Kit version 0.3.0. See the
[project overview](../README.md) and [developer guide](guide.md).
Requirements here describe the implemented public contract. Build instructions
are below; measured validation results are recorded in the
[validation report](validation.md).

Here, a **contract** is a rule that any replacement algorithm must satisfy, such as retaining every cleaned input residue. A **reference algorithm** is the particular star implementation shipped with the project. Its scoring and gap choices describe that implementation, while the shared input/output rules apply to every core. The architecture below locates each responsibility in the program.

## 1. Status and scope

**Current stage: implemented source template.** The repository includes the C++
program, reference core, fixed header-only dependencies, synthetic examples,
and tests. Normal configuration and compilation do not fetch dependencies.

MSA-Kit is a reusable C++ template for DNA and RNA multiple sequence alignment.
It separates ordinary application behavior from a replaceable alignment core.
The first core is a small, deterministic star aligner intended for learning,
interface verification, and replacement by another algorithm.

The implementation targets Linux, C++17, and CMake 3.18 or later. Its
supported input is unaligned FASTA or FASTQ, either plain or gzip-compressed, from a file
or standard input. Its output is aligned FASTA, optionally gzip-compressed when
written to a file. Sequences are held in memory during alignment.

Protein alignment, URL downloads, profile input, external alignment
commands, dynamic plugins, and parallel execution are outside the first
implementation. The reference algorithm does not inherit HAlign-5's scale or
performance claims.

The source baseline is [HAlign-5 commit
`b0d5608811d44e30bc323da54f0583ceaaf684d2`](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2).
Section 10 records the provenance and the intended reuse boundaries.

## 2. Architecture

```mermaid
flowchart LR
    A[CLI parsing] --> B[Read, clean, and validate input]
    B --> C[Replaceable alignment core]
    C --> D[Validate alignment result]
    D --> E[Write FASTA or gzip output]
```

| Layer | Responsibility | Implementation location |
|---|---|---|
| Entry point | Invoke the shared application with the selected core | `src/main.cpp` |
| Command line and flow | Options, logging, application orchestration, and error mapping | `src/cli.cpp` |
| Input | Files/stdin, strict gzip decoding, kseq records, cleaning, and statistics | `src/input.cpp` |
| Output | File identity checks, FASTA/gzip serialization, protected writes, and atomic publication | `src/output.cpp` |
| Result validation | Row identity/count, lengths, residues, and column checks | `src/validation.cpp` |
| Core | Align validated in-memory sequences | `src/align.cpp` |
| Internal interface | Types, declarations, fault seams, error categories, and checked arithmetic | `src/internal.hpp` |
| Public interface | Declare the record types and the core entry point | `include/msakit/core.hpp` |

The public types are in [core.hpp](../include/msakit/core.hpp), and the replaceable
algorithm is in [align.cpp](../src/align.cpp). The core has no
dependency on CLI11, spdlog, filesystem paths, or compression. The surrounding
layers own those concerns. There is one selected core implementation in the
executable, rather than a runtime plugin loader. `src` contains seven files:
six implementation files grouped by function and one shared internal header.
The command-line module owns `run_application()` and its core-function injection
point; `main.cpp` only calls it with the selected algorithm. The shared internal header uses
standard-library types and a forward declaration of `z_stream_s`, so including
it in the core does not require zlib, CLI11, or logging headers. The vendored
parser is [extern/kseq/kseq.h](../extern/kseq/kseq.h).

The support library compiles the command-line, input, output and validation
modules separately. The production executable and test drivers link this same
library. kseq is instantiated only in the input implementation; zlib-specific
helpers stay in the input/output implementations. Shared error helpers use only
the standard library. Each implementation includes its own dependencies.
Version 0.3.0 retains this seven-file organization and version 0.2.0's
record, CLI, output, and error semantics. Its core parameter changes to a
read-only vector reference to support C++17, as described below.

The application validates input before calling the core and validates the
complete returned alignment before opening a final output transaction. It keeps
input records available for validation and for writing their rebuilt headers.
No raw-input copy or HAlign-style working-directory tree is required.

## 3. Core interface

The public header exposes the following interface:

```cpp
#include <string>
#include <vector>

namespace msakit {

struct SequenceRecord {
    std::string header;
    std::string sequence;
};

struct Alignment {
    std::vector<std::string> rows;
};

Alignment align(const std::vector<SequenceRecord>& input);

}  // namespace msakit
```

The input vector position is the record identity. `header` contains the title
rebuilt from kseq's `name` and `comment`, without the record marker or line ending
(see section 4). Headers are not unique keys:
duplicate headers and duplicate sequences must remain separate records.
`sequence` contains the cleaned, nonempty sequence, with no gaps. The input
collection must contain at least one record.

The vector is passed by const reference, without copying the collection or its
records. The framework owns the input and keeps it valid for the duration of
the call. The core must not modify input records, retain references into them
after returning, parse command
arguments, read or write files, or write to standard output. Returned strings
own their storage. `rows[i]` corresponds to `input[i]`; the core cannot add,
remove, or reorder records. It returns only aligned strings, leaving all header
handling to the application.

Core failures are reported by exceptions. The application translates a core
exception, allocation failure, or failed result check into exit code 5. It must
not silently select another algorithm or return a truncated alignment.

Before output, the framework checks all of these properties:

1. The result has exactly as many rows as the input has records.
2. Every result row has the same positive length.
3. Removing `-` from row `i` gives exactly `input[i].sequence`.
4. Result characters belong to `A`, `C`, `G`, `T`, `U`, `N`, and `-`.
5. No column contains only gaps.

These checks define valid output, not its biological accuracy. An alternative
algorithm may choose a different valid alignment from the reference core.

### Migrating from 0.2.0

The 0.3.0 interface replaces the old `std::span<const SequenceRecord>` parameter
with `const std::vector<SequenceRecord>&`. Derived projects must update their
core declaration, definition, and custom callbacks to match, remove unused
`<span>` includes, and adapt any span-specific operations. The framework's
internal core-function pointer, validation, and output declarations use the
same const vector reference. Ordinary calls passing the input vector remain
`align(records)`; no copy or wrapper is needed.

Rebuild the framework, custom core, and tests together in a fresh build
directory and run the contract group. The fields in `SequenceRecord` and
`Alignment`, record ordering, reference-algorithm rules, and runtime data
contracts do not change. CLI options and exit codes remain the same;
`--version` reports `0.3.0`.

## 4. Input contract

### Byte streams and record identity

Input is a local file path or `-` for standard input. Compression is detected by
the gzip magic bytes, independent of the filename, including for standard
input. The bytes used to detect compression must still reach the parser or
decompressor. A gzip read detects corruption and premature end of stream.
Concatenated gzip members are supported, including empty members; non-gzip
trailing bytes are rejected with exit code 4 rather than silently discarded.

The byte decoder feeds ordinary or decompressed bytes directly to kseq through
a read callback. The framework calls `kseq_init()`, repeatedly calls
`kseq_read()`, and uses RAII to call `kseq_destroy()`. It does not first parse
records with another parser or re-encode the stream for kseq. FASTA, FASTQ, and
mixed records recognized by the pinned parser are accepted through this path.
FASTQ quality strings are parsed for length consistency, then discarded; they
do not reach the core or output and do not affect cleaning statistics.

Header reconstruction matches HAlign-5: start with `name`; append a nonempty
`comment`; insert one space only when both fields are nonempty. Neither field
is sequence-cleaned. Repeated titles and sequences retain separate input
positions. Original header separator bytes are not preserved exactly.

### Pinned kseq parsing behavior and 0.1.0 compatibility

Version 0.2.0 replaced the strict FASTA parser from 0.1.0; version 0.3.0 retains
that change. Record semantics are defined by the pinned kseq implementation,
including these explicit examples:

| Input context | Parsed behavior before cleaning |
|---|---|
| `>sample\tinfo` | `name` is `sample`, `comment` is `info`; rebuilt title is `sample info` |
| `>sample  info` | The first space separates the name; the remaining leading space belongs to the comment, so the rebuilt title contains two spaces |
| `> sample` | Empty name and comment `sample`; rebuilt title is `sample` |
| `>sample\rinfo` | CR separates the name; rebuilt title is `sample info` |
| `>sample info\rmore` | The CR inside the comment remains part of the title |
| Preamble before the first marker, including a BOM or indentation | Seeking a record skips bytes until `>` or `@`, even if that marker is not at line start |
| `>` or `@` at the beginning of a sequence line | Ends the current sequence and introduces the next record |
| `+` at the beginning of a sequence line | Begins a quality section, including after a `>` header |
| `A>+@C` within a sequence line | All five bytes are sequence content; cleaning gives `ANNNC` |
| Multiline sequence or quality fields | Lines are concatenated by kseq; quality length must match sequence length |
| Sequence `AC\r` at EOF | The trailing CR is stripped; the sequence is `AC` |
| A CRLF blank line before the first residue | Its initial CR is retained in the sequence and subsequently becomes `N` |
| Bare trailing `>` after an otherwise complete record | kseq can report EOF without returning another record; this is not an empty-record rejection |

The line reader removes a trailing CR when its accumulated field length is
greater than one, including at EOF. This is a native kseq rule, not the old
promise that only actual CRLF pairs are removed. Spaces and tabs inside a
returned sequence generally remain bytes to clean. LF-only blank sequence
lines are skipped. Input is not interpreted as Unicode and no separate format
repair or stricter pre-parser is applied.

No returned records, a returned header containing only ASCII whitespace, or a
returned empty sequence causes content error 3. Reported quality-length errors
also map to 3. Errors identify the source and, when determinable, the one-based
record being read. Physical line numbers are no longer part of the contract.
The parser may accept or skip unusual input that the old strict reader rejected;
MSA-Kit only validates the records actually returned by kseq.

The vendored header retains its MIT notice and includes narrowly scoped safety
patches: checked initialization and expansion, temporary-pointer reallocation,
cleanup of partially initialized objects, checked lengths, and propagation of
read errors. Each parsed field is limited to `INT_MAX` bytes because kseq returns
lengths as `int`. Allocation or length failures map to 5; underlying read or
decompression errors retain code 4. Ordinary record semantics are unchanged.

### Sequence cleaning

Cleaning follows HAlign-5's byte-table approach with an explicit unaligned-input
requirement. First reject every `-` or `.` in the sequence bytes returned by
kseq. Then apply this
table to each remaining byte:

| Input byte | Cleaned character |
|---|---|
| `A C G T U N` | Unchanged |
| `a c g t u n` | Corresponding uppercase character |
| Every other byte | `N` |

Only bytes remaining after kseq parsing reach this table. Spaces, tabs, digits,
other IUPAC ambiguity symbols, and other returned sequence bytes become
`N`. Consequently, this is a byte-based
normalization policy, not Unicode sequence interpretation or protein support.
RNA `U` is preserved and is never rewritten as `T` by the input layer.

The application reports separate totals for accepted lowercase letters changed
to uppercase and other bytes replaced with `N`. The counts are disjoint:
existing `N` is unchanged, and `n` counts only as a case conversion. Quiet mode
suppresses the ordinary summary, but never errors. Gap rejection takes
precedence over cleaning: a gap must not be disguised as an unknown residue.

The sequence used by the core and the result validator is the cleaned sequence.
For example, `acgRyUn` becomes `ACGNNUN`. Its raw input spelling is not the
sequence-preservation reference used to validate the alignment.

## 5. Output contract

The result is FASTA with one record for every input record, in input order. The
application writes `>` followed by the saved rebuilt header. FASTQ qualities
are never written. Each aligned row
contains its cleaned input residues and any newly inserted `-` characters.
Duplicate headers and duplicate sequences remain separate output records.

Output uses LF and ends with a newline. Sequence lines are wrapped at 80
characters by default. `--line-width 0` disables sequence wrapping; other
nonnegative widths set the maximum number of characters on each sequence line.
Headers are never wrapped.

A file path ending in the literal suffix `.gz` selects gzip output. Other file
paths select plain FASTA. Output to `-` is always plain FASTA on standard output;
users may pipe it to a compressor. Filename-based output selection is separate
from magic-byte-based input detection.

### Completed file writes

The complete alignment must pass result validation before serialization begins.
For a file destination, create an exclusively owned temporary file in the
destination directory, serialize into it, and explicitly check write, flush,
and close results, including gzip finalization. Publish the completed file only
after those steps succeed. Destructors provide cleanup, not proof of success.

The output parent directory must already exist. The program does not create a
missing parent automatically. Without `--force`, an existing destination is an
error. This no-overwrite rule must also hold if a destination appears between
the initial check and final publication; a check followed by an unconditional
replacement is insufficient. With `--force`, replacement happens only after a
complete temporary file is ready. An unsuccessful run must preserve an existing
destination and remove only temporary files owned by that run.

Input and output must not refer to the same file, even with `--force`. Check
equivalent paths and existing symbolic or hard-link aliases, not merely equal
argument strings. Stream mode cannot reliably detect shell-created aliases;
documentation must not recommend redirecting output onto its input file.

Standard output cannot be rolled back. The program validates the complete
alignment first, but a broken pipe or later write failure can still leave a
partial stream. Detect that failure and return a nonzero status. Only alignment
data is written to standard output during an alignment run. Logs, progress, and
errors go to standard error.

## 6. Command-line interface

The executable name is `msakit`, with this complete first-version
parameter set:

| Parameter | Contract |
|---|---|
| `-i, --input PATH` | Required; local input path or `-` |
| `-o, --output PATH` | Required; output path or `-` |
| `--line-width N` | Nonnegative integer; default 80; 0 disables wrapping |
| `--force` | Permit replacement of an existing output file |
| `--quiet` | Suppress ordinary logs, including normalization summaries; keep errors |
| `-h, --help` | Print help and exit successfully |
| `-v, --version` | Print the program name and version and exit successfully |

Help and version output use standard output and do not require input/output
arguments, read sequence data, or create output files. Unknown options, missing
required options, negative or overflowing widths, and invalid argument
combinations are parameter errors. The first reference core is single-threaded;
there is no `--threads` option or hidden algorithm-tuning option.

Usage examples after building; use `./build/msakit` instead of `msakit` if the
executable has not been installed on `PATH`:

```bash
set -o pipefail
msakit -i input.fasta -o aligned.fasta
msakit -i input.fasta.gz -o aligned.fasta.gz
msakit -i reads.fastq.gz -o aligned.fasta
cat input.fasta | msakit -i - -o - > aligned.fasta
gzip -c input.fasta | msakit -i - -o - | gzip > aligned.fasta.gz
```

The Bash `pipefail` setting makes a failure in an earlier pipeline stage visible
in the pipeline's exit status. Check that status: a failed pipeline can still
leave a compressed partial file created by shell redirection, so file existence
does not establish success. The program's own file-output transaction applies
only when a named output is passed through `-o`.

| Exit code | Meaning and examples |
|---|---|
| 0 | Alignment written successfully, or successful help/version request |
| 2 | CLI error, including invalid width or input/output referring to the same file |
| 3 | Invalid returned records, no records, existing gaps, or reported FASTQ quality-length errors |
| 4 | Read, decompression, or write failure; missing/unreadable input; missing output parent; overwrite refusal |
| 5 | Core exception, resource allocation failure, or invalid returned alignment |

Do not rely on CLI11's native error codes as the public exit-code contract;
translate parser failures to code 2. Missing or unreadable files and corrupted
or truncated gzip streams belong to code 4. A zero exit status requires a
successfully completed write, not merely a completed alignment calculation.

## 7. Reference star alignment

Mark the implementation entry and its documentation with:

**Reference star alignment — replaceable core**

The first input sequence is the center. Align each remaining sequence against
that same center with global Needleman–Wunsch and linear gap penalties. A single
input record is returned unchanged as a one-row alignment.

### Pairwise scoring and deterministic traceback

For center `c` and query `q`, substitution scoring is defined in this order:

1. If either character is `N`, score 0, including `N` against `N`.
2. If characters are equal, or are the pair `T`/`U`, score +1.
3. Otherwise score -1.

Every gap character scores -1, including terminal gaps. There is no local
clipping, free-end gap, affine-gap option, or reverse-complement search. T/U
equivalence changes only scoring; the aligned strings retain their original
cleaned `T` or `U` characters.

Let `i` and `j` be the numbers of consumed center and query characters:

```text
D(0, 0) = 0
D(i, 0) = -i
D(0, j) = -j
D(i, j) = max(
    D(i-1, j-1) + substitution(c[i-1], q[j-1]),
    D(i-1, j) - 1,
    D(i, j-1) - 1
)
```

Start traceback at the final cell. Equal-score choices use the following order:
diagonal (consume both), up (consume center only, insert a query gap), then left
(consume query only, insert a center gap). Apply the same definitions at matrix
boundaries where only one move is possible. Reverse the collected traceback
strings before extracting insertion slots. For center `A` and query `AA`, this
tie rule produces `-A` and `AA`.

### Merging pairwise alignments

A center of length `L` has `L+1` insertion slots. Slot `s` is the position after
consuming `s` center residues: slot 0 is the prefix, slot `L` is the suffix.
For each pairwise alignment, collect the query character or gap at each center
residue and the query insertion string in each slot, preserving residue order.

Set each slot's width to the maximum insertion-string length across queries.
Write the output in this order: slot 0, center position 0, slot 1, center
position 1, and so on through the final slot. A query's insertion string is
left-aligned in its slot and padded on the right with gaps. The center contains
gaps throughout every insertion slot and is written exactly once.

For example, these synthetic sequences give the following reference result:

```text
Input                  Aligned rows
center  AC             center  A--C
q1      ATC            q1      AT-C
q2      AGGC           q2      AGGC
```

Insertion strings sharing a slot are not aligned against one another. This is
a deterministic teaching merge, not a claim of optimal insertion homology or
optimal multiple sequence alignment.

### Cost and failure boundaries

Let `n` be the sequence count, `L` the center length, `m_i` each query length,
and `W` the final alignment width. Pairwise calculation costs
`O(L × sum(m_i))` time; construction and validation add `O(n × W)`. Process
queries sequentially and release each pairwise DP table before the next one.
The largest full DP table occupies `O((L+1) × (max(m_i)+1))` space; inputs,
stored pairwise/slot information, and the final `O(n × W)` matrix also consume
memory. Do not describe DP space alone as total memory use.

Use a signed score representation. Check matrix-dimension multiplication,
score bounds, and output-length accumulation before allocation or arithmetic
can overflow. Resource exhaustion is an explicit failure, not permission to
truncate data or switch scoring or algorithms. This reference core is intended
for small demonstrations and contract tests.

## 8. Replacing the core

A developer replacing the supplied core should:

1. Copy or clone the project and build its unchanged reference implementation.
2. Read the public interface and result conditions in section 3.
3. Replace the marked `align` implementation in `src/align.cpp`, retaining
   the public declaration. Additional private algorithm helpers may be added;
   update the build inputs if extra translation units are introduced.
4. Keep CLI parsing, input cleaning, header handling, output writing, and result
   validation in the shared layers.
5. Run the common contract tests with `(cd build && ctest -L contract --output-on-failure)`, add algorithm-specific tests, and update the
   algorithm description and dependency notices.

The replacement may choose another center, scoring system, or alignment
strategy. It must preserve row-to-input correspondence and cleaned residues.
Common tests must not require its gap layout to match the reference star core.
Reference-core-specific expected alignments are tests of that implementation,
not constraints on every future algorithm. CTest labels these groups `contract`
and `reference`; the unfiltered test command runs both. New public parameters or input
semantics require an explicit extension of the shared interface documentation;
they must not be introduced by parsing arguments inside the core.

## 9. Verification plan

All tests use small synthetic fixtures. Module tests use doctest, and integration
tests launch the actual executable through Python's standard library. The
following table describes coverage; the validation report records actual runs.

The [input testing guide](testing.md) maps individual input rules to stable case
identifiers, cross-channel checks, and instructions for reproducing failures.
The shared input-case catalog contains expected records and conversion counts
independently of the production parser. Generated inputs use a fixed seed.

| Area | Required scenarios |
|---|---|
| Kseq records | FASTA/FASTQ/mixed records, multiline sequences and qualities, quality-length errors, no final newline, empty returned records, preamble, marker and CR behavior |
| Identity | Name/comment reconstruction, retained comment bytes, duplicate headers, duplicate sequences, original order |
| Cleaning | Accepted lowercase letters, ambiguity symbols, spaces/tabs, disjoint conversion counts, retained U, rejection of both `-` and `.` |
| Compression and streams | Plain and gzip files, both stdin forms, magic detection independent of suffix, truncated/corrupt gzip, plain stdout, gzip output |
| Serialization | Width 80, another positive width, width 0, LF and final newline, headers never wrapped |
| CLI and failures | Help/version without data, missing/unknown options, invalid widths, all exit-code categories, quiet mode, stderr-only diagnostics |
| File lifecycle | Existing output with/without force, same-file path/symlink/hard-link aliases, missing parent, write/flush/close failure, broken pipe, preservation of an existing target on failure |
| Core contract | Substitute test cores returning too few rows, unequal widths, reordered distinct sequences, altered residues, extra characters, and all-gap columns |
| Reference core | One sequence, identical sequences, prefix/internal/suffix insertions, deletion, different insertion lengths in one slot, N, T/U, fixed ties, repeatability |
| Resource boundaries | Checked dimensions and lengths, kseq initialization/expansion failure and cleanup, read-error propagation, no successful partial result |

The build target is `msakit`; its executable is `build/msakit` for the
single-configuration build below. Tests are registered with CTest and use
doctest. From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
./build/msakit --help
```

An instrumented Debug build uses the same tests:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

Tests additionally require Python 3.8 or later; configure with
`-DBUILD_TESTING=OFF` to build without Python or test executables. The program
does not require Python at runtime. The `msakit_support` target contains the
shared CLI, I/O, and validation layers; `msakit_core` contains the replaceable
reference implementation; `msakit` links both.

Production and C++ test targets use C++17 with compiler extensions disabled.
The compatibility acceptance matrix includes GCC 13.1 Release, GCC 13.1 Debug
with ASan/UBSan, and GCC 9.5 Release. Each uses a separate build directory and
the same behavioral assertions. Actual compiler commands and results belong
in the [validation report](validation.md); choosing C++17 alone does not
establish support for every older compiler. See the
[testing guide](testing.md#c17-build-compatibility) for the remaining build and
installation checks.

File-operation fault injection is confined to internal interfaces and test
executables. It covers short/interrupted reads and writes, read/close failures,
gzip finalization, publication, and allocation errors without adding public
testing switches. Normal builds use the same code paths with real system calls.
Tests also check independent exhaustive short-pair optimal scores and verify
the reference algorithm's deterministic examples. Structural validity remains
separate from biological accuracy, and synthetic tests are not a performance
benchmark.

Keep documentation links, parameter and type names, cleaning examples, and exit
codes synchronized with implementation. The actual commands and environment
used for acceptance are in [validation.md](validation.md).

## 10. HAlign-5 provenance and dependencies

All source links below are pinned to the baseline commit rather than a moving
branch. The byte-cleaning table is adapted from that revision; the surrounding
implementation follows MSA-Kit's public contract. CLI11, spdlog, and doctest
headers are copied unchanged from the pinned snapshot. The kseq header has the
documented allocation, length, and read-error safety patches; the original and
modified checksums are recorded separately in the dependency manifest.

| HAlign-5 source | MSA-Kit treatment |
|---|---|
| [CMakeLists.txt](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/CMakeLists.txt) | Retain CMake organization while using C++17 in MSA-Kit; make the dependency set small and zlib required; use a common CTest integration |
| [include/config.hpp](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/config.hpp) | Adapt CLI11 registration and logging organization; keep only the documented general options and send logs to stderr |
| [include/utils.h](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/utils.h) | Adapt byte-table cleaning; reject pre-existing gaps; keep rebuilt headers and positional identity instead of header-keyed maps |
| [src/utils/seq_io.cpp](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/src/utils/seq_io.cpp) | Use the same kseq lifecycle and name/comment header reconstruction; feed a stricter gzip decoder through a callback and retain MSA-Kit's completed-write checks |
| [include/kseq.h](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/kseq.h) | Vendor the fixed parser with its MIT notice; patch allocation, length checks, cleanup, and error propagation without changing normal record semantics |
| [src/utils/preprocess.cpp](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/src/utils/preprocess.cpp) | Keep a separate preparation stage; omit consensus selection, downloads, work-directory trees, and external tool invocation |
| [include/align.h](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/align.h) | Replace algorithm-specific interfaces with the small in-memory API; do not carry over WFA2/KSW2, sketches, profiles, or SAM intermediates |
| [test/test_read_fasta.cpp](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/test/test_read_fasta.cpp) and [test/test_fasta_writer.cpp](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/test/test_fasta_writer.cpp) | Adapt small reader/writer test patterns; create isolated synthetic fixtures rather than inherit the entire existing test workload |

The dependencies are bundled CLI11 2.6.1 for argument parsing, spdlog 1.16.0 for
diagnostics, doctest 2.4.12 for tests, the pinned kseq header for record parsing,
and the required system zlib library for gzip. The reference alignment itself uses the C++
standard library. No external MSA executable, OpenMP runtime, WFA2, KSW2,
MinHash, cereal, or libcurl is required for this template. The source revision,
license sources, and SHA-256 for each bundled file are recorded in
[extern/provenance.json](../extern/provenance.json).

MSA-Kit adopts the [MIT license](../LICENSE). HAlign-5 is also distributed under
the [MIT license](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/LICENSE),
with the notice `Copyright (c) 2026 HAlign-5 contributors`. Preserve that notice
and permission text in copied or substantially adapted code. Third-party
libraries retain their own licenses and notices; the MSA-Kit MIT license does
not replace them. [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) identifies
the adapted code, copied headers, upstream revisions, and retained license files.
