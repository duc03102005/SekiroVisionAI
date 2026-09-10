param(
    [string]$BuildDirectory = 'out/build/windows-x64/Release',
    [string]$OutputDirectory = 'out/package',
    [string]$SourceSha = ''
)
$ErrorActionPreference = 'Stop'
if (-not $SourceSha) { $SourceSha = (git rev-parse HEAD) }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
foreach ($name in @('Models/production', 'Runtime', 'Config', 'LICENSES')) {
    New-Item -ItemType Directory -Force "$OutputDirectory/$name" | Out-Null
}
Copy-Item "$BuildDirectory/SekiroVisionAI.exe" "$OutputDirectory/"
Copy-Item "$BuildDirectory/ReplayHarness.exe" "$OutputDirectory/"
# ORT and its VC++ imports use Windows app-local dependency resolution.
Copy-Item "$BuildDirectory/*.dll" "$OutputDirectory/"
Copy-Item docs/APPLICATION_README_VI.txt "$OutputDirectory/README.txt"
Copy-Item docs/runtime-dependencies.md "$OutputDirectory/Runtime/README.md"
Copy-Item models/production/manifest.json "$OutputDirectory/Models/production/manifest.json"
Copy-Item Config/defaults.ini "$OutputDirectory/Config/defaults.ini"
$sdk = 'out/build/windows-x64/_deps'
Copy-Item "$sdk/svai_ort-src/LICENSE" "$OutputDirectory/LICENSES/ONNXRuntime-LICENSE.txt"
Copy-Item "$sdk/svai_ort-src/ThirdPartyNotices.txt" "$OutputDirectory/LICENSES/ONNXRuntime-ThirdPartyNotices.txt"
Copy-Item "$sdk/svai_directml-src/LICENSE.txt" "$OutputDirectory/LICENSES/DirectML-LICENSE.txt"
Copy-Item "$sdk/svai_directml-src/ThirdPartyNotices.txt" "$OutputDirectory/LICENSES/DirectML-ThirdPartyNotices.txt"
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsPath = & $vswhere -latest -products '*' -property installationPath
$crtDirectory = Get-ChildItem "$vsPath/VC/Redist/MSVC" -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crtDirectory) { throw 'App-local VC++ redistributable runtime was not found' }
Copy-Item "$crtDirectory/*.dll" "$OutputDirectory/"

$production = Get-Content models/production/manifest.json -Raw | ConvertFrom-Json
$modelIncluded = $false
if ($production.status -eq 'validated') {
    foreach ($asset in $production.assets) {
        if ($asset.file -notmatch '^[a-zA-Z0-9_.-]+\.onnx$' -or $asset.sha256 -notmatch '^[a-f0-9]{64}$') {
            throw 'Invalid production model asset manifest'
        }
        $path = "models/production/$($asset.file)"
        if ((Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Production model checksum mismatch: $($asset.file)"
        }
        Copy-Item $path "$OutputDirectory/Models/production/"
    }
    $modelIncluded = (Test-Path "$OutputDirectory/Models/production/model.onnx") -and (Test-Path "$OutputDirectory/Models/production/targets.onnx")
    if (-not $modelIncluded) { throw 'Validated package is missing an attack or target model' }
}

# Run the packaged app from its own directory; no compiler/Python on its PATH.
$absolutePackage = (Resolve-Path $OutputDirectory).Path
$originalPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $packaged = Start-Process -FilePath "$absolutePackage/SekiroVisionAI.exe" -WorkingDirectory $absolutePackage -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
    if (-not $packaged.WaitForExit(15000)) { Stop-Process -Id $packaged.Id -Force; throw 'Packaged application startup timeout' }
    $packaged.Refresh()
    if ($packaged.ExitCode -ne 0) { throw "Packaged application failed: $($packaged.ExitCode)" }
    $replay = Start-Process -FilePath "$absolutePackage/ReplayHarness.exe" -WorkingDirectory $absolutePackage -ArgumentList '--help' -NoNewWindow -PassThru -Wait
    if ($replay.ExitCode -ne 0) { throw 'Packaged native ReplayHarness failed to load' }
} finally { $env:PATH = $originalPath }

$manifest = [ordered]@{
    version = '0.4.0-development'
    status = $(if ($modelIncluded) { 'models_packaged_validation_evidence_required' } else { 'INTERNAL_NOT_A_FUNCTIONAL_GAMEPLAY_RELEASE' })
    built_commit = (git rev-parse HEAD)
    source_head = $SourceSha
    architecture = 'Windows Release x64'
    gameplay_models_included = $modelIncluded
    live_sekiro_and_rtx3070 = 'not measured on this hosted runner'
    runtime = 'C++20 WGC D3D11 ORT DirectML/CPU MediaFoundation WIC SendInput'
    onnxruntime = '1.24.4 DirectML'
    directml = '1.15.4'
    packaged_application_and_replay_startup = 'passed with system-only PATH'
    signature = 'unsigned'
    files = @(Get-ChildItem $OutputDirectory -Recurse -File | Where-Object Name -ne 'build-manifest.json' | ForEach-Object {
        @{ path = $_.FullName.Substring($absolutePackage.Length + 1); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$OutputDirectory/build-manifest.json"
