// qtty/backend.h -- L1 terminal backend seam (section 5.1) and the optional graphics
// extension (section 5.7). This is the interface the four legacy TUI implementations
// adapt to in Phase 1; the in-tree AnsiRuntime will be rehosted behind it in
// Phase 2 (it currently drives the tty directly -- see src/backend/ansi).
#pragma once
#include <QSize>
#include <QPoint>
#include <QRegion>
#include <QImage>
#include <QColor>
#include <QSize>
#include <QString>
#include <optional>
#include "cell.h"

namespace Qtty {

struct Capabilities {
	enum ColorDepth { Mono, Ansi16, Xterm256, TrueColor };
	ColorDepth color        = Ansi16;
	bool mouse              = false;   // SGR 1006 or better
	bool bracketed_paste     = false;
	bool synchronised_output = false;   // DEC 2026 -- tear-free frames
	bool unicode_wide        = true;    // honours wcwidth-2 correctly
	bool title              = false;

	// Pixel-graphics support, negotiated with the terminal (section 5.7).
	enum GraphicsMode { NoGraphics, Halfblocks, Sixel, ITerm2, Kitty, KittyAlpha };
	GraphicsMode graphics   = NoGraphics;

	// Placements are delivered as kitty Unicode placeholders rather than as
	// direct placements: the image is transmitted once and displayed by
	// printing ordinary text, so anything Unicode-aware in between moves it
	// correctly when it redraws. That is what gets an image through tmux,
	// where a direct placement would land at the OUTER terminal's cursor
	// rather than where tmux is drawing.
	//
	// A flag rather than another GraphicsMode, because the tier is still
	// kitty -- what changes is how a placement is carried, not what the
	// terminal can draw.
	bool unicode_placements = false;

	// What one cell measures, if the terminal was willing to say. Invalid
	// until it answers, and it may answer LATER: the reply arrives on stdin
	// like everything else, and a resize is re-asked because a font change
	// moves this without moving the cell count.
	//
	// It matters because a half-block pixel is one cell wide and half a cell
	// tall. Treating that as square squashes every image on a terminal whose
	// cells are not 1:2, which is a wrong picture rather than a missing one.
	QSize cell_px;

	// The terminal's background, if it answered OSC 11. Every tier below
	// kitty composites an image's alpha against it, so without this they have
	// to guess -- and guessing black on a light terminal haloes every icon.
	bool background_known = false;
	QColor background;
};

enum class CursorShape { Block, Underline, Bar, Hidden };

struct KeyEvent   { int qt_key = 0; QString text; bool ctrl = false, alt = false, shift = false; };
// The modifiers are spelled as KeyEvent spells them, and they are here for the
// same reason they are there: a widget cannot tell an extend-select from a
// plain click without them. SGR 1006 carries them in bits 4, 8 and 16 of the
// button word, and they were decoded and discarded for the life of the file.
struct MouseEvent {
	QPoint cell;
	int button = 0;
	bool press = false, release = false, motion = false;
	int wheel = 0;
	bool ctrl = false, alt = false, shift = false;
};

class ITerminalEventSink {
public:
	virtual ~ITerminalEventSink() = default;
	virtual void on_key(const KeyEvent &) = 0;
	virtual void on_mouse(const MouseEvent &) = 0;
	virtual void on_paste(const QString &) = 0;
	virtual void on_resize(QSize cells) = 0;
	virtual void on_focus_change(bool focused) = 0;
};

class ITerminalBackend {
public:
	virtual ~ITerminalBackend() = default;

	virtual Capabilities capabilities() const = 0;
	virtual QSize size() const = 0;                 // in cells

	// Present a full frame. `damage` is advisory: backends may repaint
	// everything, but must never render outside it incorrectly.
	virtual void present(const CellBuffer &frame, const QRegion &damage) = 0;

	virtual void set_cursor(std::optional<QPoint> cell, CursorShape shape) = 0;

	// Backends push input; they never poll. Event-loop integration is the
	// backend's business (QSocketNotifier, thread, ...).
	virtual void set_event_sink(ITerminalEventSink *) = 0;

	virtual void suspend() = 0;                     // SIGTSTP / shelling out
	virtual void resume() = 0;
};

// Optional extension -- only for backends whose terminal accepts pixel data.
// Legacy backends need not implement it: the Halfblocks fallback is a pure L2
// transform reaching them through present() with zero changes (section 5.7).
class IGraphicsOutput {
public:
	virtual ~IGraphicsOutput() = default;
	// One full-terminal RGBA frame, already composited by GraphicsPlane.
	virtual void present_pixels(const QImage &frame, const QRegion &cell_region) = 0;
	// KittyAlpha only: alpha image over live text, terminal-blended.
	virtual void present_overlay(int id, const QImage &rgba, QPoint cell, int z) = 0;
	virtual void clear_overlay(int id) = 0;
};

} // namespace Qtty
