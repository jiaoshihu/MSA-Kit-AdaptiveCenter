# Sources and third-party notices

**English** | [简体中文](THIRD_PARTY_NOTICES.zh-CN.md)

MSA-Kit is distributed under the [MIT license](LICENSE). The following upstream
notices apply to copied or adapted material and are retained separately where
the upstream license differs.

## HAlign-5

Source: [pinglu-zhang/HAlign-5, commit
`b0d5608811d44e30bc323da54f0583ceaaf684d2`](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2).

The nucleotide-cleaning table in `src/input.cpp` is adapted from
`include/utils.h` at that revision. Its notice is **Copyright (c) 2026 HAlign-5
contributors**. The complete MIT permission and warranty text is included in
[LICENSE](LICENSE), together with the upstream copyright attribution.

The reader/writer organization, CLI conventions, and separation of preparation
from alignment informed this template's design. The kseq record reader follows
HAlign-5's `src/utils/seq_io.cpp` use of `kseq_init`, `kseq_read`, and
`kseq_destroy`, including name/comment title reconstruction and ignored FASTQ
qualities. The gzip stream handling, output transaction, and reference star core implement
MSA-Kit's documented contract. The provenance table in
[the design specification](docs/design.md) describes the retained concepts and
intentional differences. HAlign-5's other algorithms, datasets, and external
tool workflow are not bundled.

## Bundled header-only dependencies

The CLI11, spdlog, and doctest header trees are copied unchanged from the same
pinned HAlign-5 snapshot. Their version macros identify the listed releases. Upstream license
files missing from that snapshot were obtained from the corresponding release
tags; their Git blob identities are recorded as well.

| Component | Version | License and retained notice |
| --- | --- | --- |
| [CLI11](https://github.com/CLIUtils/CLI11/tree/v2.6.1) | 2.6.1 | [BSD-3-Clause license](extern/CLI11/LICENSE); University of Cincinnati and contributors |
| [spdlog](https://github.com/gabime/spdlog/tree/v1.16.0) | 1.16.0 | [MIT license](extern/spdlog/LICENSE); Gabi Melman and contributors |
| fmt, bundled with spdlog | As included in the pinned snapshot | [Retained fmt license](extern/spdlog/include/spdlog/fmt/bundled/fmt.license.rst) |
| [doctest](https://github.com/doctest/doctest/tree/v2.4.12) | 2.4.12 | [MIT license](extern/doctest/LICENSE.txt); Viktor Kirilov |
| [kseq](https://github.com/pinglu-zhang/HAlign-5/blob/b0d5608811d44e30bc323da54f0583ceaaf684d2/include/kseq.h) | Upstream header dated 30JAN2023, with the safety patch below | [MIT notice retained in the header](extern/kseq/kseq.h); Attractive Chaos, 2008, 2009, 2011 |

[extern/provenance.json](extern/provenance.json) records the source revision,
component versions, license-source identities, and SHA-256 of every bundled
header and license file. Build configuration never fetches newer versions.
doctest is used only when tests are enabled.

### Local kseq safety patch

The original `include/kseq.h` SHA-256 is
`9279bfe0f8bfcfe507d2852b838d2156d793f70b76ae9c63e3f6d381239ba901`.
The manifest records that original hash and Git blob separately from the hash
of the patched file distributed here. The retained MIT notice covers the
upstream code; local maintenance is under MSA-Kit's MIT license.

The patch checks initialization and expansion failures, frees partial
initialization, preserves owned pointers when `realloc` fails, and checks
lengths against `INT_MAX` before addition or narrowing. It propagates stream
errors through all parsing stages, including the quality loop. A `-4` return
value denotes allocation or length failure; the framework maps it to exit code
5. Negative/invalid read callbacks produce `-3`, mapped to exit code 4.
Compile-time allocator hooks let module tests trigger these failures without
production flags or environment settings. Macro whitespace is reformatted for
maintenance; normal record semantics remain those of the pinned header.

The adapter supplies bytes from MSA-Kit's strict gzip decoder. It does not use
`gzread` to relax gzip integrity checks, and it does not preparse or re-encode
records before passing them to kseq.

## System dependency

zlib is linked as a required system library and is not vendored here. Its own
license remains applicable; see [zlib's license](https://zlib.net/zlib_license.html).
Python 3 is used by the optional test suite and the explicit website adapter; the C++ executable does not require it. No third-party license is
replaced by MSA-Kit's license.
