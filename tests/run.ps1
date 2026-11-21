# Run in a Visual Studio developer shell targeting the same architecture as the add-on.
param([Parameter(Mandatory)][string]$Addon, [string]$ReShadeDll)
$ErrorActionPreference = 'Stop'
$binary = Get-Item -LiteralPath $Addon
if ($ReShadeDll) { $runtime = Get-Item -LiteralPath $ReShadeDll }
$root = (Get-Item $PSScriptRoot).Parent.FullName
$scratch = New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ("rest-tests-" + [guid]::NewGuid()))
Push-Location $scratch.FullName
try {
    Copy-Item -LiteralPath $binary.FullName -Destination $scratch.FullName
    $options = @('/nologo', '/std:c++20', '/EHsc', '/O1', '/Gy', '/DWIN32', '/DNOMINMAX',
                 '/DBUILTIN_ADDON', '/DIMGUI_DISABLE_INCLUDE_IMCONFIG_H', '/DImTextureID=ImU64',
                 "/I$root/src", "/I$root/deps/reshade/include", "/I$root/deps/reshade/deps/imgui",
                 "/I$root/deps/robin-map/include", "/I$root/deps/minhook/include",
                 "/I$root/deps/sigmatch/include/sigmatch")
    $sources = @("$root/tests/regression.cpp", "$root/src/CDataFile.cpp", "$root/src/ToggleGroup.cpp",
                 "$root/src/ShaderManager.cpp", "$root/src/ConstantHandlerBase.cpp", "$root/src/StateTracking.cpp",
                 "$root/src/DescriptorTracking.cpp")
    & cl.exe @options @sources /Feregression.exe /link /OPT:REF user32.lib
    if ($LASTEXITCODE) { throw "Test compilation failed ($LASTEXITCODE)" }
    & .\regression.exe (Join-Path $scratch.FullName $binary.Name)
    if ($LASTEXITCODE) { throw "Regression checks failed ($LASTEXITCODE)" }
    if ($ReShadeDll) {
        Copy-Item -LiteralPath $runtime.FullName -Destination d3d11.dll
        & cl.exe /nologo /std:c++20 /EHsc "/I$root/src" "$root/tests/d3d11_smoke.cpp" d3dcompiler.lib user32.lib
        if ($LASTEXITCODE) { throw "D3D11 test compilation failed ($LASTEXITCODE)" }
        & .\d3d11_smoke.exe
        if ($LASTEXITCODE) { throw "D3D11 pixel checks failed ($LASTEXITCODE). See $scratch\ReShade.log" }
    }
} finally {
    Pop-Location
}
