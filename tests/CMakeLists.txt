# ---------------------------------------------------------------------------
# tests/ — host/simulator-only test harness (no QtTest on the N9 device).
#
# Its own subproject (per request). The root adds this directory only when
# NOT BUILD_N9, after add_subdirectory(src) so meetube-core (which these tests
# link) already exists. enable_testing() stays in the root.
# ---------------------------------------------------------------------------
project(MeeTubeTests CXX)

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
# Request classes + engine live in meetube-core (linked above), so no extra
# sources here — listing them again would cause duplicate-symbol link errors.
mt_test(tst_meetube_requests)
# Model + ServiceListModel live in meetube-core (linked above), so no extra
# sources here — listing them again would cause duplicate-symbol link errors.
mt_test(tst_meetube_model)
# InnertubeClient lifetime machinery (track/onFinished/onTimeout/
# onOwnerDestroyed) + the request Canceled guard. Drives the real m_nam over a
# local QTcpServer loopback (QtNetwork) and the async FakeTransport.
mt_test(tst_meetube_client)
