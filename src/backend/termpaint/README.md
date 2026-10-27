# termpaint backend (Phase 2/5)

Reference `ITerminalBackend` over termpaint (§5.1): capability detection,
escape decoding, and presentation delegated to termpaint's integration API.
The yardstick the legacy backends are measured against and the default for
new products. Not yet implemented; the in-tree `AnsiBackend`
(`src/backend/ansi`) covers Phase 0/2 needs until this lands. This file
said `AnsiRuntime` -- a class the tree stopped having in 73fdee6.

Unstarted as of 2026-10-27, and project.md 8.612 measures upstream and
asks whether this plan is still wanted: BSL-1.0, release 0.3.1 bumped
2025-04-01, four portability commits since, and upstream's own note that
"breaking changes are still happening". The decision is the copyright
holder's; this file describes an option, not a commitment.
