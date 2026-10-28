# Phase 3 native integration repair

Draft stacked on ROM integration PR #6. Hold merge for final two-process mGBA, fresh Host/Join screenshot, and routed gameplay acceptance. The parent repository `docs/phase-3-integration-repair.md` contains the full audit, measured evidence and limitations.

Changes: bounded idle/death work; immutable ROM face tiles; native stage/authorize/nonce fencing with full 32-bit revisions and event counter floor; permanent overflow lock; one finalized CRC per save transaction; complete protected partial saves; bounds/duplicate/generation validation of native flash recovery. Existing mailbox layout and save metadata version are retained. An immutable `SL_RUN_FENCE_V2` marker and capability distinguish compatible builds. Developer fixture hooks and sprite queue instrumentation do not link into release builds.

Generate all four raw tile variants using `python3 tools/generate_soullink_faces.py` with built gbagfx tools. Only the configured variant links. Generic descriptor animation must remain paused for raw tile pointers; the native icon callback owns frame advancement.

Validation: normal/release/dev builds, 49 SoulLink tests, 4 RunIntegrity tests, 9 personalized RunProfile tests through the parent adapter, 3 SaveBlock3 tests and 1 PokemonStorage test passed. Native VRAM test checks all complete selected and animated frames over 120 iterations; sprite-copy queue highwater 55/64. Idle maximum 0.244 ms, worst tested bounded full-Graveyard iteration 1.343 ms. Sixteen commands require 1776 iterations, so enforcement completion can take about 29.6 seconds at 60 calls/sec. Authorization stays locked until completion. Face array recovery is 16384 bytes, net EWRAM improvement 16376 bytes after worker overhead.

The final R2 compatibility additions have native/transport coverage but await a fresh composed two-GUI run. mGBA rollback detection occurs at the next observed frame; no reliable pre-restored-frame lock is certified. A crash before local durable event export is an unproved capture window. Do not certify arbitrary offline emulator manipulation protection. No issue closure or Phase 4 work.
