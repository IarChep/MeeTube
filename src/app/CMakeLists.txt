# ---------------------------------------------------------------------------
# app/ — the MeeTube GUI application (the `meetube` executable).
#
# The booster entry point (main.cpp + QmlApplicationViewer), the squircle/
# Perlin/QR QDeclarativeItems + image provider (harmattan/), and the QML
# resources. Links the `meetube-core` static lib (defined in core/) for all
# backend work. Added from the root AFTER core/ + webp-imageformat/ (this
# target links meetube-core and add_dependencies() on qwebp).
# ---------------------------------------------------------------------------
add_executable(meetube
    main.cpp
    qmlapplicationviewer/qmlapplicationviewer.cpp
    # Squircle avatar: a QDeclarativeItem (MaskedItem) + QGraphicsEffect (MaskEffect)
    # that composites children over an alpha mask (CompositionMode_SourceIn), ported
    # from cuteTube2. Both declare Q_OBJECT -> moc'd via the target's AUTOMOC.
    harmattan/maskeffect.cpp
    harmattan/maskeditem.cpp
    # Animated Perlin-noise brand banner (QDeclarativeItem) used as the HeaderBar
    # background — ported from throne-harmattan, recolored to MeeTube's red palette.
    harmattan/perlinbackground.cpp
    # QR image provider (image://qr/...) for the device-code login; renders via the
    # bundled qrcodegen static lib (linked below).
    harmattan/qrimageprovider.cpp
    # QML resources (main.qml + pages/components + avatar-mask.png) baked into the
    # binary; main.cpp loads qrc:/qml/main.qml. Compiled via the target's AUTORCC.
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

# qmlapplicationviewer.h declares Q_OBJECT (the QDeclarativeView subclass), so the
# app target must be moc'd. (meetube-core is moc'd in core/CMakeLists.txt.) AUTORCC
# compiles resources/resources.qrc into the binary.
set_target_properties(meetube PROPERTIES AUTOMOC ON AUTORCC ON)

# main.cpp / the harmattan items include their siblings as "harmattan/..." and
# "qmlapplicationviewer/..." — resolve those from this app/ dir. The backend
# headers ("innertube/...", "models/...", "core/...") come from meetube-core's
# PUBLIC include roots (core/ + core/types/), inherited via the link below.
target_include_directories(meetube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(meetube PRIVATE
    meetube-core
    qrcodegen
    Qt4::QtCore
    Qt4::QtGui
    Qt4::QtDeclarative
    Qt4::QtNetwork
)

# Host Simulator: link the libpng12 shim AFTER the Qt libs so it resolves
# libQtGui's png_*@PNG12_0 references. Built before meetube via the dependency.
if(NOT BUILD_N9)
    target_link_libraries(meetube PRIVATE png12)
    add_dependencies(meetube libpng12_build libjpeg62_build openssl102)
    set_target_properties(meetube PROPERTIES BUILD_RPATH ${LIBPNG12_INSTALL}/lib)
endif()

if(BUILD_N9)
    # The N9 Harmattan rootfs ships /lib/ld-linux.so.3 (it has NO armhf-suffixed loader),
    # but the GCC-14 cross toolchain — targeting armv7hf (hard-float) — defaults PT_INTERP
    # to /lib/ld-linux-armhf.so.3 — pin it back or the binary fails to exec on device
    # (ENOENT). Static C++ runtime: the device's GCC-4.x libstdc++ lacks the newer
    # GLIBCXX/CXXABI symbols this cross build emits.
    #
    # /opt/meetube/lib RPATH: the deps/ block bundles our freshly-built, ABI-compatible
    # libpng12.so.0 / libjpeg.so.62 (and OpenSSL/libwebp) there. --disable-new-dtags emits
    # a DT_RPATH (not DT_RUNPATH); DT_RPATH is inherited by transitively-loaded libs, so
    # Qt's image plugins resolving libpng12.so.0 / libjpeg.so.62 also search
    # /opt/meetube/lib first → our copies win over the device sysroot's stock ones.
    #
    # Harmattan startup booster: the app is now linked -pie/-rdynamic against
    # libmdeclarativecache so applauncherd can pre-fork it (faster cold start).
    # HARMATTAN_BOOSTER makes createApplication() return MDeclarativeCache::qApplication()
    # (see qmlapplicationviewer/qmlapplicationviewer.cpp); the booster flags/cflags come
    # from the device sysroot's qdeclarative-boostable.pc. -pie turns the binary into a
    # PIE (ELF Type=DYN), which is required by applauncherd.
    pkg_check_modules(boostable ${CMAKE_SYSROOT}/usr/lib/pkgconfig/qdeclarative-boostable.pc REQUIRED)
    target_compile_definitions(meetube PRIVATE HARMATTAN_BOOSTER)
    target_compile_options(meetube PRIVATE ${boostable_CFLAGS})
    target_include_directories(meetube PRIVATE ${CMAKE_SYSROOT}${boostable_INCLUDE_DIRS})
    target_link_options(meetube PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--dynamic-linker,/lib/ld-linux.so.3
        -Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags
        -pie -rdynamic -lmdeclarativecache)
    # The device app does not link png12, but it must wait for the bundled libs to be
    # built + installed (they package into /opt/meetube/lib via the deps/ install rules).
    add_dependencies(meetube libpng12_build libjpeg62_build)
endif()

# Install prefix: /opt/meetube
install(TARGETS meetube
    RUNTIME DESTINATION /opt/meetube/bin
)

# ---------------------------------------------------------------------------
# WebP plugin dir — let main.cpp register it as a Qt library path.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(WEBP_PLUGIN_DIR "/opt/meetube/qtplugins")
else()
    set(WEBP_PLUGIN_DIR "${CMAKE_BINARY_DIR}/qtplugins")
endif()
target_compile_definitions(meetube PRIVATE WEBP_PLUGIN_DIR="${WEBP_PLUGIN_DIR}")
add_dependencies(meetube qwebp)

# ---------------------------------------------------------------------------
# CA certificate bundle path — compiled in so main.cpp can load it into
# QSslConfiguration::defaultConfiguration() at startup.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(MEETUBE_CA_BUNDLE "/opt/meetube/ssl/cert.pem")
else()
    set(MEETUBE_CA_BUNDLE "${OSSL_INSTALL}/ssl/cert.pem")
endif()
target_compile_definitions(meetube PRIVATE MEETUBE_CA_BUNDLE="${MEETUBE_CA_BUNDLE}")

# ---------------------------------------------------------------------------
# N9 packaging (device only): the freedesktop launcher entry + icons, and the
# `package` target that stages everything through debian/rules and builds the
# .deb with the Madde-wrapped dpkg-buildpackage. The app payload itself
# (binary, bundled OpenSSL/png/jpeg/webp, WebP plugin, CA bundle) is installed
# under /opt/meetube by these install() rules and by deps/ + webp-imageformat/.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/packaging/meetube_harmattan.desktop
            DESTINATION /usr/share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/packaging/meetube80.png
            DESTINATION /usr/share/icons/hicolor/80x80/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/packaging/meetube64.png
            DESTINATION /usr/share/icons/hicolor/64x64/apps)

    # Copy debian/ into the build dir so dpkg-buildpackage (run from there) finds it.
    add_custom_target(copy_debian_dir
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/debian" "${CMAKE_BINARY_DIR}/debian"
        COMMENT "Copying debian/ into the build directory")

    # Build the .deb. debian/rules' install target re-enters
    # `cmake --build . --target install` with DESTDIR, so every install() rule
    # populates the package staging root. -nc keeps the already-built tree;
    # -uc -us skip signing (aegis still signs via mad). Run: make -C build-n9 package
    add_custom_target(package
        # dpkg-genchanges chokes on a stale debian/files referencing removed .debs;
        # drop it so dpkg regenerates a fresh list each run.
        COMMAND ${CMAKE_COMMAND} -E rm -f debian/files
        COMMAND /home/iarchep/QtSDK/Madde/bin/mad -t harmattan_10.2011.34-1_rt1.2 dpkg-buildpackage -nc -uc -us
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building the Debian package with mad..."
        DEPENDS meetube copy_debian_dir)
endif()
