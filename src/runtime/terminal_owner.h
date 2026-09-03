// src/runtime/terminal_owner.h -- who has the screen, for the one message
// that cannot wait for it back.
//
// INTERNAL. Not shipped in include/qtty/, because no application needs it and
// a header that leaves the tree is a promise about its contents.
//
// The deferring message handler in application.cpp asks this before printing a
// fatal message, and gives the screen back first where somebody has it: a
// message printed onto the alternate screen dies with the alternate screen,
// which is what 2746 bytes of frame and no sentence looked like.
//
// It is set by the BACKEND, in resume() and suspend(), rather than by exec().
// Those are the two calls where "who has the screen" actually changes, and
// backend.h supports an application driving a backend through its own frame
// loop -- which exec() never sees. Registering in exec() left exactly that
// application with a fatal message printed onto a frame about to be torn down.
#pragma once

namespace Qtty {

class ITerminalBackend;

void set_terminal_owner(ITerminalBackend *owner);

} // namespace Qtty
