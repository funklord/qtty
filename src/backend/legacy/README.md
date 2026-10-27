# Legacy backend adapters (Phase 1)

One subdirectory per product: each adapts an existing TUI implementation's
terminal layer to `Qtty::ITerminalBackend` (§5.1). Escape decoding stays in
the legacy code — that is where its accumulated bug fixes live (§5.1);
convergence onto one backend is Phase 5 and optional.

    legacy/
      app1/   ...adapter over product 1's terminal core
      app2/   ...

No adapter was ever written -- there is no `app1/` or `app2/` -- so the
reference backend this would be measured against has nothing to measure.
project.md 8.612 records that and leaves the decision with the copyright
holder.
