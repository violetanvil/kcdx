# Third-party licenses

The license manifest for kcdx's vendored / third-party dependencies
(`.claude/rules/dependencies.md` — every dependency is recorded with its name,
version, license, and one-line purpose, in the same change that adds it). Each
license is verified against the vendored source in `vendor/<name>/`, not recalled.

| Name | Version | License | Purpose |
|---|---|---|---|
| miniz | 11.0.2 | Public domain (Unlicense) | DEFLATE inflater for kcdx's own PKZIP/DEFLATE pak reader (file-system takeover) + crash-bundle zip (watchdog). |

## Notes

- **miniz** — license verified from `vendor/miniz/miniz.c`'s end-of-file license
  block (the "This is free and unencumbered software released into the public
  domain … For more information, please refer to <http://unlicense.org/>"
  dedication, i.e. the Unlicense), and the version string `MZ_VERSION "11.0.2"`
  in `vendor/miniz/miniz.h`. (The `vendor/miniz/miniz.h` top banner comment
  carries a stale upstream "miniz.c 3.0.0" header line; the authoritative
  version macro is 11.0.2.) The Copyright lines in the source headers (RAD Game Tools / Valve /
  Rich Geldreich / Tenacious Software / Martin Raiber) are the authorship credits
  accompanying the public-domain dedication — the Unlicense is the operative grant.
