# QtRDP

Cliente de escritorio remoto RDP para Linux con una interfaz inspirada en mstsc.exe.

## Características

- Gestión de perfiles de conexión con persistencia
- Importación/exportación de archivos `.rdp`
- Almacenamiento seguro de credenciales (libsecret o archivo local)
- Resolución dinámica del escritorio remoto
- Redirección de portapapeles, audio y unidades locales
- Interfaz en español con pestañas estilo Windows
- Backend basado en `xfreerdp3` para compatibilidad total

## Requisitos

- Linux con X11 o Wayland
- `xfreerdp3` instalado (para el paquete `.deb`)
- Qt 6.5+

## Instalación

### .deb (Debian/Ubuntu)

```bash
sudo apt install ./qtrdp_*.deb
```

### AppImage

```bash
chmod +x QtRDP-*-x86_64.AppImage
./QtRDP-*-x86_64.AppImage
```

### Flatpak

```bash
flatpak install --user ./QtRDP-*-x86_64.flatpak
flatpak run org.qtrdp.QtRDP
```

## Uso

```bash
qtrdp                          # Abrir gestor de perfiles
qtrdp equipo.rdp              # Abrir archivo .rdp
qtrdp 10.0.0.1 -u usuario    # Conectar directamente
```

## Compilación

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build
```

## Paquetes

```bash
# .deb
cpack --config build/CPackConfig.cmake

# AppImage
packaging/build-appimage.sh

# Flatpak
packaging/build-flatpak.sh
```

## Licencia

MIT
