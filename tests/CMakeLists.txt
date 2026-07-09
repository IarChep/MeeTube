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
    target_link_libraries(${name} PRIVATE meetube-core Qt4::QtTest glaze)
    target_compile_definitions(${name} PRIVATE
        MT_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests"
        MT_SRC_DIR="${CMAKE_SOURCE_DIR}/src")
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

mt_test(tst_meetube_parsers)
mt_test(tst_meetube_context)
# core::chains — the pure-C++ request layer (Task 12b: the sole successor to the
# deleted tst_meetube_requests). Driven over the FakeHttp seam; ports every old
# request case 1:1 + the oauth chains.
mt_test(tst_meetube_chains)
# Model + ServiceListModel live in meetube-core (linked above), so no extra
# sources here — listing them again would cause duplicate-symbol link errors.
mt_test(tst_meetube_model)
# core::Http transport: manager-level finish/timeout/coalescing/cancel + the TTL
# response cache + per-client context cache + visitor sink. Drives the real m_nam
# over a local QTcpServer loopback (QtNetwork).
mt_test(tst_meetube_client)
# Auth: AccountStore (QSettings round-trip) + AccountManager device-code flow +
# AccountDetails identity, all over the FakeHttp seam.
mt_test(tst_meetube_account)
# Threading foundation: WorkerHost (cross-thread posted-closure seam, inline when
# not started), JobToken (cancellation flag), core::Status mirror. Drives a real
# QThread round trip (invoke → worker → invokeGui → GUI) via QTRY_VERIFY.
mt_test(tst_meetube_threading)
# Media player backend: ByteSource ranged fetch (real CurlNAM over a loopback
# Range server), StreamPlayer state machine (fake pipeline/policy). Host-only;
# the GStreamer/resourceqt glue is device-only and lives in src/app/media/.
mt_test(tst_meetube_media)

find_package(CURL REQUIRED)
mt_test(tst_meetube_curlnam tst_meetube_curlnam.cpp)
target_link_libraries(tst_meetube_curlnam PRIVATE ${CURL_LIBRARIES})
target_include_directories(tst_meetube_curlnam PRIVATE ${CURL_INCLUDE_DIRS})
