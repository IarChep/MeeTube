# ---------------------------------------------------------------------------
# src/ — the MeeTube source subprojects, added in dependency order:
#   core/             → the meetube-core STATIC lib (the backend)
#   webp-imageformat/ → libqwebp.so image plugin (defines the `qwebp` target)
#   app/              → the meetube GUI executable (links meetube-core + qwebp)
# Each has its own CMakeLists.txt. deps/ (bundled third-party) and tests/ stay
# at the repo root (added around this subdir from there).
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(webp-imageformat)
add_subdirectory(app)
