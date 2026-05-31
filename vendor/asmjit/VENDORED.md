# asmjit (vendored)

Flat copy of [github.com/asmjit/asmjit](https://github.com/asmjit/asmjit)
at commit `0bd5787` (HEAD of master as of 2026-05-18 — asmjit doesn't tag
releases).

License: Zlib. See [LICENSE.md](LICENSE.md).

## How this was vendored

```sh
git clone --depth 1 https://github.com/asmjit/asmjit.git
cd asmjit
rm -rf .git asmjit-testing tools db
rm -f configure.sh configure_sanitizers.sh configure_vs2022_x64.bat \
      configure_vs2022_x86.bat CMakePresets.json CONTRIBUTING.md
```

The remaining tree:
- `CMakeLists.txt`, `LICENSE.md`, `README.md` — upstream build config + docs
- `asmjit/` — the actual source (5 subdirs: `core`, `support`, `arm`, `x86`,
  `ujit`). x86-64 is what kcdx uses; the others get dead-stripped at link.

## How kcdx consumes it

Via `add_subdirectory(vendor/asmjit)` in the top-level CMakeLists.txt. The
asmjit target is linked into kcdx.asi. We don't expose asmjit symbols to
plugin authors — it's an engine-internal dep for the typed-marshaling
trampolines used by the `kcdx.hook` interface with Lua callbacks and by the mid-function hook interface.

## When to update

Bump when:
- A new asmjit release fixes a bug we care about
- An upstream API change breaks our `rom-borrowed/asmjit_helper.cpp`
  adapter (unlikely — asmjit ABI is stable)

Procedure: re-run the clone-and-strip commands above, replace this
directory, update the commit SHA in the first line of this file.
