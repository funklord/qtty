// qtty/qtty.h — umbrella header.
// Contract (§10.1): everything lives in namespace qtty; no public macros;
// string identifiers are prefixed ("qtty.*", "org.qtty.*"). In GUI mode the
// library is inert unless setup()/exec() are called.
#pragma once
#include "cell.h"
#include "backend.h"
#include "grid.h"
#include "paint.h"
#include "application.h"
