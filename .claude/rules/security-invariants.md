---
paths:
  - "**/*"
---

# Security invariants — a security-critical guarantee is unconditional, declared, and tested

Applies ONLY to a repo with a security boundary — one that authenticates a peer, encrypts a channel, handles a secret/credential, or makes a trust decision. Such a repo declares its security invariants, holds them on EVERY code path with no conditional relaxation, and tests + reviews each. The repo names its concrete crypto suite, trust model, secret-storage backend, and the invariant list in the relevant append; this rule is the floor that every named invariant clears. A repo with no security boundary does not invoke this rule.

## A security invariant is unconditional — no path relaxes it

A security-critical guarantee (the channel is encrypted, the peer is authenticated, the secret stays out of a readable store) holds on **every** code path — there is no "trusted network" shortcut, no LAN-only exception, no plaintext fallback, no debug/development mode that disables it. A development build uses a self-signed credential, never a disabled gate. The phrase "this path is internal, so it's safe to skip" is the failure mode; an invariant that holds only on the remote path and not the loopback path is not an invariant.

## Authenticate before any privileged action — strict, unskippable order

Authentication completes BEFORE any session/privileged data is exchanged or any privileged operation runs. The sequence is fixed: a connection that fails authentication is rejected at the authentication step, before any application logic reads a single self-asserted field from the peer. No step is skipped or reordered. A self-reported field on the wire (a claimed identity, a capability flag, an auto-accept intent) is inert until AFTER the peer is cryptographically established — it is read only post-authentication, never as the basis for granting trust.

## No trust escalation from connection properties

Passing authentication grants exactly the scoped capability the trust model defines — never more. An authenticated peer is not thereby trusted to execute arbitrary commands, read outside its protocol's data scope, modify configuration, or bypass caps/rate-limits. Trust is keyed to the **verified** identity (the validated credential/fingerprint), never to a self-reported identifier. A property of the connection (same subnet, came-in-on-the-trusted-port) NEVER widens who counts as trusted.

## Secrets never enter a readable or syncable store

A secret (a private key, a password/PIN hash, a session token, a session key) lives in the platform's protected secret store or in memory only — never in a user-editable config file, a log line, an error message, or any artifact that may be backed up, synced, or committed. A config file holds only non-secret references (public identifiers, public certificate material). A credential identifier surfaced in a log/error is truncated to a non-reconstructable prefix, never the full material. Network/untrusted data is never passed to a privileged or command-executing API (the inward floor is `.claude/rules/input-validation.md`).

## Every invariant is tested and reviewed against — same change

Each declared security invariant ships a test that asserts it holds (a test proving the gate rejects the unauthenticated peer, that the secret is absent from the config/log surface, that the relaxation path does not exist) — same change as the code, the same completeness bar as `.claude/rules/test-discipline.md`. A change touching a security boundary is reviewed against the specific invariant it could violate, not merely "it builds": build-green is necessary, not sufficient, for a security path (`.claude/rules/skeptical-expert.md`).

## What this is NOT

- NOT `input-validation.md` — that owns untrusted data crossing a trust boundary INWARD (validate length/content before allocating, no command-exec from received data); this owns the trust/crypto guarantees themselves (the channel is encrypted, the peer is authenticated, the secret is protected). A boundary clears both: it validates inbound data AND holds its security invariants.
- NOT `public-private-boundary.md` — that owns private information crossing a publication boundary OUTWARD (no private reference in a public file); this owns the runtime security guarantees of a live system.
- NOT a mandate that every repo have security invariants — only a repo with a security boundary invokes it. A repo with no auth/crypto/secret/trust surface does not.
- NOT the repo's concrete crypto suite / trust model / secret-store backend / invariant list — those are the repo's (the relevant append). This rule is the floor each named invariant clears.
