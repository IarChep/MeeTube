# ---------------------------------------------------------------------------
# meetube-core — pure value types + JSON/parser helpers; Qt4::QtNetwork linked
# for future transport classes.
# Built as a STATIC PIC lib so it can be linked into both the app and any
# future MODULE plugins.
# ---------------------------------------------------------------------------
add_library(meetube-core STATIC
    parsers/continuation.cpp
    parsers/rendererparser.cpp
    parsers/playerparser.cpp
    innertube/clientconfig.cpp
    innertube/contextbuilder.cpp
)
set_target_properties(meetube-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# Two PUBLIC include roots:
#   ${CMAKE_CURRENT_SOURCE_DIR}        → resolves "parsers/continuation.h" etc.
#   ${CMAKE_CURRENT_SOURCE_DIR}/types  → resolves "servicedatatypes.h" (no dir prefix)
target_include_directories(meetube-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/types)

target_link_libraries(meetube-core PUBLIC
    Qt4::QtCore
    Qt4::QtNetwork
    nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# Test harness — host/simulator only (no QtTest on the N9 device).
# ---------------------------------------------------------------------------
if(NOT BUILD_N9)
    function(mt_test name)
        add_executable(${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp ${ARGN})
        set_target_properties(${name} PROPERTIES AUTOMOC ON)
        target_link_libraries(${name} PRIVATE meetube-core Qt4::QtTest)
        target_compile_definitions(${name} PRIVATE
            MT_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests"
            MT_SRC_DIR="${CMAKE_SOURCE_DIR}/src")
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/tests)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    mt_test(tst_meetube_parsers)
    mt_test(tst_meetube_context)
endif()
