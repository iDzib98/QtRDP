cmake_minimum_required(VERSION 3.22)

project(QtRDP
    VERSION 0.1.0
    DESCRIPTION "Cliente de escritorio remoto estilo mstsc.exe para Linux"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(QTRDP_BUILD_TESTS "Compilar las pruebas unitarias" ON)
option(QTRDP_SYSTEM_FREERDP "Enlazar contra el FreeRDP instalado en el sistema" OFF)
option(QTRDP_ENABLE_KEYRING "Guardar contraseñas con libsecret cuando esté disponible" ON)
option(QTRDP_BUILD_NATIVE_BACKEND "Compilar el backend RDP nativo con FreeRDP" ON)

find_package(PkgConfig QUIET)
if(QTRDP_ENABLE_KEYRING)
    pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
endif()

if(QTRDP_BUILD_NATIVE_BACKEND)
    include(FreeRDP)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network DBus)
if(QTRDP_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

set(QTRDP_SOURCES
    src/main.cpp

    src/core/ConnectionProfile.h
    src/core/ConnectionProfile.cpp
    src/core/RdpFile.h
    src/core/RdpFile.cpp
    src/core/ProfileStore.h
    src/core/ProfileStore.cpp
    src/core/CredentialStore.h
    src/core/CredentialStore.cpp
    src/core/CredentialFormat.h
    src/core/CredentialFormat.cpp
    src/core/XFreeRdpLauncher.h
    src/core/XFreeRdpLauncher.cpp
    src/core/SessionTypes.h
    src/core/SessionBackend.h

    src/ui/ConnectionDialog.h
    src/ui/ConnectionDialog.cpp
    src/ui/SessionView.h
    src/ui/SessionView.cpp
    src/ui/SessionWindow.h
    src/ui/SessionWindow.cpp

    resources/qtrdp.qrc
)

if(QTRDP_BUILD_NATIVE_BACKEND)
    list(APPEND QTRDP_SOURCES
        src/core/SessionRegistry.h
        src/core/SessionRegistry.cpp
        src/core/backends/rdp/RdpSession.h
        src/core/backends/rdp/RdpSession.cpp
        src/core/backends/rdp/RdpInput.h
        src/core/backends/rdp/RdpInput.cpp
    )
endif()

qt_add_executable(qtrdp ${QTRDP_SOURCES})

target_include_directories(qtrdp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(QTRDP_BUILD_NATIVE_BACKEND)
    target_include_directories(qtrdp PRIVATE ${QTRDP_FREERDP_INCLUDE_DIRS})
endif()

target_compile_definitions(qtrdp PRIVATE QTRDP_VERSION="${PROJECT_VERSION}")

if(LIBSECRET_FOUND)
    target_compile_definitions(qtrdp PRIVATE QTRDP_HAVE_LIBSECRET)
    target_link_libraries(qtrdp PRIVATE PkgConfig::LIBSECRET)
    message(STATUS "QtRDP: las contraseñas se guardarán con libsecret")
else()
    message(WARNING "QtRDP: libsecret-1 no está disponible; "
                    "las contraseñas se guardarán sin cifrar en texto plano")
endif()

target_link_libraries(qtrdp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::DBus
)

if(QTRDP_BUILD_NATIVE_BACKEND)
    target_link_libraries(qtrdp PRIVATE ${QTRDP_FREERDP_LIBRARIES})
    if(QTRDP_FREERDP_STATIC)
        target_link_libraries(qtrdp PRIVATE ${QTRDP_FREERDP_SYSTEM_LIBS})
    endif()
endif()

target_link_libraries(qtrdp PRIVATE ${QTRDP_APP_LIBRARIES})

if(QTRDP_BUILD_NATIVE_BACKEND AND QTRDP_FREERDP_DEFINITIONS)
    target_compile_definitions(qtrdp PRIVATE ${QTRDP_FREERDP_DEFINITIONS})
endif()

if(QTRDP_BUILD_TESTS)
    if(QTRDP_BUILD_NATIVE_BACKEND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING "QtRDP: las pruebas requieren el backend nativo; se omiten")
    endif()
endif()

install(TARGETS qtrdp RUNTIME DESTINATION bin)
install(FILES packaging/qtrdp.desktop DESTINATION share/applications)
install(FILES resources/icons/qtrdp.svg DESTINATION share/icons/hicolor/scalable/apps)

set(CPACK_PACKAGE_NAME "qtrdp")
set(CPACK_PACKAGE_VENDOR "QtRDP")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cliente de escritorio remoto RDP")
set(CPACK_PACKAGE_CONTACT "QtRDP maintainers")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "freerdp3-x11, libqt6core6, libqt6gui6, libqt6widgets6, libqt6network6, libqt6dbus6, libsecret-1-0")
include(CPack)
