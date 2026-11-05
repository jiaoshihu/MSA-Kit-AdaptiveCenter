"""Named synthetic input contracts, independent of the production parser.

Expected records and counts are explicit. No fixture calls MSA-Kit to discover
its expected result. All generated inputs are small and reproducible.
"""

from dataclasses import dataclass
import gzip
import random
import struct
from typing import Optional, Tuple
import zlib


@dataclass(frozen=True)
class InputCase:
    case_id: str
    raw: bytes
    headers: Tuple[bytes, ...] = ()
    sequences: Tuple[bytes, ...] = ()
    uppercase: int = 0
    unknown: int = 0
    exit_code: int = 0
    record: Optional[int] = None
    reason: bytes = b""
    seed: Optional[int] = None


VALID_CASES = (
    InputCase("INPUT-VALID-001", b">dna\nACGT\n>rna\nACGU\n",
              (b"dna", b"rna"), (b"ACGT", b"ACGU")),
    InputCase("INPUT-VALID-002", b">clean\nacgtunACGTUNRYSWKMBDHVryswkmbdhv012 \t\x00\xff\n",
              (b"clean",), (b"ACGTUNACGTUN" + b"N" * 27,), 6, 27),
    InputCase("INPUT-VALID-003", b">unknown\nRYSWKMBDHV012 \t\n",
              (b"unknown",), (b"N" * 15,), 0, 15),
    InputCase("INPUT-VALID-004", b">title\rinside>\x00\xff end\nA>C\n",
              (b"title inside>\x00\xff end",), (b"ANC",), 0, 1),
    InputCase("INPUT-VALID-005", b"> same name \t\nac\n> same name \t\nac\n>third\nAU\n",
              (b"same name \t", b"same name \t", b"third"),
              (b"AC", b"AC", b"AU"), 4, 0),
    InputCase("INPUT-VALID-006", b">all N\nNNnnnn\n",
              (b"all N",), (b"N" * 6,), 4, 0),
    InputCase("INPUT-VALID-007", b"\n\r\n>first\r\nac\n\nGT\r\n>second\nTu\r",
              (b"first", b"second"), (b"ACGT", b"TU"), 3, 0),
    InputCase("INPUT-VALID-008", b">spaces\n \t\v\f\rA\n",
              (b"spaces",), (b"NNNNNA",), 0, 5),
    InputCase("INPUT-VALID-009", ">序列 描述\nAé\n".encode("utf-8"),
              ("序列 描述".encode("utf-8"),), (b"ANN",), 0, 2),
    InputCase("INPUT-VALID-010", b">sequence punctuation\nA;XYZ\n",
              (b"sequence punctuation",), (b"ANNNN",), 0, 4),
    InputCase("INPUT-VALID-011", b">protein-looking bytes\nMPEPTIDE\n",
              (b"protein-looking bytes",), (b"NNNNTNNN",), 0, 7),
    InputCase("INPUT-VALID-012", b">mixed U T\nuUtT\n>other\nTUN\n",
              (b"mixed U T", b"other"), (b"UUTT", b"TUN"), 2, 0),
)


ERROR_CASES = (
    InputCase("INPUT-ERROR-001", b"", exit_code=3, reason=b"no sequence records"),
    InputCase("INPUT-ERROR-002", b"\n\r\n", exit_code=3,
              reason=b"no sequence records"),
    InputCase("INPUT-ERROR-003", b">\nAC\n", exit_code=3, record=1,
              reason=b"header"),
    InputCase("INPUT-ERROR-004", b">a\nAC\n> \t\v\f\r\nTT\n", exit_code=3,
              record=2, reason=b"header"),
    InputCase("INPUT-ERROR-005", b">a\nA\n>b\nC\n>\n", exit_code=3,
              record=3, reason=b"header"),
    InputCase("INPUT-ERROR-006", b">a\n>b\nAC\n", exit_code=3,
              record=1, reason=b"empty sequence"),
    InputCase("INPUT-ERROR-007", b">a\nAC\n>b\n\n>c\nGT\n", exit_code=3,
              record=2, reason=b"empty sequence"),
    InputCase("INPUT-ERROR-008", b">a\nAC\n>b\nGT\n>c\n\n", exit_code=3,
              record=3, reason=b"empty sequence"),
    InputCase("INPUT-ERROR-009", b">only", exit_code=3, record=1,
              reason=b"empty sequence"),
    # 010--014 were strict-FASTA rejections in 0.1.0. Their raw inputs now
    # appear as successful KSEQ-COMPAT-001--004 and KSEQ-FASTQ-001 cases.
    InputCase("INPUT-ERROR-015", b">a\nA-C\n", exit_code=3,
              record=1, reason=b"existing gap"),
    InputCase("INPUT-ERROR-016", b">a\nAC\n>b\nT\nU.\n", exit_code=3,
              record=2, reason=b"existing gap"),
    InputCase("INPUT-ERROR-017", b">a\r\nA\r\n>b\nT\n>c\r\nG-",
              exit_code=3, record=3, reason=b"existing gap"),
)

# These expectations deliberately describe the pinned kseq record semantics,
# including surprising but compatible behavior. They are not format repairs.
KSEQ_COMPAT_CASES = (
    InputCase("KSEQ-COMPAT-001", b"AC\n>a\nAC\n", (b"a",), (b"AC",)),
    InputCase("KSEQ-COMPAT-002", b" >a\nAC\n", (b"a",), (b"AC",)),
    InputCase("KSEQ-COMPAT-003", b"\xef\xbb\xbf>a\nAC\n", (b"a",), (b"AC",)),
    InputCase("KSEQ-COMPAT-004", b";comment\n>a\nAC\n", (b"a",), (b"AC",)),
    InputCase("KSEQ-COMPAT-005", b">name\t  desc\t tail \nAC\n",
              (b"name   desc\t tail ",), (b"AC",)),
    InputCase("KSEQ-COMPAT-006", b">  shared name\t long  \nAC\n",
              (b" shared name\t long  ",), (b"AC",)),
    InputCase("KSEQ-COMPAT-007", b">name comment\rinside\nAC\n",
              (b"name comment\rinside",), (b"AC",)),
    InputCase("KSEQ-COMPAT-008", b">cr\n\r\nA\r\nC\r",
              (b"cr",), (b"NAC",), 0, 1),
    InputCase("KSEQ-COMPAT-009", b">one\nA\r", (b"one",), (b"A",)),
    InputCase("KSEQ-COMPAT-010", b">one\nA\n@two\nC\n",
              (b"one", b"two"), (b"A", b"C")),
    InputCase("KSEQ-COMPAT-011", b">one\nAC\n+ignored label\nII\n",
              (b"one",), (b"AC",)),
    InputCase("KSEQ-COMPAT-012", b">internal\nA@+>C\n",
              (b"internal",), (b"ANNNC",), 0, 3),
    InputCase("KSEQ-COMPAT-013", b">one\nAC\n>", (b"one",), (b"AC",)),
    InputCase("KSEQ-COMPAT-014", b">one\nAC\n@", (b"one",), (b"AC",)),
)

KSEQ_FASTQ_CASES = (
    InputCase("KSEQ-FASTQ-001", b"@a\nAC\n+\nII\n", (b"a",), (b"AC",)),
    InputCase("KSEQ-FASTQ-002", b"@same\t description\r\nac\r\ngU\n+label\r\nI!\r\n##\n"
              b"@same\t description\nTU\n+\n55\n",
              (b"same  description", b"same  description"), (b"ACGU", b"TU"), 3),
    InputCase("KSEQ-FASTQ-003", b">first\nAC\n@second\nAU\n+\n##\n>third\nAG\n",
              (b"first", b"second", b"third"), (b"AC", b"AU", b"AG")),
    InputCase("KSEQ-FASTQ-004", b"@clean\nacR N\n+\n-.>\x00\xff\n",
              (b"clean",), (b"ACNNN",), 2, 2),
    InputCase("KSEQ-FASTQ-005", b"@last\nAC\n+\nII", (b"last",), (b"AC",)),
    InputCase("KSEQ-FASTQ-006", b"@dna\nAT\n+\n@+\n@rna\nAU\n+\n>-\n",
              (b"dna", b"rna"), (b"AT", b"AU")),
)

KSEQ_ERROR_CASES = (
    InputCase("KSEQ-ERROR-001", b"@short\nAC\n+\nI\n", exit_code=3, record=1,
              reason=b"quality length mismatch"),
    InputCase("KSEQ-ERROR-002", b"@long\nAC\n+\nIII\n", exit_code=3, record=1,
              reason=b"quality length mismatch"),
    InputCase("KSEQ-ERROR-003", b">first\nAC\n@second\nAU\n+\nI\n", exit_code=3,
              record=2, reason=b"quality length mismatch"),
    InputCase("KSEQ-ERROR-004", b"@missing\nAC\n+", exit_code=3, record=1,
              reason=b"quality length mismatch"),
    InputCase("KSEQ-ERROR-005", b"@\nAC\n+\nII\n", exit_code=3, record=1,
              reason=b"header"),
    InputCase("KSEQ-ERROR-006", b"@gap\nA-C\n+\nIII\n", exit_code=3, record=1,
              reason=b"existing gap"),
    InputCase("KSEQ-ERROR-007", b">first\nA\n+\n", exit_code=3, record=1,
              reason=b"quality length mismatch"),
    InputCase("KSEQ-ERROR-008", b"preamble without any record marker\n", exit_code=3,
              reason=b"no sequence records"),
    InputCase("KSEQ-ERROR-009", b"@empty\n+\n\n", exit_code=3, record=1,
              reason=b"empty sequence"),
    InputCase("KSEQ-ERROR-010", b"@ \t \nAC\n+\nII\n", exit_code=3, record=1,
              reason=b"header"),
)

ALL_VALID_CASES = VALID_CASES + KSEQ_COMPAT_CASES + KSEQ_FASTQ_CASES
ALL_ERROR_CASES = ERROR_CASES + KSEQ_ERROR_CASES

# Repeat these distinct failure categories for new and forced-old destinations.
LIFECYCLE_ERROR_IDS = {
    "INPUT-ERROR-001", "INPUT-ERROR-004", "INPUT-ERROR-007",
    "KSEQ-ERROR-003", "INPUT-ERROR-016",
}


def gzip_member(raw, *, extra=None, name=None, comment=None, header_crc=False):
    """Build a gzip member with explicit optional fields using raw DEFLATE."""
    flags = ((4 if extra is not None else 0) | (8 if name is not None else 0)
             | (16 if comment is not None else 0) | (2 if header_crc else 0))
    header = b"\x1f\x8b\x08" + bytes([flags]) + struct.pack("<I", 0) + b"\x00\xff"
    if extra is not None:
        header += struct.pack("<H", len(extra)) + extra
    if name is not None:
        header += name + b"\x00"
    if comment is not None:
        header += comment + b"\x00"
    if header_crc:
        header += struct.pack("<H", zlib.crc32(header) & 0xffff)
    encoder = zlib.compressobj(wbits=-15)
    payload = encoder.compress(raw) + encoder.flush()
    trailer = struct.pack("<II", zlib.crc32(raw) & 0xffffffff, len(raw) & 0xffffffff)
    return header + payload + trailer


GZIP_RAW = b">first description\r\nac\r\nGU\n>second\nTn\n"
GZIP_HEADERS = (b"first description", b"second")
GZIP_SEQUENCES = (b"ACGU", b"TN")


def valid_gzip_cases():
    options = (
        ("GZIP-VALID-001", {"extra": b"AB\x02\x00xy"}),
        ("GZIP-VALID-002", {"name": b"original.fa"}),
        ("GZIP-VALID-003", {"comment": b"synthetic fixture"}),
        ("GZIP-VALID-004", {"header_crc": True}),
        ("GZIP-VALID-005", {"extra": b"AB\x02\x00xy", "name": b"original.fa",
                            "comment": b"all fields", "header_crc": True}),
    )
    result = []
    for case_id, kwargs in options:
        result.append(InputCase(case_id, gzip_member(GZIP_RAW, **kwargs),
                                GZIP_HEADERS, GZIP_SEQUENCES, 3, 0))
    # A member boundary has no FASTA meaning: split inside a header, a CRLF,
    # and sequence text. Empty members are valid at the start/middle/end.
    split_points = (("GZIP-VALID-006", 4),
                    ("GZIP-VALID-007", GZIP_RAW.index(b"\r\n") + 1),
                    ("GZIP-VALID-008", GZIP_RAW.index(b"ac") + 1))
    for case_id, split in split_points:
        wire = (gzip_member(b"") + gzip_member(b"")
                + gzip_member(GZIP_RAW[:split]) + gzip_member(b"")
                + gzip_member(GZIP_RAW[split:]) + gzip_member(b""))
        result.append(InputCase(case_id, wire,
                                GZIP_HEADERS, GZIP_SEQUENCES, 3, 0))
    return tuple(result)


def invalid_gzip_cases():
    raw = b">valid\nAC\n"
    original = gzip_member(raw)
    cases = []

    def add(case_id, data):
        cases.append(InputCase(case_id, data, exit_code=4))

    for case_id, offset, value in (("GZIP-ERROR-001", 2, 0),
                                   ("GZIP-ERROR-002", 3, 0x20)):
        corrupted = bytearray(original)
        corrupted[offset] = value  # Invalid compression method / reserved flag.
        add(case_id, bytes(corrupted))
    with_crc = bytearray(gzip_member(raw, header_crc=True))
    with_crc[10] ^= 1
    add("GZIP-ERROR-003", bytes(with_crc))
    bad_deflate = bytearray(original)
    bad_deflate[10] = (bad_deflate[10] & ~7) | 7  # Forbidden DEFLATE block type.
    add("GZIP-ERROR-004", bytes(bad_deflate))
    for case_id, offset in (("GZIP-ERROR-005", -8), ("GZIP-ERROR-006", -4)):
        corrupted = bytearray(original)
        corrupted[offset] ^= 1
        add(case_id, bytes(corrupted))  # CRC32 / uncompressed length.
    for case_id, tail in (("GZIP-ERROR-007", b"x"), ("GZIP-ERROR-008", b"\x00"),
                          ("GZIP-ERROR-009", b"\x1f"), ("GZIP-ERROR-010", b"garbage")):
        add(case_id, original + tail)
    later = bytearray(gzip_member(b">later\nGT\n"))
    later[-8] ^= 1
    add("GZIP-ERROR-011", original + bytes(later))
    add("GZIP-ERROR-012", original + gzip_member(b">later\nGT\n")[:-1])
    add("GZIP-ERROR-013", b"\x1f\x8b\x08\x04\x00\x00\x00\x00\x00\xff\xff\xff")
    return tuple(cases)


def gzip_prefix_cases():
    original = gzip_member(b">s\nAC\n")
    return tuple(InputCase(f"GZIP-PREFIX-{size:03}", original[:size],
                           exit_code=3 if size < 2 else 4)
                 for size in range(len(original)))


GENERATOR_SEED = 20261026


def generated_cases():
    """Yield 32 prepared expectations, transformed FASTA, and member-split gzip."""
    rng = random.Random(GENERATOR_SEED)
    for index in range(1, 33):
        headers = []
        sequences = []
        pieces = []
        uppercase = 0
        for record in range(rng.randint(1, 6)):
            # Intentional duplicate headers exercise positional record identity.
            header = b"shared description" if record % 2 == 0 else f"row {record}".encode()
            sequence = bytes(rng.choice(b"ACGTUN") for _ in range(rng.randint(1, 32)))
            headers.append(header)
            sequences.append(sequence)
            spelled = bytearray(sequence)
            for position, base in enumerate(spelled):
                if rng.randrange(2):
                    spelled[position] = base + 32
                    uppercase += 1
            pieces.append(b">" + header + rng.choice((b"\n", b"\r\n")))
            offset = 0
            while offset < len(spelled):
                width = rng.randint(1, 7)
                pieces.append(bytes(spelled[offset:offset + width])
                              + rng.choice((b"\n", b"\r\n")))
                offset += width
                if rng.randrange(5) == 0:
                    pieces.append(rng.choice((b"\n", b"\r\n")))
        raw = b"".join(pieces)
        cuts = sorted(rng.sample(range(1, len(raw)), min(4, len(raw) - 1)))
        bounds = [0, *cuts, len(raw)]
        wire = b"".join(gzip.compress(raw[left:right], mtime=0)
                        for left, right in zip(bounds, bounds[1:]))
        case = InputCase(f"GENERATED-{index:03}", raw, tuple(headers), tuple(sequences),
                         uppercase, 0, seed=GENERATOR_SEED)
        yield case, wire
