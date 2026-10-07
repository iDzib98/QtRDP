# Configuración de FreeRDP para QtRDP.
#
# Por defecto se compila FreeRDP dentro del propio proyecto (add_subdirectory)
# y se enlaza de forma estática. Motivo: la librería compartida que instala
# Debian exporta una superficie de símbolos muy reducida (no existen
# rdpgfx_client_context_new, cliprdr_init, ni el cliente rdpsnd) y el paquete
# freerdp3-dev no incluye las librerías estáticas.
#
# Para usar el FreeRDP del sistema: -DQTRDP_SYSTEM_FREERDP=ON
# (requiere que la instalación exponga la API completa de cliente).

set(QTRDP_FREERDP_TAG "3.32.1" CACHE STRING "Tag de FreeRDP a compilar")
set(QTRDP_FREERDP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRDP"
    CACHE PATH "Ruta al código fuente de FreeRDP")

if(QTRDP_SYSTEM_FREERDP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREERDP3 REQUIRED IMPORTED_TARGET freerdp3)
    pkg_check_modules(FREERDP_CLIENT3 REQUIRED IMPORTED_TARGET freerdp-client3)
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
    set(QTRDP_FREERDP_INCLUDE_DIRS "")
    set(QTRDP_FREERDP_LIBRARIES PkgConfig::FREERDP_CLIENT3 PkgConfig::FREERDP3)
    set(QTRDP_FREERDP_DEFINITIONS "")
    set(QTRDP_FREERDP_STATIC OFF)
    set(QTRDP_FREERDP_SYSTEM_LIBS "")
    set(QTRDP_APP_LIBRARIES PkgConfig::XKBCOMMON)
    return()
endif()

if(NOT EXISTS "${QTRDP_FREERDP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "No se encuentra FreeRDP en ${QTRDP_FREERDP_SOURCE_DIR}.\n"
        "Clónalo con:\n"
        "  git clone --depth 1 --branch ${QTRDP_FREERDP_TAG} "
        "https://github.com/FreeRDP/FreeRDP.git third_party/FreeRDP")
endif()

# --- Opciones de FreeRDP ---------------------------------------------------
# Solo la librería de cliente: nada de X11/Wayland/SDL (el rendering lo hace Qt).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING_INTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_COMM_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(WITH_MANPAGES OFF CACHE BOOL "" FORCE)
set(WITH_SERVER OFF CACHE BOOL "" FORCE)
set(WITH_SAMPLE OFF CACHE BOOL "" FORCE)
set(WITH_CLIENT ON CACHE BOOL "" FORCE)
set(WITH_X11 OFF CACHE BOOL "" FORCE)
set(WITH_WAYLAND OFF CACHE BOOL "" FORCE)
set(WITH_CLIENT_SDL OFF CACHE BOOL "" FORCE)
set(WITH_CUPS OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG_HWACCEL OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER_ENCODER OFF CACHE BOOL "" FORCE)
set(WITH_GFX_AV1 OFF CACHE BOOL "" FORCE)
set(WITH_GFX_AZURE OFF CACHE BOOL "" FORCE)
set(WITH_SWSCALE OFF CACHE BOOL "" FORCE)
set(WITH_CAIRO OFF CACHE BOOL "" FORCE)
set(WITH_PKCS11 OFF CACHE BOOL "" FORCE)
set(WITH_PULSE OFF CACHE BOOL "" FORCE)
set(WITH_ALSA OFF CACHE BOOL "" FORCE)
set(WITH_OSS OFF CACHE BOOL "" FORCE)
set(WITH_PROFILER OFF CACHE BOOL "" FORCE)
set(WITH_PCSC_WINPR OFF CACHE BOOL "" FORCE)
set(WITH_DEBUG_ALL OFF CACHE BOOL "" FORCE)
set(WITH_THREADED_DECODE ON CACHE BOOL "" FORCE)

# Los asserts de WinPR son muy caros en una sesión interactiva.
set(WITH_VERBOSE_WINPR_ASSERT OFF CACHE BOOL "" FORCE)
set(WITH_VERBOSE_DEBUG OFF CACHE BOOL "" FORCE)

# Canales estáticos: se necesitan rdpgfx, cliprdr, rdpsnd, rdpdr, drive, audin.
set(WITH_CHANNELS ON CACHE BOOL "" FORCE)
set(WITH_CLIENT_CHANNELS ON CACHE BOOL "" FORCE)
set(CHANNEL_RDPGFX ON CACHE BOOL "" FORCE)
set(CHANNEL_CLIPRDR ON CACHE BOOL "" FORCE)
set(CHANNEL_RDPSND ON CACHE BOOL "" FORCE)
set(CHANNEL_AUDIN ON CACHE BOOL "" FORCE)
set(CHANNEL_RDPDR ON CACHE BOOL "" FORCE)
set(CHANNEL_DRIVE ON CACHE BOOL "" FORCE)
set(CHANNEL_DRDDYNVC ON CACHE BOOL "" FORCE)
set(CHANNEL_DISP ON CACHE BOOL "" FORCE)
set(CHANNEL_RAIL ON CACHE BOOL "" FORCE)
set(CHANNEL_RDPEI ON CACHE BOOL "" FORCE)
set(CHANNEL_GEOMETRY ON CACHE BOOL "" FORCE)
set(CHANNEL_AINPUT ON CACHE BOOL "" FORCE)

# Canales que requieren dependencias del sistema que no queremos ni necesitamos:
#   urbdrc   -> libusb (redirección USB)
#   printer  -> cairo/pango (impresoras)
#   smartcard-> pcsc/pkcs11 (tarjetas)
#   serial/parallel -> puertos legacy
set(CHANNEL_URBDRC OFF CACHE BOOL "" FORCE)
set(CHANNEL_PRINTER OFF CACHE BOOL "" FORCE)
set(CHANNEL_SMARTCARD OFF CACHE BOOL "" FORCE)
set(CHANNEL_SERIAL OFF CACHE BOOL "" FORCE)
set(CHANNEL_PARALLEL OFF CACHE BOOL "" FORCE)
set(CHANNEL_WEBVIEW OFF CACHE BOOL "" FORCE)
set(CHANNEL_SMARTCARD_UI OFF CACHE BOOL "" FORCE)

add_subdirectory("${QTRDP_FREERDP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/freerdp-build" EXCLUDE_FROM_ALL)

# FreeRDP usa include_directories() en el ámbito de directorio, así que las
# rutas de inclusiones NO se propagan a este proyecto: hay que añadirlas a mano.
set(QTRDP_FREERDP_INCLUDE_DIRS
    "${QTRDP_FREERDP_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/freerdp-build/include"
    "${QTRDP_FREERDP_SOURCE_DIR}/winpr/include"
    "${CMAKE_BINARY_DIR}/freerdp-build/winpr/include"
)

# Los canales cliente estáticos se compilan dentro del target `freerdp-client`,
# por eso no hay un target separado que enlazar.
set(QTRDP_FREERDP_LIBRARIES
    freerdp-client
    freerdp
    winpr
)

set(QTRDP_FREERDP_DEFINITIONS "")
if(BUILD_SHARED_LIBS)
    set(QTRDP_FREERDP_STATIC OFF)
else()
    set(QTRDP_FREERDP_STATIC ON)
    set(QTRDP_FREERDP_DEFINITIONS FREERDP_STATIC)
endif()

# Dependencias del sistema que FreeRDP deja como transitivas (enlaces estáticos).
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
set(QTRDP_FREERDP_SYSTEM_LIBS OpenSSL::SSL OpenSSL::Crypto PkgConfig::ZLIB ${CMAKE_DL_LIBS} m)

# Dependencias exclusivas de QtRDP (mapeo de teclado a scancodes RDP).
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
set(QTRDP_APP_LIBRARIES PkgConfig::XKBCOMMON)
