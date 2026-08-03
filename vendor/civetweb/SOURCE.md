# Vendored CivetWeb source

- Upstream: <https://github.com/civetweb/civetweb>
- Release: `v1.16`
- Commit: `d7ba35bbb649209c66e582d5a0244ba988a15159`
- License: MIT; see `LICENSE.md`
- Retrieved: 2026-08-03

This directory contains the unmodified minimal source set needed to embed the
C and C++ CivetWeb server APIs:

- `include/civetweb.h`
- `include/CivetServer.h`
- `src/civetweb.c`
- `src/CivetServer.cpp`
- `src/md5.inl`
- `src/sha1.inl`
- `src/sort.inl`
- `src/match.inl`
- `src/response.inl`
- `src/handle_form.inl`

`LICENSE.md` and `CREDITS.md` are copied from the same upstream tag. The
recommended Web Radar compile definitions are documented in
`external-cheat-base/src/features/web_radar/README.md`.
