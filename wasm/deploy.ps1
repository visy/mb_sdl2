# Deploy Mine Bombers to quad.fi.
#   - Static game files -> /usr/share/nginx/html/mb/   (rm -rf safe; locked path)
#   - Signal server     -> /opt/mb-signal/             (Node WS, systemd unit)
#
# HARD-LOCKED paths. No flag overrides. Refuses if hardcoded constants drift.
#
# Requires PuTTY tools (plink.exe, pscp.exe) on PATH.
# Install: winget install PuTTY.PuTTY
#
# Usage:  .\deploy.ps1                     # interactive prompts; deploys both
#         .\deploy.ps1 -NoBuild            # skip emcc rebuild
#         .\deploy.ps1 -StaticOnly         # skip signal server upload
#         .\deploy.ps1 -SignalOnly         # skip static upload (signal only)
#
# First-time setup on quad.fi: see signal/README.deploy.md.

param(
    [string]$RemoteHost = 'quad.fi',
    [string]$Username   = '',
    [switch]$NoBuild,
    [switch]$StaticOnly,
    [switch]$SignalOnly
)

$ErrorActionPreference = 'Stop'

# --- Hard-coded deploy targets. DO NOT parameterize. ---
$STATIC_DEST = '/usr/share/nginx/html/mb'
$SIGNAL_DEST = '/opt/mb-signal'
# -------------------------------------------------------

# Safety nets.
if ($STATIC_DEST -notmatch '^/usr/share/nginx/html/mb(/|$)') {
    throw "STATIC_DEST must stay under /usr/share/nginx/html/mb. Got: $STATIC_DEST"
}
if ($STATIC_DEST.TrimEnd('/') -eq '/usr/share/nginx/html') {
    throw "STATIC_DEST cannot be nginx root."
}
if ($SIGNAL_DEST -notmatch '^/opt/mb-signal(/|$)') {
    throw "SIGNAL_DEST must stay under /opt/mb-signal. Got: $SIGNAL_DEST"
}

$here      = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Resolve-Path (Join-Path $here '..')
$buildDir  = Join-Path $here 'build'
$signalDir = Join-Path $repoRoot 'signal'

foreach ($tool in 'plink','pscp') {
    if (-not (Get-Command "$tool.exe" -ErrorAction SilentlyContinue)) {
        throw "$tool.exe not on PATH. Install PuTTY: winget install PuTTY.PuTTY"
    }
}

$doStatic = -not $SignalOnly
$doSignal = -not $StaticOnly

if ($doStatic -and -not $NoBuild) {
    Write-Host "[build] emcc"
    & (Join-Path $here 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

if ($doStatic -and -not (Test-Path $buildDir)) { throw "no build/ dir. Run build.ps1 first." }
if ($doSignal -and -not (Test-Path $signalDir)) { throw "no signal/ dir at $signalDir" }

if (-not $Username) { $Username = Read-Host "SSH username for $RemoteHost" }
$sshPwSec  = Read-Host "SSH password for $Username@$RemoteHost" -AsSecureString
$rootPwSec = Read-Host "root password (sudo) on $RemoteHost"   -AsSecureString

function Unwrap([System.Security.SecureString]$s) {
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s)
    try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

$sshPw  = Unwrap $sshPwSec
$rootPw = Unwrap $rootPwSec
$target = "$Username@$RemoteHost"
$staging = "/tmp/mb_deploy_$([guid]::NewGuid().ToString('N').Substring(0,8))"

$plinkBase = @('-ssh','-batch','-pw',$sshPw,$target)
$pscpBase  = @('-batch','-pw',$sshPw)

# Cache host key on first connect. -batch refuses the prompt, so do one
# non-batch handshake piping 'y' to accept. PuTTY stores it in the registry
# under HKCU\Software\SimonTatham\PuTTY\SshHostKeys.
#
# Don't pipe stderr to PS: $ErrorActionPreference=Stop turns NativeCommandError
# into a thrown exception even when we want to inspect $LASTEXITCODE.
Write-Host "[hostkey] probing $RemoteHost"
$ok = $true
try {
    & plink.exe -ssh -batch -pw $sshPw $target "echo ok" *> $null
    if ($LASTEXITCODE -ne 0) { $ok = $false }
} catch { $ok = $false }
if (-not $ok) {
    Write-Host "[hostkey] not cached -> accepting fingerprint on first use"
    try {
        "y`n" | & plink.exe -ssh -pw $sshPw $target "echo cached" *> $null
    } catch {}
    try {
        & plink.exe -ssh -batch -pw $sshPw $target "echo ok" *> $null
    } catch {}
    if ($LASTEXITCODE -ne 0) { throw "host key still not cached after accept" }
}

# Helper: pipe N copies of rootPw into a remote script for sudo -S.
function Invoke-Remote([string]$script, [int]$sudoCount) {
    $stdin = ($rootPw + "`n") * $sudoCount
    $stdin | & plink.exe @plinkBase $script
    if ($LASTEXITCODE -ne 0) { throw "remote script failed (exit $LASTEXITCODE)" }
}

try {
    Write-Host "[stage] $staging"
    & plink.exe @plinkBase "mkdir -p $staging/static $staging/signal"
    if ($LASTEXITCODE -ne 0) { throw "ssh mkdir failed" }

    if ($doStatic) {
        Write-Host "[upload] static build/ -> $staging/static"
        & pscp.exe @pscpBase -r "$buildDir\*" "${target}:$staging/static/"
        if ($LASTEXITCODE -ne 0) { throw "pscp static failed" }
    }

    if ($doSignal) {
        Write-Host "[upload] signal/ Go source -> $staging/signal"
        $signalFiles = @(
            'server.go','go.mod','go.sum',
            'mb-signal.service','nginx-mb.conf'
        )
        $existing = $signalFiles | Where-Object { Test-Path (Join-Path $signalDir $_) } |
                                   ForEach-Object { Join-Path $signalDir $_ }
        & pscp.exe @pscpBase $existing "${target}:$staging/signal/"
        if ($LASTEXITCODE -ne 0) { throw "pscp signal failed" }
    }

    Write-Host "[install] sudo apply on $RemoteHost"
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("set -e")
    # Non-interactive plink shells don't source /etc/profile.d/*. Inject the
    # standard go install location so `command -v go` finds it.
    $null = $sb.AppendLine('export PATH=/usr/local/go/bin:/usr/local/bin:/usr/bin:/bin:$PATH')

    if ($doStatic) {
        $null = $sb.AppendLine(@"
STATIC='$STATIC_DEST'
case "`$STATIC" in
  /usr/share/nginx/html/mb|/usr/share/nginx/html/mb/*) ;;
  *) echo "refusing static dest: `$STATIC" >&2; exit 99 ;;
esac
sudo -S -p '' mkdir -p "`$STATIC"
sudo -S -p '' sh -c 'rm -rf "'"`$STATIC"'"/* && cp -r "$staging/static"/* "'"`$STATIC"'"/ && (chown -R nginx:nginx "'"`$STATIC"'" 2>/dev/null || chown -R www-data:www-data "'"`$STATIC"'" 2>/dev/null || true)'
echo "static OK"
"@)
    }

    if ($doSignal) {
        $null = $sb.AppendLine(@"
SIGNAL='$SIGNAL_DEST'
case "`$SIGNAL" in
  /opt/mb-signal|/opt/mb-signal/*) ;;
  *) echo "refusing signal dest: `$SIGNAL" >&2; exit 99 ;;
esac
# Verify go is installed (server compiles its own binary).
command -v go >/dev/null 2>&1 || { echo "go not installed on server. apt install golang-go" >&2; exit 98; }
# Ensure dir + user exist (idempotent).
sudo -S -p '' id mbsignal >/dev/null 2>&1 || sudo -S -p '' useradd --system --home "`$SIGNAL" --shell /usr/sbin/nologin mbsignal
sudo -S -p '' mkdir -p "`$SIGNAL/src"
# Drop sources into a build subdir, build, install binary to top dir.
sudo -S -p '' sh -c 'cp -f "$staging/signal/server.go" "$staging/signal/go.mod" "'"`$SIGNAL"'"/src/'
[ -f "$staging/signal/go.sum" ] && sudo -S -p '' cp -f "$staging/signal/go.sum" "`$SIGNAL/src/"
sudo -S -p '' chown -R mbsignal:mbsignal "`$SIGNAL"
# Build as the service user. sudo strips PATH, so re-inject it via env so
# /usr/local/go/bin (tarball install) is visible to the child shell.
sudo -S -p '' -u mbsignal env \
    HOME="`$SIGNAL" \
    GOPATH="`$SIGNAL/go" \
    GOCACHE="`$SIGNAL/go/cache" \
    PATH=/usr/local/go/bin:/usr/local/bin:/usr/bin:/bin \
    sh -c 'cd "'"`$SIGNAL"'/src" && go mod tidy && go build -trimpath -o ../mb-signal ./server.go'
# Install systemd unit if changed.
if ! sudo -S -p '' cmp -s "$staging/signal/mb-signal.service" /etc/systemd/system/mb-signal.service 2>/dev/null; then
  sudo -S -p '' cp "$staging/signal/mb-signal.service" /etc/systemd/system/mb-signal.service
  sudo -S -p '' systemctl daemon-reload
fi
sudo -S -p '' systemctl enable mb-signal >/dev/null 2>&1 || true
sudo -S -p '' systemctl restart mb-signal
echo "signal OK"
"@)
    }

    $null = $sb.AppendLine("rm -rf '$staging'")
    $null = $sb.AppendLine("echo DONE")
    # Normalize line endings: PS here-strings produce CRLF; bash chokes on \r
    # ("set -e\r" -> "invalid option" gibberish). Force LF.
    $script = $sb.ToString().Replace("`r`n", "`n").Replace("`r", "`n")

    # Count sudo invocations to know how many password lines to feed.
    $sudoCount = ([regex]::Matches($script, 'sudo -S')).Count
    Invoke-Remote $script $sudoCount

    Write-Host ""
    if ($doStatic) { Write-Host "static: https://$RemoteHost/mb/" }
    if ($doSignal) { Write-Host "signal: wss://$RemoteHost/mb/ws  (systemd: mb-signal.service)" }
}
finally {
    $sshPw  = $null
    $rootPw = $null
    [GC]::Collect()
}
