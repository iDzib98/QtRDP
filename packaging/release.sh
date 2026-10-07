#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"

if [ -z "${VERSION}" ]; then
    echo "Uso: $0 <versión>  (ej: v0.2.0)" >&2
    exit 1
fi

# Verificar que gh está instalado y autenticado
if ! command -v gh >/dev/null 2>&1; then
    echo "gh CLI no está instalado. Instale con:" >&2
    echo "  sudo apt install gh" >&2
    echo "  o visite https://cli.github.com" >&2
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "gh no está autenticado. Ejecute:" >&2
    echo "  gh auth login" >&2
    exit 1
fi

echo "==> Construyendo paquetes para ${VERSION}..."

# .deb
echo "  - Generando .deb..."
cmake --build "${ROOT_DIR}/build" -j$(nproc) >/dev/null 2>&1
cpack --config "${ROOT_DIR}/build/CPackConfig.cmake" >/dev/null 2>&1

# AppImage
echo "  - Generando AppImage..."
LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy}"
APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"
if command -v "${LINUXDEPLOY}" >/dev/null 2>&1 && command -v "${APPIMAGETOOL}" >/dev/null 2>&1; then
    "${ROOT_DIR}/packaging/build-appimage.sh" >/dev/null 2>&1
else
    echo "    (saltado: linuxdeploy/appimagetool no disponibles)"
fi

# Flatpak
echo "  - Generando Flatpak..."
if command -v flatpak-builder >/dev/null 2>&1; then
    "${ROOT_DIR}/packaging/build-flatpak.sh" >/dev/null 2>&1
else
    echo "    (saltado: flatpak-builder no disponible)"
fi

echo "==> Creando release ${VERSION}..."

# Crear release con gh
gh release create "${VERSION}" \
    --title "QtRDP ${VERSION}" \
    --notes "## QtRDP ${VERSION}

### Paquetes incluidos
- **.deb** — Debian/Ubuntu (requiere \`freerdp3-x11\`)
- **AppImage** — Ejecutable portable
- **Flatpak** — Instalable con \`flatpak install --user\`

### Instalación

**.deb:**
\`\`\`bash
sudo apt install ./qtrdp_*.deb
\`\`\`

**AppImage:**
\`\`\`bash
chmod +x QtRDP-*.AppImage
./QtRDP-*.AppImage
\`\`\`

**Flatpak:**
\`\`\`bash
flatpak install --user ./QtRDP-*.flatpak
flatpak run org.qtrdp.QtRDP
\`\`\`" \
    "${ROOT_DIR}"/qtrdp_*.deb \
    "${ROOT_DIR}"/dist/*.AppImage \
    "${ROOT_DIR}"/dist/*.flatpak

echo "==> Release ${VERSION} creado exitosamente."
echo "    https://github.com/iDzib98/QtRDP/releases/tag/${VERSION}"
