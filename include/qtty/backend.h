// qtty/backend.h -- L1 terminal backend seam (section 5.1) and the optional graphics
// extension (section 5.7). This is the interface the four legacy TUI implementations
// adapt to in Phase 1, and the one the in-tree backend already implements:
// AnsiBackend, in src/backend/ansi, rehosted behind this seam in 73fdee6. The
// header said "the in-tree AnsiRuntime will be rehosted behind it in Phase 2"
// long after that happened, and named a class the tree does not have.
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

	// And the terminal's foreground, if it answered OSC 10. It is a pair
	// with the background rather than a second colour of its own: whether
	// the terminal is dark is the comparison between the two, which is what
	// Qtty::color_scheme() answers.
	//
	// Separate flags, because the two replies are separate facts. A terminal
	// that answered one and not the other has said nothing about which
	// scheme it is, and that silence has to survive as silence -- the wrong
	// answers here are not symmetric. Guessing LIGHT wrongly leaves an
	// application looking plain; guessing DARK wrongly puts pale text on a
	// pale ground, which is unreadable.
	bool foreground_known = false;
	QColor foreground;

	// The terminal speaks the kitty keyboard protocol, and qtty has asked it
	// to disambiguate its escape codes.
	//
	// It is here because a legacy terminal CANNOT SAY Ctrl+Shift+C: a
	// control byte is one of 32 values and carries no shift bit, so every
	// Ctrl+Shift+letter arrives as the plain control chord, and an
	// application binding one is binding a chord nothing can send. With the
	// protocol on, such a key arrives as CSI <code> ; <modifiers> u and the
	// shift is in the modifiers.
	//
	// AND SIX CHORDS ARE NOT UNSENDABLE BUT AMBIGUOUS, which is the worse
	// half and is easy to miss beside the first. The control byte for
	// Ctrl+I is 0x09, and 0x09 is Tab; measured, it arrives as Key_Tab with
	// no ctrl on it at all, while the protocol delivers Key_I with ctrl.
	// So the binding is not silently dead, the key silently does something
	// ELSE -- and a user watching the focus jump has nothing to go on.
	//
	//     Ctrl+I      0x09  Tab          Ctrl+[  0x1b  Escape
	//     Ctrl+M      0x0d  Return       Ctrl+H  0x08  Backspace
	//     Ctrl+J      0x0a  Line feed    Ctrl+Space  0x00  Ctrl+@
	//
	// Every one is a chord an application might reasonably pick: Ctrl+I for
	// italic, Ctrl+M for a mark, Ctrl+H for help, Ctrl+Space for
	// completion.
	//
	// Ctrl+Space is the quiet one: almost nothing binds Ctrl+@, so instead
	// of doing something wrong the key does nothing at all.
	//
	// True means BOTH that the terminal answered CSI ? u and that qtty
	// pushed its own flags, which is one fact rather than two: qtty pushes
	// only where the answer came back, and pops on the way out.
	bool keyboard_protocol = false;
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
	// The vertical wheel keeps the name it has always had, and the horizontal
	// one is a second field rather than a rename: `wheel` is public API. SGR
	// distinguishes them by bit 1 of the button word -- 64/65 are up/down and
	// 66/67 are left/right -- and the decoder read only bit 0 for the whole
	// life of the file, so scrolling sideways scrolled up and down.
	int wheel = 0, wheel_x = 0;
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

	// The terminal has gone: its window was closed, or the output is a pipe
	// whose reader has finished. The backend learns it from EOF on the way in
	// and from a failed write on the way out, and both say it here.
	//
	// It was a synthesised Ctrl+D until now, and that cost more than it
	// looked. Three things claimed the chord: qtty's quit key, readline's
	// delete-forward in a text field, and this. At the router the machine
	// event and a typed one ARE the same event, so nothing could give the
	// chord to a text field without swallowing the signal that stops a
	// program whose terminal has closed -- and nothing could let an
	// application change its quit keys without the same risk.
	//
	// Not pure, and a default that does nothing: a sink written before this
	// existed keeps compiling, and the one that matters -- InputRouter --
	// implements it. That is the interface-only-as-wired-as-its-least-used-
	// method hazard by construction, so the suite checks the wiring end to
	// end rather than the method alone.
	virtual void on_terminal_lost() {}
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

	// The terminal's title, from the window's own `windowTitle()`. A
	// terminal shows it where a desktop shows a title bar -- the tab, the
	// task list, the window manager's furniture -- and an unmodified Qt
	// application already sets one, so this asks nothing new of it.
	//
	// NOT pure, deliberately. Every backend outside this tree would stop
	// compiling if it were, and a backend that cannot set a title is a
	// normal thing rather than a broken one: `NullBackend` is the case in
	// this repository. The default does nothing, and `Capabilities::title`
	// says whether anything will happen.
	virtual void set_title(const QString &) {}

	// Ring the terminal's bell -- BEL, 0x07, the one attention signal a
	// terminal has. It is worth more than a noise: most emulators map it to
	// the window-urgency hint, which is what marks a background tab, so it
	// carries what QApplication::alert() means as well as what
	// QApplication::beep() does.
	//
	// NOT pure, for the reason set_title() is not: this interface ships, and
	// tool/consume-check builds a program against the installed headers to
	// prove an adopter can reach it. A pure virtual added here would stop
	// every adopter's backend compiling on an upgrade that promised them
	// nothing, and a backend that cannot ring anything is a normal thing
	// rather than a broken one -- NullBackend and any test double are the
	// case in this tree. The default does nothing, which is exactly what a
	// backend with no terminal should do.
	virtual void bell() {}

	virtual void suspend() = 0;                     // SIGTSTP / shelling out
	virtual void resume() = 0;

	// How many times this backend has given the terminal up and taken it
	// back. Anything holding a copy of what is ON the terminal has to know
	// when that copy stopped being true, and a handover is exactly when: the
	// alternate screen is cleared on the way back in, so a frame diff
	// measured against the last frame sent describes a screen that no longer
	// exists and every cell of it compares equal to nothing.
	//
	// A COUNT rather than a flag, so that each reader compares against its
	// own last-seen value and no reader can consume the news on behalf of
	// another -- the fault measured inside AnsiBackend when this arrived as
	// a flag.
	//
	// Not pure, and zero by default: a backend that never gives the terminal
	// up answers truthfully without implementing anything, which is the case
	// for NullBackend and for any test double.
	virtual int handovers() const { return 0; }
};

// The MIME type a mirrored terminal paste carries, and it exists so that a
// backend forwarding QClipboard to the terminal can tell a COPY from an ECHO.
//
// InputRouter mirrors an arriving paste into QClipboard, because otherwise
// Ctrl+V inside the application pastes whatever the program itself last
// copied -- nothing at all, in a program that has copied nothing -- while the
// user has just demonstrated what they meant to paste. Qt's clipboard is the
// only store a widget reads, so agreeing with the terminal means writing it.
//
// That write is a clipboard change like any other, and a backend watching for
// changes would send it straight back out as OSC 52. Harmless when the paste
// came from the terminal's own clipboard and destructive when it did not: a
// middle-click pastes the PRIMARY selection, so echoing it would overwrite
// the clipboard the user had, which is the loss this library already fixed
// once for a copy with no text half.
//
// A marker on the data rather than a flag or a timer, because the question is
// about THIS clipboard content and travels with it: a later copy of the same
// text by the application carries no marker and goes out normally.
//
// A backend that forwards the clipboard must skip a change carrying it.
inline const char *terminal_paste_format() {
	return "application/x-qtty-terminal-paste";
}

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
