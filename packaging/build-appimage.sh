#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/dist}"
PACKAGE_VERSION="${PACKAGE_VERSION:-$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*//p' "${ROOT_DIR}/CMakeLists.txt" | head -n 1)}"
PACKAGE_VERSION="${PACKAGE_VERSION:-0.1.1}"
LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy}"
APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"

if ! command -v "${LINUXDEPLOY}" >/dev/null 2>&1; then
    printf 'No se encontró linuxdeploy. Defina LINUXDEPLOY=/ruta/linuxdeploy.\n' >&2
    exit 1
fi
if ! command -v "${APPIMAGETOOL}" >/dev/null 2>&1; then
    printf 'No se encontró appimagetool. Defina APPIMAGETOOL=/ruta/appimagetool.\n' >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"
rm -rf "${BUILD_DIR}/AppDir"

DESTDIR="${BUILD_DIR}/AppDir" cmake --install "${BUILD_DIR}" --prefix /usr

# El backend de QtRDP es xfreerdp3. Se copia al AppImage junto con sus plugins
# para que la aplicación no dependa de una instalación separada.
mkdir -p "${BUILD_DIR}/AppDir/usr/bin" "${BUILD_DIR}/AppDir/usr/lib/freerdp3"
XFREERDP="$(command -v xfreerdp3)"
cp -L "${XFREERDP}" "${BUILD_DIR}/AppDir/usr/bin/xfreerdp3"
if [ -d /usr/lib/x86_64-linux-gnu/freerdp3 ]; then
    cp -a /usr/lib/x86_64-linux-gnu/freerdp3/. "${BUILD_DIR}/AppDir/usr/lib/freerdp3/"
fi

pushd "${BUILD_DIR}" >/dev/null
"${LINUXDEPLOY}" \
    --appdir "${BUILD_DIR}/AppDir" \
    --executable "${BUILD_DIR}/AppDir/usr/bin/qtrdp" \
    --executable "${BUILD_DIR}/AppDir/usr/bin/xfreerdp3" \
    --desktop-file "${ROOT_DIR}/packaging/qtrdp.desktop" \
    --icon-file "${ROOT_DIR}/resources/icons/qtrdp.svg" \
    --output appimage
popd >/dev/null

mv "${BUILD_DIR}"/*.AppImage "${OUTPUT_DIR}/QtRDP-${PACKAGE_VERSION}-x86_64.AppImage"
printf 'AppImage generado en %s\n' "${OUTPUT_DIR}/QtRDP-${PACKAGE_VERSION}-x86_64.AppImage"
