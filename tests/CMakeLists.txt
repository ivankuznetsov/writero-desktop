find_package(Qt6 6.5 REQUIRED COMPONENTS Test QuickTest Quick)

function(writero_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE writero_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
    )
endfunction()

writero_add_test(tst_app_info core/tst_app_info.cpp)
writero_add_test(tst_aiclient ai/tst_aiclient.cpp)
writero_add_test(tst_document document/tst_document.cpp)
writero_add_test(tst_documentio document/tst_documentio.cpp)
writero_add_test(tst_markdown markdown/tst_markdown.cpp)
writero_add_test(tst_modelcatalog ai/tst_modelcatalog.cpp)
writero_add_test(tst_textactions ai/tst_textactions.cpp)
writero_add_test(tst_workspacestore storage/tst_workspacestore.cpp)

add_executable(tst_aicontroller ai/tst_aicontroller.cpp)
target_link_libraries(tst_aicontroller PRIVATE writero_ui writero_uiplugin Qt6::Test)
add_test(NAME tst_aicontroller COMMAND tst_aicontroller)
set_tests_properties(tst_aicontroller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)

add_executable(tst_accountsession cloud/tst_accountsession.cpp)
target_link_libraries(tst_accountsession PRIVATE writero_ui writero_uiplugin Qt6::Test)
add_test(NAME tst_accountsession COMMAND tst_accountsession)
set_tests_properties(tst_accountsession PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)

add_executable(tst_providerregistry ai/tst_providerregistry.cpp)
target_link_libraries(tst_providerregistry PRIVATE writero_ui writero_uiplugin Qt6::Test)
add_test(NAME tst_providerregistry COMMAND tst_providerregistry)
set_tests_properties(tst_providerregistry PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)

add_executable(tst_workspace storage/tst_workspace.cpp)
target_link_libraries(tst_workspace PRIVATE writero_ui writero_uiplugin Qt6::Test)
add_test(NAME tst_workspace COMMAND tst_workspace)
set_tests_properties(tst_workspace PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)

add_executable(tst_documentcontroller editor/tst_documentcontroller.cpp)
target_link_libraries(tst_documentcontroller PRIVATE writero_ui writero_uiplugin Qt6::Test)
add_test(NAME tst_documentcontroller COMMAND tst_documentcontroller)
set_tests_properties(tst_documentcontroller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)

qt_add_executable(tst_ui ui/tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE
    writero_core
    writero_uiplugin
    Qt6::Test
    Qt6::QuickTest
)
target_compile_definitions(tst_ui PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui"
)
add_test(NAME tst_ui COMMAND tst_ui -input "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set_tests_properties(tst_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
)
