# Legacy backend adapters (Phase 1)

One subdirectory per product: each adapts an existing TUI implementation's
terminal layer to `qtty::ITerminalBackend` (§5.1). Escape decoding stays in
the legacy code — that is where its accumulated bug fixes live (§5.1);
convergence onto one backend is Phase 5 and optional.

    legacy/
      app1/   ...adapter over product 1's terminal core
      app2/   ...
