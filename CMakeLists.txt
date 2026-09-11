cmake_minimum_required(VERSION 3.21)

project(writero-desktop
    VERSION 0.1.0
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

qt_standard_project_setup(REQUIRES 6.5)

add_library(writero_core STATIC
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
)
target_include_directories(writero_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(writero_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Sql
)
target_compile_definitions(writero_core PUBLIC WRITERO_VERSION="${PROJECT_VERSION}")

set(WRITERO_QML_FILES
    qml/Main.qml
)

qt_add_executable(writero-desktop
    src/main.cpp
)
target_link_libraries(writero-desktop PRIVATE writero_core)

qt_add_qml_module(writero-desktop
    URI Writero
    VERSION 0.1
    QML_FILES
        ${WRITERO_QML_FILES}
)

if(WRITERO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
