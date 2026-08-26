#!/bin/bash

echo "=========================================="
echo "  Zombeer - AppImage Builder"
echo "=========================================="

APP_NAME="Zombeer"
APP_VERSION="2.0.0"
APP_DIR="AppDir"
OUTPUT="Zombeer-$APP_VERSION-x86_64.AppImage"

rm -rf $APP_DIR
mkdir -p $APP_DIR

echo "Building game..."
mkdir -p build
cd build

if command -v apt-get &> /dev/null; then
    echo "Debian/Ubuntu detected. Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev libgl1-mesa-dev
elif command -v pacman &> /dev/null; then
    echo "Arch Linux detected. Installing dependencies..."
    sudo pacman -S --needed sdl2 sdl2_ttf
elif command -v dnf &> /dev/null; then
    echo "Fedora detected. Installing dependencies..."
    sudo dnf install -y SDL2-devel SDL2_ttf-devel
fi

if command -v cmake &> /dev/null; then
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr
    make -j$(nproc)
else
    cd ..
    g++ -std=c++17 \
        Zombeer.cpp \
        imgui/imgui.cpp \
        imgui/imgui_draw.cpp \
        imgui/imgui_tables.cpp \
        imgui/imgui_widgets.cpp \
        imgui/backends/imgui_impl_sdl2.cpp \
        imgui/backends/imgui_impl_sdlrenderer2.cpp \
        -o Zombeer \
        $(pkg-config --cflags --libs sdl2) \
        -lSDL2_ttf \
        -I. -Iimgui \
        -O2 -s
    cd build
fi

cp ../Zombeer $APP_DIR/
cp ../Zombeer $APP_DIR/AppRun
chmod +x $APP_DIR/AppRun


cat > $APP_DIR/Zombeer.desktop << EOF
[Desktop Entry]
Name=Zombeer
Comment=Infinite Zombie Survival Game
Exec=Zombeer
Icon=zombeer
Terminal=false
Type=Application
Categories=Game;
EOF

mkdir -p $APP_DIR/usr/share/icons/hicolor/256x256/apps
cat > $APP_DIR/usr/share/icons/hicolor/256x256/apps/zombeer.svg << 'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <rect width="256" height="256" rx="50" fill="#1a1a2e"/>
  <text x="128" y="128" font-family="Arial" font-size="120" font-weight="bold" text-anchor="middle" fill="#ffcc00">🧟</text>
  <text x="128" y="200" font-family="Arial" font-size="40" font-weight="bold" text-anchor="middle" fill="#ffcc00">ZOMBEER</text>
</svg>
EOF

echo "Copying libraries..."
mkdir -p $APP_DIR/usr/lib

LIBS=$(ldd $APP_DIR/Zombeer | grep -E "SDL2|libc|libstdc|libgcc|libm" | awk '{print $3}')
for lib in $LIBS; do
    if [ -f "$lib" ]; then
        cp "$lib" $APP_DIR/usr/lib/ 2>/dev/null || true
    fi
done

cat > $APP_DIR/AppRun << 'EOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
exec "$HERE/Zombeer" "$@"
EOF
chmod +x $APP_DIR/AppRun

echo "Downloading appimagetool..."
wget -q -O appimagetool https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool

echo "Creating AppImage..."
./appimagetool $APP_DIR $OUTPUT

echo "=========================================="
echo "Build complete! AppImage created: $OUTPUT"
echo "Run with: ./$OUTPUT"
echo "=========================================="