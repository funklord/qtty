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
#include <QSet>
#include <QVector>

class QSocketNotifier;

namespace Qtty {

class AnsiBackend : public QObject, public ITerminalBackend,
                    public IGraphicsOutput {
public:
	AnsiBackend();
	~AnsiBackend() override;

	Capabilities capabilities() const override;
	QSize size() const override;
	void present(const CellBuffer &frame, const QRegion &damage) override;
	void setCursor(std::optional<QPoint> cell, CursorShape shape) override;
	void setEventSink(ITerminalEventSink *s) override { sink_ = s; }
	void suspend() override;
	void resume() override;

	// IGraphicsOutput (section 5.7): pixel tiers for capable terminals.
	void presentPixels(const QImage &frame, const QRegion &cellRegion) override;
	void presentOverlay(int id, const QImage &rgba, QPoint cell, int z) override;
	void clearOverlay(int id) override;

private:
	void readInput();
	bool decodeOne();                    // one event from pending_ -> sink
	// A complete CSI at the head of pending_, or -1 if more bytes are needed.
	// Fills the private prefix, the numeric parameters and the final byte.
	int parseCsi(QByteArray &prefix, QVector<int> &params, char &final) const;
	bool dispatchCsi(const QByteArray &prefix, const QVector<int> &params,
	                 char final);
	void readWinch();                    // SIGWINCH arrived down the self-pipe

	ITerminalEventSink *sink_ = nullptr;
	QSocketNotifier *notifier_ = nullptr;
	QSocketNotifier *winch_notifier_ = nullptr;
	QByteArray pending_;
	QByteArray paste_;                   // accumulating between CSI 200~/201~
	bool in_paste_ = false;
	QSize cells_;
	Capabilities::GraphicsMode mode_;
	Capabilities::ColorDepth depth_;             // negotiated (section 6)
	QSet<quint64> uploaded_;                     // kitty upload-once cache
	bool rawOk_ = false;
	bool tty_out_ = false;               // stdout is a terminal
	bool active_ = false;
	termios saved_{};
};

} // namespace Qtty
