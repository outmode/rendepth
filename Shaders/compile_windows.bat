@echo off
setlocal
pushd "%~dp0"
if "%SHADERCROSS%"=="" (
    set shadercross_bin=%~dp0..\Binary\shadercross.exe
) else (
    set shadercross_bin=%SHADERCROSS%
)
if not exist "Compiled\" mkdir "Compiled\"
if not exist "Temp\" mkdir "Temp\"
for %%f in (*.vert) do (
    glslangValidator -V "%%f" -o "Compiled/%%f.spv"
    "%shadercross_bin%" "Compiled/%%f.spv" -s SPIRV -d MSL -o "Compiled/%%f.msl"
    "%shadercross_bin%" "Compiled/%%f.spv" -s SPIRV -d HLSL -o "Temp/%%f.hlsl"
    "%shadercross_bin%" "Temp/%%f.hlsl" -s HLSL -d DXIL -o "Compiled/%%f.dxil"
)
for %%f in (*.frag) do (
    glslangValidator -V "%%f" -o "Compiled/%%f.spv"
    "%shadercross_bin%" "Compiled/%%f.spv" -s SPIRV -d MSL -o "Compiled/%%f.msl"
    "%shadercross_bin%" "Compiled/%%f.spv" -s SPIRV -d HLSL -o "Temp/%%f.hlsl"
    "%shadercross_bin%" "Temp/%%f.hlsl" -s HLSL -d DXIL -o "Compiled/%%f.dxil"
)
popd
endlocal
