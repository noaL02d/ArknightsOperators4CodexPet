param(
  [switch]$Console,
  [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ArknightsPet.exe'

if ($Clean -and (Test-Path $build)) {
  Remove-Item -LiteralPath (Join-Path $build '*.o') -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $exe -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $build | Out-Null

$mode = if ($Console) { 'console' } else { 'gui' }
$stamp = Join-Path $build 'mode.txt'
if (-not (Test-Path $stamp) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -ne $mode) {
  Get-ChildItem $build -Filter *.o | Remove-Item -Force
  Set-Content -LiteralPath $stamp -Value $mode
}

$sources = @(
  (Join-Path $root 'src/main.cpp'),
  (Join-Path $root 'src/renderer.cpp'),
  (Join-Path $root 'src/spine_player.cpp'),
  (Join-Path $root 'src/image.cpp'),
  (Join-Path $root 'src/spine_extension.cpp'),
  (Join-Path $root 'src/session.cpp'),
  (Join-Path $root 'src/card_window.cpp'),
  (Join-Path $root 'vendor/sqlite/sqlite3.c')
)
$sources += Get-ChildItem (Join-Path $root 'vendor/spine-cpp/src/spine') -Filter *.cpp |
  ForEach-Object { $_.FullName }

$common = @(
  '-std=c++17',
  '-O2',
  '-DNDEBUG',
  '-DUNICODE',
  '-D_UNICODE',
  '-DWIN32_LEAN_AND_MEAN',
  '-fno-strict-aliasing',
  '-I', (Join-Path $root 'src'),
  '-I', (Join-Path $root 'vendor/spine-cpp/include'),
  '-I', (Join-Path $root 'vendor/sqlite')
)

$sqliteFlags = @(
  '-DSQLITE_THREADSAFE=0',
  '-DSQLITE_OMIT_LOAD_EXTENSION',
  '-DSQLITE_DEFAULT_MEMSTATUS=0',
  '-DSQLITE_OMIT_DEPRECATED',
  '-DSQLITE_DQS=0'
)

if ($Console) {
  $common += @('-DPET_CONSOLE', '-mconsole')
} else {
  $common += @('-municode', '-mwindows')
}

$objects = @()
$newestHeader = Get-ChildItem (Join-Path $root 'src') -Filter *.h |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
$headerTime = if ($newestHeader) { $newestHeader.LastWriteTime } else { [datetime]::MinValue }
foreach ($source in $sources) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
  $object = Join-Path $build ($name + '.o')
  if ((Test-Path $object) -and
      ((Get-Item $object).LastWriteTime -gt (Get-Item $source).LastWriteTime) -and
      ((Get-Item $object).LastWriteTime -gt $headerTime)) {
    $objects += $object
    continue
  }
  Write-Host "g++ $name"
  if ($source.EndsWith('.c')) {
    $cFlags = @('-std=c11') + ($common | Where-Object { $_ -ne '-std=c++17' })
    & gcc @cFlags @sqliteFlags -c $source -o $object
  } else {
    & g++ @common -c $source -o $object
  }
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $source" }
  $objects += $object
}

Write-Host 'linking'
$resource = Join-Path $build 'pet.res'
if (-not (Test-Path $resource) -or
    (Get-Item $resource).LastWriteTime -lt (Get-Item (Join-Path $root 'pet.manifest')).LastWriteTime) {
  & windres (Join-Path $root 'pet.rc') -O coff -o $resource
  if ($LASTEXITCODE -ne 0) { throw 'windres failed' }
}
$linkFlags = @()
if ($Console) {
  $linkFlags += '-mconsole'
} else {
  $linkFlags += '-municode'
  $linkFlags += '-mwindows'
}
& g++ @linkFlags @objects $resource -o $exe -ld3d11 -ldxgi -ldcomp -luser32 -lgdi32 -lshell32 -lole32 -lwinmm -ladvapi32 -static-libgcc -static-libstdc++
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

$size = (Get-Item $exe).Length / 1MB
Write-Host ("built {0} ({1:N2} MB)" -f $exe, $size)

$assetsSource = Join-Path (Split-Path $root -Parent) 'public/assets'
if (Test-Path $assetsSource) {
  $assetsTarget = Join-Path $build 'assets'
  New-Item -ItemType Directory -Force -Path $assetsTarget | Out-Null
  Copy-Item -Path (Join-Path $assetsSource '*') -Destination $assetsTarget -Recurse -Force
  Write-Host 'copied assets'
}
