---
paths:
  - "include/kcdx/**"
  - "src/interfaces.*"
  - "src/messaging.*"
  - "src/serialization.*"
  - "src/console.*"
---

# SKSE parity — naming and interface shape

kcdx is a deliberate SKSE / F4SE port. Naming, structure, plugin API shape, lifecycle messages — all mirror SKSE with `kcdx` substituted for `SKSE`.

## Rules

- **When in doubt about a naming choice, check what SKSE does and copy it.**
- **Adding a plugin interface**: mirror an existing SKSE interface from `CommonLibSSE-NG/Interfaces.h`. Do not invent.
- **Interface versioning**: bump `kcdx<Name>Interface_Version` when the struct changes.
- **System messages** mirror SKSE's: `kcdxMessage_SaveGame`, `kcdxMessage_PreLoadGame`, `kcdxMessage_PostLoadGame`, etc.

## References

- skse64/PluginAPI.h: https://github.com/ianpatt/skse64/blob/master/skse64/PluginAPI.h
- CommonLibSSE-NG/Interfaces.h: https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/SKSE/Interfaces.h
- skyrim.dev/skse/system-messages
