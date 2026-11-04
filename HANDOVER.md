# HANDOVER.md

Överlämning mellan agenter och sessioner. Läs innan du börjar, uppdatera innan du slutar.

## Aktuellt läge

*Senast inventerat 2026-11-04 av Claude (Cowork).*

Projekt: Rigid transforms are PGA motors: eight floats that carry rotation and translation together.

- Huvudbranch: `main`
- Senaste commit på `main`: 2026-10-24 `efc8c82` Make editor foundation and G0 build/test chain reliable (#56)
- Öppna PR:er: 1
- Andra brancher på remote: 4

## Pågående arbete

| Agent/vem | Branch | Status | Beskrivning |
|-----------|--------|--------|-------------|
| Cursor | `cursor/privacy-guard-d879` | PR #57 öppen, uppdaterad 2026-11-01 | Add a privacy guard for commits and CI |
| Codex | `codex/editor-foundation` | branch utan PR, senaste commit 2026-10-24 | Fix integrated buffer suballocator split |
| Cursor | `cursor/readme-pga-hook-18c0` | branch utan PR, senaste commit 2026-10-16 | docs: lead the README with PGA motors |
| Claude | `claude/3d-engine-architecture-8dadf7` | branch utan PR, senaste commit 2026-07-22 | fix: Exclude Windows-only editor smoke test from non-Windows builds |

## Nästa steg

- Granska och besluta om de öppna PR:erna ovan (merga eller stäng).
- Gå igenom brancher utan PR: öppna PR, merga eller radera (fråga användaren innan radering).
- Fyll på "Bygga och testa" i AGENTS.md om något saknas eller är fel.

## Kända problem

- Inga dokumenterade ännu.

## Senaste commits

- 2026-10-24 `efc8c82` Make editor foundation and G0 build/test chain reliable (#56)
- 2026-10-16 `560ab1f` docs: lead the README with PGA motors (#55)
- 2026-07-22 `a76aefe` Merge pull request #54 from jojo-swe/claude/3d-engine-architecture-8dadf7
- 2026-07-22 `ff80187` fix: Exclude Windows-only editor smoke test from non-Windows builds
- 2026-07-22 `a2f86b1` fix: Guard Windows-only GetInstanceHandle in editor smoke test
- 2026-07-11 `e2dbdd3` Merge pull request #53 from jojo-swe/claude/3d-engine-architecture-8dadf7
- 2026-07-11 `68c1859` Merge remote branch (parallel Codex conflict resolution)
- 2026-07-11 `7179dc0` Merge origin/main (GPU upload implementation); keep real glTF importer
- 2026-07-11 `e1006a1` Merge branch 'main' into codex/fix-pr-conflicts
- 2026-07-11 `33a00a8` Merge remote-tracking branch 'origin/claude/3d-engine-architecture-8dadf7' into codex/fix-pr-conflicts

## Logg

| Datum | Agent | Vad som gjordes |
|-------|-------|-----------------|
| 2026-11-04 | Claude (Cowork) | Lade till AGENTS.md, HANDOVER.md och CLAUDE.md; inventerade PR:er, brancher och commits. |
