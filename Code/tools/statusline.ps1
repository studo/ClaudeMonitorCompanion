<#
    Claude Code status line for Windows, feeding the mascot its rate limits.

    Set as statusLine.command in %USERPROFILE%\.claude\settings.json:

        powershell -NoProfile -ExecutionPolicy Bypass -File C:/path/to/statusline.ps1 <board-host>[,<host>...] <token>

    Several hosts, comma-separated, feed several mascots. gen_hooks.py writes
    this line for you.

    Note the forward slashes. Claude Code runs the status line through Git Bash
    when Git Bash is installed, and Git Bash eats unquoted backslashes in a
    Windows path, failing with no visible error.

    This is the Windows twin of statusline.py. It exists because statusLine has
    no exec form -- unlike hooks, it is always run through a shell -- so the
    command cannot be made byte-identical across platforms the way the hooks
    are. Python is not guaranteed on Windows either, whereas PowerShell is, so
    this version has no dependencies at all beyond curl.exe, which has shipped
    with Windows since 1809.

    Why the board needs this at all: the five-hour and seven-day limit figures
    exist only in the status line's JSON. No hook payload carries them.

    Two details are load-bearing, same as the Python version:

      Fire and forget. Claude Code cancels an in-flight status line script when
      a new update arrives, so a blocking HTTP call would be killed part-way
      and, with the board off, would stall the status bar on every run.

      The line prints regardless. If the board is unreachable or the arguments
      are wrong, the status line still renders. A desk ornament must never be
      able to break the editor it is decorating.
#>

param(
    [string]$BoardHost,
    [string]$Token
)

$raw = [Console]::In.ReadToEnd()

try {
    $d = $raw | ConvertFrom-Json
} catch {
    Write-Output "claude"
    exit 0
}

function Get-Pct($bucket) {
    if ($null -eq $bucket) { return $null }
    $v = $bucket.used_percentage
    if ($null -eq $v) { return $null }
    return [int][math]::Round([double]$v)
}

function Get-Reset($bucket) {
    if ($null -eq $bucket -or $null -eq $bucket.resets_at) { return 0 }
    return [int64]$bucket.resets_at
}

# --- forward the figures, without ever blocking -----------------------------
if ($BoardHost) {
    try {
        $params = @()
        $five  = $d.rate_limits.five_hour
        $seven = $d.rate_limits.seven_day

        $p = Get-Pct $five
        if ($null -ne $p) {
            $params += "pct=$p"
            $params += "reset=$(Get-Reset $five)"
        }
        $w = Get-Pct $seven
        if ($null -ne $w) {
            $params += "wpct=$w"
            $params += "wreset=$(Get-Reset $seven)"
        }

        if ($params.Count -gt 0) {
            $query = $params -join "&"
            foreach ($h in ($BoardHost -split ",")) {
                $h = $h.Trim()
                if (-not $h) { continue }
                # Start-Process returns immediately; the request outlives this
                # script, which is the whole point.
                Start-Process -FilePath "curl.exe" -WindowStyle Hidden -ArgumentList @(
                    "-s", "-m", "2", "-X", "POST",
                    "-H", "Authorization: Bearer $Token",
                    "http://$h/limits?$query"
                ) | Out-Null
            }
        }
    } catch {
        # Never let a forwarding problem reach the status bar.
    }
}

# --- draw the line ----------------------------------------------------------
$parts = @()

try { if ($d.model.display_name) { $parts += $d.model.display_name } } catch {}
try {
    if ($d.workspace.current_dir) {
        $parts += (Split-Path -Leaf $d.workspace.current_dir)
    }
} catch {}
try {
    if ($null -ne $d.context_window.used_percentage) {
        $parts += ("ctx {0}%" -f [int][double]$d.context_window.used_percentage)
    }
} catch {}
try {
    $p = Get-Pct $d.rate_limits.five_hour
    if ($null -ne $p) {
        $filled = [int][math]::Round($p / 10)
        $bar = "[" + ("#" * $filled) + ("-" * (10 - $filled)) + "]"
        $parts += ("5h {0} {1}%" -f $bar, $p)
    }
} catch {}

if ($parts.Count -eq 0) { $parts += "claude" }
Write-Output ($parts -join " | ")
