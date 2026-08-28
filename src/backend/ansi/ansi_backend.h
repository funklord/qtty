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

class QSocketNotifier;

namespace Qtty {

// Exposed so the rules can be tested against a TermCaps built by hand. Doing
// it against a live terminal would mean owning one that answers each way.
Capabilities::GraphicsMode negotiate_graphics(const TermCaps &caps);
Capabilities::ColorDepth negotiate_color(const TermCaps &caps);

class AnsiBackend : public QObject, public ITerminalBackend,
                    public IGraphicsOutput {
public:
	AnsiBackend();
	~AnsiBackend() override;

	Capabilities capabilities() const override;
	QSize size() const override;
	void present(const CellBuffer &frame, const QRegion &damage) override;
	void set_cursor(std::optional<QPoint> cell, CursorShape shape) override;
	void set_event_sink(ITerminalEventSink *s) override { sink_ = s; }
	void suspend() override;
	void resume() override;

	// IGraphicsOutput (section 5.7): pixel tiers for capable terminals.
	void present_pixels(const QImage &frame, const QRegion &cell_region) override;
	void present_overlay(int id, const QImage &rgba, QPoint cell, int z) override;
	void clear_overlay(int id) override;

private:
	void read_input();
	bool decode_one();                    // one event from pending_ -> sink
	// A complete CSI at the head of pending_, or -1 if more bytes are needed.
	// Fills the private prefix, the numeric parameters and the final byte.
	int parse_csi(QByteArray &prefix, QVector<int> &params, char &final) const;
	// OSC, DCS, APC, PM and SOS all share one framing: ESC <opener> ... ST,
	// where ST is ESC \ or, for OSC, a bare BEL. Returns the length consumed,
	// -1 while it is still arriving. Nothing in here is ever a key.
	int parse_string_sequence() const;
	bool dispatch_csi(const QByteArray &prefix, const QVector<int> &params,
	                 char final);
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
	Capabilities::ColorDepth depth_;             // negotiated (section 6)
	QSet<quint64> uploaded_;                     // kitty upload-once cache
	bool raw_ok_ = false;
	bool tty_out_ = false;               // stdout is a terminal
	bool active_ = false;
	termios saved_{};
};

} // namespace Qtty
