[CmdletBinding()]
param(
  [ValidateSet('Begin','Finish','Package','Inspect')][string]$Action = 'Inspect',
  [ValidateSet('test','release')][string]$Mode = 'test',
  [string]$Path
)
$ErrorActionPreference = 'Stop'
# A cmd/Python child of PowerShell 7 can inherit its incompatible PSModulePath.
# Load this interpreter's built-in modules explicitly without changing user settings.
foreach ($moduleName in 'Microsoft.PowerShell.Utility','Microsoft.PowerShell.Management','Microsoft.PowerShell.Security') {
  Import-Module (Join-Path $PSHOME "Modules\$moduleName\$moduleName.psd1") -ErrorAction Stop
}
$project = Split-Path $PSScriptRoot -Parent
$workspace = Split-Path $project -Parent
$out = Join-Path $project "out\$Mode"
$statePath = Join-Path $out 'build-state.json'
$versionMatch = [regex]::Match((Get-Content (Join-Path $project 'version.h') -Raw), 'HAPTICS_VERSION_STRING\s+"([^"]+)"')
if (!$versionMatch.Success) { throw 'Missing version definition' }
$version = $versionMatch.Groups[1].Value

function Get-PEFacts([string]$File) {
  $data = [IO.File]::ReadAllBytes($File)
  function Read-U16([int]$Offset) { [BitConverter]::ToUInt16($data, $Offset) }
  function Read-U32([int]$Offset) { [BitConverter]::ToUInt32($data, $Offset) }
  function Read-Ascii([int]$Offset) {
    $end = $Offset
    while ($end -lt $data.Length -and $data[$end] -ne 0) { $end++ }
    [Text.Encoding]::ASCII.GetString($data, $Offset, $end - $Offset)
  }
  if ($data.Length -lt 256 -or (Read-U16 0) -ne 0x5A4D) { throw 'Not a PE file' }
  $pe = Read-U32 60
  if ((Read-U32 $pe) -ne 0x4550) { throw 'Invalid PE signature' }
  $optional = $pe + 24
  if ((Read-U16 ($pe + 4)) -ne 0x8664 -or (Read-U16 $optional) -ne 0x20b) { throw 'Expected AMD64 PE32+' }
  $sections = @()
  $sectionStart = $optional + (Read-U16 ($pe + 20))
  for ($i = 0; $i -lt (Read-U16 ($pe + 6)); $i++) {
    $p = $sectionStart + $i * 40
    $sections += [pscustomobject]@{ RVA=(Read-U32 ($p+12)); Size=[Math]::Max((Read-U32 ($p+8)),(Read-U32 ($p+16))); Raw=(Read-U32 ($p+20)) }
  }
  function Rva-Offset([uint32]$Rva) {
    foreach ($section in $sections) {
      if ($Rva -ge $section.RVA -and $Rva -lt ($section.RVA+$section.Size)) { return [int]($section.Raw+$Rva-$section.RVA) }
    }
    throw "Unmapped RVA: $Rva"
  }
  $directory = Rva-Offset (Read-U32 ($optional+112))
  $base = Read-U32 ($directory+16)
  $count = Read-U32 ($directory+20)
  $namedCount = Read-U32 ($directory+24)
  $functions = Rva-Offset (Read-U32 ($directory+28))
  $names = Rva-Offset (Read-U32 ($directory+32))
  $ordinals = Rva-Offset (Read-U32 ($directory+36))
  $named = @{}
  for ($i=0; $i -lt $namedCount; $i++) {
    $index = [int](Read-U16 ($ordinals+2*$i))
    $named[$index] = Read-Ascii (Rva-Offset (Read-U32 ($names+4*$i)))
  }
  $exports = @()
  for ($i=0; $i -lt $count; $i++) {
    if ((Read-U32 ($functions+4*$i)) -ne 0) {
      $exports += [pscustomobject]@{ Ordinal=[int]($base+$i); Name=$named[[int]$i] }
    }
  }
  [pscustomobject]@{
    Architecture='x64'; Exports=$exports; DllCharacteristics=(Read-U16 ($optional+70))
    SHA256=(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash
    Version=(Get-Item -LiteralPath $File).VersionInfo.FileVersion
    Signature=(Get-AuthenticodeSignature -FilePath $File).Status.ToString()
  }
}

function Assert-Exports($Facts) {
  $expected = @()
  foreach ($line in Get-Content (Join-Path $project 'xinput_proxy.def')) {
    if ($line -match '^\s*(\w+)\s+@(\d+)(.*)$') {
      $exportName = $Matches[1]; $exportOrdinal = [int]$Matches[2]; $suffix = $Matches[3]
      $name = if ($suffix.Contains('NONAME')) { '' } else { $exportName }
      $expected += ('{0}:{1}' -f $exportOrdinal, $name)
    }
  }
  $actual = @($Facts.Exports | ForEach-Object { '{0}:{1}' -f $_.Ordinal, $_.Name })
  if (Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)) { throw 'Export names/ordinals differ from .def' }
  if (($Facts.DllCharacteristics -band 0x4160) -ne 0x4160) { throw 'ASLR/NX/CFG/high-entropy mitigation missing' }
}

function Get-Inputs {
  $files = @('xinput_proxy.cpp','xinput_proxy.def','build.cmd','version.h','version.rc','load_test.cpp','README.md') |
    ForEach-Object { Get-Item -LiteralPath (Join-Path $project $_) }
  $files += @('COMPTE_RENDU_CROISE_AUDIT_HAPTIQUES.md','RAPPORT_REVISION_HAPTIQUES.md',
              'RAPPORT_POINTS_RESIDUELS_20260922.md') |
    ForEach-Object { if (Test-Path (Join-Path $project $_)) { Get-Item -LiteralPath (Join-Path $project $_) } }
  $files += @(Get-ChildItem (Join-Path $project 'tests'),(Join-Path $project 'tools') -File -Recurse | Where-Object { $_.Extension -in '.cpp','.py','.ps1','.lua' })
  $files += @(Get-ChildItem (Join-Path $workspace 'revlimiter_haptics\mod') -File -Recurse)
  @($files | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{ Path=$_.FullName.Substring($workspace.Length+1).Replace('\','/'); SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
  })
}

function Assert-Inputs($Expected) {
  $before = @($Expected | ForEach-Object { $_.Path + ':' + $_.SHA256 })
  $after = @(Get-Inputs | ForEach-Object { $_.Path + ':' + $_.SHA256 })
  if (Compare-Object $before $after) { throw 'Sources changed since build started; rebuild before packaging' }
}

function Save-State($State) { $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statePath -Encoding UTF8 }

if ($Action -eq 'Inspect') {
  if (!$Path) { $Path = Join-Path $project 'XInput1_4.dll' }
  $facts = Get-PEFacts $Path
  Assert-Exports $facts
  $facts | ConvertTo-Json -Depth 6
  exit 0
}

if ($Action -eq 'Begin') {
  New-Item -ItemType Directory -Force -Path $out | Out-Null
  $commit = $null
  $clean = $false
  if (Get-Command git -ErrorAction SilentlyContinue) {
    # Windows PowerShell turns native stderr into terminating errors under Stop.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
      $commit = & git -C $workspace rev-parse HEAD 2>$null
      if ($LASTEXITCODE -eq 0) {
        $changes = & git -C $workspace status --porcelain --untracked-files=normal 2>$null
        $clean = $LASTEXITCODE -eq 0 -and !$changes
      } else { $commit = $null }
    } finally { $ErrorActionPreference = $previousPreference }
  }
  if ($Mode -eq 'release' -and (!$commit -or !$clean)) { throw 'Signed release requires a clean Git checkout with a commit' }
  Save-State ([ordered]@{ Status='building'; Version=$version; Mode=$Mode; StartedUtc=[DateTime]::UtcNow.ToString('o'); Commit=$commit; GitClean=$clean; Inputs=(Get-Inputs) })
  exit 0
}

if (!(Test-Path -LiteralPath $statePath)) { throw 'No build receipt. Run build.cmd first.' }
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ($state.Version -ne $version -or $state.Mode -ne $Mode) { throw 'Build/version mismatch' }
Assert-Inputs $state.Inputs
$dll = Join-Path $out 'XInput1_4.dll'
$facts = Get-PEFacts $dll
Assert-Exports $facts
if ($facts.Version -ne $version) { throw "Wrong DLL VERSIONINFO: $($facts.Version)" }
if ($Mode -eq 'release' -and $facts.Signature -ne 'Valid') { throw 'Unsigned or invalid signature: release refused' }
if ($Action -eq 'Finish') {
  if ($state.Status -ne 'building') { throw 'Build not in progress' }
  if ((Get-Item -LiteralPath $dll).LastWriteTimeUtc -lt [DateTime]::Parse($state.StartedUtc).ToUniversalTime()) { throw 'Stale DLL predates this build' }
  foreach ($test in @(@('native-tests.log','PASS: native regression suite'), @('lua-tests.log','(?m)^OK\s*$'), @('artifact-tests.log','(?m)^OK( \(skipped=\d+\))?\s*$'))) {
    $log = Join-Path $out $test[0]
    if (!(Test-Path -LiteralPath $log) -or
        (Get-Item -LiteralPath $log).LastWriteTimeUtc -lt [DateTime]::Parse($state.StartedUtc).ToUniversalTime() -or
        (Get-Content -LiteralPath $log -Raw) -notmatch $test[1]) { throw "Missing or failed test receipt: $log" }
  }
  $state.Status = 'validated'
  $state | Add-Member -NotePropertyName DLL -NotePropertyValue $facts -Force
  Save-State $state
  exit 0
}
if ($state.Status -ne 'validated' -or $facts.SHA256 -ne $state.DLL.SHA256) { throw 'DLL differs from validated build' }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Write-Zip([string]$Folder, [string]$Archive) {
  $zip = [IO.Compression.ZipFile]::Open($Archive, [IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($file in Get-ChildItem -LiteralPath $Folder -File -Recurse) {
      $relative = $file.FullName.Substring($Folder.Length+1).Replace('\','/')
      $entry = $zip.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
      $input = [IO.File]::OpenRead($file.FullName)
      $output = $entry.Open()
      try { $input.CopyTo($output) }
      finally { $output.Dispose(); $input.Dispose() }
    }
  } finally { $zip.Dispose() }
}
function Verify-Zip([string]$Archive, [string]$Folder) {
  $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
  try {
    $files = @(Get-ChildItem -LiteralPath $Folder -File -Recurse)
    $entries = @($zip.Entries | Where-Object { $_.Name })
    if ($entries.Count -ne $files.Count) { throw 'ZIP file count mismatch' }
    foreach ($file in $files) {
      $relative = $file.FullName.Substring($Folder.Length+1).Replace('\','/')
      $entry = $zip.GetEntry($relative)
      if (!$entry) { throw "ZIP missing $relative" }
      $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
      try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
      finally { $stream.Dispose(); $sha.Dispose() }
      if ($hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) { throw "ZIP content mismatch: $relative" }
    }
  } finally { $zip.Dispose() }
}

$run = Join-Path $out ('package-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,6))
$stage = Join-Path $run 'stage'
New-Item -ItemType Directory -Path (Join-Path $stage 'Bin64'),(Join-Path $stage 'mods'),(Join-Path $run 'sources') | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'Bin64\XInput1_4.dll')
Copy-Item -LiteralPath (Join-Path $project 'README.md') -Destination $stage
$modRoot = Join-Path $workspace 'revlimiter_haptics\mod'
$modZip = Join-Path $stage "mods\Xbox-Haptic-Feedback-Controller-$version-$Mode.zip"
Write-Zip $modRoot $modZip
Verify-Zip $modZip $modRoot
foreach ($inputFile in $state.Inputs) {
  $target = Join-Path (Join-Path $run 'sources') $inputFile.Path
  New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
  Copy-Item -LiteralPath (Join-Path $workspace $inputFile.Path) -Destination $target
}
$sourceZip = Join-Path $stage 'sources.zip'
Write-Zip (Join-Path $run 'sources') $sourceZip
Verify-Zip $sourceZip (Join-Path $run 'sources')
Assert-Inputs $state.Inputs
if ((Get-FileHash (Join-Path $stage 'Bin64\XInput1_4.dll')).Hash -ne $state.DLL.SHA256) { throw 'Packaged DLL mismatch' }
$manifest = [ordered]@{ Version=$version; Mode=$Mode; Commit=$state.Commit; DLL=$facts; Inputs=$state.Inputs; LuaZipSHA256=(Get-FileHash $modZip).Hash; SourcesZipSHA256=(Get-FileHash $sourceZip).Hash; BuiltUtc=$state.StartedUtc }
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $stage 'manifest.json') -Encoding UTF8
if ($Mode -eq 'test') { 'UNSIGNED TEST CANDIDATE. NOT A VALIDATED PUBLIC RELEASE.' | Set-Content (Join-Path $stage 'TEST-CANDIDATE.txt') -Encoding ASCII }
$archive = Join-Path $run "Xbox-Haptic-Feedback-Controller-$version-$Mode.zip"
Write-Zip $stage $archive
Verify-Zip $archive $stage
((Get-FileHash $archive).Hash + '  ' + [IO.Path]::GetFileName($archive)) | Set-Content ($archive + '.sha256') -Encoding ASCII
Write-Output $archive
