# ---------------------------------------------------------------------------
# meetube-core — pure value types + JSON/parser helpers; Qt4::QtNetwork linked
# for future transport classes.
# Built as a STATIC PIC lib so it can be linked into both the app and any
# future MODULE plugins.
# ---------------------------------------------------------------------------
add_library(meetube-core STATIC
    parsers/continuation.cpp
    parsers/videolistparser.cpp
    parsers/watchparser.cpp
    parsers/playlistparser.cpp
    parsers/channelparser.cpp
    parsers/commentparser.cpp
    parsers/accountparser.cpp
    parsers/playerparser.cpp
    innertube/clientconfig.cpp
    innertube/contextbuilder.cpp
    core/http.cpp
    core/chains.cpp
    requests/bodies.cpp
    innertube/accountstore.cpp
    innertube/accountmanager.cpp
    innertube/videoapi.cpp
    innertube/channelapi.cpp
    innertube/playlistapi.cpp
    innertube/videodetails.cpp
    innertube/streamset.cpp
    innertube/subtitleset.cpp
    innertube/channeldetails.cpp
    innertube/accountdetails.cpp
    innertube/accountapi.cpp
    innertube/innertube.cpp
    models/servicelistmodel.cpp
    models/videomodel.cpp
    models/commentmodel.cpp
    models/playlistmodel.cpp
    models/channelmodel.cpp
    models/accountmodel.cpp
    models/servicemetatypes.cpp
    threading/workerhost.cpp
    net/curlengine.cpp
    net/curlnetworkreply.cpp
    net/curlnetworkaccessmanager.cpp
    media/ipolicy.cpp
    media/ipipeline.cpp
    media/bytesource.cpp
    media/streamplayer.cpp
)
# AUTOMOC: core/http.h declares a Q_OBJECT (signals/slots for the
# QNetworkAccessManager-backed transport), as do the models, detail objects,
# threading/workerhost.h and the Innertube engine, so the lib must be moc'd.
set_target_properties(meetube-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON)

# Two PUBLIC include roots:
#   ${CMAKE_CURRENT_SOURCE_DIR}        → resolves "parsers/continuation.h" etc.
#   ${CMAKE_CURRENT_SOURCE_DIR}/types  → resolves "servicedatatypes.h" (no dir prefix)
target_include_directories(meetube-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/types)

# CA bundle path — core::Http seeds it into the libcurl NAM (CURLOPT_CAINFO) so
# HTTPS to YouTube/OAuth/RYD verifies against the vendored Mozilla bundle. Must
# resolve to a path that EXISTS at runtime on the target: the build-tree
# cacert.pem on the host (downloaded/verified by deps/), the installed
# /opt/meetube/ssl/cert.pem on the device. CACERT_PEM is exported CACHE INTERNAL
# by deps/CMakeLists.txt. Kept in lockstep with the meetube app target (src/app/).
if(BUILD_N9)
    target_compile_definitions(meetube-core PRIVATE MEETUBE_CA_BUNDLE="/opt/meetube/ssl/cert.pem")
else()
    target_compile_definitions(meetube-core PRIVATE MEETUBE_CA_BUNDLE="${CACERT_PEM}")
endif()

target_link_libraries(meetube-core PUBLIC
    Qt4::QtCore
    Qt4::QtNetwork)

# Glaze is an implementation detail of the parser/transport .cpps — the public
# headers expose only std::string_view / std::string, so the link stays PRIVATE
# and nothing else in the app pays the template-heavy include.
target_link_libraries(meetube-core PRIVATE glaze)

# libcurl.
#   Host: the system libcurl (found via CMake's FindCURL / Conan).
#   Device: the freshly cross-built libcurl bundled to /opt/meetube/lib by deps/
#           (OpenSSL 3 backend). CURL_SO / CURL_INCLUDE are exported CACHE INTERNAL
#           by deps/CMakeLists.txt; add_dependencies orders the ExternalProject
#           before meetube-core links. PUBLIC so the app inherits the include +
#           link (the meetube target already carries the /opt/meetube/lib rpath).
if(NOT BUILD_N9)
    find_package(CURL REQUIRED)
    target_link_libraries(meetube-core PUBLIC ${CURL_LIBRARIES})
    target_include_directories(meetube-core PUBLIC ${CURL_INCLUDE_DIRS})
else()
    add_dependencies(meetube-core libcurl_build)
    # The include dir is created by the ExternalProject's install step, which
    # runs before meetube-core's link but AFTER CMake's configure — so the dir
    # may not exist at configure time. Pre-create it so the PUBLIC include is
    # valid (INTERFACE_INCLUDE_DIRECTORIES entries must exist at generate time).
    file(MAKE_DIRECTORY ${CURL_INCLUDE})
    target_include_directories(meetube-core PUBLIC ${CURL_INCLUDE})
    # libcurl.so NEEDs libssl.so.3 / libcrypto.so.3 (our cross-built OpenSSL 3,
    # bundled to /opt/meetube/lib). They live in the openssl3-install tree, not
    # the sysroot, so the linker can't satisfy libcurl's transitive references
    # from the sysroot alone — link them explicitly, AFTER libcurl so GNU ld's
    # left-to-right resolution sees the crypto provider last. OSSL3_SSL /
    # OSSL3_CRYPTO are exported CACHE INTERNAL by deps/CMakeLists.txt.
    target_link_libraries(meetube-core PUBLIC ${CURL_SO} ${OSSL3_SSL} ${OSSL3_CRYPTO})
endif()

# The test harness lives in its own subproject — see tests/CMakeLists.txt,
# wired from the root (NOT BUILD_N9, after this core/ subdir). The GUI app is
# likewise its own subproject in app/.
