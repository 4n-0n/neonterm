#!/bin/bash
# Script INTERNO di packaging/build-appimage.sh: gira dentro il container Ubuntu 24.04.
# Non va lanciato a mano sull'host.
set -euo pipefail

version="${1:?versione mancante}"
mkdir -p /out

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build qt6-base-dev qt6-multimedia-dev libqt6opengl6-dev \
    qt6-wayland libgl1-mesa-dev libglx-dev libxkbcommon-x11-0 libpulse0 file wget \
    ca-certificates patchelf zsync desktop-file-utils xvfb xauth \
    libgl1-mesa-dri libegl1 libopengl0 libfontconfig1 libharfbuzz0b fonts-dejavu-core \
    >/dev/null
echo "build: Qt $(qmake6 -query QT_VERSION), glibc $(ldd --version | head -1 | awk '{print $NF}')"

# --- 1. compilazione ---------------------------------------------------------
rm -rf /work && mkdir -p /work/tools
cp -r /src /work/src
chmod -R u+w /work/src
cd /work/src && rm -rf build packaging/out .git
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build b -j"$(nproc)"

# --- 2. strumenti AppImage (niente FUSE nel container: si autoestraggono) ----
cd /work/tools
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="/work/tools:$PATH"
base=https://github.com
wget -q -O linuxdeploy "$base/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
wget -q -O linuxdeploy-plugin-qt "$base/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
wget -q -O appimagetool "$base/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
chmod +x linuxdeploy linuxdeploy-plugin-qt appimagetool

# --- 3. AppDir ---------------------------------------------------------------
cd /work
AppDir=/work/AppDir
rm -rf "$AppDir"
export QMAKE=/usr/bin/qmake6
export ARCH=x86_64
export EXTRA_QT_PLUGINS=multimedia

# Niente driver grafici, audio di sistema o libc nel bundle: quei pezzi devono
# essere quelli della macchina, altrimenti GL/EGL e PipeWire/Pulse si rompono
# a seconda della distro.
mkdir -p "$AppDir"
# Il desktop file in AppDir deve chiamarsi come l'app (appimagetool lo usa come
# id), e l'Exec assoluto dell'installazione non ha senso dentro il bundle.
sed 's|@BINDIR@/neonterm|neonterm|g' /src/packaging/neonterm.desktop.in > /work/neonterm.desktop
cp /src/assets/icons/neonterm-256.png /work/neonterm.png

linuxdeploy --appdir "$AppDir" \
    --executable src/b/neonterm \
    --executable src/b/show \
    --desktop-file /work/neonterm.desktop \
    --icon-file /work/neonterm.png \
    --plugin qt \
    --exclude-library "libpulse*;libpipewire*;libasound*;libGL*;libEGL*;libdrm*;libgbm*;libvulkan*;libGLX*;libOpenGL*;libwayland*"

# Solo il backend ffmpeg di QtMultimedia: quello gstreamer, senza i plugin gst
# del sistema, non riprodurrebbe niente e tirerebbe dentro ~30 MB di librerie.
rm -f "$AppDir"/usr/plugins/multimedia/libgstreamermediaplugin.so
rm -f "$AppDir"/usr/lib/libgst*.so* "$AppDir"/usr/lib/libgstreamer*.so* "$AppDir"/usr/lib/liborc*.so*

# linuxdeploy-plugin-qt deploya solo il plugin xcb: la piattaforma Wayland si
# aggiunge a mano (Qt 6.4 la porta in pacchetti separati). Senza, su KDE/GNOME
# Wayland il terminale gira sotto XWayland, cioe' sfocato con la scala frazionaria.
qt_plugins=/usr/lib/x86_64-linux-gnu/qt6/plugins
u=/usr/lib/x86_64-linux-gnu
cp "$qt_plugins"/platforms/libqwayland-*.so "$AppDir"/usr/plugins/platforms/ 2>/dev/null || true
for p in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [ -d "$qt_plugins/$p" ] && cp -r "$qt_plugins/$p" "$AppDir"/usr/plugins/
done
for l in libQt6WaylandClient.so.6 libQt6WaylandEglClientHwIntegration.so.6 \
         libQt6WlShellIntegration.so.6; do
    cp -L "$u/$l" "$AppDir"/usr/lib/ 2>/dev/null || true
done
# libwayland-client/egl/cursor NON si incapsulano: il driver EGL della macchina
# (libEGL_mesa o nvidia) linka la libwayland di sistema, e due copie nello stesso
# processo fanno fallire l'EGL integration ("qt.qpa.wayland: EGL not available",
# poi QOpenGLWidget senza contesto). Devono venire dal sistema, come libGL.
rm -f "$AppDir"/usr/lib/libwayland-client.so.0 "$AppDir"/usr/lib/libwayland-cursor.so.0 \
      "$AppDir"/usr/lib/libwayland-egl.so.1

# AppRun: librerie e plugin dal bundle, e `show` in PATH (il terminale lo lancia
# per nome). $PATH resta quello dell'utente.
#
# linuxdeploy lascia AppRun come SYMLINK a usr/bin/<eseguibile>: scrivendoci sopra
# si sovrascrive il binario con lo script e il bundle parte con "usr/bin/usr/bin/…
# not found". Va tolto prima.
rm -f "$AppDir/AppRun"
cat > "$AppDir/AppRun" <<'APPRUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export PATH="$HERE/usr/bin:$PATH"
exec "$HERE/usr/bin/neonterm" "$@"
APPRUN
chmod +x "$AppDir/AppRun"
[ ! -L "$AppDir/AppRun" ] || { echo "AppRun e' ancora un symlink"; exit 1; }
file "$AppDir/usr/bin/neonterm" | grep -q ELF || { echo "neonterm non e' un ELF"; exit 1; }

echo "--- dipendenze non incapsulate (attese: libc, libGL*, libX11, fontconfig...):"
deps=$(for f in "$AppDir"/usr/bin/* "$AppDir"/usr/lib/*.so* "$AppDir"/usr/plugins/*/*.so; do
    objdump -p "$f" 2>/dev/null | awk '/NEEDED/{print $2}'
done | sort -u || true)
for l in $deps; do [ -e "$AppDir/usr/lib/$l" ] || printf ' %s' "$l"; done
echo

# --- 4. bundle ---------------------------------------------------------------
rm -f /work/*.AppImage
VERSION="$version" appimagetool "$AppDir" "/work/neonterm-$version-x86_64.AppImage" 2>&1 | tail -2
cp "/work/neonterm-$version-x86_64.AppImage" /out/
chmod 755 "/out/neonterm-$version-x86_64.AppImage"

echo "--- peso: $(du -h "/out/neonterm-$version-x86_64.AppImage" | cut -f1)"
echo "--- librerie: $(ls "$AppDir"/usr/lib | wc -l), piattaforme: $(ls "$AppDir"/usr/plugins/platforms | tr '\n' ' ')"
echo "--- plugin: $(ls "$AppDir"/usr/plugins | tr '\n' ' ')"
echo -n "--- glibc massima richiesta: "
for f in "$AppDir"/usr/bin/* "$AppDir"/usr/lib/*.so*; do
    objdump -T "$f" 2>/dev/null | grep -oE "GLIBC_[0-9]+\.[0-9]+"
done | sort -uV | tail -1 || true

# --- 5. prova: parte, disegna, e `show` e' nel PATH --------------------------
img="/work/neonterm-$version-x86_64.AppImage"
rm -rf /tmp/cap
echo "--- prova X11 (xcb), renderer CPU:"
xvfb-run -a -s "-screen 0 1600x1000x24" env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
    "$img" --capture /tmp/cap/shot.png --size 1200x800 2>&1 | tail -2
[ -s /tmp/cap/shot.png ] || { echo "cattura vuota"; exit 1; }
echo "--- prova GL (renderer OpenGL sotto Xvfb software):"
xvfb-run -a -s "-screen 0 1600x1000x24" env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
    "$img" --capture /tmp/cap/gl.png --size 1200x800 --demo 2>&1 | tail -2
[ -s /tmp/cap/gl.png ] || { echo "cattura GL vuota"; exit 1; }
"$AppDir/usr/bin/show" --help >/dev/null 2>&1 && echo "show: presente ed eseguibile"
ls -la /tmp/cap
