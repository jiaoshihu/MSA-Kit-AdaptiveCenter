"""Bounded synthetic multi-sequence datasets for output-contract tests.

The fixed FASTA files are checked against explicit prepared records below, not
against a second invocation of MSA-Kit or a copy of its input parser. Generated
records are chosen before their FASTA representation. None of these datasets
is a biological reference alignment or an alignment-quality benchmark.
"""

from pathlib import Path
import random

from input_cases import InputCase


MSA_DATA_DIRECTORY = Path(__file__).resolve().parent / "data" / "msa"
MSA_GENERATOR_SEED = 20261027


def fixed_msa_cases():
    """Yield the six checked-in files and independent expected record values."""
    specs = (
        ("MSA-FIXED-001", "dna_indels.fasta",
         (b"center synthetic DNA", b"prefix insertion synthetic",
          b"internal insertion synthetic", b"suffix insertion synthetic",
          b"deletion synthetic", b"substitution synthetic"),
         (b"ACGTCAGTACGA", b"TTACGTCAGTACGA", b"ACGTCATTTGTACGA",
          b"ACGTCAGTACGAGG", b"ACGTAGTACGA", b"ACGTCGGTACGA"), 0, 0),
        ("MSA-FIXED-002", "rna_tu.fasta",
         (b"center synthetic RNA", b"DNA spelling synthetic",
          b"RNA insertion synthetic", b"RNA deletion synthetic",
          b"mixed T U synthetic", b"unknown RNA synthetic"),
         (b"ACGUACGU", b"ACGTACGT", b"ACGUUACGU", b"ACGACGU",
          b"AUTGCU", b"NCGUACGN"), 0, 0),
        ("MSA-FIXED-003", "duplicates_cleaning.fasta",
         (b"duplicate synthetic", b"duplicate synthetic", b"duplicate synthetic",
          b"ambiguity synthetic", b"N and unknown synthetic",
          b"mixed cleaning synthetic"),
         (b"ACGTN", b"ACGTN", b"UUTTNN", b"ANNNNNN", b"NNN", b"ACNTU"),
         13, 8),
        ("MSA-FIXED-004", "divergent_n.fasta",
         (b"N center synthetic", b"adenine synthetic", b"cytosine synthetic",
          b"guanine synthetic", b"thymine synthetic", b"mixed unknown synthetic"),
         (b"N" * 12, b"A" * 16, b"C" * 8, b"G" * 20, b"T" * 12,
          b"TUNNACGTUN"), 0, 0),
        ("MSA-FIXED-005", "unequal_lengths.fasta",
         (b"long center synthetic", b"one residue synthetic", b"two residues synthetic",
          b"nine residues synthetic", b"thirty one synthetic", b"sixty four synthetic"),
         (b"ACGT" * 32 + b"A", b"T", b"GU", b"ACGTNACGU",
          b"ACGT" * 7 + b"ACG", b"ACGT" * 16), 0, 0),
        ("MSA-FIXED-006", "wrap_boundaries.fasta",
         (b"length79 synthetic", b"length80 synthetic", b"length81 synthetic",
          b"length159 synthetic", b"length160 synthetic", b"length161 synthetic"),
         (b"ACGT" * 19 + b"ACG", b"ACGT" * 20, b"ACGT" * 20 + b"A",
          b"ACGT" * 39 + b"ACG", b"ACGT" * 40, b"ACGT" * 40 + b"A"), 0, 0),
    )
    for case_id, filename, headers, sequences, uppercase, unknown in specs:
        yield InputCase(case_id, (MSA_DATA_DIRECTORY / filename).read_bytes(),
                        headers, sequences, uppercase, unknown)


def _serialize_records(headers, sequences):
    # Formatting is deliberately independent of the aligner's output writer.
    # Wrap input at 37 bases and alternate LF/CRLF between records.
    chunks = []
    for index, (header, sequence) in enumerate(zip(headers, sequences)):
        ending = b"\r\n" if index % 2 else b"\n"
        chunks.append(b">" + header + ending)
        for offset in range(0, len(sequence), 37):
            chunks.append(sequence[offset:offset + 37] + ending)
    return b"".join(chunks)


def generated_msa_cases():
    """Yield six count/length cases plus a small longer pairwise-DP workload."""
    rng = random.Random(MSA_GENERATOR_SEED)
    specs = (("MSA-GENERATED-001", 2), ("MSA-GENERATED-002", 3),
             ("MSA-GENERATED-003", 8), ("MSA-GENERATED-004", 16),
             ("MSA-GENERATED-005", 64), ("MSA-GENERATED-006", 128))
    for case_id, count in specs:
        headers = []
        sequences = []
        for index in range(count):
            length = 128 if index == 0 else 16 if index == 1 else rng.randint(16, 128)
            sequence = bytes(rng.choice(b"ACGTUN") for _ in range(length))
            if index > 0 and index % 7 == 0:
                sequence = sequences[0]
            # Shared names include both identical and distinct sequences.
            header = (b"shared synthetic record" if index % 3 == 0
                      else f"synthetic row {index}".encode("ascii"))
            headers.append(header)
            sequences.append(sequence)
        headers = tuple(headers)
        sequences = tuple(sequences)
        yield InputCase(case_id, _serialize_records(headers, sequences), headers,
                        sequences, seed=MSA_GENERATOR_SEED)

    center = bytes(rng.choice(b"ACGT") for _ in range(512))
    headers = (b"synthetic long center", b"synthetic one insertion",
               b"synthetic two deletions", b"synthetic substitution")
    replacement = b"C" if center[400:401] == b"A" else b"A"
    sequences = (center, center[:256] + b"T" + center[256:],
                 center[:128] + center[130:], center[:400] + replacement + center[401:])
    yield InputCase("MSA-GENERATED-007", _serialize_records(headers, sequences),
                    headers, sequences, seed=MSA_GENERATOR_SEED)
