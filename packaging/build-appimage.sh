#!/usr/bin/env bash
# Costruisce l'AppImage portabile di neonterm.
#
# Uso:  packaging/build-appimage.sh            (versione letta da CMakeLists.txt)
# Esito: packaging/out/neonterm-<versione>-x86_64.AppImage
#
# La build gira dentro un container Ubuntu 24.04 e non sull'host: un AppImage non
# incapsula la libc, quindi il "pavimento" di compatibilita' e' la glibc della
# macchina che compila. Compilato qui (glibc 2.39, Qt 6.4.2) il bundle parte da
# Ubuntu 24.04 / Debian 13 / Fedora 40 in su; compilato sull'host (Ubuntu 26.04,
# glibc 2.43) partirebbe solo su distro di pochi mesi.
#
# Qt 6.4 e' anche la prima versione con il backend ffmpeg di QtMultimedia incluso
# nei pacchetti: serve per `show` (video e audio).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -n 's/^project(neonterm VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
[ -n "$version" ] || { echo "versione non trovata in CMakeLists.txt" >&2; exit 1; }

image="ubuntu:24.04"
command -v docker >/dev/null || { echo "serve docker"; exit 1; }
if ! docker info >/dev/null 2>&1; then
    echo "docker non risponde: sudo systemctl start docker" >&2
    exit 1
fi
docker image inspect "$image" >/dev/null 2>&1 || docker pull -q "$image"

mkdir -p "$root/packaging/out"
docker run --rm -e APPIMAGE_EXTRACT_AND_RUN=1 \
    -v "$root":/src:ro \
    -v "$root/packaging/out":/out \
    -v "$root/packaging/appimage-inner.sh":/inner.sh:ro \
    "$image" bash /inner.sh "$version"

out="$root/packaging/out/neonterm-$version-x86_64.AppImage"
if [ -e "$out" ] && [ ! -w "$out" ]; then
    sudo -n chown "$(id -u):$(id -g)" "$out" 2>/dev/null || true
fi
echo "bundle: $out ($(du -h "$out" | cut -f1))"
sha256sum "$out"
