# EasyMSA Benchmark integration

**English** | [简体中文](README.zh-CN.md)

This page connects MSA-Kit's alignment program to website evaluation. Your program receives unaligned sequences, inserts gaps and writes aligned FASTA. The website checks that file and measures the program. The [README example](../README.md#first-result) shows the same input/output idea locally.

## Understand the files you provide and receive

| Stage | Example content or artifact | Who produces it |
| --- | --- | --- |
| Input FASTA | `center: AC`, `q1: ATC`, `q2: AGGC`, each with its FASTA header | Platform dataset or your local test file. |
| Aligned FASTA | `center: A--C`, `q1: AT-C`, `q2: AGGC` for the supplied core | Your `run.sh` invocation and aligner. |
| Evaluation results | Time, peak memory, normalized SP and available Q/TC | The platform after running, validation and scoring. |

The colon notation above summarizes records; actual files use `>` header lines, as shown in the README. Write the alignment file to the second script argument. Return a nonzero exit status on failure. You do not provide a score table in place of FASTA or write a scorer yourself.

An **interface precheck** runs small inputs to check calling conventions and output validity. A **benchmark run** measures the program on the dataset collection. **Scoring** assesses the valid alignment; reference-based Q/TC require a reference alignment. These are separate stages, so use the task's stage and status to locate a failure.

## The evaluation journey

```text
Your repository -> build.sh -> run.sh on a small input -> output validation
                -> dataset runs -> platform scoring -> result page
```

`build.sh` prepares the program once. `run.sh` receives a sequence file and writes aligned FASTA for one invocation. The platform checks the file and computes metrics separately. Passing a precheck means the interface worked on small examples; a long dataset can still time out or exceed memory.

To explore the result page immediately, choose the MSA-Kit example on [the submission page](https://easymsa.cn/#/benchmark/submit). It reuses completed results and does not start a new benchmark.

Submit a public repository at [EasyMSA](https://easymsa.cn/#/benchmark/submit).
The website pins a commit, builds it offline, runs small interface checks, and then evaluates the supported datasets.
When deriving a tool from MSA-Kit, keep this directory and primarily replace `src/align.cpp`.

## 1. Required repository layout

```text
your-repository/
├── CMakeLists.txt
├── src/
├── include/
├── extern/                  # Bundled offline dependencies
└── benchmark/
    ├── build.sh             # Required platform build entry
    ├── run.sh               # Required platform alignment entry
    ├── adapter.py           # This template's title/residue adapter
    └── test_adapter.py      # Adapter tests
```

The platform requires the two shell scripts under `benchmark/` at the repository root.
The Python adapter is this template's implementation choice, not a required language for other tools.
Commit all scripts, sources and offline dependencies; files present only on your computer cannot be used by the website.

## 2. Build entry: `benchmark/build.sh`

The platform starts from the repository root and invokes:

```bash
bash benchmark/build.sh
```

The supplied script is:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-benchmark --parallel 2
```

Success produces `build-benchmark/msakit` and returns zero. Build artifacts are retained for dataset runs;
do not compile again in `run.sh`. Return a nonzero status on failure rather than hiding it with `|| true`.

- Builds run in Linux with the platform's C++/CMake, Python and Bash toolchain. MSA-Kit needs C++17, CMake and zlib.
- **Build and execution have no network access.** Do not use online `apt`, `pip install`, `git clone`, or CMake downloads.
  Dependencies must already be in the platform image or checked into the repository as ordinary files.
  Additional Git submodule and Git LFS downloads are not supported.
- Do not depend on your local username, absolute installation directories or a locally compiled executable as the only build method.
- Interpreted projects still provide `build.sh`; it may perform offline checks and exit successfully.
- Use LF shell-script line endings. The platform invokes Bash explicitly, so executable permission is not required for these two entries.

## 3. Run entry: `benchmark/run.sh`

Exactly four positional arguments are passed:

```bash
bash benchmark/run.sh INPUT_FASTA OUTPUT_FASTA THREADS SEQUENCE_TYPE
```

| Argument | Meaning | Required handling |
| --- | --- | --- |
| `$1` | Absolute path to unaligned FASTA | Read without modifying it. |
| `$2` | Absolute aligned-FASTA destination | Write the complete result here, not to a fixed filename. |
| `$3` | Positive CPU/thread allowance | Pass it to parallel tools and never exceed it. |
| `$4` | `dna` or `rna` | Select the appropriate mode; declare only supported types. |

Paths belong to the platform. Do not assume an `/outputs/` directory: a precheck may use
`/inputs/input.fasta` and `/work/output/aligned.raw.fasta`. These are examples, not paths to hard-code.
Quote `"$1"` and `"$2"` so spaces in paths work.

The supplied script is:

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

The output parent must exist and the output file must not already exist. Write plain, uncompressed aligned FASTA.
Finish and close it before returning zero; return nonzero on failure. Send diagnostics to stderr, not into FASTA.
Do not return while background alignment processes are running. Treat input and built project files as read-only.
Put temporary data in the writable workspace; this adapter uses the output parent and removes its temporary directory afterwards.

The reference core is single-threaded. Accepting a thread allowance does not make it parallel.
When replacing it with a parallel command, explicitly forward the allowance in the adapter.

## 4. Output checks

The website matches **complete FASTA titles**, not the first whitespace-delimited word.
`sample strain_A description` and `sample\tstrain_B` are distinct records even though both begin with `sample`.
Preserve descriptions, spaces and tabs when restoring headers.

| Property | Requirement |
| --- | --- |
| Record identity | Exactly the original full-title set, with each record once; no insertion, deletion or merging. |
| Alignment shape | Nonempty rows of equal length; use `-` for inserted gaps. |
| Residues | Removing `-` must reproduce the corresponding input, compared case-insensitively. |
| DNA/RNA symbols | Preserve original IUPAC ambiguity symbols and T/U; do not permanently change ambiguity to N or U to T. |
| Serialization | FASTA titles and sequence lines only; no logs, score tables or FASTQ qualities. |

The platform can match reordered output by title. This template restores input order and additionally rejects all-gap columns.
Repeated sequences remain separate records. The normal MSA-Kit CLI accepts duplicate headers;
the current website input policy requires unique complete titles. These are distinct contracts.

## 5. The MSA-Kit adapter and replacement points

The ordinary CLI cleans ambiguity symbols to N, and kseq can reconstruct header separators.
Calling `msakit -i "$1" -o "$2"` directly can therefore fail the website's original-residue checks.

[adapter.py](adapter.py), identified as `msakit-easymsa-adapter-1`, does the following:

1. Save original full titles and sequences and assign unique short IDs.
2. Keep A/C/G/T/U/N and temporarily mask other valid IUPAC symbols as N for the core.
3. Run `build-benchmark/msakit` and wait for completion.
4. Validate IDs, widths and ungapped **masked input** before accepting the core output.
5. Restore original ambiguity symbols by residue position, restore full titles, and publish the final output.

T/U are never interconverted. The platform independently validates restored output against the original input.
Other submitted programs receive original platform FASTA; this masking is specific to this adapter.
Record its version and `IUPAC ambiguity -> N before alignment; restore original symbols afterwards` as preprocessing.
Timing includes the adapter. The ordinary MSA-Kit CLI and core contract are unchanged.

If only `src/align.cpp` changes, these four files usually need no edits. If the binary name/location changes,
update `build.sh` and `build-benchmark/msakit` in `adapter.py`. For another CLI, change `subprocess.run(...)`
inside `run()`, including thread forwarding. Always validate the core output **before** restoring symbols;
restoration must not hide changed or missing residues. A tool that handles IUPAC natively can implement a direct interface.

## 6. Local Linux checks before submission

From the repository root:

```bash
# Normalize shell files after editing on Windows.
python3 -c 'from pathlib import Path; files = [Path("benchmark/build.sh"), Path("benchmark/run.sh")]; [p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n")) for p in files]'
bash -n benchmark/build.sh benchmark/run.sh
bash benchmark/build.sh
python3 benchmark/test_adapter.py
```

Then run this complete example. It exercises DNA and RNA, shared first words in full titles, a tab,
IUPAC symbols and wrapped sequence lines. Each run uses a new output path.

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

Expect the adapter tests to succeed, followed by `dna: PASS` and `rna: PASS`.
Use Benchmark's **Download data** menu for evaluated FASTA datasets, beginning with small inputs.
Passing these interface checks does not guarantee that the reference star core fits large-data time or memory limits.

## 7. Submit and retrieve results

1. Commit and push sources, offline dependencies and `benchmark/` to your public repository.
2. Open [Submit evaluation](https://easymsa.cn/#/benchmark/submit), enter the URL, and select supported types and an optional version in additional settings.
3. The website pins a full commit. Later pushes do not change that task; submit a new task to evaluate fixes.
4. After building and checking declared types, the platform evaluates the datasets. Undeclared types are marked unsupported.
5. Save the result link or recovery file. **Recent evaluations** uses this browser's records; optional email delivers task links.

Current user evaluations run once per dataset: at most 4 CPU and 32 GiB, 30 minutes for building and each alignment,
and a separate 30-minute limit per scorer. Temporary space is 8 GiB; per-unit output/log limits are 1 GiB/8 MiB;
the process limit is 512. Consult the task's experiment details for its actual conditions.
Your entry only creates aligned FASTA. The platform validates and scores it independently; do not download reference answers or emit score JSON instead.

## 8. Troubleshooting

| Symptom/category | What to check |
| --- | --- |
| `missing_entrypoint` | Both required scripts exist in the pinned commit at the root `benchmark/` directory; spelling and case match. |
| `build_failed` | First build-log error; online downloads, local paths or unavailable toolchains. |
| `precheck_failed` | Run the example above; verify complete titles, IUPAC, RNA U and output at `$2`. |
| `title_mismatch` / `duplicate_title` | Truncated titles, merged records, changed IDs or incorrect restoration. |
| `sequence_changed` | Ambiguity lost as N, U changed to T, or residues removed/modified; inspect the core before restoration. |
| `unequal_lengths` / invalid output | Equal-width aligned FASTA is required, not an input copy, empty file or partial output. |
| Permission/read-only errors | Writes to input/project directories or fixed cache paths; use the writable workspace/output parent. |
| `bad interpreter` / `$'\r'` | Convert shell files from CRLF to LF and commit the change. |
| Timeout / memory limit | Algorithm complexity, thread configuration and input size; check whether the full-DP reference core remains selected. |
| Scoring failure | Confirm alignment validation succeeded, then inspect the scorer status separately from build/execution status. |
