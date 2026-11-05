# MSA-Kit 0.3.0 validation

**English** | [简体中文](validation.zh-CN.md)

The C++17 migration was built and tested on the Linux server on 2026-10-27. This record describes newly executed checks for production, replacement-core and runtime-only configurations, starting from commit `a880fa7787e5a603c7b5db62924a972d8681e03e`. Version 0.3.0 changes the public core parameter to `const std::vector<SequenceRecord>&`; derived cores must update their signatures and rebuild with the framework. The seven-file layout and input/output behavior remain unchanged.

## Environment and source

| Item | Observed value |
| --- | --- |
| Platform | Linux 6.8.0-136-generic, x86_64, glibc 2.35 |
| Compilers | GCC 13.1.0 and GCC 9.5.0 |
| Language mode | `-std=c++17`, required standard, compiler extensions disabled |
| CMake | 3.26.4 |
| System zlib | 1.2.11 |
| Test interpreter | Python 3.10.12 |
| Bundled headers | CLI11 2.6.1; spdlog 1.16.0; doctest 2.4.12; pinned kseq with safety patches |
| Installed version | `msakit 0.3.0` |

The HAlign-5 baseline is `b0d5608811d44e30bc323da54f0583ceaaf684d2`. The [dependency manifest](../extern/provenance.json) records the original and distributed kseq hashes and its local modifications. All 138 listed dependency header and license hashes were verified.

`src` contains exactly `main.cpp`, `cli.cpp`, `input.cpp`, `output.cpp`, `validation.cpp`, `align.cpp`, and `internal.hpp`. The core, callback, validation and output signatures now use the same read-only vector reference without copying input records. The remaining compatibility edits replace string suffix/prefix helpers and constant-expression array filling with C++17 forms. Reference scoring, traceback and insertion-slot merging are unchanged. Dependencies, input/sequence fixtures and algorithm assertions are unchanged; test edits are limited to callback signatures, prefix checks and the expected version string.

Every entry in the three test builds' `compile_commands.json` uses exactly `-std=c++17`: 11 compilation units per build. The runtime-only build was checked independently. The core target still has no third-party include directories. These are actual GCC 9.5.0 and GCC 13.1.0 results, not a claim that every compiler with a C++17 option is supported.

## Completed builds and tests

| CTest entry | Cases or methods | GCC 13.1 Release | GCC 13.1 Debug + ASan/UBSan | GCC 9.5 Release |
| --- | ---: | --- | --- | --- |
| `unit.contract` | 29 doctest cases | Passed | Passed | Passed |
| `unit.reference` | 8 doctest cases | Passed | Passed | Passed |
| `integration.contract` | 63 Python methods | Passed | Passed | Passed |
| `integration.reference` | 2 Python methods | Passed | Passed | Passed |
| Total | **102 cases/methods** | **4/4 entries passed** | **4/4 entries passed** | **4/4 entries passed** |

The 37 doctest cases performed 8,863 assertions in each build: 3,888 contract and 4,975 reference assertions. Python methods include parameterized subcases. No Python methods were skipped. Doctest's skipped counts refer to the complementary contract/reference filter; every case was executed in the full run.

CTest took 8.21 seconds for GCC 13.1 Release, 39.36 seconds for GCC 13.1 with sanitizers, and 8.58 seconds for GCC 9.5 Release. These are test-suite durations, not alignment performance measurements. All three configurations used new directories and reported zero failed tests. Compilation and tests completed on the first attempt, and the instrumented run reported no sanitizer errors. Existing behavior assertions and expected alignment results were retained.

Additional completed checks:

- `ctest -L contract --output-on-failure`: 2/2 entries passed.
- The complete Python contract suite ran with the existing alternative core, which right-pads sequences instead of using star alignment: all 63 methods passed.
- A fresh GCC 13.1 `BUILD_TESTING=OFF` configuration built the production targets and installed the executable under an isolated build-directory prefix. No Python discovery occurred. The installed executable reported `msakit 0.3.0`, reproduced `examples/tiny.expected.fasta`, and converted FASTQ stdin `@one description\nacTu\n+\n!!!!\n` to `>one description\nACTU\n`.
- Seven relative output names were checked separately in each of the three builds: `x`, `gz`, `.g`, `.gz`, `x.gz`, `x.GZ`, and `x.gzx`. All 21 scenarios passed. Only `.gz` and `x.gz` were compressed; Python independently decompressed them. These checks exercise genuinely short path arguments, exact suffix and case sensitivity, unchanged input, quiet output, and temporary-file cleanup. The retained runner and `short-output-results.json` record these additional scenarios; they are not included in the CTest case counts.
- Documentation links, dependency hashes, the seven-file source layout, and matching local/server source copies were checked after implementation.

## Input behavior exercised

The [testing guide](testing.md) maps behavior to stable IDs and explicit expected records. Expected answers are not generated by the production parser.

- 32 valid catalog cases use four input paths and three output paths: 384 combinations. They cover FASTA, FASTQ, mixed records, wrapped sequences/qualities, header reconstruction, preambles, CR/EOF, duplicate records and byte cleaning.
- 22 content-error cases use four input paths: 88 combinations. Selected failures add 80 named-output lifecycle combinations, checking no new output, unchanged existing output under `--force`, unchanged source and no temporary files. Other error cases also check empty stdout.
- Focused FASTQ methods add 16 quality-invariance, 36 gzip-field-split and 12 sequence/quality boundary combinations.
- Existing gzip tests retain 8 valid variants, 13 corrupted variants and all 26 incomplete prefixes of a small member. They cover optional headers, header CRC, concatenation, empty members, payload/trailer corruption and garbage. Recognized gzip errors return 4; complete streams with invalid returned records return 3. Zero/one-byte magic prefixes remain ordinary input.
- Module tests cover all 256 byte values in sequence context, structural markers and CR separately, and 80 boundaries around the 16 KiB kseq and 64 KiB decoder buffers. Deterministic short reads, EINTR, read errors and checked-close failures cover FASTA and FASTQ.
- Six dedicated kseq cases cover partial initialization cleanup, every parsing allocation of a long FASTQ record, failed realloc ownership, length/arithmetic limits, errors at each byte of a small record, and short reads. Allocation faults return the parser resource status, mapped by the framework to exit 5. Length checks do not attempt huge allocations.
- The 32 generated input groups retain seed 20261026 and bounded record sizes. Existing CLI, output publication, link/same-file, faulty-core and reference-algorithm checks continue to run.

All 13 multi-sequence datasets are retained: six fixed sets with six records each and seven generated sets using seed 20261027, up to 128 records. Their 127 transport/order/wrapping scenarios passed in all three builds and with the alternative core. The independent checker verifies rebuilt headers, record order/count, exact cleaned residues after removing gaps, equal nonempty row widths, allowed characters and no all-gap column. Its self-tests accept two distinct valid layouts and reject 16 malformed outputs plus two serialization-width errors.

These are synthetic software tests. The largest score table in the MSA set has 263,682 cells, approximately 2.1 MB of scores. Large stream fixtures contain only one sequence and avoid pairwise DP.

## Retained pinned-parser comparison

During the preceding kseq migration, a separate bounded comparison instantiated the original pinned kseq and the patched header on identical streams. Seed 20261026 generated 100,000 short byte streams, each checked with read chunks of 1, 3, 17 and 16,384 bytes. All 400,000 comparisons agreed on return values and raw name, comment, sequence and quality fields. Inputs include preambles, markers, whitespace, CR, NUL and high bytes. This historical comparison was not rerun for C++17: the vendored header and parser logic are unchanged. Allocation/read-fault tests and all-byte cleaning checks are included in the newly executed module suites.

This establishes compatibility for the exercised inputs, not every possible stream. Reproducible comparison source, original/patched headers and the result log are retained in the ignored `build-kseq-review/` directory on both the server and local mirror.

## Reproducing the checks

Run from the repository root:

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

Recorded CTest runs additionally used `--output-junit cpp17-test-results.xml`, supported by the observed CMake version, and configuration enabled `CMAKE_EXPORT_COMPILE_COMMANDS` to inspect compiler and standard flags. These reporting options do not change the declared CMake 3.18 build minimum. CMake selects C++17 with `CMAKE_CXX_STANDARD_REQUIRED=ON` and `CMAKE_CXX_EXTENSIONS=OFF`. To reproduce the alternative-core contract run:

```bash
mkdir -p build-cpp17-evidence
printf '#!/bin/sh\nexec "%s/build-cpp17-gcc13-release/msakit_test_driver" valid "$@"\n' "$PWD" \
  > build-cpp17-evidence/alternate-core
chmod +x build-cpp17-evidence/alternate-core
python3 tests/integration.py --binary build-cpp17-evidence/alternate-core \
  --test-driver build-cpp17-gcc13-release/msakit_test_driver --suite contract
```

Full test output is preserved in `build-cpp17-evidence/gcc13-release-full.log`, `gcc13-sanitize-full.log`, and `gcc9-release-full.log`, independent of later filtered CTest runs. The same directory contains the executed `run_validation.py`, build/CTest/contract/alternative-core/runtime logs, `commands.json`, environment and compile-standard records, short-output results, and baseline/final hashes. JUnit files are under the respective `build-cpp17-gcc13-release/`, `build-cpp17-gcc13-sanitize/`, and `build-cpp17-gcc9-release/` directories. Copies of the evidence are included in the local mirror's ignored directories.

## Sanitizers and retained history

The existing sanitizer-only `-fno-pie` / `-no-pie` configuration is retained. During the 0.1.0 implementation, an empty instrumented PIE reproduced intermittent startup crashes on this host; its non-PIE control did not. Release keeps its usual layout. ASan leak detection and immediate failure, and UBSan immediate failure and stack traces, remain enabled. No suppression list or host setting change was introduced.

The 0.1.0 input-test and multi-sequence logs remain in `build-input-tests-evidence/` and `build-msa-evidence/`. The latter contains earlier retained input/output FASTA and observed-width tables for all 13 datasets. The preceding kseq migration and functional split logs remain in `build-kseq-evidence/` and `build-modular-evidence/`. Acceptance of C++17 uses the newly executed suites above, not historical runs.

## Interpretation and limits

These checks establish the software contract and bounded reference-core correctness. They do not measure biological alignment accuracy or large-data scalability. Native kseq parsing intentionally changes some 0.1.0 behavior; [the design](design.md#4-input-contract) records the differences. Protein alignment, profiles, parallel execution and non-Linux platforms remain outside scope. CMake 3.18 is the declared minimum; acceptance used 3.26.4. System software was unchanged. No Git commit, push, tag or release was created for this delivery.
