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
	bool decodeOne();                    // one key from pending_ -> sink

	ITerminalEventSink *sink_ = nullptr;
	QSocketNotifier *notifier_ = nullptr;
	QByteArray pending_;
	QSize cells_;
	Capabilities::GraphicsMode mode_;
	QSet<quint64> uploaded_;                     // kitty upload-once cache
	bool rawOk_ = false;
	bool active_ = false;
	termios saved_{};
};

} // namespace Qtty
