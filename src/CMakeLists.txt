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
    innertube/itransport.cpp
    innertube/innertubeclient.cpp
    requests/servicerequest.cpp
    requests/videorequest.cpp
    requests/streamsrequest.cpp
    requests/commentrequest.cpp
    requests/categoryrequest.cpp
    requests/subtitlesrequest.cpp
    requests/playlistrequest.cpp
    requests/userrequest.cpp
    innertube/accountstore.cpp
    innertube/accountmanager.cpp
    innertube/innertube.cpp
    models/servicelistmodel.cpp
    models/videomodel.cpp
    models/streammodel.cpp
    models/commentmodel.cpp
    models/categorymodel.cpp
    models/subtitlemodel.cpp
    models/playlistmodel.cpp
    models/usermodel.cpp
    models/accountmodel.cpp
    models/servicemetatypes.cpp
)
# AUTOMOC: innertube/innertubeclient.h declares a Q_OBJECT (signals/slots for
# the QNetworkAccessManager-backed transport), as do the request classes
# (requests/*.h) and the Innertube engine, so the lib must be moc'd.
set_target_properties(meetube-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON)

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

# The test harness now lives in its own subproject — see tests/CMakeLists.txt,
# wired from the root (NOT BUILD_N9, after this src/ subdir).
