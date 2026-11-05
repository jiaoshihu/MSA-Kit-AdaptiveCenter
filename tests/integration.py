#!/usr/bin/env python3
"""Small synthetic subprocess tests; no downloads or real-data experiments."""

import argparse
from dataclasses import replace
import gzip
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile
import unittest
import zlib

from input_cases import (ALL_ERROR_CASES, ALL_VALID_CASES, GENERATOR_SEED, GZIP_RAW,
                         KSEQ_FASTQ_CASES, LIFECYCLE_ERROR_IDS,
                         generated_cases, gzip_member, gzip_prefix_cases,
                         invalid_gzip_cases, valid_gzip_cases)
from msa_cases import fixed_msa_cases, generated_msa_cases


BINARY = None
TEST_DRIVER = None
INPUT_MODES = ("plain-file", "gzip-file", "plain-stdin", "gzip-stdin")
OUTPUT_MODES = ("stdout", "plain-file", "gzip-file")

# Only these tests constrain the reference algorithm's particular gap layout.
# Every other test can be run after replacing the alignment core.
REFERENCE_TESTS = {
    "test_reference_example_and_named_output",
    "test_tie_rule_is_deterministic",
}


class ProgramTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="msakit-integration-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, contents):
        path = self.root / name
        path.write_bytes(contents)
        return path

    def run_tool(self, *args, data=None, expected=0, binary=None, context=""):
        result = subprocess.run(
            [str(binary or BINARY), *map(str, args)],
            input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=self.root, timeout=20, check=False,
        )
        self.assertEqual(
            result.returncode, expected,
            f"{context}\ncommand args={args!r}\nstdout={result.stdout!r}\nstderr={result.stderr!r}",
        )
        return result

    def align_bytes(self, raw, *args, expected=0):
        return self.run_tool("-i", "-", "-o", "-", *args, data=raw, expected=expected)

    def assert_no_artifacts(self, *expected_names):
        self.assertEqual({p.name for p in self.root.iterdir()}, set(expected_names))

    def assert_alignment_contract(self, data, expected_headers, expected_sequences, context="",
                                  line_width=None):
        """Parse output independently and check invariants, not a gap layout."""
        self.assertTrue(data.endswith(b"\n"), context + "\nFASTA output must end with LF")
        headers = []
        rows = []
        line_counts = []
        for line in data.split(b"\n")[:-1]:
            if line.startswith(b">"):
                headers.append(line[1:])
                rows.append(bytearray())
                line_counts.append(0)
            else:
                self.assertTrue(rows, context + "\nSequence data precedes its header")
                self.assertTrue(line, context + "\nEmpty output sequence line")
                self.assertNotIn(b"\r", line, context + "\nSequence lines must use LF, not CRLF")
                if line_width is not None and line_width > 0:
                    self.assertLessEqual(len(line), line_width, context + "\nLine exceeds requested width")
                rows[-1].extend(line)
                line_counts[-1] += 1
        rows = [bytes(row) for row in rows]
        self.assertEqual(headers, expected_headers, context)
        self.assertEqual(len(rows), len(expected_sequences), context)
        self.assertTrue(rows, context)
        if line_width == 0:
            self.assertEqual(line_counts, [1] * len(rows), context + "\nWidth zero must not wrap")
        width = len(rows[0])
        self.assertGreater(width, 0, context)
        for row, sequence in zip(rows, expected_sequences):
            self.assertEqual(len(row), width, context)
            self.assertTrue(set(row).issubset(set(b"ACGTUN-")), context)
            self.assertEqual(row.replace(b"-", b""), sequence, context)
        for column in zip(*rows):
            self.assertTrue(any(base != ord("-") for base in column), context + "\nAll-gap column")
        return rows

    def exercise_input_case(self, case, input_mode, output_mode="stdout", *,
                            wire=None, existing=False, line_width=None):
        """Run a named case in an isolated directory and check its whole outcome."""
        if wire is None:
            wire = gzip.compress(case.raw, mtime=0) if input_mode.startswith("gzip") else case.raw
        context = (f"case={case.case_id} input={input_mode} output={output_mode} "
                   f"existing={existing} seed={case.seed} line_width={line_width}\nraw={case.raw!r}")
        with tempfile.TemporaryDirectory(prefix=case.case_id + "-", dir=self.root) as folder:
            work = Path(folder)
            source = "-"
            original_files = set()
            if input_mode.endswith("file"):
                # Deliberately misleading suffixes also check magic-based input
                # detection. These real paths exercise Unicode and spaces.
                source = work / ("输入 sequence.data" if input_mode.startswith("gzip")
                                 else "输入 sequence.fa.gz")
                source.write_bytes(wire)
                original_files.add(source.name)
            destination = "-"
            sentinel = b"previous result must survive\x00\xff\n"
            if output_mode != "stdout":
                destination = work / ("输出 aligned.fa.gz" if output_mode == "gzip-file"
                                      else "输出 aligned.fa")
                if existing:
                    destination.write_bytes(sentinel)
                    original_files.add(destination.name)
            args = ["-i", source, "-o", destination]
            if line_width is not None:
                args.extend(("--line-width", str(line_width)))
            if existing:
                args.append("--force")
            result = self.run_tool(*args, data=wire if source == "-" else None,
                                   expected=case.exit_code, context=context)
            context += (f"\ncommand={result.args!r}\nstdout={result.stdout!r}"
                        f"\nstderr={result.stderr!r}")
            if source != "-":
                self.assertEqual(source.read_bytes(), wire, context)
            if case.exit_code:
                self.assertEqual(result.stdout, b"", context)
                self.assertTrue(result.stderr, context)
                if destination != "-":
                    if existing:
                        self.assertEqual(destination.read_bytes(), sentinel, context)
                    else:
                        self.assertFalse(destination.exists(), context)
                if case.reason:
                    self.assertIn(case.reason, result.stderr, context)
                    self.assertIn(os.fsencode(str(source)) + b":", result.stderr, context)
                if case.record is not None:
                    location = re.search(rb"\brecord (\d+):", result.stderr)
                    self.assertIsNotNone(location, context)
                    self.assertEqual(int(location.group(1)), case.record, context)
                self.assertEqual({path.name for path in work.iterdir()}, original_files, context)
                return
            if destination == "-":
                aligned = result.stdout
            else:
                self.assertEqual(result.stdout, b"", context)
                aligned = destination.read_bytes()
                if output_mode == "gzip-file":
                    self.assertTrue(aligned.startswith(b"\x1f\x8b"), context)
                    try:
                        aligned = gzip.decompress(aligned)
                    except (OSError, EOFError, zlib.error) as error:
                        self.fail(f"{context}\nCannot decode gzip output: {error}")
                original_files.add(destination.name)
            self.assert_alignment_contract(aligned, list(case.headers), list(case.sequences), context,
                                           line_width=80 if line_width is None else line_width)
            counters = re.search(
                rb"Read (\d+) sequences; lowercase conversions: (\d+); replaced with N: (\d+)(?:\r?\n|$)",
                result.stderr)
            self.assertIsNotNone(counters, context)
            self.assertEqual(tuple(map(int, counters.groups())),
                             (len(case.headers), case.uppercase, case.unknown), context)
            self.assertEqual({path.name for path in work.iterdir()}, original_files, context)

    def test_input_catalog_identifiers_and_lifecycle_selection(self):
        msa_cases = (*fixed_msa_cases(), *generated_msa_cases())
        all_cases = (*ALL_VALID_CASES, *ALL_ERROR_CASES, *valid_gzip_cases(), *invalid_gzip_cases(),
                     *gzip_prefix_cases(), *(case for case, _ in generated_cases()), *msa_cases)
        identifiers = [case.case_id for case in all_cases]
        self.assertEqual(len(identifiers), len(set(identifiers)), "Duplicate input case ID")
        self.assertTrue(LIFECYCLE_ERROR_IDS.issubset({case.case_id for case in ALL_ERROR_CASES}),
                        "Unknown case in lifecycle selection")
        for identifier in identifiers:
            self.assertRegex(identifier, r"^[A-Z]+(?:-[A-Z]+)*-\d{3}$")
        for case in msa_cases:
            with self.subTest(case=case.case_id):
                self.assertGreaterEqual(len(case.sequences), 2, "MSA cases must contain multiple records")
                self.assertEqual(len(case.headers), len(case.sequences))
                self.assertGreater(len(set(case.sequences)), 1, "Include distinct rows to test correspondence")

    def test_multisequence_fasta_files_all_transports(self):
        # Six actual FASTA fixtures, each with six records, through 12 routes.
        for case in fixed_msa_cases():
            for input_mode in INPUT_MODES:
                for output_mode in OUTPUT_MODES:
                    with self.subTest(case=case.case_id, input=input_mode, output=output_mode):
                        self.exercise_input_case(case, input_mode, output_mode)

    def test_multisequence_generated_counts_and_lengths(self):
        for index, case in enumerate(generated_msa_cases()):
            for channel, input_mode in enumerate(INPUT_MODES):
                output_mode = OUTPUT_MODES[(index + channel) % len(OUTPUT_MODES)]
                with self.subTest(case=case.case_id, seed=case.seed, records=len(case.sequences),
                                  input=input_mode, output=output_mode):
                    self.exercise_input_case(case, input_mode, output_mode)

    def test_multisequence_reordering_preserves_new_record_positions(self):
        for case in fixed_msa_cases():
            count = len(case.sequences)
            orders = (("reversed", tuple(reversed(range(count)))),
                      ("rotated", tuple(range(1, count)) + (0,)))
            for name, order in orders:
                headers = tuple(case.headers[index] for index in order)
                sequences = tuple(case.sequences[index] for index in order)
                raw = b"".join(b">" + header + b"\n" + sequence + b"\n"
                               for header, sequence in zip(headers, sequences))
                reordered = replace(case, raw=raw, headers=headers, sequences=sequences,
                                    uppercase=0, unknown=0)
                with self.subTest(case=case.case_id, order=name):
                    # Changing the first row may change the reference center.
                    # Only the new input correspondence is constrained here.
                    self.exercise_input_case(reordered, "gzip-stdin", "stdout")

    def test_multisequence_output_wrapping(self):
        case = next(case for case in fixed_msa_cases() if case.case_id == "MSA-FIXED-006")
        for width in (0, 1, 7, 80, 81):
            for output_mode in OUTPUT_MODES:
                with self.subTest(case=case.case_id, width=width, output=output_mode):
                    self.exercise_input_case(case, "gzip-stdin", output_mode, line_width=width)

    def test_independent_msa_checker_accepts_distinct_legal_layouts(self):
        headers = [b"same", b"same", b"third"]
        sequences = [b"AC", b"ATC", b"GC"]
        for rows in ((b"A-C", b"ATC", b"G-C"), (b"AC-", b"ATC", b"GC-")):
            with self.subTest(rows=rows):
                data = b"".join(b">" + header + b"\n" + row + b"\n"
                                for header, row in zip(headers, rows))
                self.assert_alignment_contract(data, headers, sequences, line_width=0)

    def test_independent_msa_checker_rejects_malformed_results(self):
        headers = [b"same", b"same", b"third"]
        sequences = [b"AC", b"ATC", b"GC"]
        rows = [b"A-C", b"ATC", b"G-C"]

        def fasta(names, aligned):
            return b"".join(b">" + name + b"\n" + row + b"\n"
                            for name, row in zip(names, aligned))

        good = fasta(headers, rows)
        damaged = {
            "missing-row": fasta(headers[:2], rows[:2]),
            "extra-row": good + b">extra\nA-C\n",
            "reordered-duplicate-header-rows": fasta(headers, [rows[1], rows[0], rows[2]]),
            "renamed-header": fasta([b"changed", *headers[1:]], rows),
            "unaligned-unequal-input": fasta(headers, sequences),
            "altered-residue": fasta(headers, [b"A-T", *rows[1:]]),
            "lost-residue": fasta(headers, [b"A--", *rows[1:]]),
            "extra-residue": fasta(headers, [b"ACC", *rows[1:]]),
            "dot-gap": fasta(headers, [b"A.C", *rows[1:]]),
            "lowercase": fasta(headers, [b"a-C", *rows[1:]]),
            "all-gap-column": fasta(headers, [b"-" + row for row in rows]),
            "empty-row": fasta(headers, [b"", *rows[1:]]),
            "missing-final-LF": good[:-1],
            "CRLF-sequence": good.replace(b"A-C\n", b"A-C\r\n", 1),
            "data-before-header": b"AC\n" + good,
            "empty-output": b"",
        }
        for name, data in damaged.items():
            with self.subTest(damage=name):
                with self.assertRaises(AssertionError):
                    self.assert_alignment_contract(data, headers, sequences)
        with self.assertRaises(AssertionError):
            self.assert_alignment_contract(good, headers, sequences, line_width=2)
        wrapped = good.replace(b"A-C\n", b"A-\nC\n", 1)
        with self.assertRaises(AssertionError):
            self.assert_alignment_contract(wrapped, headers, sequences, line_width=0)

    def test_named_valid_input_transport_matrix(self):
        # Include FASTA, FASTQ, mixed records and explicit native kseq semantics.
        for case in ALL_VALID_CASES:
            for input_mode in INPUT_MODES:
                for output_mode in OUTPUT_MODES:
                    with self.subTest(case=case.case_id, input=input_mode, output=output_mode,
                                      raw=case.raw):
                        self.exercise_input_case(case, input_mode, output_mode)

    def test_named_content_errors_all_input_paths(self):
        for case in ALL_ERROR_CASES:
            for input_mode in INPUT_MODES:
                with self.subTest(case=case.case_id, input=input_mode, raw=case.raw):
                    self.exercise_input_case(case, input_mode)

    def test_named_content_errors_preserve_new_and_existing_targets(self):
        for case in ALL_ERROR_CASES:
            if case.case_id not in LIFECYCLE_ERROR_IDS:
                continue
            for input_mode in INPUT_MODES:
                for output_mode in ("plain-file", "gzip-file"):
                    for existing in (False, True):
                        with self.subTest(case=case.case_id, input=input_mode,
                                          output=output_mode, existing=existing, raw=case.raw):
                            self.exercise_input_case(case, input_mode, output_mode,
                                                     existing=existing)

    def test_fastq_quality_values_do_not_change_prepared_sequences(self):
        # Qualities can contain sequence gap symbols, record markers, NUL and
        # non-ASCII bytes. Only their returned length matters to kseq.
        for quality in (b"!!!!", b"-.@+", b"\x00\xff\x80>", b"~~##"):
            raw = b"@name\tdesc\nacRn\n+\n" + quality + b"\n"
            case = replace(KSEQ_FASTQ_CASES[0], raw=raw, headers=(b"name desc",),
                           sequences=(b"ACNN",), uppercase=3, unknown=1)
            for index, input_mode in enumerate(INPUT_MODES):
                with self.subTest(quality=quality, input=input_mode):
                    self.exercise_input_case(case, input_mode,
                                             OUTPUT_MODES[index % len(OUTPUT_MODES)])

    def test_fastq_gzip_members_can_split_any_record_field(self):
        case = KSEQ_FASTQ_CASES[1]
        raw = case.raw
        cuts = (3, raw.index(b"ac") + 1, raw.index(b"\r\n") + 1,
                raw.index(b"+label") + 2, raw.index(b"I!") + 1,
                raw.index(b"@same", 1) + 1)
        for cut in cuts:
            wire = (gzip_member(b"") + gzip_member(raw[:cut], header_crc=True)
                    + gzip_member(b"") + gzip_member(raw[cut:]) + gzip_member(b""))
            self.assertEqual(gzip.decompress(wire), raw)
            for input_mode in ("gzip-file", "gzip-stdin"):
                for output_mode in OUTPUT_MODES:
                    with self.subTest(case=case.case_id, split=cut, input=input_mode,
                                      output=output_mode):
                        self.exercise_input_case(case, input_mode, output_mode, wire=wire)

    def test_fastq_sequence_and_quality_buffer_boundaries(self):
        # Single records avoid pairwise DP while crossing kseq's 16 KiB and
        # the decoding layer's 64 KiB buffers in both sequence and quality.
        for size in (16_383, 16_384, 16_385, 65_535, 65_536, 65_537):
            sequence = (b"ACGTUN" * (size // 6 + 1))[:size]
            quality = (b"@+>.-!" * (size // 6 + 1))[:size]
            raw = b"@boundary\tdescription\r\n" + sequence + b"\r\n+\r\n" + quality + b"\r\n"
            case = replace(KSEQ_FASTQ_CASES[0], raw=raw,
                           headers=(b"boundary description",), sequences=(sequence,))
            for input_mode in ("plain-stdin", "gzip-file"):
                with self.subTest(size=size, input=input_mode):
                    self.exercise_input_case(case, input_mode, "gzip-file", line_width=0)

    def test_optional_gzip_headers_and_member_splits(self):
        for case in valid_gzip_cases():
            # Check every member independently with zlib, including its FHCRC
            # (which Python gzip may ignore), then check concatenation with gzip.
            self.assertEqual(gzip.decompress(case.raw), GZIP_RAW, case.case_id)
            remaining = case.raw
            decoded = []
            while remaining:
                decoder = zlib.decompressobj(wbits=31)
                decoded.append(decoder.decompress(remaining) + decoder.flush())
                self.assertTrue(decoder.eof, case.case_id)
                self.assertLess(len(decoder.unused_data), len(remaining), case.case_id)
                remaining = decoder.unused_data
            self.assertEqual(b"".join(decoded), GZIP_RAW, case.case_id)
            for input_mode in ("gzip-file", "gzip-stdin"):
                for output_mode in OUTPUT_MODES:
                    with self.subTest(case=case.case_id, input=input_mode, output=output_mode,
                                      raw=case.raw):
                        self.exercise_input_case(case, input_mode, output_mode, wire=case.raw)

    def test_named_gzip_corruption_and_file_lifecycle(self):
        for case in invalid_gzip_cases():
            for input_mode in ("gzip-file", "gzip-stdin"):
                for output_mode, existing in (("stdout", False), ("plain-file", False),
                                              ("gzip-file", True)):
                    with self.subTest(case=case.case_id, input=input_mode, output=output_mode,
                                      existing=existing, raw=case.raw):
                        self.exercise_input_case(case, input_mode, output_mode,
                                                 wire=case.raw, existing=existing)

    def test_every_prefix_of_a_small_gzip_member(self):
        for case in gzip_prefix_cases():
            for input_mode in ("gzip-file", "gzip-stdin"):
                with self.subTest(case=case.case_id, input=input_mode, raw=case.raw):
                    self.exercise_input_case(case, input_mode, wire=case.raw)

    def test_seeded_input_transformations_preserve_prepared_records(self):
        for index, (case, concatenated) in enumerate(generated_cases()):
            self.assertEqual(gzip.decompress(concatenated), case.raw, case.case_id)
            for channel, input_mode in enumerate(INPUT_MODES):
                output_mode = OUTPUT_MODES[(index + channel) % len(OUTPUT_MODES)]
                wire = concatenated if input_mode.startswith("gzip") else case.raw
                with self.subTest(case=case.case_id, input=input_mode, output=output_mode,
                                  seed=GENERATOR_SEED, raw=case.raw):
                    self.exercise_input_case(case, input_mode, output_mode, wire=wire)

    def test_input_directory_is_an_io_error_without_output(self):
        directory = self.root / "输入 folder"
        directory.mkdir()
        for output_mode in ("stdout", "new", "existing"):
            with self.subTest(case="PATH-INPUT-DIRECTORY", output=output_mode):
                output = "-" if output_mode == "stdout" else self.root / "result.fa"
                if output_mode == "existing":
                    output.write_bytes(b"old")
                result = self.run_tool("-i", directory, "-o", output, "--force", expected=4)
                self.assertEqual(result.stdout, b"")
                self.assertIn(os.fsencode(str(directory)), result.stderr)
                if output_mode == "existing":
                    self.assertEqual(output.read_bytes(), b"old")
                    output.unlink()
                elif output_mode == "new":
                    self.assertFalse(output.exists())
                self.assertEqual(list(directory.iterdir()), [])
                self.assert_no_artifacts("输入 folder")

    def test_long_header_and_sequence_marker_are_preserved(self):
        header = b"long " + b"A" * 65_537 + b" > end"
        raw = b">" + header + b"\nA>C\n"
        for compressed in (False, True):
            with self.subTest(case="HEADER-LONG", gzip=compressed):
                wire = gzip.compress(raw, mtime=0) if compressed else raw
                result = self.align_bytes(wire, "--line-width", "1")
                self.assert_alignment_contract(result.stdout, [header], [b"ANC"])
                self.assertEqual(result.stdout.split(b"\n")[0], b">" + header)

    def test_reference_example_and_named_output(self):
        raw = b">center\nAC\n>q1\nATC\n>q2\nAGGC\n"
        source = self.write("input.fa", raw)
        output = self.root / "aligned.fa"
        result = self.run_tool("-i", source, "-o", output)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(output.read_bytes(), b">center\nA--C\n>q1\nAT-C\n>q2\nAGGC\n")
        self.assertEqual(source.read_bytes(), raw)
        self.assertIn(b"Read 3 sequences", result.stderr)
        self.assert_no_artifacts("input.fa", "aligned.fa")

    def test_standard_input_and_clean_standard_output(self):
        result = self.align_bytes(b">one\nacgt\n")
        self.assertEqual(result.stdout, b">one\nACGT\n")
        self.assertIn(b"lowercase conversions: 4", result.stderr)
        self.assertNotIn(b"[msakit]", result.stdout)
        self.assert_no_artifacts()

    def test_cleanup_counts_are_disjoint(self):
        result = self.align_bytes(b">seq desc\nacgRyUn \t\n")
        self.assertEqual(result.stdout, b">seq desc\nACGNNUNNN\n")
        self.assertIn(b"lowercase conversions: 4", result.stderr)
        self.assertIn(b"replaced with N: 4", result.stderr)

    def test_existing_n_and_lowercase_n_are_not_unknown(self):
        result = self.align_bytes(b">n\nNn\n")
        self.assertEqual(result.stdout, b">n\nNN\n")
        self.assertIn(b"lowercase conversions: 1", result.stderr)
        self.assertIn(b"replaced with N: 0", result.stderr)

    def test_header_reconstruction_and_duplicate_names(self):
        header = b"  shared name\t long  "
        raw = b">" + header + b"\r\nac\r\n>" + header + b"\r\nag\r\n"
        result = self.align_bytes(raw)
        # The first separator after the marker is consumed by kseq; an empty
        # name is rebuilt from the comment, without an extra prefix space.
        rebuilt = b" shared name\t long  "
        self.assert_alignment_contract(result.stdout, [rebuilt, rebuilt], [b"AC", b"AG"])

    def test_duplicate_sequences_remain_separate(self):
        raw = b">same\nACG\n>same\nACG\n>third\nACG\n"
        self.assert_alignment_contract(self.align_bytes(raw).stdout,
                                       [b"same", b"same", b"third"], [b"ACG"] * 3)

    def test_multiline_crlf_and_missing_final_newline(self):
        result = self.align_bytes(b"\n\r\n>one description\r\nac\r\n\r\ngu")
        self.assertEqual(result.stdout, b">one description\nACGU\n")

    def test_final_cr_without_lf_follows_kseq_line_trimming(self):
        result = self.align_bytes(b">one\nA\r")
        self.assertEqual(result.stdout, b">one\nA\n")
        self.assertIn(b"replaced with N: 0", result.stderr)

    def test_whitespace_only_sequence_line_is_not_empty(self):
        result = self.align_bytes(b">one\n \t\n")
        self.assertEqual(result.stdout, b">one\nNN\n")

    def test_non_ascii_header_and_sequence_bytes(self):
        raw = ">序列 描述\nAé\n".encode("utf-8")
        result = self.align_bytes(raw)
        self.assertEqual(result.stdout, ">序列 描述\nANN\n".encode("utf-8"))

    def test_t_and_u_are_preserved(self):
        raw = b">dna\nAT\n>rna\nAU\n"
        self.assert_alignment_contract(self.align_bytes(raw).stdout,
                                       [b"dna", b"rna"], [b"AT", b"AU"])

    def test_tie_rule_is_deterministic(self):
        raw = b">center\nA\n>query\nAA\n"
        expected = b">center\n-A\n>query\nAA\n"
        for _ in range(3):
            self.assertEqual(self.align_bytes(raw).stdout, expected)

    def test_invalid_records_and_input_gaps_return_3(self):
        cases = {
            "empty": b"", "empty-lines": b"\n\r\n",
            "empty-title": b">\nAC\n", "blank-title": b"> \t \nAC\n",
            "empty-sequence": b">one\n", "empty-first": b">one\n>two\nAC\n",
            "no-marker": b"ACGTUN\n",
            "fastq-short-quality": b"@one\nAC\n+\nI\n",
            "fastq-long-quality": b"@one\nAC\n+\nIII\n",
            "dash": b">one\nA-C\n", "dot": b">one\nAC.\n",
        }
        for label, raw in cases.items():
            with self.subTest(label=label):
                result = self.align_bytes(raw, expected=3)
                self.assertEqual(result.stdout, b"")
                self.assertTrue(result.stderr)

    def test_invalid_input_does_not_create_named_output(self):
        source = self.write("bad.fa", b">one\nA-\n")
        output = self.root / "result.fa.gz"
        self.run_tool("-i", source, "-o", output, expected=3)
        self.assertFalse(output.exists())
        self.assert_no_artifacts("bad.fa")

    def test_gzip_file_input_detected_by_magic(self):
        raw = b">one\nACG\n"
        source = self.write("input.not-gz", gzip.compress(raw, mtime=0))
        result = self.run_tool("-i", source, "-o", "-")
        self.assertEqual(result.stdout, raw)

    def test_plain_file_with_gz_suffix_is_plain_input(self):
        source = self.write("input.fa.gz", b">one\nACG\n")
        self.assertEqual(self.run_tool("-i", source, "-o", "-").stdout, b">one\nACG\n")

    def test_gzip_standard_input(self):
        raw = b">one\nacgu\n"
        self.assertEqual(self.align_bytes(gzip.compress(raw, mtime=0)).stdout, b">one\nACGU\n")

    def test_gzip_roundtrip_across_input_and_output_buffer_boundaries(self):
        # One record bypasses pairwise DP: this exercises byte-stream boundaries,
        # not a large alignment workload. Fixed-seed high-entropy bases ensure
        # the compressed input itself spans more than one 64 KiB input buffer.
        rng = random.Random(20261026)
        sequence = "".join(rng.choices("ACGTUN", k=250_000)).encode("ascii")
        raw = b">buffer-boundary\n" + sequence + b"\n"
        compressed = gzip.compress(raw, mtime=0)
        self.assertGreater(len(compressed), 65_536)

        plain_result = self.align_bytes(raw, "--line-width", "0", "--quiet")
        compressed_result = self.align_bytes(compressed, "--line-width", "0", "--quiet")
        self.assertEqual(plain_result.stdout, raw)
        self.assertEqual(compressed_result.stdout, plain_result.stdout)
        self.assertEqual(plain_result.stderr, b"")
        self.assertEqual(compressed_result.stderr, b"")

        source = self.write("boundary.data", compressed)
        output = self.root / "roundtrip.fa.gz"
        result = self.run_tool("-i", source, "-o", output, "--line-width", "0", "--quiet")
        self.assertEqual(result.stdout, b"")
        self.assertEqual(result.stderr, b"")
        self.assertEqual(gzip.decompress(output.read_bytes()), raw)

        # A small trailing member continues the same FASTA sequence after a
        # large first member. No extra record or quadratic core work is added.
        first = gzip.compress(raw[:-4], mtime=0)
        self.assertGreater(len(first), 65_536)
        concatenated = first + gzip.compress(raw[-4:], mtime=0)
        joined_result = self.align_bytes(concatenated, "--line-width", "0", "--quiet")
        self.assertEqual(joined_result.stdout, raw)
        self.assertEqual(joined_result.stderr, b"")
        self.assert_no_artifacts("boundary.data", "roundtrip.fa.gz")

    def test_concatenated_gzip_members_continue_same_fasta_stream(self):
        data = gzip.compress(b">one\nA", mtime=0) + gzip.compress(b"C\n>two\nAG\n", mtime=0)
        self.assert_alignment_contract(self.align_bytes(data).stdout,
                                       [b"one", b"two"], [b"AC", b"AG"])

    def test_empty_gzip_member_then_valid_member(self):
        data = gzip.compress(b"", mtime=0) + gzip.compress(b">one\nAC\n", mtime=0)
        self.assertEqual(self.align_bytes(data).stdout, b">one\nAC\n")

    def test_valid_gzip_member_then_empty_member(self):
        raw = b">one\nAC\n"
        data = gzip.compress(raw, mtime=0) + gzip.compress(b"", mtime=0)
        result = self.align_bytes(data, "--quiet")
        self.assertEqual(result.stdout, raw)
        self.assertEqual(result.stderr, b"")

    def test_gzip_output_can_be_decoded_independently(self):
        source = self.write("input.fa", b">one\nACGU\n")
        output = self.root / "output.fa.gz"
        result = self.run_tool("-i", source, "-o", output)
        self.assertEqual(result.stdout, b"")
        self.assertTrue(output.read_bytes().startswith(b"\x1f\x8b"))
        self.assertEqual(gzip.decompress(output.read_bytes()), b">one\nACGU\n")
        self.assert_no_artifacts("input.fa", "output.fa.gz")

    def test_output_suffix_is_literal_lowercase_gz(self):
        source = self.write("input.fa", b">one\nAC\n")
        output = self.root / "output.GZ"
        self.run_tool("-i", source, "-o", output)
        self.assertEqual(output.read_bytes(), b">one\nAC\n")

    def test_truncated_gzip_returns_4(self):
        compressed = gzip.compress(b">one\nAC\n", mtime=0)
        for size in (2, 10, len(compressed) - 8, len(compressed) - 1):
            with self.subTest(size=size):
                result = self.align_bytes(compressed[:size], expected=4)
                self.assertEqual(result.stdout, b"")

    def test_corrupt_gzip_crc_returns_4(self):
        compressed = bytearray(gzip.compress(b">one\nAC\n", mtime=0))
        compressed[-8] ^= 0x80
        result = self.align_bytes(bytes(compressed), expected=4)
        self.assertEqual(result.stdout, b"")

    def test_gzip_trailing_garbage_returns_4(self):
        compressed = gzip.compress(b">one\nAC\n", mtime=0)
        result = self.align_bytes(compressed + b"not-a-gzip-member", expected=4)
        self.assertEqual(result.stdout, b"")

    def test_truncated_later_gzip_member_returns_4(self):
        first = gzip.compress(b">one\nAC\n", mtime=0)
        second = gzip.compress(b">two\nAG\n", mtime=0)
        result = self.align_bytes(first + second[:-3], expected=4)
        self.assertEqual(result.stdout, b"")

    def test_empty_decompressed_input_is_content_error(self):
        self.align_bytes(gzip.compress(b"", mtime=0), expected=3)

    def test_corrupt_input_preserves_existing_output(self):
        source = self.write("bad.gz", gzip.compress(b">one\nAC\n", mtime=0)[:-2])
        output = self.write("output.fa", b"existing result\n")
        self.run_tool("-i", source, "-o", output, "--force", expected=4)
        self.assertEqual(output.read_bytes(), b"existing result\n")
        self.assert_no_artifacts("bad.gz", "output.fa")

    def test_help_without_files_and_without_side_effects(self):
        for flag in ("-h", "--help"):
            with self.subTest(flag=flag):
                result = self.run_tool(flag, "-i", "missing.fa", "-o", "missing-parent/output.fa", "--force")
                self.assertIn(b"--line-width", result.stdout)
                self.assertIn(b"--input", result.stdout)
                self.assertEqual(result.stderr, b"")
                self.assert_no_artifacts()

    def test_version_without_files(self):
        for flag in ("-v", "--version"):
            with self.subTest(flag=flag):
                result = self.run_tool(flag)
                self.assertIn(b"msakit 0.3.0", result.stdout)
                self.assertEqual(result.stderr, b"")
                self.assert_no_artifacts()

    def test_missing_unknown_and_empty_arguments_return_2(self):
        cases = [(), ("-i", "-"), ("-o", "-"),
                 ("-i", "-", "-o", "-", "--unknown"),
                 ("-i", "-", "-o", "-", "--threads", "2"),
                 ("-i", "", "-o", "-"), ("-i", "-", "-o", "")]
        for args in cases:
            with self.subTest(args=args):
                result = self.run_tool(*args, data=b">one\nAC\n", expected=2)
                self.assertEqual(result.stdout, b"")
                self.assertTrue(result.stderr)

    def test_invalid_line_width_returns_2(self):
        for width in ("-1", "+1", "1.5", "5x", "", " 2", "0x10", "9" * 100):
            with self.subTest(width=width):
                result = self.align_bytes(b">one\nAC\n", "--line-width", width, expected=2)
                self.assertEqual(result.stdout, b"")

    def test_line_width_default_zero_positive_and_leading_zeros(self):
        raw = b">long title which must never wrap\n" + b"A" * 81 + b"\n"
        prefix = b">long title which must never wrap\n"
        self.assertEqual(self.align_bytes(raw).stdout, prefix + b"A" * 80 + b"\nA\n")
        self.assertEqual(self.align_bytes(raw, "--line-width", "0").stdout, raw)
        self.assertEqual(self.align_bytes(b">one\nACGU\n", "--line-width", "1").stdout,
                         b">one\nA\nC\nG\nU\n")
        self.assertEqual(self.align_bytes(b">one\nACGU\n", "--line-width", "0002").stdout,
                         b">one\nAC\nGU\n")

    def test_quiet_suppresses_logs_but_keeps_errors(self):
        result = self.align_bytes(b">one\nacR\n", "--quiet")
        self.assertEqual(result.stdout, b">one\nACN\n")
        self.assertEqual(result.stderr, b"")
        failure = self.align_bytes(b">one\nA-\n", "--quiet", expected=3)
        self.assertTrue(failure.stderr)
        self.assertEqual(failure.stdout, b"")

    def test_missing_input_returns_4(self):
        result = self.run_tool("-i", "missing.fa", "-o", "out.fa", expected=4)
        self.assertTrue(result.stderr)
        self.assert_no_artifacts()

    def test_missing_output_parent_returns_4(self):
        source = self.write("input.fa", b">one\nAC\n")
        self.run_tool("-i", source, "-o", "missing/out.fa", expected=4)
        self.assert_no_artifacts("input.fa")

    def test_existing_output_requires_force(self):
        source = self.write("input.fa", b">one\nAC\n")
        output = self.write("out.fa", b"existing result\n")
        self.run_tool("-i", source, "-o", output, expected=4)
        self.assertEqual(output.read_bytes(), b"existing result\n")
        self.assert_no_artifacts("input.fa", "out.fa")

    def test_force_replaces_complete_output(self):
        source = self.write("input.fa", b">one\nAC\n")
        output = self.write("out.fa", b"existing result\n")
        self.run_tool("-i", source, "-o", output, "--force")
        self.assertEqual(output.read_bytes(), b">one\nAC\n")
        self.assert_no_artifacts("input.fa", "out.fa")

    def test_same_file_and_lexical_alias_return_2(self):
        raw = b">one\nAC\n"
        source = self.write("input.fa", raw)
        for output in (str(source), str(self.root) + "/./input.fa"):
            for force in ((), ("--force",)):
                with self.subTest(output=output, force=force):
                    self.run_tool("-i", source, "-o", output, *force, expected=2)
                    self.assertEqual(source.read_bytes(), raw)
        self.assert_no_artifacts("input.fa")

    def test_symlink_and_hardlink_alias_return_2(self):
        raw = b">one\nAC\n"
        source = self.write("input.fa", raw)
        symlink = self.root / "symlink.fa"
        symlink.symlink_to(source)
        hardlink = self.root / "hardlink.fa"
        os.link(source, hardlink)
        for alias in (symlink, hardlink):
            for force in ((), ("--force",)):
                with self.subTest(alias=alias.name, force=force):
                    self.run_tool("-i", source, "-o", alias, *force, expected=2)
                    self.assertEqual(source.read_bytes(), raw)
                    self.assertEqual(alias.read_bytes(), raw)
        self.assertTrue(symlink.is_symlink())
        self.assert_no_artifacts("input.fa", "symlink.fa", "hardlink.fa")

    def test_output_directory_is_not_replaced(self):
        source = self.write("input.fa", b">one\nAC\n")
        target = self.root / "directory"
        target.mkdir()
        (target / "sentinel").write_bytes(b"keep")
        self.run_tool("-i", source, "-o", target, "--force", expected=4)
        self.assertTrue(target.is_dir())
        self.assertEqual((target / "sentinel").read_bytes(), b"keep")
        self.assert_no_artifacts("input.fa", "directory")

    @unittest.skipUnless(Path("/dev/full").exists(), "requires Linux /dev/full")
    def test_real_stdout_write_failure_returns_4(self):
        source = self.write("input.fa", b">one\nAC\n")
        with open("/dev/full", "wb", buffering=0) as sink:
            result = subprocess.run([str(BINARY), "-i", str(source), "-o", "-", "--quiet"],
                                    stdout=sink, stderr=subprocess.PIPE, timeout=20, check=False)
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertTrue(result.stderr)

    @unittest.skipUnless(os.name == "posix", "requires POSIX pipes")
    def test_closed_pipe_returns_4_not_signal(self):
        source = self.write("input.fa", b">one\nAC\n")
        read_fd, write_fd = os.pipe()
        os.close(read_fd)  # No reader exists before the child can write.
        try:
            result = subprocess.run([str(BINARY), "-i", str(source), "-o", "-", "--quiet"],
                                    stdout=write_fd, stderr=subprocess.PIPE, timeout=20, check=False)
        finally:
            os.close(write_fd)
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertTrue(result.stderr)

    def test_invalid_core_outputs_and_exceptions_return_5(self):
        raw = b">first\nAC\n>second\nTT\n"
        source = self.write("input.fa", raw)
        output = self.write("out.fa", b"existing result\n")
        modes = ("missing", "extra", "reorder", "unequal", "changed", "invalid", "all-gap",
                 "empty-width", "throw", "badalloc", "typed-error", "unknown-exception")
        for mode in modes:
            with self.subTest(mode=mode):
                result = self.run_tool(mode, "-i", source, "-o", output, "--force", "--quiet",
                                       expected=5, binary=TEST_DRIVER)
                self.assertEqual(result.stdout, b"")
                self.assertTrue(result.stderr)
                self.assertEqual(output.read_bytes(), b"existing result\n")
                self.assertEqual(source.read_bytes(), raw)
                self.assert_no_artifacts("input.fa", "out.fa")

    def test_valid_alternate_core_passes_framework_contract(self):
        raw = b">first\nAC\n>second\nATC\n"
        result = self.run_tool("valid", "-i", "-", "-o", "-", "--quiet",
                               data=raw, binary=TEST_DRIVER)
        # Intentionally different from the reference star's A-C / ATC layout.
        self.assertEqual(result.stdout, b">first\nAC-\n>second\nATC\n")
        self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--test-driver", required=True, type=Path)
    parser.add_argument("--suite", choices=("all", "contract", "reference"), default="all",
                        help="Run reusable contracts, reference-only layouts, or both")
    options = parser.parse_args()
    BINARY = options.binary.resolve(strict=True)
    TEST_DRIVER = options.test_driver.resolve(strict=True)
    names = unittest.defaultTestLoader.getTestCaseNames(ProgramTests)
    if not REFERENCE_TESTS.issubset(names):
        raise RuntimeError("Reference test names no longer match the test methods")
    selected = [name for name in names
                if options.suite == "all"
                or (options.suite == "reference" and name in REFERENCE_TESTS)
                or (options.suite == "contract" and name not in REFERENCE_TESTS)]
    suite = unittest.TestSuite(ProgramTests(name) for name in selected)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
