# Qt 4 WebP image-format plugin (libqwebp.so). Decodes Google WebP via libwebp so
# QImageReader / QML Image can read "webp" transparently. Built into a private
# imageformats/ dir that main.cpp adds to the Qt library path at startup.
add_library(qwebp MODULE qwebpplugin.cpp qwebphandler.cpp)
set_target_properties(qwebp PROPERTIES
    AUTOMOC ON
    # Qt finds image plugins under <libraryPath>/imageformats/, so emit it there.
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qtplugins/imageformats
    PREFIX "lib")
target_link_libraries(qwebp PRIVATE Qt4::QtGui Qt4::QtCore)

if(BUILD_N9)
    # Cross-built libwebp (armel) — see the libwebp ExternalProject in the root CMakeLists.
    add_dependencies(qwebp libwebp_build)
    target_include_directories(qwebp PRIVATE ${LIBWEBP_INSTALL}/include)
    target_link_libraries(qwebp PRIVATE ${LIBWEBP_SO})
    # Same device-loadability fix as the service plugins: the N9's GCC-4.x libstdc++ lacks the
    # newer GLIBCXX/CXXABI symbols this GCC-14 cross build emits, so link the C++ runtime
    # statically (else QImageReader silently drops the plugin), and pin the device loader.
    target_link_options(qwebp PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--dynamic-linker,/lib/ld-linux.so.3)
    # Package the plugin + the bundled libwebp into the .deb.
    install(TARGETS qwebp LIBRARY DESTINATION /opt/meetube/qtplugins/imageformats)
    install(DIRECTORY ${LIBWEBP_INSTALL}/lib/ DESTINATION /opt/meetube/lib
            FILES_MATCHING PATTERN "libwebp.so*")
else()
    # Host simulator: link the host libwebp (Arch ships libwebp + headers).
    find_path(WEBP_INCLUDE_DIR webp/decode.h)
    find_library(WEBP_LIBRARY NAMES webp)
    if(NOT WEBP_LIBRARY OR NOT WEBP_INCLUDE_DIR)
        message(FATAL_ERROR "libwebp not found — install the host libwebp dev package for the simulator build")
    endif()
    target_include_directories(qwebp PRIVATE ${WEBP_INCLUDE_DIR})
    target_link_libraries(qwebp PRIVATE ${WEBP_LIBRARY})
endif()
