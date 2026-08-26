#!/bin/bash

echo "compiling zombeer"

# Get SDL2 include and lib paths
SDL2_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS=$(pkg-config --libs sdl2 2>/dev/null)

if [ -z "$SDL2_CFLAGS" ]; then
    # Manual paths for MSYS2
    SDL2_CFLAGS="-I/mingw64/include/SDL2 -I/mingw64/include"
    SDL2_LIBS="-L/mingw64/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf"
fi

g++ -std=c++17 \
    Zombeer.cpp \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_sdl2.cpp \
    imgui/backends/imgui_impl_sdlrenderer2.cpp \
    -o Zombeer.exe \
    $SDL2_LIBS \
    $SDL2_CFLAGS \
    -I. -Iimgui \
    -DIMGUI_IMPL_API=extern

if [ $? -eq 0 ]; then
    echo "compiled successfully!"
    echo "copying needed dlls"
    cp /mingw64/bin/*.dll . 2>/dev/null
else
    echo "Build failed!"
    exit 1
fi