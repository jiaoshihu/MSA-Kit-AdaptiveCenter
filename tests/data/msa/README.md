# Synthetic multi-sequence inputs

**English** | [简体中文](README.zh-CN.md)

These six FASTA files are hand-specified, synthetic software-test fixtures.
They are not biological samples, published benchmark datasets, or reference
alignments. No expected gap layout is attached: any core that returns a legal
alignment while preserving the prepared records can pass the contract tests.

| Case | File | Records | Input lengths | Purpose |
| --- | --- | ---: | --- | --- |
| `MSA-FIXED-001` | `dna_indels.fasta` | 6 | 11–15 | Prefix/internal/suffix insertion, deletion, substitution |
| `MSA-FIXED-002` | `rna_tu.fasta` | 6 | 6–9 | RNA, mixed T/U, N and different lengths |
| `MSA-FIXED-003` | `duplicates_cleaning.fasta` | 6 | 3–7 | Duplicate headers/records and explicit cleaning counts |
| `MSA-FIXED-004` | `divergent_n.fasta` | 6 | 8–20 | N-rich center and mutually different residues |
| `MSA-FIXED-005` | `unequal_lengths.fasta` | 6 | 129, 1, 2, 9, 31, 64 | Strong length differences and a long center |
| `MSA-FIXED-006` | `wrap_boundaries.fasta` | 6 | 79, 80, 81, 159, 160, 161 | Output line-width boundaries with multiple rows |

Expected headers, prepared sequences and cleaning counts are explicitly listed
in [msa_cases.py](../../msa_cases.py). They are not obtained by parsing the
aligner's output or calling its input code. The file bytes and expected values
are maintained separately so a changed fixture cannot silently redefine its
own expected result.

The same module supplies generated datasets using seed `20261027`: six cases
with 2, 3, 8, 16, 64 and 128 records of 16–128 residues, plus one four-record case
of 510–513 residues. The longer case requires at most 263,682 score cells per
pair, roughly 2.1 MB for 64-bit scores, and only three pairwise calculations.
