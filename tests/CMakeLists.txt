find_package(Qt6 6.5 REQUIRED COMPONENTS Test Widgets)

set(QTRDP_TEST_CORE_SOURCES
    ../src/core/ConnectionProfile.h
    ../src/core/ConnectionProfile.cpp
    ../src/core/RdpFile.h
    ../src/core/RdpFile.cpp
    ../src/core/ProfileStore.h
    ../src/core/ProfileStore.cpp
    ../src/core/CredentialStore.h
    ../src/core/CredentialStore.cpp
    ../src/core/CredentialFormat.h
    ../src/core/CredentialFormat.cpp
    ../src/core/XFreeRdpLauncher.h
    ../src/core/XFreeRdpLauncher.cpp
    ../src/core/SessionTypes.h
    ../src/core/SessionBackend.h
    ../src/core/SessionRegistry.h
    ../src/core/SessionRegistry.cpp
    ../src/core/backends/rdp/RdpSession.h
    ../src/core/backends/rdp/RdpSession.cpp
    ../src/core/backends/rdp/RdpInput.h
    ../src/core/backends/rdp/RdpInput.cpp
)

set(QTRDP_TEST_UI_SOURCES
    ../resources/qtrdp.qrc
    ../src/ui/ConnectionDialog.h
    ../src/ui/ConnectionDialog.cpp
    ../src/ui/SessionView.h
    ../src/ui/SessionView.cpp
    ../src/ui/SessionWindow.h
    ../src/ui/SessionWindow.cpp
)

qt_add_executable(qtrdp_tests TestCore.cpp ${QTRDP_TEST_CORE_SOURCES})

target_include_directories(qtrdp_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    ${QTRDP_FREERDP_INCLUDE_DIRS}
)

target_compile_definitions(qtrdp_tests PRIVATE QTRDP_VERSION="${PROJECT_VERSION}")

target_link_libraries(qtrdp_tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus Qt6::Test
    ${QTRDP_FREERDP_LIBRARIES}
    ${QTRDP_APP_LIBRARIES}
)

if(QTRDP_FREERDP_STATIC)
    target_link_libraries(qtrdp_tests PRIVATE ${QTRDP_FREERDP_SYSTEM_LIBS})
endif()

qt_add_executable(qtrdp_ui_tests TestUi.cpp ${QTRDP_TEST_CORE_SOURCES} ${QTRDP_TEST_UI_SOURCES})

target_include_directories(qtrdp_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    ${QTRDP_FREERDP_INCLUDE_DIRS}
)

target_compile_definitions(qtrdp_ui_tests PRIVATE QTRDP_VERSION="${PROJECT_VERSION}")

target_link_libraries(qtrdp_ui_tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Test
    ${QTRDP_FREERDP_LIBRARIES}
    ${QTRDP_APP_LIBRARIES}
)

if(QTRDP_FREERDP_STATIC)
    target_link_libraries(qtrdp_ui_tests PRIVATE ${QTRDP_FREERDP_SYSTEM_LIBS})
endif()

add_test(NAME core COMMAND qtrdp_tests)
add_test(NAME ui COMMAND qtrdp_ui_tests)

set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
