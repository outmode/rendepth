#!/bin/sh
mkdir -p Compiled
for filename in *.vert; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
    shadercross "Compiled/$filename.spv" -s SPIRV -d MSL -o "Compiled/$filename.msl"
done

for filename in *.frag; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
    shadercross "Compiled/$filename.spv" -s SPIRV -d MSL -o "Compiled/$filename.msl"
done