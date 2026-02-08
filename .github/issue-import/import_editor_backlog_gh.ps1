[CmdletBinding()]
param(
    [string]$Repo = "",
    [string]$CsvPath = ".github/issue-import/editor_backlog_phase8.csv",
    [switch]$Execute,
    [string]$GithubToken = "",
    [switch]$AllowDuplicateTitles
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoFromOrigin {
    $url = (git remote get-url origin).Trim()
    if (-not $url) {
        throw "Could not resolve origin remote URL. Pass -Repo owner/name explicitly."
    }

    if ($url -match "github\.com[:/](?<owner>[^/]+)/(?<name>[^/.]+)(\.git)?$") {
        return "$($Matches.owner)/$($Matches.name)"
    }

    throw "Origin remote is not a GitHub URL: $url"
}

function Invoke-GhChecked([string[]]$Arguments) {
    $output = & gh @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $cmd = "gh " + ($Arguments -join " ")
        $details = (($output | ForEach-Object { "$_" }) -join [Environment]::NewLine).Trim()
        if ([string]::IsNullOrWhiteSpace($details)) {
            $details = "(no output)"
        }
        throw "$cmd failed with exit code $exitCode. $details"
    }
    return $output
}

function Test-GhAvailable {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        return $false
    }
    & gh auth status | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function Resolve-Token([string]$ExplicitToken) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitToken)) {
        return $ExplicitToken
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
        return $env:GITHUB_TOKEN
    }
    return ""
}

function Get-ApiHeaders([string]$Token) {
    $headers = @{
        "Accept" = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
    }
    if (-not [string]::IsNullOrWhiteSpace($Token)) {
        $headers["Authorization"] = "Bearer $Token"
    }
    return $headers
}

function Invoke-GithubApi([string]$Method, [string]$Path, [hashtable]$Body, [string]$Token) {
    $uri = "https://api.github.com/$Path"
    $headers = Get-ApiHeaders -Token $Token
    if ($null -eq $Body) {
        return Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers
    }
    $json = $Body | ConvertTo-Json -Depth 10
    return Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers -Body $json -ContentType "application/json"
}

function Get-MilestonesGh([string]$RepoName) {
    $json = Invoke-GhChecked -Arguments @("api", "repos/$RepoName/milestones?state=all&per_page=100")
    if ([string]::IsNullOrWhiteSpace($json)) { return @() }
    $items = $json | ConvertFrom-Json
    if (-not $items) { return @() }
    return @($items)
}

function Get-MilestonesRest([string]$RepoName, [string]$Token) {
    $items = Invoke-GithubApi -Method "GET" -Path "repos/$RepoName/milestones?state=all&per_page=100" -Body $null -Token $Token
    if (-not $items) { return @() }
    return @($items)
}

function Get-MilestoneMap([string]$RepoName, [string]$Mode, [string]$Token) {
    if ($Mode -eq "dryrun-only") {
        return @{}
    }
    $milestones = if ($Mode -eq "gh") { Get-MilestonesGh -RepoName $RepoName } else { Get-MilestonesRest -RepoName $RepoName -Token $Token }
    $map = @{}
    foreach ($m in $milestones) {
        $map[$m.title] = [int]$m.number
    }
    return $map
}

function Get-ExistingIssueTitlesGh([string]$RepoName) {
    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    $lines = Invoke-GhChecked -Arguments @(
        "api",
        "--paginate",
        "repos/$RepoName/issues?state=all&per_page=100",
        "--jq",
        '.[] | select(has("pull_request")|not) | .title'
    )
    foreach ($line in $lines) {
        $title = "$line".Trim()
        if (-not [string]::IsNullOrWhiteSpace($title)) {
            $null = $set.Add($title)
        }
    }
    return $set
}

function Get-ExistingIssueTitlesRest([string]$RepoName, [string]$Token) {
    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    $page = 1
    while ($true) {
        $items = Invoke-GithubApi -Method "GET" -Path "repos/$RepoName/issues?state=all&per_page=100&page=$page" -Body $null -Token $Token
        $batch = @($items)
        if ($batch.Count -eq 0) { break }

        foreach ($item in $batch) {
            if ($item.PSObject.Properties.Name -contains "pull_request") { continue }
            $title = "$($item.title)".Trim()
            if (-not [string]::IsNullOrWhiteSpace($title)) {
                $null = $set.Add($title)
            }
        }

        if ($batch.Count -lt 100) { break }
        $page++
    }
    return $set
}

function Get-ExistingIssueTitleSet([string]$RepoName, [string]$Mode, [string]$Token) {
    if ($Mode -eq "gh") {
        return Get-ExistingIssueTitlesGh -RepoName $RepoName
    }
    if ($Mode -eq "rest") {
        return Get-ExistingIssueTitlesRest -RepoName $RepoName -Token $Token
    }
    return [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
}

function Ensure-MilestonesExist([string]$RepoName, [string[]]$Titles, [bool]$DoExecute, [string]$Mode, [string]$Token) {
    if (-not $Titles -or $Titles.Count -eq 0) {
        return @{}
    }

    if ($Mode -eq "dryrun-only") {
        foreach ($title in $Titles) {
            if ([string]::IsNullOrWhiteSpace($title)) { continue }
            Write-Host "[DRY-RUN] Would ensure milestone exists: $title"
        }
        return @{}
    }

    $map = Get-MilestoneMap -RepoName $RepoName -Mode $Mode -Token $Token
    foreach ($title in $Titles) {
        if ([string]::IsNullOrWhiteSpace($title)) { continue }
        if ($map.ContainsKey($title)) { continue }

        if ($DoExecute) {
            Write-Host "Creating milestone: $title"
            if ($Mode -eq "gh") {
                Invoke-GhChecked -Arguments @("api", "repos/$RepoName/milestones", "--method", "POST", "--field", "title=$title") | Out-Null
            } else {
                Invoke-GithubApi -Method "POST" -Path "repos/$RepoName/milestones" -Body @{ title = $title } -Token $Token | Out-Null
            }
        } else {
            Write-Host "[DRY-RUN] Would create milestone: $title"
        }
    }

    if ($DoExecute) {
        return Get-MilestoneMap -RepoName $RepoName -Mode $Mode -Token $Token
    }
    return $map
}

if (-not (Test-Path $CsvPath)) {
    throw "CSV not found: $CsvPath"
}

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = Resolve-RepoFromOrigin
}

$token = Resolve-Token -ExplicitToken $GithubToken
$hasGh = Test-GhAvailable
$mode = if ($hasGh) { "gh" } elseif (-not [string]::IsNullOrWhiteSpace($token)) { "rest" } else { "dryrun-only" }

if ($Execute -and $mode -eq "dryrun-only") {
    throw "Cannot execute import: neither authenticated gh CLI nor GITHUB_TOKEN is available."
}

if ($mode -eq "gh") {
    Write-Host "Importer mode: gh CLI"
} elseif ($mode -eq "rest") {
    Write-Host "Importer mode: GitHub REST API (GITHUB_TOKEN)"
} else {
    Write-Host "Importer mode: dry-run only (no gh and no GITHUB_TOKEN)"
}

$rows = Import-Csv $CsvPath
if (-not $rows -or $rows.Count -eq 0) {
    Write-Host "No rows found in $CsvPath. Nothing to import."
    exit 0
}

$milestoneTitles = @(
    $rows |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_.milestone) } |
    Select-Object -ExpandProperty milestone -Unique
)

$milestoneMap = Ensure-MilestonesExist -RepoName $Repo -Titles $milestoneTitles -DoExecute:$Execute -Mode $mode -Token $token

$created = 0
$skipped = 0
$existingIssueTitles = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
if ($Execute -and -not $AllowDuplicateTitles) {
    Write-Host "Loading existing issue titles for dedupe..."
    $existingIssueTitles = Get-ExistingIssueTitleSet -RepoName $Repo -Mode $mode -Token $token
    Write-Host ("Loaded {0} existing issue title(s)." -f $existingIssueTitles.Count)
}

foreach ($row in $rows) {
    $title = "$($row.title)"
    $body = "$($row.body)"
    $milestone = "$($row.milestone)"
    $labels = @()
    if (-not [string]::IsNullOrWhiteSpace($row.labels)) {
        $labels = @($row.labels -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    }

    if (-not $Execute) {
        Write-Host ("[DRY-RUN] Would create issue: {0} (milestone: {1}, labels: {2})" -f $title, $milestone, ($labels -join ","))
        continue
    }

    if (-not $AllowDuplicateTitles -and $existingIssueTitles.Contains($title)) {
        $skipped++
        Write-Host ("Skipping duplicate [{0}/{1}] {2}" -f ($created + $skipped), $rows.Count, $title)
        continue
    }

    if ($mode -eq "gh") {
        $args = @(
            "issue", "create",
            "--repo", $Repo,
            "--title", $title,
            "--body", $body
        )

        foreach ($label in $labels) {
            $args += @("--label", $label)
        }

        if (-not [string]::IsNullOrWhiteSpace($milestone)) {
            $args += @("--milestone", $milestone)
        }

        Invoke-GhChecked -Arguments $args | Out-Null
    } elseif ($mode -eq "rest") {
        $payload = @{
            title = $title
            body = $body
        }
        if ($labels.Count -gt 0) {
            $payload["labels"] = @($labels)
        }
        if (-not [string]::IsNullOrWhiteSpace($milestone) -and $milestoneMap.ContainsKey($milestone)) {
            $payload["milestone"] = [int]$milestoneMap[$milestone]
        }
        Invoke-GithubApi -Method "POST" -Path "repos/$Repo/issues" -Body $payload -Token $token | Out-Null
    } else {
        throw "Unexpected mode '$mode' during execute."
    }

    $created++
    if (-not $AllowDuplicateTitles) {
        $null = $existingIssueTitles.Add($title)
    }
    Write-Host ("Created [{0}/{1}] {2}" -f $created, $rows.Count, $title)
}

if ($Execute) {
    Write-Host "Import complete. Created $created issue(s), skipped $skipped duplicate title(s) in $Repo."
} else {
    Write-Host "Dry-run complete. Re-run with -Execute to create issues in $Repo."
}
