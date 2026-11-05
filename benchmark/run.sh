#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 4 ]; then
  echo 'Usage: benchmark/run.sh INPUT OUTPUT THREADS dna|rna' >&2
  exit 2
fi
cd "$(dirname "$0")/.."
exec python3 benchmark/adapter.py "$1" "$2" "$3" "$4"
