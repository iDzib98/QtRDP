#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-flatpak}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/dist}"
PACKAGE_VERSION="${PACKAGE_VERSION:-$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*//p' "${ROOT_DIR}/CMakeLists.txt" | head -n 1)}"
PACKAGE_VERSION="${PACKAGE_VERSION:-0.2.0}"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "flatpak-builder no está instalado. Instale con:" >&2
    echo "  sudo apt install flatpak-builder" >&2
    exit 1
fi

for runtime in org.kde.Platform//6.10 org.kde.Sdk//6.10; do
    if ! flatpak info "${runtime}" >/dev/null 2>&1; then
        echo "Falta el runtime: ${runtime}" >&2
        echo "Instale con: flatpak install flathub ${runtime}" >&2
        exit 1
    fi
done

# Fuente limpia sin artefactos de compilación
STAGE_DIR="${BUILD_DIR}/source"
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"
for item in CMakeLists.txt cmake src resources packaging; do
    cp -a "${ROOT_DIR}/${item}" "${STAGE_DIR}/"
done

# Generar manifiesto con ruta ajustada
sed "s|path: ../..|path: ${STAGE_DIR}|" \
    "${ROOT_DIR}/packaging/flatpak/org.qtrdp.QtRDP.yml" \
    > "${BUILD_DIR}/org.qtrdp.QtRDP.yml"

mkdir -p "${OUTPUT_DIR}"
flatpak-builder \
    --force-clean \
    --ccache \
    --repo="${BUILD_DIR}/repo" \
    "${BUILD_DIR}/flatpak-build" \
    "${BUILD_DIR}/org.qtrdp.QtRDP.yml"

flatpak build-bundle \
    "${BUILD_DIR}/repo" \
    "${OUTPUT_DIR}/QtRDP-${PACKAGE_VERSION}-x86_64.flatpak" \
    org.qtrdp.QtRDP

echo "Flatpak generado en ${OUTPUT_DIR}/QtRDP-${PACKAGE_VERSION}-x86_64.flatpak"
