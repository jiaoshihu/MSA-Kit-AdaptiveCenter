# Input handling tests

**English** | [简体中文](testing.zh-CN.md)

This guide shows how to turn the result checks used in the [README](../README.md#check-result) into reusable software tests. `contract` means program rules every core must satisfy; `reference` means the supplied algorithm's particular scores and layouts.

## Read an assertion as an alignment question

For the tiny input AC/ATC/AGGC, consider:

```text
                 center    q1      q2
Reference core   A--C      AT-C    AGGC
Another layout   A--C      A-TC    AGGC
```

Both layouts have four columns, preserve each input after removing gaps and have no all-gap column. A contract test allows either; the reference-core test expects the first layout under that core's fixed rules. If q1 becomes AC, a residue was lost and the contract must reject it even if all rows are padded to the same width.

This is why tests separately ask whether data was preserved and whether a particular algorithm followed its scoring/traceback rules. Biological quality is assessed separately using appropriate evidence or reference data.

MSA-Kit's input tests describe what each input means, how it is cleaned or
rejected, and what may happen to output files. They use synthetic bytes and
small sequences. The authoritative behavior remains the
[design specification](design.md); actual runs are recorded in
[the validation report](validation.md).

## Test organization

- [The input-case catalog](../tests/input_cases.py) contains named inputs and
  expected records, cleaning counts, or error record numbers. Expected values do not
  come from running the production parser.
- [The multi-sequence catalog](../tests/msa_cases.py) adds six checked-in FASTA
  files and seven bounded generated datasets, all with at least two records.
- [The integration runner](../tests/integration.py) launches the real program,
  feeds files or pipes, independently parses output, and checks file lifetimes.
- [The I/O module tests](../tests/test_io.cpp) cover all byte values, exact
  buffer boundaries, and deterministic failures at the internal I/O boundary.
- [The kseq tests](../tests/test_kseq.cpp) exercise allocation failures, length
  limits, short reads and read-error propagation in the vendored parser.
- [The core tests](../tests/test_core.cpp) retain independent short-pair scoring
  checks and result validation. Exact star-alignment layouts remain in the
  `reference` group.

The four CTest entries remain `unit.contract`, `unit.reference`,
`integration.contract`, and `integration.reference`. A case identifier or a
parameterized subcase is not a separate CTest entry. Reports distinguish
doctest cases, Python test methods, and parameter combinations.

Production code is organized into seven source-directory files: the entry point,
command line/application flow, input, output, result validation, algorithm, and
one shared internal header. See the [source map](guide.md#1-responsibilities-and-data-flow).
The support library compiles the four framework modules separately. Module and
alternative-core tests link this same library as the executable. Replacing
`src/align.cpp` does not require copying input or output code into a separate
test program. The functional split preserves all existing test cases and
expected answers; no tests merely mirror the number or names of source files.

## C++17 build compatibility

Version 0.3.0 uses C++17 for production code and C++ tests, with compiler
extensions disabled. The core, injected test cores, validation, and output
interfaces accept `const std::vector<SequenceRecord>&`; the caller retains
ownership and no input records are copied by the call. Derived projects using
the old span interface should follow the [migration steps](design.md#migrating-from-020)
and rebuild all targets together.

The acceptance matrix is:

| Configuration | Checks |
| --- | --- |
| GCC 13.1, Release | Full build and all four CTest entries |
| GCC 13.1, Debug with ASan/UBSan | Full build and all four CTest entries under instrumentation |
| GCC 9.5, Release | Full build and the same tests with the older toolchain |
| Contract group and alternative core | Algorithm-independent checks and the common integration runner with a substituted core |
| `BUILD_TESTING=OFF` | Build, installation, version output, FASTA example, and FASTQ through stdin |

Use separate build directories when changing compilers. Select the intended
compiler at initial configuration with `-DCMAKE_CXX_COMPILER=/path/to/g++`.
Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and inspect
`compile_commands.json` to confirm that production and test compilations use
`-std=c++17`, without GNU language extensions. Release and sanitizer command
templates are in the [design document](design.md#9-verification-plan).

This migration preserves existing behavioral expectations and the 13
multi-sequence datasets. Acceptance includes the complete byte-cleaning
checks, short output names and case-sensitive `.gz` selection, temporary-file
cleanup, input ordering, and read-only core inputs. Only language syntax,
function signatures, and version-string expectations change. Record actual
commands, compiler versions, passes, failures, and skips in the
[validation report](validation.md); the matrix above describes required checks,
not their execution status. Choosing C++17 does not by itself establish support
for every older compiler.

## Input matrix

The stable identifiers below appear in assertions or Python subtest contexts.
The catalog contains the exact raw bytes and expected values for each case.

| Behavior | Case family | Expected handling | Layer |
| --- | --- | --- | --- |
| Uppercase, lowercase, N, and mixed T/U | `INPUT-VALID-*`, `INPUT-BYTES-*` | Preserve uppercase and U; uppercase the six accepted lowercase letters; report disjoint counts | Module and integration |
| IUPAC ambiguity, digits, whitespace, NUL, high-bit bytes | `INPUT-VALID-*`, `INPUT-BYTES-*` | Other bytes returned in kseq's sequence become N, including every byte of non-ASCII sequence text | Module and integration |
| Existing dash or dot gaps | `INPUT-ERROR-*`, `INPUT-BYTES-*` | Content error, exit 3; never clean gaps into N | Module and integration |
| Header reconstruction, duplicate names/records, embedded header characters | `INPUT-VALID-004/005/009`, `KSEQ-COMPAT-005/006/007`, `HEADER-LONG` | Rebuild name and comment with one joining space when both exist; preserve duplicate positions; never wrap output headers | Module and integration |
| Multiline sequences, LF/CRLF, empty lines, final line without LF | `INPUT-VALID-007/008`, `KSEQ-COMPAT-008/009`, `INPUT-STRUCTURE-001`, `INPUT-BOUNDARY-001` | Follow pinned kseq line handling, including its terminal-CR behavior | Module and integration |
| Empty returned headers/sequences, or no records | `INPUT-ERROR-*`, `KSEQ-ERROR-005/008/009/010`, `INPUT-KSEQ-002` | Exit 3 with source and a record number where available; no physical-line promise | Module and integration |
| BOM, content before a marker and header indentation | `KSEQ-COMPAT-001/002/003/004` | kseq scans to the first record marker; these formerly rejected fixtures now succeed | Integration |
| FASTQ and mixed FASTA/FASTQ, wrapped sequence and quality | `KSEQ-FASTQ-001` through `006`, `INPUT-KSEQ-001` | Read records in order; ignore quality values; output aligned FASTA | Module and integration |
| Quality strings containing gaps, markers, NUL or high bytes | `KSEQ-FASTQ-004/006`, quality-invariance method | Quality values do not enter the core or affect cleaning counts | Module and integration |
| Short, long or missing quality strings | `KSEQ-ERROR-001/002/003/004/007` | Exit 3; no partial output | Module and integration |
| Line-start @/+, internal markers, terminal bare marker | `KSEQ-COMPAT-010` through `014` | Preserve the fixed parser's record semantics rather than adding a strict pre-parser | Integration |
| Ordinary/gzip contents independent of filename | All valid and content-error catalog families | Content-based detection; four equivalent input transports | Integration |
| Optional gzip headers, header CRC, empty and concatenated members | `GZIP-VALID-*`, FASTQ member-split method | Decode one continuous byte stream; member boundaries inside record fields have no parsing meaning | Integration |
| Damaged gzip header, payload, CRC, size, later member or trailer | `GZIP-ERROR-*`, `GZIP-PREFIX-*` | Exit 4 once gzip magic is recognized | Integration |
| Empty input and one-byte gzip prefix | `GZIP-PREFIX-*` | Ordinary input detection; exit 3 | Integration |
| CRLF, header and sequence boundaries near 16 and 64 KiB | `INPUT-BOUNDARY-*` | Block boundaries and the magic-detection offset do not alter records or counts | Module |
| FASTQ sequences and qualities near 16 and 64 KiB | FASTQ buffer-boundary method | Return the complete sequence; ignore complete quality values | Integration |
| Single-byte/short reads, EINTR, read and close failures | `INPUT-READ-*` | Retry recoverable interruptions; report read/close failure as exit 4 | Module |
| kseq initialization and string expansion failures | `KSEQ-RESOURCE-001/002/003` | Resource failure, exit 5; release partial initialization and retain owners on failed realloc | Parser module |
| Parsed-field and allocation arithmetic limits | `KSEQ-LIMIT-001` | Reject lengths exceeding the supported int range before allocation or narrowing; exit 5 | Parser module |
| Negative callback results in headers, sequence or quality | `KSEQ-READ-001/002` | Propagate read failure instead of reporting EOF or returning a partial successful record; exit 4 | Parser module |
| Case, wrapping, line endings and gzip splits of small generated records | `GENERATED-*` | Preserve expected cleaned records and alignment invariants | Integration |
| Invalid input with stdout, new output, or forced replacement | Input and gzip error families | Empty stdout; no new result; preserve old target and source; no temporary files | Integration |
| Directory supplied as input | `PATH-INPUT-DIRECTORY` | Exit 4; leave directory contents and existing output intact | Integration |

The existing CLI, output-write, publication-race, core-failure, and reference
algorithm tests continue to run alongside this matrix.

The fixed catalog contains **32 valid cases** (12 input cases, 14 kseq
compatibility cases and 6 FASTQ cases), **22 content-error cases**, 8 valid gzip
variants, 13 gzip-error variants, and 26 incomplete prefixes of a small gzip
member. The valid catalog produces 384 input/output combinations; content
errors produce 88 input-path combinations and 80 selected output-lifecycle
combinations. Three focused FASTQ methods add 16 quality-invariance scenarios,
36 gzip-field-split scenarios and 12 large-field boundary scenarios.

These combinations are subcases inside test methods, not separate CTest
entries. A catalog integrity test checks identifier uniqueness and
lifecycle-case references. The 32 small generated groups, 13 multi-sequence
datasets and existing CLI/file/error tests remain additional coverage.

## Input compatibility cases introduced in 0.2.0

These expectations describe the pinned HAlign-5 kseq implementation introduced
in 0.2.0 and retained in 0.3.0. They are explicit fixtures, not results obtained
by calling the production reader.

- Old rejection IDs `INPUT-ERROR-010` through `013` are retired. The same bytes
  now succeed as `KSEQ-COMPAT-001` through `004`: preceding sequence-like text,
  an indented header, a UTF-8 BOM and a comment before the first marker.
- Old `INPUT-ERROR-014` is now the successful FASTQ case `KSEQ-FASTQ-001`.
- `INPUT-VALID-004/005` use rebuilt header expectations. A separator between
  name and comment becomes the joining space; the first separator after an
  empty name is consumed. Remaining comment whitespace is retained.
- `INPUT-VALID-007` now expects final `Tu\r` to become `TU`. Native kseq strips
  a trailing CR from a line result whose accumulated length is greater than
  one, even at EOF. `KSEQ-COMPAT-008` separately checks the initial empty CRLF
  case, whose retained CR becomes N.
- `KSEQ-COMPAT-010/011` check record/quality interpretation of line-start
  markers; `012` checks ordinary marker bytes inside a sequence line. `013/014`
  document that a bare final `>` or `@` can yield no further record.

Diagnostics assert source, error category and determinable returned-record
number. Tests no longer assert the physical line where an error was found.

## Multi-sequence result checks

[The fixed FASTA files](../tests/data/msa/README.md) each contain six synthetic
sequences. Their prepared records are explicitly declared in the multi-sequence
catalog, independently of the program's parser and returned alignment.

| Stable case | Dataset or behavior |
| --- | --- |
| `MSA-FIXED-001` | DNA prefix/internal/suffix insertions, deletion and substitution |
| `MSA-FIXED-002` | RNA and mixed T/U, insertion, deletion and N |
| `MSA-FIXED-003` | Duplicate full headers and sequences, casing and unknown-character cleaning |
| `MSA-FIXED-004` | Divergent sequences and an N-rich center |
| `MSA-FIXED-005` | Input lengths 129, 1, 2, 9, 31 and 64 |
| `MSA-FIXED-006` | Input lengths 79, 80, 81, 159, 160 and 161 |
| `MSA-GENERATED-001` through `006` | Respectively 2, 3, 8, 16, 64 and 128 records; lengths 16–128 |
| `MSA-GENERATED-007` | Four related sequences of lengths 512, 513, 510 and 512 |

The generated MSA datasets use seed **20261027**, separate from the smaller
input-representation generator. All fixtures are synthetic; they are not
biological benchmarks or reference alignments.

Four end-to-end methods cover **127 subprocess scenarios**: 72 fixed-file
transport combinations, 28 generated-data combinations, 12 reversed/rotated
input orders, and 15 output-width combinations. The width checks cover 0, 1, 7,
80 and 81 through stdout and both named-output formats. Positive widths limit
each sequence line; width zero requires one unwrapped line per sequence.

Every result is parsed independently and must preserve expected rebuilt headers, input
positions, record count and cleaned residues, with equal nonzero row widths,
only `ACGTUN-`, and no all-gap column. Reordering inputs is checked against the
new order; the resulting gap layout may change with the selected center.

Two further methods test this independent checker itself: it must accept two
distinct valid layouts and reject 16 malformed outputs plus two width errors.
These include unaligned unequal-length rows, missing/extra/reordered rows,
changed headers, altered/lost/added residues, illegal characters, all-gap
columns and malformed serialization. They do not call the production validator.

With the supplied reference core, every MSA dataset invokes pairwise alignment.
The largest pairwise score matrix in this set is 513 by 514 cells (2,109,456
bytes of scores); each table is released before the next query. These bounded
checks exercise actual alignment without a large-data benchmark. The existing
250,000-character single-sequence stream test remains a separate I/O check.

The [validation record](validation.md) records the executed matrix and the
location of retained earlier input/output files and observed-width tables.
Output widths are observations of the reference core, not golden layouts
imposed on other cores.

## Transport and output checks

Every representative valid catalog case is exercised through four input paths:
plain file, gzip file, plain stdin, and gzip stdin. Each path is combined with
plain stdout, a plain output file, and a gzip output file. The filenames include
spaces and non-ASCII text; some suffixes deliberately disagree with contents.

Success checks inspect the expected rebuilt headers, record order and count, cleaned
sequences after removing gaps, equal row lengths, permitted residue characters,
absence of all-gap columns, and the two cleaning counts. Named outputs must not
leak alignment data to stdout. Python independently decompresses gzip results.

The tests do not require a replacement core to return the same gap layout on
different runs. Only a single-row result, a specified test core, or a
`reference` test has an exact-layout expectation. Compressed bytes are not
compared, because gzip metadata and compression choices are not part of the
public alignment contract.

Header bytes are checked separately from sequence lines. For example, an
embedded CR inside the returned comment is preserved, whereas a CR separating
the name from the comment becomes the joining space. The output checker does
not reject every CR byte globally.

Content errors are checked through all four input paths. Selected failures also
exercise both plain and gzip named outputs, including existing targets passed
with `--force`. A damaged gzip fixture starts from valid FASTA, so the test
isolates the container error rather than relying on precedence between
different simultaneous faults.

## Boundaries and generated inputs

The byte tests insert each value into a controlled FASTA context and check an
independent expectation. LF, CR, and record/quality markers also have separate
context tests, because kseq may interpret them structurally before cleaning.
An internal CR followed by another residue remains a byte to clean; a terminal
CR can be stripped. A `>` inside a sequence line is distinct from a `>` at the
start of a line. These cases are not inferred from the cleaning table.

Eighty module boundary combinations position delimiters around the 16 KiB
kseq and 64 KiB decoding buffers, including the two-byte prefix consumed
during input detection. FASTQ integration checks use sequence and quality
lengths 16,383/16,384/16,385 and 65,535/65,536/65,537. Large fixtures contain only
one sequence, bypassing pairwise dynamic programming. The pre-existing
250,000-character roundtrip also tests compressed data that exceeds a buffer.

Input-operation substitutes deterministically limit reads and inject EINTR or
I/O errors. They do not depend on sleeps or operating-system pipe scheduling.
Close-failure substitutes release the real descriptor once before reporting an
error, matching the Linux ownership assumption used by the implementation.
These substitutes are internal; production CLI and environment variables do
not expose fault-injection controls.

The vendored-parser tests inject failures into each initialization allocation,
each parsing allocation of a long FASTQ record, and individual string growth.
They verify that a failed reallocation preserves the previous owner and
bytes, and that destruction releases all outstanding allocations. Length tests
exercise the `INT_MAX` boundary without attempting a multi-gigabyte allocation.
Read-error tests cover all record regions and both EOF/error distinctions.

Generated tests use seed **20261026** and **32 groups**, each with 1–6 records
and 1–32 residues per record. Expected records are chosen first; casing,
wrapping, LF/CRLF, and gzip-member splits then vary their representation. The
fixed cases remain the specification for edge conditions; generated cases add
combinations. Failures include the seed and case identifier.

gzip fixtures with optional headers are assembled independently in the test
catalog and checked with zlib, including the header CRC. Tiny gzip prefixes
cover every truncation point: fewer than two magic bytes follow ordinary input
detection, while recognized incomplete gzip streams fail as I/O errors.

## Running and extending the tests

From the repository root on Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)

# Reusable contracts after changing an alignment core:
(cd build && ctest -L contract --output-on-failure)

# Verbose subprocess tests, including case context on failure:
python3 tests/integration.py --binary build/msakit \
  --test-driver build/msakit_test_driver --suite contract

# Focus on the I/O and parser case families:
./build/msakit_unit_tests --test-case='*INPUT-*'
./build/msakit_unit_tests --test-case='*KSEQ-*'
```

For memory and undefined-behavior diagnostics:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

To add an input rule or regression:

1. Choose an unused stable identifier in the appropriate catalog family. Keep
   existing identifiers when adding or reordering cases.
2. Supply literal input bytes and independently determined rebuilt headers,
   cleaned sequences and counts, or the documented error code and location.
3. Include only one fault when asserting its error category. For content errors,
   check the affected record where one exists; input with no records has no
   returned-record number to assert. Match diagnostic meaning and source, not
   entire operating-system or zlib error strings or physical line numbers.
4. Use the shared matrix for ordinary inputs. Put precise byte/read boundaries
   in module tests and container-specific cases in the gzip families. Select
   representative file-lifetime checks rather than multiplying every dimension.
5. Run the relevant tests, then the complete Release and instrumented suites
   after fixes. Update this matrix when introducing a new behavior family and
   record actual results in the validation report.

For example, a new successful catalog entry can state its expected values
directly:

```python
InputCase("INPUT-VALID-013", b">example\nacR\n",
          (b"example",), (b"ACN",), uppercase=2, unknown=1)
```

This is an illustration of adding a case, not an additional checked-in fixture.
The input is automatically exercised through the valid-case transport matrix
when the entry is added to `VALID_CASES`, `KSEQ_COMPAT_CASES`, or
`KSEQ_FASTQ_CASES`. The shared `ALL_VALID_CASES` collection includes all three.

No real sequence data, downloads, optional testing packages, or changes to
system settings are needed. The tests establish software behavior, not
biological alignment accuracy or large-data performance.
