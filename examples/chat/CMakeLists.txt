# examples/chat — dual-frontend chat, the canonical qtty example.
#
#   chat       dual-frontend; picks GUI/TUI at runtime      <- ship this one
#   chat-gui   GUI-only  (-DQTTY_NO_TUI)  — policy variant
#   chat-tui   TUI-only  (-DQTTY_NO_GUI)  — policy variant
#
# All three link the same libraries; variants strip code paths, not deps.
# Recommended packaging: install `chat` alone, optionally with a `chat-tui`
# symlink (argv[0] selects the frontend — see main.cpp).
#
# Out-of-tree builds: find_package(qtty REQUIRED) provides qtty::qtty.

set(CHAT_SOURCES main.cpp chat.h)

function(add_chat_variant name)
    add_executable(${name} ${CHAT_SOURCES})
    target_link_libraries(${name} PRIVATE qtty::qtty)
    target_compile_definitions(${name} PRIVATE ${ARGN})
endfunction()

add_chat_variant(chat)
add_chat_variant(chat-gui QTTY_NO_TUI)
add_chat_variant(chat-tui QTTY_NO_GUI)
