[CmdletBinding()]
param(
    [string]$CsvPath = ".github/issue-import/editor_backlog_phase8.csv",
    [string]$OutputDir = ".github/issue-import"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path $CsvPath)) {
    throw "CSV not found: $CsvPath"
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$rows = Import-Csv $CsvPath
if (-not $rows -or $rows.Count -eq 0) {
    throw "No rows found in $CsvPath"
}

$groups = $rows | Group-Object milestone
$generated = @()

foreach ($g in $groups) {
    $name = if ([string]::IsNullOrWhiteSpace($g.Name)) { "backlog" } else { $g.Name }
    $safe = ($name.ToLower() -replace "[^a-z0-9]+", "_").Trim("_")
    if ([string]::IsNullOrWhiteSpace($safe)) { $safe = "backlog" }

    $outPath = Join-Path $OutputDir ("editor_backlog_phase8_{0}.csv" -f $safe)
    $g.Group | Export-Csv -Path $outPath -NoTypeInformation -Encoding utf8
    $generated += [pscustomobject]@{
        milestone = $name
        path      = $outPath
        count     = $g.Count
    }
}

$generated | Sort-Object milestone | ForEach-Object {
    Write-Host ("{0}: {1} row(s) -> {2}" -f $_.milestone, $_.count, $_.path)
}
