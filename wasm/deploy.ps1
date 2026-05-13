# Deploy wasm/build/ to quad.fi nginx subdir /usr/share/nginx/html/mb/
#
# HARD-LOCKED to /usr/share/nginx/html/mb. Never touches nginx root or any
# sibling directory. No flag overrides this. If you need a different path,
# edit $REMOTE_DEST below and verify by hand.
#
# Requires PuTTY tools (plink.exe, pscp.exe) on PATH.
# Install: winget install PuTTY.PuTTY
#
# Usage:  .\deploy.ps1                 # interactive prompts for user + passwords
#         .\deploy.ps1 -NoBuild        # skip rebuild before upload

param(
    [string]$RemoteHost = 'quad.fi',
    [string]$Username   = '',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

# --- Hard-coded deploy target. DO NOT parameterize. ---
$REMOTE_DEST = '/usr/share/nginx/html/mb'
# ------------------------------------------------------

# Safety net: refuse if anyone edits the above to something dangerous.
if ($REMOTE_DEST -notmatch '^/usr/share/nginx/html/mb(/|$)') {
    throw "REMOTE_DEST must stay under /usr/share/nginx/html/mb. Got: $REMOTE_DEST"
}
if ($REMOTE_DEST.TrimEnd('/') -eq '/usr/share/nginx/html') {
    throw "REMOTE_DEST cannot be nginx root."
}

$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $here 'build'

foreach ($tool in 'plink','pscp') {
    if (-not (Get-Command "$tool.exe" -ErrorAction SilentlyContinue)) {
        throw "$tool.exe not on PATH. Install PuTTY: winget install PuTTY.PuTTY"
    }
}

if (-not $NoBuild) {
    Write-Host "[1/4] build"
    & (Join-Path $here 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

if (-not (Test-Path $buildDir)) { throw "no build/ dir. Run build.ps1 first." }

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

try {
    Write-Host "[2/4] staging on $RemoteHost at $staging"
    & plink.exe @plinkBase "mkdir -p $staging"
    if ($LASTEXITCODE -ne 0) { throw "ssh mkdir failed" }

    Write-Host "[3/4] uploading build/ -> $staging"
    & pscp.exe @pscpBase -r "$buildDir\*" "${target}:$staging/"
    if ($LASTEXITCODE -ne 0) { throw "pscp failed" }

    Write-Host "[4/4] sudo install -> $REMOTE_DEST"
    # Quoted paths so spaces / special chars cannot break out. Hardcoded prefix
    # makes the rm -rf safe: it can only target /usr/share/nginx/html/mb/*.
    $remoteScript = @"
set -e
DEST='$REMOTE_DEST'
case "`$DEST" in
  /usr/share/nginx/html/mb|/usr/share/nginx/html/mb/*) ;;
  *) echo "refusing destination: `$DEST" >&2; exit 99 ;;
esac
sudo -S -p '' mkdir -p "`$DEST"
sudo -S -p '' sh -c 'rm -rf "'"`$DEST"'"/* && cp -r "$staging"/* "'"`$DEST"'"/ && (chown -R nginx:nginx "'"`$DEST"'" 2>/dev/null || chown -R www-data:www-data "'"`$DEST"'" 2>/dev/null || true)'
rm -rf '$staging'
echo OK
"@
    $stdin = "$rootPw`n$rootPw`n"
    $stdin | & plink.exe @plinkBase $remoteScript
    if ($LASTEXITCODE -ne 0) { throw "remote install failed (exit $LASTEXITCODE)" }

    Write-Host "deployed: https://$RemoteHost/mb/"
}
finally {
    $sshPw  = $null
    $rootPw = $null
    [GC]::Collect()
}
