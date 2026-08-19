@echo off
setlocal
pushd "%~dp0" || exit /b 1
if "%SHADERCROSS%"=="" (
    set shadercross_bin=%~dp0..\Binary\shadercross.exe
) else (
    set shadercross_bin=%SHADERCROSS%
)
if "%DXC%"=="" (
    set dxc_bin=dxc.exe
) else (
    set dxc_bin=%DXC%
)
if not exist "%shadercross_bin%" (
    echo shadercross not found at "%shadercross_bin%". Build the Shaders target first or set SHADERCROSS.
    goto :error
)
"%dxc_bin%" -help >nul 2>&1
if errorlevel 1 (
    echo dxc was not found. Add dxc.exe to PATH or set DXC to its full path.
    goto :error
)
if not exist "Compiled\" mkdir "Compiled\"
if not exist "Temp\" mkdir "Temp\"
for %%f in (*.vert) do (
    call :compile "%%f" vs_6_0
    if errorlevel 1 goto :error
)
for %%f in (*.frag) do (
    call :compile "%%f" ps_6_0
    if errorlevel 1 goto :error
)
popd
endlocal
exit /b 0

:compile
glslangValidator -V "%~1" -o "Compiled/%~1.spv" || exit /b 1
"%shadercross_bin%" "Compiled/%~1.spv" -s SPIRV -d MSL -o "Compiled/%~1.msl" || exit /b 1
"%shadercross_bin%" "Compiled/%~1.spv" -s SPIRV -d HLSL -o "Temp/%~1.hlsl" || exit /b 1
"%dxc_bin%" -T %~2 -E main -Fo "Compiled/%~1.dxil" "Temp/%~1.hlsl" || exit /b 1
exit /b 0

:error
popd
endlocal
exit /b 1
