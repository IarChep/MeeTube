# Creates a "farm" of soname symlinks pointing at the real libssl/libcrypto 1.0.2
# in LIBDIR. The OpenSSL runtime loader inside Qt 4.7.4 looks the library up by a
# specific soname (varies across Qt builds: libssl.so.1.0.0 / .so.0.9.8 / .so.10 /
# .so etc.), so we lay down links for every likely variant — whichever is requested
# will resolve.
#
# Invoke: cmake -DLIBDIR=<dir> -P make_soname_links.cmake
foreach(lib ssl crypto)
    set(real "lib${lib}.so.1.0.0")
    foreach(alias so so.0 so.1 so.10 so.1.0 so.0.9.8)
        file(CREATE_LINK "${real}" "${LIBDIR}/lib${lib}.${alias}" SYMBOLIC)
    endforeach()
endforeach()
