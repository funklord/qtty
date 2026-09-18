// src/runtime/url_opener.h -- what QDesktopServices::openUrl() does here.
//
// Internal, the way terminal_owner.h is internal: setup() calls this and no
// application has any reason to. It is DECLARED rather than left file-static
// for one reason, and the reason is a test. An application overriding qtty's
// handler with its own QDesktopServices::setUrlHandler() is the documented
// escape hatch, so the suite has to prove it works -- and Qt keeps one
// handler per scheme, so proving it destroys qtty's. Without a way to put it
// back, that check would leave every suite running after it with no handler
// at all, which is a check that breaks the thing it was written to defend.
#pragma once

namespace Qtty {

// Register qtty's handler for http and https, replacing whatever those two
// schemes had. Called by setup(); idempotent, and safe to call again to put
// the handler back after something else has taken a scheme over.
void install_url_handlers();

} // namespace Qtty
