// qtty/qtty.h — umbrella header.
// Contract (§10.1): everything lives in namespace Qtty; no public macros;
// string identifiers are prefixed ("qtty.*", "org.qtty.*"). In GUI mode the
// library is inert unless setup()/exec() are called.
#pragma once
#include "color.h"
#include "cell.h"
#include "theme.h"
#include "backend.h"
#include "grid.h"
#include "paint.h"
#include "runtime.h"
#include "application.h"
#include "testing.h"
