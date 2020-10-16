cmake .
make
glslc -x glsl -fshader-stage=compute shader.glsl -o shader.bin
./PolarTracer -w 960 -h 540
mogrify -format png *.pam
rm -f *.pam