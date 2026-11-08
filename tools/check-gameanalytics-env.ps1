param([switch]$Required)

$ErrorActionPreference = 'Stop'
$names = @('NOVA_GA_GAME_KEY', 'NOVA_GA_SECRET_KEY')
$missing = @($names | Where-Object {
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
})

if ($missing.Count -eq 0) {
    Write-Output 'GameAnalytics build credentials are configured.'
} elseif ($Required -or $missing.Count -ne $names.Count) {
    throw ('Missing GameAnalytics build environment variables: ' + ($missing -join ', '))
} else {
    Write-Output 'GameAnalytics is disabled for this build; no credentials were supplied.'
}
