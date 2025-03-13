@echo off
set shader_dir=%~dp0%
if not exist "Compiled/" mkdir "Compiled/"
if not exist "Temp/" mkdir "Temp/"
for %%f in (*.vert) do (
    glslangValidator -V "%%f" -o "Compiled/%%f.spv"
    shadercross "Compiled/%%f.spv" -s SPIRV -d HLSL -o "Temp/%%f.hlsl"
    shadercross "Temp/%%f.hlsl" -s HLSL -d DXIL -o "Compiled/%%f.dxil"
)
for %%f in (*.frag) do (
    glslangValidator -V "%%f" -o "Compiled/%%f.spv"
    shadercross "Compiled/%%f.spv" -s SPIRV -d HLSL -o "Temp/%%f.hlsl"
    shadercross "Temp/%%f.hlsl" -s HLSL -d DXIL -o "Compiled/%%f.dxil"
)