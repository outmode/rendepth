#!/bin/sh
mkdir -p Compiled
for filename in *.vert; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
done

for filename in *.frag; do
    glslangValidator -V "$filename" -o "Compiled/$filename.spv"
done