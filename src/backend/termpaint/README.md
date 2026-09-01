# termpaint backend (Phase 2/5)

Reference `ITerminalBackend` over termpaint (§5.1): capability detection,
escape decoding, and presentation delegated to termpaint's integration API.
The yardstick the legacy backends are measured against and the default for
new products. Not yet implemented; the in-tree `AnsiBackend`
(`src/backend/ansi`) covers Phase 0/2 needs until this lands. This file
said `AnsiRuntime` -- a class the tree stopped having in 73fdee6.
