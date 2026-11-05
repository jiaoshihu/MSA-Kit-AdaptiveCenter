"""EasyMSA adapter v1: validate masked output before restoring original symbols.

MSA-Kit's normal CLI and cleaning rules are unchanged. Only this explicit
Benchmark entry masks IUPAC ambiguity codes to N for the core. Timing includes
ID mapping, masking, the program itself and restoration.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

VERSION = 'msakit-easymsa-adapter-1'
ALPHABET = frozenset('ACGTURYSWKMBDHVN')
CORE_ALPHABET = frozenset('ACGTUN')


def read_records(path, aligned=False):
    records, title, chunks, seen = [], None, [], set()
    def finish():
        if title is not None:
            sequence = ''.join(chunks).upper()
            if not sequence:
                raise ValueError('Empty sequence')
            records.append((title, sequence))
    with open(path, encoding='utf-8', newline=None) as stream:
        for line in stream:
            line = line.rstrip('\r\n')
            if not line:
                continue
            if line.startswith('>'):
                finish()
                title = line[1:]
                if not title or title in seen:
                    raise ValueError('Empty or duplicate complete title')
                seen.add(title)
                chunks = []
            else:
                if title is None:
                    raise ValueError('Sequence before header')
                if any(c.upper() not in ALPHABET and not (aligned and c == '-') for c in line):
                    raise ValueError('Invalid sequence character')
                chunks.append(line)
    finish()
    if not records:
        raise ValueError('No records')
    return records


def masked_records(records):
    return [('msakit_%08d' % i,
             ''.join(c if c in CORE_ALPHABET else 'N' for c in sequence))
            for i, (_, sequence) in enumerate(records)]


def restore_records(original, aligned):
    expected = masked_records(original)
    after = dict(aligned)
    if len(after) != len(aligned) or set(after) != {name for name, _ in expected}:
        raise ValueError('Output record identity mismatch')
    widths = {len(row) for row in after.values()}
    if len(widths) != 1 or not next(iter(widths)):
        raise ValueError('Output alignment rows have different or empty lengths')
    for name, sequence in expected:
        if after[name].replace('-', '') != sequence:
            raise ValueError('Output changed the masked input sequence')
    if any(all(row[column] == '-' for row in after.values()) for column in range(next(iter(widths)))):
        raise ValueError('Output contains an all-gap column')
    restored = []
    for (name, _), (title, sequence) in zip(expected, original):
        offset = 0
        chars = []
        for char in after[name]:
            if char == '-':
                chars.append('-')
            else:
                chars.append(sequence[offset])
                offset += 1
        restored.append((title, ''.join(chars)))
    return restored


def write_records(path, records):
    with open(path, 'x', encoding='utf-8', newline='\n') as stream:
        for title, sequence in records:
            stream.write('>' + title + '\n' + sequence + '\n')


def run(input_path, output_path, threads, sequence_type, binary=None):
    if threads < 1 or sequence_type not in ('dna', 'rna'):
        raise ValueError('Expected positive threads and dna or rna')
    source, output = Path(input_path), Path(output_path)
    if source.resolve() == output.resolve() or output.exists():
        raise ValueError('Output must be a new file distinct from input')
    records = read_records(source)
    executable = Path(binary) if binary is not None else Path(__file__).resolve().parents[1] / 'build-benchmark/msakit'
    if not executable.is_file():
        raise ValueError('Executable is missing; run benchmark/build.sh first')
    # The current reference core is single threaded. THREADS is a resource
    # ceiling for replacement cores, not a promise to spawn that many threads.
    with tempfile.TemporaryDirectory(prefix='msakit-adapter-', dir=output.parent) as temp:
        temp = Path(temp)
        cleaned, aligned, final = temp / 'input.fasta', temp / 'aligned.fasta', temp / 'restored.fasta'
        write_records(cleaned, masked_records(records))
        result = subprocess.run([str(executable), '-i', str(cleaned), '-o', str(aligned)], stdin=subprocess.DEVNULL)
        if result.returncode:
            return result.returncode if result.returncode > 0 else 1
        restored = restore_records(records, read_records(aligned, aligned=True))
        write_records(final, restored)
        os.replace(final, output)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('threads', type=int)
    parser.add_argument('sequence_type', choices=['dna', 'rna'])
    args = parser.parse_args()
    try:
        return run(args.input, args.output, args.threads, args.sequence_type)
    except (ValueError, OSError, UnicodeError) as error:
        print('Benchmark adapter: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
