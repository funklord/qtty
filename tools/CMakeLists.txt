add_executable(qtty-inspect qtty-inspect.cpp)
target_link_libraries(qtty-inspect PRIVATE qtty::qtty)
# qtty-replay (§9) lands with the snapshot harness in Phase 2.
