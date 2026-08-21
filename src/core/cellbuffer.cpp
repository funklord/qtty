// src/core/cellbuffer.cpp — L2 (§5.2). CellBuffer is header-implemented for
// now; this TU anchors the target and will take colour quantisation and the
// grapheme/width tables in Phase 2 (§17.1).
#include "qtty/cell.h"
static_assert(sizeof(qtty::Cell) > 0, "qtty L2");
