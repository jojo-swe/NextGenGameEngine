# Issue Import Helpers

This folder contains helpers for importing the Phase 8 editor backlog into GitHub Issues.

## Files

- `editor_backlog_phase8.csv`: master CSV generated from Phase 8 backlog (`Editor-001` to `Editor-050`).
- `split_editor_backlog_by_sprint.ps1`: splits master CSV into per-milestone CSV files.
- `import_editor_backlog_gh.ps1`: imports CSV rows as GitHub issues using `gh` or GitHub REST API.

## Split CSV By Sprint

```powershell
pwsh .github/issue-import/split_editor_backlog_by_sprint.ps1
```

Output files are created in `.github/issue-import/` with names like:
- `editor_backlog_phase8_sprint_1.csv`
- `editor_backlog_phase8_sprint_2.csv`

## Import Issues (Dry-Run First)

Dry-run (no issues created):

```powershell
pwsh .github/issue-import/import_editor_backlog_gh.ps1
```

Create issues:

```powershell
pwsh .github/issue-import/import_editor_backlog_gh.ps1 -Execute
```

Optional explicit repo override:

```powershell
pwsh .github/issue-import/import_editor_backlog_gh.ps1 -Repo jojo-swe/NextGenGameEngine -Execute
```

Optional explicit token override (REST mode):

```powershell
pwsh .github/issue-import/import_editor_backlog_gh.ps1 -Repo jojo-swe/NextGenGameEngine -GithubToken $env:GITHUB_TOKEN -Execute
```

Notes:
- Mode selection is automatic:
  - Uses `gh` if installed and authenticated.
  - Else uses GitHub REST API when `GITHUB_TOKEN` (or `-GithubToken`) is available.
  - Else supports dry-run only.
- The script creates missing milestones automatically before issue creation in execute mode.
- In `gh` mode, non-zero `gh` command exits now fail fast (for example, missing labels), so partial imports are easier to detect.
- In execute mode, duplicate issue titles are skipped by default to keep reruns idempotent.
- Use `-AllowDuplicateTitles` to disable dedupe and force creation of duplicate titles.

## Sprint Dashboard Queries

One-click issue views for editor backlog triage:

- Sprint 1 (all): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+1%22
- Sprint 1 (P0): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+1%22+label%3A%22priority%2Fp0%22
- Sprint 2 (all): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+2%22
- Sprint 2 (P0): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+2%22+label%3A%22priority%2Fp0%22
- Sprint 3 (all): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+3%22
- Sprint 3 (P1): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+3%22+label%3A%22priority%2Fp1%22
- Sprint 4 (all): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+4%22
- Sprint 4 (P1): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+4%22+label%3A%22priority%2Fp1%22
- Sprint 4 (P2): https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+milestone%3A%22Sprint+4%22+label%3A%22priority%2Fp2%22
- All editor backlog: https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22
- Dependency-blocked items: https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+label%3A%22has-dependencies%22
- Stretch items: https://github.com/jojo-swe/NextGenGameEngine/issues?q=is%3Aissue+is%3Aopen+label%3A%22area%2Feditor%22+label%3A%22stretch%22
