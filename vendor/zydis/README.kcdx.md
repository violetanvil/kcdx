Zydis 4.1.0 (amalgamated single-file distribution from the v4.1.0 GitHub release).

Vendored as the runtime instruction decoder safetyhook requires (RIP-relative
fixup + safe prologue relocation at hook-install time). kcdx never calls Zydis
directly; safetyhook consumes it internally. MIT license — the notice is carried
inline at the top of Zydis.h / Zydis.c. Update by dropping in a newer
zydis-amalgamated.tar.gz from the Zydis releases page.
