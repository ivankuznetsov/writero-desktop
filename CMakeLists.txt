cmake_minimum_required(VERSION 3.21)

project(writero-desktop
    VERSION 0.1.1
    DESCRIPTION "Native Writero desktop client"
    HOMEPAGE_URL "https://github.com/ivankuznetsov/writero-desktop"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WRITERO_BUILD_TESTS "Build the test suite" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Sql
    Svg)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
endif()

qt_standard_project_setup(REQUIRES 6.5)

add_library(writero_core STATIC
    src/ai/aiclient.h
    src/ai/aiclient.cpp
    src/ai/modelcatalog.h
    src/ai/modelcatalog.cpp
    src/ai/providerprofile.h
    src/ai/providerprofile.cpp
    src/ai/textactions.h
    src/ai/textactions.cpp
    src/core/AppInfo.h
    src/core/AppInfo.cpp
    src/document/block.h
    src/document/block.cpp
    src/document/blocktype.h
    src/document/blocktype.cpp
    src/document/document.h
    src/document/document.cpp
    src/document/documentchange.h
    src/document/documentsession.h
    src/document/documentsession.cpp
    src/document/listcontent.h
    src/document/listcontent.cpp
    src/document/documentio.h
    src/document/documentio.cpp
    src/markdown/markdown.h
    src/markdown/markdown.cpp
    src/security/credentialstore.h
    src/security/credentialstore.cpp
    src/storage/mediastore.h
    src/storage/mediastore.cpp
    src/storage/revision.h
    src/storage/workspacestore.h
    src/storage/workspacestore.cpp
)
target_include_directories(writero_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(writero_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Sql
)
if(LIBSECRET_FOUND)
    target_link_libraries(writero_core PUBLIC PkgConfig::LIBSECRET)
    target_compile_definitions(writero_core PUBLIC WRITERO_HAVE_LIBSECRET)
endif()
target_compile_definitions(writero_core PUBLIC WRITERO_VERSION="${PROJECT_VERSION}")

# The QML user interface lives in a static library so that both the
# application and the Qt Quick tests load the same components. The controller
# and list model are part of the module because they are exposed to QML.
add_library(writero_ui STATIC
    src/ai/aicontroller.h
    src/ai/aicontroller.cpp
    src/ai/providerregistry.h
    src/ai/providerregistry.cpp
    src/ai/providers/WriteroProvider.h
    src/ai/providers/WriteroProvider.cpp
    src/cloud/accountsession.h
    src/cloud/accountsession.cpp
    src/cloud/changereconciler.h
    src/cloud/changereconciler.cpp
    src/cloud/cloudclient.h
    src/cloud/cloudclient.cpp
    src/cloud/syncengine.h
    src/cloud/syncengine.cpp
    src/editor/blocklistmodel.h
    src/editor/blocklistmodel.cpp
    src/editor/documentcontroller.h
    src/editor/documentcontroller.cpp
    src/editor/documentlibrarymodel.h
    src/editor/documentlibrarymodel.cpp
    src/editor/formatactions.h
    src/editor/formatactions.cpp
    src/storage/workspace.h
    src/storage/workspace.cpp
)
target_link_libraries(writero_ui PUBLIC writero_core)
target_include_directories(writero_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cloud
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(writero_ui
    URI Writero
    VERSION 0.1
    SOURCES
        src/ai/aicontroller.h
        src/ai/providerregistry.h
        src/cloud/accountsession.h
        src/cloud/syncengine.h
        src/editor/blocklistmodel.h
        src/editor/documentcontroller.h
        src/editor/documentlibrarymodel.h
        src/storage/workspace.h
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/ai/AiPanel.qml
        qml/editor/BlockDelegate.qml
        qml/editor/BlockEditor.qml
        qml/editor/SlashMenu.qml
        qml/library/DocumentSidebar.qml
        qml/settings/Account.qml
        qml/settings/Providers.qml
        qml/settings/WriteroUsage.qml
        qml/sync/ConflictPanel.qml
        qml/sync/ShareLinkDialog.qml
)

qt_add_executable(writero-desktop
    src/main.cpp
)
target_link_libraries(writero-desktop PRIVATE
    writero_core
    writero_uiplugin
)

install(TARGETS writero-desktop RUNTIME DESTINATION bin)
install(FILES packaging/writero.desktop
    DESTINATION share/applications
    RENAME app.writero.Writero.desktop)
install(FILES packaging/app.writero.Writero.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME app.writero.Writero.svg)

if(WRITERO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
