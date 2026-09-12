// src/backend/ansi/ansi_backend.h -- built-in ITerminalBackend over a raw
// ANSI tty (section 5.1). Escape decoding lives here (the backend side of the seam);
// input is pushed to the sink, never polled. Placements render as the
// NoGraphics mosaic tier (section 5.7); richer tiers land with the kitty/sixel
// encoders (section 17.3).
#pragma once
#include <QObject>
#include <QByteArray>
#include <termios.h>
#include "qtty/backend.h"
#include "term_caps.h"
#include "scroll_settle.h"
#include <QElapsedTimer>
#include <QSet>
#include <QVector>

class QTimer;

class QSocketNotifier;

namespace Qtty {

// Exposed so the rules can be tested against a TermCaps built by hand. Doing
// it against a live terminal would mean owning one that answers each way.
Capabilities::GraphicsMode negotiate_graphics(const TermCaps &caps);
Capabilities::ColorDepth negotiate_color(const TermCaps &caps);

// Whether placements should be carried as Unicode placeholders. True only
// where they are both NEEDED and SAFE: inside a host application that would
// otherwise misplace them, on a terminal proven to speak kitty, at a colour
// depth that can carry the image id exactly.
bool use_placeholders(const TermCaps &caps, Capabilities::ColorDepth depth);

// How long an unaccompanied ESC waits before it is delivered as Escape.
//
// ESC prefixes every escape sequence, so a lone one can only be told from the
// start of a longer one by a clock. The trade is symmetrical and both ends are
// real: too short and a link that splits Alt-<key> across two reads turns it
// into Escape followed by a stray letter; too long and Escape feels dead in a
// dialog. Named here rather than written into the decoder so that a test reads
// the shipped number instead of repeating it.
int escape_flush_ms();

class AnsiBackend : public QObject, public ITerminalBackend,
                    public IGraphicsOutput {
public:
	AnsiBackend();
	~AnsiBackend() override;

	Capabilities capabilities() const override;
	QSize size() const override;
	void present(const CellBuffer &frame, const QRegion &damage) override;
	void set_cursor(std::optional<QPoint> cell, CursorShape shape) override;
	void set_title(const QString &title) override;
	// Setting the sink drains whatever is already buffered, because the
	// constructor may have put TYPE-AHEAD there -- keys pressed before the
	// program drew. decode_one() clears the buffer when there is no sink, so
	// the draining cannot happen any earlier than this.
	void set_event_sink(ITerminalEventSink *s) override {
		sink_ = s;
		while (!pending_.isEmpty()) { if (!decode_one()) break; }
		// Type-ahead can be a bare Escape too, and until the sink existed
		// there was nobody to deliver it to. Without this the one key a user
		// presses to back out of a program that is still starting is the one
		// key that waits for ever.
		arm_escape_timer();
	}
	void suspend() override;
	void resume() override;

	// IGraphicsOutput (section 5.7): pixel tiers for capable terminals.
	void present_pixels(const QImage &frame, const QRegion &cell_region) override;
	void present_overlay(int id, const QImage &rgba, QPoint cell, int z) override;

	// qtty's pixel units into the terminal's, for anything about to go on
	// the wire. Named rather than local because both functions that transmit
	// an image need it and only one of them used to have it.
	QImage for_terminal(const QImage &img) const;
	QRect  for_terminal(const QRect &r) const;
	void clear_overlay(int id) override;

	// ---- the clipboard going OUT (OSC 52) ---------------------------------
	//
	// Which of the terminal's selections a copy lands in. xterm's ctlseqs
	// allows c p q s and the eight cut buffers; these are the two an
	// application means, and they are not interchangeable -- PRIMARY is what
	// a middle click pastes, so writing it turns a copy into an edit of
	// something the user did not ask about.
	enum class Selection { Clipboard, Primary };

	// Put `text` in the terminal's selection. False when nothing was written,
	// which is a real answer rather than a formality: it is returned for a
	// stream that is not a terminal, for a terminal that has been suspended,
	// and for text past clipboard_limit(). There is NO third state -- a copy
	// is written whole or not at all, because a truncated one the caller
	// believes went out is worse than a refused one.
	//
	// Applications need not call it. AnsiBackend watches QClipboard, so an
	// ordinary QClipboard::setText() reaches the terminal on its own; this is
	// for a caller that wants to name the selection, which Qt cannot express
	// under the offscreen platform qtty pins.
	bool write_clipboard(const QString &text,
	                     Selection sel = Selection::Clipboard);

	// The largest copy qtty will put on the wire, in bytes of UTF-8. Public
	// so an application can ask before it offers the user a Copy that cannot
	// work, and so a test reads the shipped bound rather than repeating it.
	static int clipboard_limit();

private:
	void read_input();
	// The pending-Escape window (section 5.1). arm_ starts it only when
	// pending_ holds exactly one ESC and nothing else; flush_ delivers the
	// Escape if that is still true when it expires. Any byte arriving in
	// between stops it, because bytes decide what the clock was guessing at.
	void arm_escape_timer();
	void flush_lone_escape();
	// Connects QClipboard to write_clipboard(), so an ordinary
	// QClipboard::setText() reaches the terminal without the application
	// knowing a backend exists. Called once, from the constructor.
	void watch_clipboard();
	QTimer *escape_timer_ = nullptr;
	bool decode_one();                    // one event from pending_ -> sink
	// A complete CSI at the head of pending_, or -1 if more bytes are needed.
	// Fills the private prefix, the numeric parameters and the final byte.
	int parse_csi(QByteArray &prefix, QVector<int> &params, QByteArray &inter,
	              char &final) const;
	// OSC, DCS, APC, PM and SOS all share one framing: ESC <opener> ... ST,
	// where ST is ESC \ or, for OSC, a bare BEL. Returns the length consumed,
	// -1 while it is still arriving. Nothing in here is ever a key.
	int parse_string_sequence() const;
	bool dispatch_csi(const QByteArray &prefix, const QVector<int> &params,
	                 const QByteArray &inter, char final);
	void read_winch();                    // SIGWINCH arrived down the self-pipe

	ITerminalEventSink *sink_ = nullptr;
	QSocketNotifier *notifier_ = nullptr;
	QSocketNotifier *winch_notifier_ = nullptr;
	QByteArray pending_;
	QByteArray paste_;                   // accumulating between CSI 200~/201~
	bool in_paste_ = false;
	QSize cells_;
	Capabilities::GraphicsMode mode_;
	TermCaps caps_;                               // what the terminal answered
	ScrollSettle settle_;                         // section 5.7, sixel/iTerm2 only
	QElapsedTimer clock_;
	void query_geometry();                        // re-ask after a resize
	bool sync_frames() const;                     // DEC 2026, confirmed only
	Capabilities::ColorDepth depth_;             // negotiated (section 6)
	QSet<quint64> uploaded_;                     // kitty upload-once cache
	// Least-recently-referenced first. The cache above is upload-ONCE and
	// was also upload-forever: see retire_uploads().
	QList<quint64> upload_order_;
	void retire_uploads(const CellBuffer &frame, QByteArray &out);
	// The pixel frame's size when the kitty path last ran. A resize changes
	// the tile grid, so the placements from the old one would linger at
	// positions that no longer mean anything -- the only case where the
	// whole screen has to be delete_all'd rather than replaced tile by tile.
	QSize last_pixel_size_;
	// Everything qtty puts on the wire goes through here, and the result is
	// READ. Named rather than left as eight fwrite/fflush pairs because the
	// checking is the point: the comment beside signal(SIGPIPE, SIG_IGN)
	// promised every write already checked, and not one of them did.
	void write_out(const QByteArray &bytes);
	// The far end has gone. Says what read_input() says for EOF on the way
	// in, and says it once.
	void terminal_gone();
	bool gone_ = false;                  // the sink has been told

	bool raw_ok_ = false;
	bool tty_out_ = false;               // stdout is a terminal
	bool active_ = false;
	termios saved_{};
};

} // namespace Qtty
