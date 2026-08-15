#!/bin/sh
set -e

shadercross_bin=${SHADERCROSS:-../Binary/shadercross}
if [ ! -x "$shadercross_bin" ]; then
    echo "shadercross not found at $shadercross_bin; build the in-repo shadercross target first or set SHADERCROSS" >&2
    exit 1
fi

mkdir -p Compiled Temp
for filename in *.vert; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
    "$shadercross_bin" "Compiled/$filename.spv" -s SPIRV -d MSL -o "Compiled/$filename.msl"
    "$shadercross_bin" "Compiled/$filename.spv" -s SPIRV -d HLSL -o "Temp/$filename.hlsl"
    "$shadercross_bin" "Temp/$filename.hlsl" -s HLSL -d DXIL -o "Compiled/$filename.dxil"
done

for filename in *.frag; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
    "$shadercross_bin" "Compiled/$filename.spv" -s SPIRV -d MSL -o "Compiled/$filename.msl"
    "$shadercross_bin" "Compiled/$filename.spv" -s SPIRV -d HLSL -o "Temp/$filename.hlsl"
    "$shadercross_bin" "Temp/$filename.hlsl" -s HLSL -d DXIL -o "Compiled/$filename.dxil"
done
