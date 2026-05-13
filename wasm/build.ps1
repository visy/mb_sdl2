# Emscripten build for Mine Bombers (Windows, no make required).
# Requires: emsdk activated in this shell (emcc.bat on PATH).
# Usage:    .\build.ps1            # build
#           .\build.ps1 -Clean     # wipe build/
#           .\build.ps1 -Serve     # build + python -m http.server 8080

param(
    [switch]$Clean,
    [switch]$Serve
)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir   = Join-Path $here '..\mb_c'
$s3mDir   = Join-Path $here '..\s3mplay\src\libs3m'
$origDir  = Join-Path $here '..\orig'
$dataDir  = Join-Path $here 'data_staged'   # filtered copy of orig/, only game assets
$buildDir = Join-Path $here 'build'

if ($Clean) {
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    Write-Host "cleaned $buildDir"
    if (-not $Serve) { return }
}

if (-not (Get-Command emcc.bat -ErrorAction SilentlyContinue) -and
    -not (Get-Command emcc     -ErrorAction SilentlyContinue)) {
    throw "emcc not on PATH. Activate emsdk first (emsdk_env.bat)."
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Stage game data: copy only flat asset files from orig/ -> data_staged/.
# Skip orig's subdirs (j/, jo/, mb_c_v3/, rel/), zips, exes, python scripts, etc.
if (Test-Path $dataDir) { Remove-Item -Recurse -Force $dataDir }
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
$assetExt = '.SPY','.MNL','.MNE','.VOC','.S3M','.CBM','.PPM','.FON','.DAT','.CFG','.TXT','.DIZ','.RAW'
Get-ChildItem -Path $origDir -File | Where-Object { $assetExt -contains $_.Extension.ToUpper() } |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $dataDir $_.Name) }
$staged = (Get-ChildItem $dataDir -File).Count
Write-Host "staged $staged data files from $origDir -> $dataDir"

$srcs = @(
    "$srcDir\main.c","$srcDir\args.c","$srcDir\context.c","$srcDir\error.c",
    "$srcDir\images.c","$srcDir\fonts.c","$srcDir\glyphs.c","$srcDir\app.c",
    "$srcDir\game.c","$srcDir\input.c","$srcDir\persist.c","$srcDir\shop.c",
    "$srcDir\editor.c","$srcDir\playersel.c","$srcDir\campaign.c",
    "$srcDir\menus.c","$srcDir\netgame.c","$srcDir\cpu.c",
    "$srcDir\net_web.c",
    "$s3mDir\s3m.c","$s3mDir\s3m_file.c","$s3mDir\s3m_sound.c",
    "$s3mDir\channel.c","$s3mDir\pattern.c","$s3mDir\s3m_info.c"
)

$cflags = @(
    '-Wall','-Wextra','-O2',
    '-DMB_NET','-DMB_WEB',
    "-I$srcDir","-I$s3mDir",
    '-sUSE_SDL=2','-sUSE_SDL_MIXER=2','-sSDL2_MIXER_FORMATS=["wav"]'
)

# ASYNCIFY: game has nested while(running){...SDL_Delay(16);} loops -- ASYNCIFY
# lets SDL_Delay yield to browser without refactoring all of them to callbacks.
$ldflags = @(
    '-sUSE_SDL=2','-sUSE_SDL_MIXER=2','-sSDL2_MIXER_FORMATS=["wav"]',
    '-sASYNCIFY','-sASYNCIFY_STACK_SIZE=65536',
    '-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=64MB',
    '-sEXPORTED_RUNTIME_METHODS=["ccall","cwrap","FS","IDBFS"]',
    '-sEXPORTED_FUNCTIONS=["_main","_malloc","_free"]',
    '-lidbfs.js',
    "--preload-file","$dataDir@/",
    '--shell-file',"$here\shell.html"
)

$out = Join-Path $buildDir 'index.html'
$args = $cflags + $srcs + $ldflags + @('-o',$out)

Write-Host "emcc $($args.Count) args -> $out"
& emcc @args
if ($LASTEXITCODE -ne 0) { throw "emcc failed (exit $LASTEXITCODE)" }

# Copy netplay.js next to mb.js so shell.html can load it.
Copy-Item -Force (Join-Path $here 'netplay.js') (Join-Path $buildDir 'netplay.js')

Write-Host "build OK: $out"

if ($Serve) {
    Write-Host "serving $buildDir on http://localhost:8080"
    Push-Location $buildDir
    try { python -m http.server 8080 } finally { Pop-Location }
}
