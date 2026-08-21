// qtty/backend.h — L1 terminal backend seam (§5.1) and the optional graphics
// extension (§5.7). This is the interface the four legacy TUI implementations
// adapt to in Phase 1; the in-tree AnsiRuntime will be rehosted behind it in
// Phase 2 (it currently drives the tty directly — see src/backends/ansi).
#pragma once
#include <QSize>
#include <QPoint>
#include <QRegion>
#include <QImage>
#include <QString>
#include <optional>
#include "cell.h"

namespace qtty {

struct Capabilities {
    enum ColorDepth { Mono, Ansi16, Xterm256, TrueColor };
    ColorDepth color        = Ansi16;
    bool mouse              = false;   // SGR 1006 or better
    bool bracketedPaste     = false;
    bool synchronisedOutput = false;   // DEC 2026 — tear-free frames
    bool unicodeWide        = true;    // honours wcwidth-2 correctly
    bool title              = false;

    // Pixel-graphics support, decided per terminal type (§5.7).
    enum GraphicsMode { NoGraphics, Halfblocks, Sixel, ITerm2, Kitty, KittyAlpha };
    GraphicsMode graphics   = NoGraphics;
};

enum class CursorShape { Block, Underline, Bar, Hidden };

struct KeyEvent   { int qtKey = 0; QString text; bool ctrl = false, alt = false, shift = false; };
struct MouseEvent { QPoint cell; int button = 0; bool press = false, release = false, motion = false; int wheel = 0; };

class ITerminalEventSink {
public:
    virtual ~ITerminalEventSink() = default;
    virtual void onKey(const KeyEvent &) = 0;
    virtual void onMouse(const MouseEvent &) = 0;
    virtual void onPaste(const QString &) = 0;
    virtual void onResize(QSize cells) = 0;
    virtual void onFocusChange(bool focused) = 0;
};

class ITerminalBackend {
public:
    virtual ~ITerminalBackend() = default;

    virtual Capabilities capabilities() const = 0;
    virtual QSize size() const = 0;                 // in cells

    // Present a full frame. `damage` is advisory: backends may repaint
    // everything, but must never render outside it incorrectly.
    virtual void present(const CellBuffer &frame, const QRegion &damage) = 0;

    virtual void setCursor(std::optional<QPoint> cell, CursorShape shape) = 0;

    // Backends push input; they never poll. Event-loop integration is the
    // backend's business (QSocketNotifier, thread, ...).
    virtual void setEventSink(ITerminalEventSink *) = 0;

    virtual void suspend() = 0;                     // SIGTSTP / shelling out
    virtual void resume() = 0;
};

// Optional extension — only for backends whose terminal accepts pixel data.
// Legacy backends need not implement it: the Halfblocks fallback is a pure L2
// transform reaching them through present() with zero changes (§5.7).
class IGraphicsOutput {
public:
    virtual ~IGraphicsOutput() = default;
    // One full-terminal RGBA frame, already composited by GraphicsPlane.
    virtual void presentPixels(const QImage &frame, const QRegion &cellRegion) = 0;
    // KittyAlpha only: alpha image over live text, terminal-blended.
    virtual void presentOverlay(int id, const QImage &rgba, QPoint cell, int z) = 0;
    virtual void clearOverlay(int id) = 0;
};

} // namespace qtty
