# Qtty — Terminal rendering for Qt Widgets applications

**Design document · Draft 2 · 2026-08-20**

*"Qtty" is a placeholder name.*

> **Phase 0 has been run.** Both gates passed on Qt 6.4.2 / offscreen; see §16 for the
> executable spike, its output, and the five corrections it forced. Sections below are
> updated to match measured behaviour — where text says "measured", it was.

---

## 1. Summary

Qtty renders an unmodified Qt Widgets application to a character-cell terminal. The
same `QWidget` tree that produces the desktop GUI produces the TUI; there is one view
codebase, not two dialects of one.

It achieves this without forking Qt and without a custom QPA platform plugin. Instead
it uses four public extension points:

| Concern | Mechanism | API stability |
|---|---|---|
| Geometry on a cell grid | `QStyle` subclass returning cell-multiple metrics | Public |
| Widget lifecycle without a window | `Qt::WA_DontShowOnScreen` + in-box `offscreen` platform plugin | Public API, *undocumented behaviour* — see §5.6 |
| Rendering to cells | Custom `QPaintDevice` / `QPaintEngine` | Public |
| Terminal I/O | `ITerminalBackend`, satisfied by our four existing implementations | Ours |

The secondary goal is consolidation: four separate TUI implementations across four
products collapse into one library, with the existing terminal code surviving as
backend adapters rather than being thrown away.

---

## 2. Goals and non-goals

### Goals

- **G1.** A `QWidget`-based screen compiles once and runs as both GUI and TUI.
- **G2.** The GUI remains the primary target. No GUI regression is acceptable as the
  price of terminal support; Qtty must be inert when not active.
- **G3.** The four existing terminal implementations are consolidated behind one
  backend interface, retiring three of them over time.
- **G4.** Deterministic, snapshot-testable rendering in CI with no tty attached.
- **G5.** No dependency on Qt private headers or a patched Qt. (One documented exception:
  `WA_DontShowOnScreen`'s lifecycle behaviour is public API but undocumented behaviour —
  §5.6.)

### Non-goals

- **N1.** Qt Quick / QML. The cell model has no meaningful mapping to a scene graph.
- **N2.** Pixel fidelity. The TUI is a legible reinterpretation, not a screenshot.
- **N3.** Automatic beauty. Screens designed for 1920×1080 will need adaptation work
  (§9); Qtty makes that work small, not zero.
- **N4.** Supporting arbitrary third-party widgets. Coverage is defined by an explicit
  widget support matrix (§8.4).

---

## 3. Context

Four products, each with a Qt Widgets GUI and an independently written TUI. The four
TUIs duplicate terminal handling (input decoding, capability detection, diffing,
Unicode width) and duplicate application logic in a second, divergent view layer.
Bugs are fixed up to four times, and TUI screens drift from their GUI counterparts.

The terminal-level work is done — four times. What is missing is the layer above it:
widget hierarchy, layout, focus, event propagation, and the discipline that keeps a
GUI screen renderable in 80×24. That layer is precisely what Qt already implements, so
the design reuses it rather than rewriting it a fifth time.

**Alternative considered and rejected as the primary path:** adopting an existing
Qt-shaped TUI toolkit (Tui Widgets, terminalgui). These are competent and QtCore-based,
but their widget classes are parallel to Qt's, not identical, which caps sharing at the
model/controller layer and leaves us maintaining two view layers per product forever.
Retained as the documented fallback (§13.2).

---

## 4. Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Application screens  (shared QWidget code, one implementation) │
└─────────────────────────────────────────────────────────────────┘
                │                                    │
        GUI build │                                  │ TUI build
                ▼                                    ▼
     ┌────────────────────┐          ┌──────────────────────────────┐
     │  QApplication      │          │  qtty::Application           │  L6 Runtime
     │  xcb / wayland /   │          │  QApplication(-platform      │
     │  windows / cocoa   │          │      offscreen)              │
     └────────────────────┘          │  Compositor · InputRouter    │
                                     │  FrameScheduler              │
                                     └──────────────────────────────┘
                                       │            │            │
                          ┌────────────┘            │            └────────────┐
                          ▼                         ▼                         ▼
              ┌───────────────────┐   ┌─────────────────────┐   ┌──────────────────┐
              │ GridStyle         │   │ CellPaintDevice     │   │ InputRouter      │  L4/L3
              │ (QProxyStyle)     │──▶│ CellPaintEngine     │   │ key/mouse → Qt   │
              │ metrics in cells  │   │ QPainter ops → cells│   │ events           │
              │ + semantic channel│   └─────────────────────┘   └──────────────────┘
              └───────────────────┘             │                        ▲
                                                ▼                        │
                                     ┌──────────────────────────────────────────┐
                                     │  CellBuffer · Cell · Attr · damage/diff   │  L2
                                     └──────────────────────────────────────────┘
                                                │                        ▲
                                                ▼                        │
                                     ┌──────────────────────────────────────────┐
                                     │  ITerminalBackend                         │  L1
                                     │  ├ app1Backend  (existing code, adapted)  │
                                     │  ├ app2Backend  …                         │
                                     │  ├ TermpaintBackend (reference)           │
                                     │  └ NullBackend  (CI / snapshot tests)     │
                                     └──────────────────────────────────────────┘
```

Layers are strictly stacked: L1 knows nothing of Qt, L2 knows nothing of widgets,
L3/L4 know nothing of the tty. Only L6 sees everything.

---

## 5. Layer specifications

### 5.1 L1 — Terminal backend

The seam that lets four existing implementations survive. A backend owns the tty and
nothing else: no diffing, no layout, no widget knowledge. Diffing deliberately lives in
L2 so all four backends inherit one optimizer.

```cpp
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
    GraphicsMode graphics   = NoGraphics;   // best mode the terminal supports
};

class ITerminalBackend {
public:
    virtual ~ITerminalBackend() = default;

    virtual Capabilities capabilities() const = 0;
    virtual QSize size() const = 0;                 // in cells

    // Present a full frame. `damage` is advisory: backends may ignore it and
    // repaint everything, but must never render outside it incorrectly.
    virtual void present(const CellBuffer &frame, const QRegion &damage) = 0;

    virtual void setCursor(std::optional<QPoint> cell, CursorShape shape) = 0;

    // Backends push input; they never poll. Integration with the host event
    // loop is the backend's business (QSocketNotifier, epoll thread, etc.).
    virtual void setEventSink(ITerminalEventSink *) = 0;

    virtual void suspend() = 0;   // SIGTSTP / shelling out
    virtual void resume()  = 0;
};

// Optional extension: implemented only by backends whose terminal can accept
// pixel data. The four legacy backends need not implement it — the Halfblocks
// fallback is a pure L2 CellBuffer transform and reaches them through the
// ordinary present() path with zero changes (§5.7).
class IGraphicsOutput {
public:
    virtual ~IGraphicsOutput() = default;
    // Ship one full-terminal RGBA frame (already composited by GraphicsPlane).
    // `cellRegion` is the damage in cells, for protocols that update partially.
    virtual void presentPixels(const QImage &frame, const QRegion &cellRegion) = 0;
    // KittyAlpha only: place an alpha image *over* live text, letting the
    // terminal blend. GraphicsPlane uses this instead of presentPixels() when
    // Capabilities::graphics == KittyAlpha.
    virtual void presentOverlay(int id, const QImage &rgba, QPoint cell, int z) = 0;
    virtual void clearOverlay(int id) = 0;
};

class ITerminalEventSink {
public:
    virtual void onKey(const KeyEvent &) = 0;       // decoded, not raw escapes
    virtual void onMouse(const MouseEvent &) = 0;
    virtual void onPaste(const QString &) = 0;
    virtual void onResize(QSize cells) = 0;
    virtual void onFocusChange(bool focused) = 0;
};

} // namespace qtty
```

**Decoding stays in the backend.** Escape-sequence parsing is where the four
implementations differ most and where their accumulated bug fixes live; forcing a
common parser on day one would discard that value. Convergence is a later, optional
step (§12, Phase 5).

**Reference backend.** `TermpaintBackend` wraps termpaint, whose integration layer
accepts caller-supplied I/O callbacks. It is the yardstick the four native backends are
measured against, and the default for new products.

### 5.2 L2 — Cell model

```cpp
struct Cell {
    QString cluster;    // one grapheme cluster, not one QChar
    Color   fg, bg;
    Attrs   attrs;      // Bold|Dim|Italic|Underline|Blink|Reverse|Strike
    quint8  width;      // 1 or 2 (East Asian wide, emoji)
};

class CellBuffer {
public:
    CellBuffer(QSize cells);
    Cell &at(int col, int row);
    void  fill(const QRect &cells, const Cell &);
    void  drawText(QPoint cell, QStringView, const Attrs &, Color fg, Color bg);
    QRegion diff(const CellBuffer &previous) const;   // damage computation
};
```

**Grapheme clusters, not code points.** A cell holds a `QString` because a user-visible
character may be a base plus combining marks, or a ZWJ emoji sequence. `QTextBoundaryFinder`
with `BoundaryReason::Grapheme` computes cluster boundaries; width comes from a table
the backend can override via `Capabilities::unicodeWide`, because terminals disagree and
the truth is empirical.

Wide (width-2) clusters occupy their cell plus a continuation cell marked with an empty
cluster. Writing into a continuation cell must clear its partner — a classic corruption
source and a required unit test.

Note `Grapheme` is a `QTextBoundaryFinder::BoundaryType`, not a `BoundaryReason`:
`QTextBoundaryFinder(QTextBoundaryFinder::Grapheme, str)`.

### 5.3 L3 — Grid metrics and style

**One cell = `CW × CH` device pixels.** Default 8×16. Everything in the Qt half of the
system is expressed in pixels that are exact multiples of these constants, so
`geometry().x() / CW` *is* the column index. No rounding at read time; rounding happens
once, at the style, on the way in.

```cpp
struct GridMetrics {
    static constexpr int CW = 8;
    static constexpr int CH = 16;
    static QSize cells(int c, int r) { return {c * CW, r * CH}; }
    static QRect toCells(QRect px);        // px → cell coords, asserts alignment
    static QSize snapUp(QSize px);         // round up to whole cells
};
```

`GridStyle : QProxyStyle` is where gridding is implemented. `QStyle` supplies not only
widget metrics but layout margins and spacing (`PM_LayoutLeftMargin`,
`PM_LayoutHorizontalSpacing`, and friends are documented as the defaults a `QLayout`
uses), so a single class grids the layout engine as well as the widgets.

```cpp
class GridStyle : public QProxyStyle {
    int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override {
        switch (m) {
        case PM_LayoutLeftMargin:  case PM_LayoutRightMargin:   return CW;
        case PM_LayoutTopMargin:   case PM_LayoutBottomMargin:  return CH;
        case PM_LayoutHorizontalSpacing:                        return CW;
        case PM_LayoutVerticalSpacing:                          return 0;
        case PM_ScrollBarExtent:   case PM_DefaultFrameWidth:   return CW;
        case PM_ButtonMargin:                                   return CW;
        case PM_FocusFrameHMargin: case PM_FocusFrameVMargin:   return 0;
        default: return snapMetric(m, QProxyStyle::pixelMetric(m, o, w));
        }
    }
    QSize sizeFromContents(ContentsType, const QStyleOption *, const QSize &,
                           const QWidget *) const override;   // snapUp
    QRect subElementRect(SubElement, const QStyleOption *, const QWidget *) const override;
    QRect subControlRect(ComplexControl, const QStyleOptionComplex *, SubControl,
                         const QWidget *) const override;
};
```

**Why the style and not widget subclasses.** Qt constructs widgets internally that the
application never sees: `QComboBox` builds its own popup view and line edit,
`QAbstractItemView` builds editors through the delegate, `QTabWidget` builds a `QTabBar`,
`QScrollArea` builds scrollbars, `QDialogButtonBox` builds buttons. A `GridButton`
subclass is never instantiated on any of those paths. The style is consulted on all of
them. Inheritance is therefore reserved for two cases only: our own composite widgets,
and widgets whose painting must be replaced wholesale (§8.4).

**Per-widget extension without subclassing.** The style receives the `QWidget*`, so
attached state is read there:

```cpp
w->setProperty("qtty.cells", QSize(20, 1));   // works on any widget, ours or Qt's
```

For behaviour that genuinely needs a vtable, use a non-QObject mixin — Qt requires the
QObject-derived base to come first, and two QObject bases are illegal, so the interface
must not itself be a QObject. `Q_DECLARE_INTERFACE` restores `qobject_cast`:

```cpp
class ICellPainted {                                    // no QObject, no Q_OBJECT
public:
    virtual ~ICellPainted() = default;
    virtual void paintCells(CellBuffer &, const QRect &cells) const = 0;
};
Q_DECLARE_INTERFACE(ICellPainted, "org.qtty.ICellPainted/1.0")

class LogView : public QAbstractScrollArea, public ICellPainted {   // QObject first
    Q_OBJECT
    Q_INTERFACES(ICellPainted)
};

if (auto *c = qobject_cast<ICellPainted *>(w))          // works either way
    c->paintCells(buf, r);
```

**Font provisioning.** Layout depends on `QFontMetrics`, so the font must have exactly
integral metrics: advance == `CW`, height == `CH`. We bundle a monospace font, load it
with `QFontDatabase::addApplicationFont`, and **assert** the resulting metrics at
startup rather than assuming them. If the assertion fails on some platform's font
backend, that is a hard startup error, not a rendering glitch to discover later.

**Scaling.** A fractional device pixel ratio silently destroys the alignment invariant,
so DPR must be exactly 1. The right levers are `QT_ENABLE_HIGHDPI_SCALING=0` (documented
as disabling high-DPI scaling, though *"no effect on platforms such as Wayland or macOS"*)
and `QGuiApplication::setHighDpiScaleFactorRoundingPolicy()`. **Not** `QT_SCALE_FACTOR=1`:
that variable is a *multiplier* on the native ratio — *"a product of the set scale factor
and the native device pixel ratio"* — so 1 is the identity and forces nothing. It is also
documented as debug-only.

In practice the `offscreen` plugin reports DPR 1 regardless, so the load-bearing
protection is the assertion, not the environment: assert `devicePixelRatio() == 1` on
every top-level at show time and treat a failure as a hard startup error.

**The alignment guard.** Highest value-per-line component in the project — it converts
a class of subtle rendering bugs into a stack trace at the point of origin:

```cpp
bool GridGuard::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::Resize || e->type() == QEvent::Move)
        if (auto *w = qobject_cast<QWidget *>(o))
            Q_ASSERT_X(GridMetrics::isAligned(w->geometry()), "qtty",
                       qPrintable(QString("%1 at %2")
                           .arg(w->metaObject()->className())
                           .arg(rectToString(w->geometry()))));
    return false;
}
```

Installed globally in debug builds, compiled out in release.

### 5.4 L4 — Rendering: the two-channel model

This is the central idea of the design and the part most worth getting right.

**Channel A — semantic (preferred).** `GridStyle` knows *what* it is drawing. When
`drawPrimitive(PE_FrameWindow, …)` is called, the style knows a window frame is wanted
and can emit box-drawing characters directly into the cell buffer — correct corners,
correct joins, correct title placement. It detects a cell target by inspecting the
painter's device:

Two details here are **measured, and both are counter-intuitive** (§16, F1/F2):

- **Detect the cell target via the paint *engine*, not the paint device.** Inside a
  `paintEvent` the painter's device is the `QWidget` itself; the redirection to our
  device is invisible at that level. `dynamic_cast<CellPaintEngine*>(p->paintEngine())`
  is the reliable test. Getting this wrong is silent: Channel A simply never fires and
  everything quietly falls through to Channel B.
- **Get position from the widget, not the painter.** Neither `QPainter::transform()`
  nor `combinedTransform()` carries the offset `QWidget::render()` applies, so
  style-level coordinates are widget-local with no way back to screen space through the
  painter. Use `w->mapTo(w->window(), opt->rect.topLeft())`.

```cpp
static CellPaintDevice *cellTarget(QPainter *p) {
    if (auto *e = dynamic_cast<CellPaintEngine *>(p->paintEngine())) return e->device();
    return nullptr;                                   // GUI path
}

void GridStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt,
                              QPainter *p, const QWidget *w) const {
    if (auto *dev = cellTarget(p)) {
        const QRect c = toCells(w->mapTo(w->window(), opt->rect.topLeft()), opt->rect.size());
        switch (pe) {
        case PE_FrameWindow:       dev->buffer().drawBox(c, boxStyle(opt)); return;
        case PE_IndicatorCheckBox: dev->buffer().text(c.topLeft(),
                                       (opt->state & State_On) ? "[x]" : "[ ]"); return;
        ...
        }
    }
    QProxyStyle::drawPrimitive(pe, opt, p, w);       // GUI path, untouched
}
```

**`State_HasFocus` is never set** in the TUI build (§16, F4): widgets cannot report focus
while no window is active. Channel A must take focus from `InputRouter`, not from the
style option — the one place shared style code legitimately needs a target-specific
branch.

**Channel B — generic (fallback).** Anything that reaches `QPainter` without passing
through a style hook — custom `paintEvent()` implementations, third-party widgets,
`QTextDocument` output — lands in `CellPaintEngine`. Qt documents this path explicitly:
support for a new backend is added by deriving from `QPaintDevice`, reimplementing
`paintEngine()`, and providing a `QPaintEngine` subclass.

```cpp
class CellPaintDevice : public QPaintDevice {
public:
    QPaintEngine *paintEngine() const override;
    // metric() is virtual but NOT pure — omitting it compiles and then silently
    // reports wrong width/height/DPI/DPR, breaking the §5.3 alignment invariant.
    int metric(PaintDeviceMetric) const override;
    CellBuffer &buffer();
};

class CellPaintEngine : public QPaintEngine {
public:
    // Declare features deliberately. Anything NOT declared, QPainter emulates —
    // which can convert text to paths and deliver it to drawPath() instead of
    // drawTextItem(), gutting text handling. Verify in the Phase 0 render gate.
    CellPaintEngine() : QPaintEngine(PaintEngineFeatures{}) {}

    // Pure virtuals: begin, end, drawPixmap, type, updateState.
    bool begin(QPaintDevice *) override;
    bool end() override;
    Type type() const override { return QPaintEngine::User; }
    void updateState(const QPaintEngineState &) override;    // pen/brush/font/clip → Attrs
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override; // → §8.6

    void drawTextItem(const QPointF &, const QTextItem &) override;   // → cells
    void drawRects(const QRectF *, int) override;                     // → fills
    void drawRects(const QRect  *, int) override;                     // integer overload
    void drawLines(const QLineF *, int) override;                     // → box chars
    void drawLines(const QLine  *, int) override;                     // integer overload
    void drawPath(const QPainterPath &) override;                     // → bbox fill
};
```

The integer overloads matter: overriding only the `QRectF`/`QLineF` forms leaves
integer-coordinate calls falling through to the base implementations.

Channel B is a safety net that guarantees *something* legible appears for any widget.
Channel A is where quality comes from. The support matrix (§8.4) is really a statement
about which widgets have Channel A coverage.

**Frame production.** We drive rendering explicitly rather than relying on backingstore
internals:

1. Damage accumulates from `QEvent::UpdateRequest` / `QWidget::update()` interception.
2. `FrameScheduler` coalesces to at most one frame per ~16 ms (configurable; terminals
   over ssh want less).
3. `Compositor` walks `QApplication::topLevelWidgets()` in z-order, plus
   `activeModalWidget()` and `activePopupWidget()`, calling
   `QWidget::render(QPainter*, offset, region, flags)` — public API — into one
   `CellPaintDevice`.
4. `CellBuffer::diff()` against the previous frame yields damage; `present()` ships it.

### 5.5 L5 — Input

```
backend escape decoding → KeyEvent/MouseEvent
    → InputRouter
        → shortcut table (ours) ─ match? → QAction::trigger()
        → grab widget? else popup? else modal? else window->focusWidget()
            → QKeyEvent / QMouseEvent / QWheelEvent via QApplication::sendEvent
```

**Focus: use `window->focusWidget()`, never `qApp->focusWidget()`.** Measured (§16, F4):
under offscreen + `WA_DontShowOnScreen` no window ever becomes active, so
`QApplication::focusWidget()` is permanently null and `QWidget::hasFocus()` permanently
false. But `QWidget::focusWidget()` on the top-level returns the right widget, and
`focusNextPrevChild()` walks the focus chain correctly — so **Tab order, focus policies
and the focus chain all still work**, and delivering events directly to that widget
works (a synthetic keystroke reaches a `QLineEdit` and edits its text). Qtty keeps Qt's
focus *model*; it only replaces Qt's notion of which window is active.

Terminals lose information the Qt event model expects: no key-release events, no
reliable modifier state on their own, no autorepeat distinction. `InputRouter` fabricates
a release immediately after each press and documents this; widgets that latch on release
(none in our matrix, but plausible in third-party code) will misbehave and that is a
known limitation.

**Shortcuts: `InputRouter` owns them.** OQ-1 is **closed, negative** (§16, F3). A
synthetic `QKeyEvent` does *not* reach Qt's shortcut map — measured across all three
shortcut contexts and both delivery targets (focus widget and `QWindow`), zero
activations. Because no window is active, `QShortcutMap` never matches. `InputRouter`
therefore maintains its own table, built by walking `QAction`s from the active window
and its children, and matches before dispatching the key onward. Manual
`QAction::trigger()` on a match works correctly.

This is a small amount of code and it has a silver lining: shortcut precedence becomes
explicit and testable rather than depending on Qt's context rules.

**Cursor placement.** Elegant trick worth adopting: query the focus widget generically
rather than special-casing input classes.

```cpp
QVariant v = focus->inputMethodQuery(Qt::ImCursorRectangle);   // widget coordinates
backend->setCursor(toCells(focus->mapToGlobal(v.toRect().topLeft())), shape);
```

(`mapToGlobal` takes a `QPoint`/`QPointF`, not a `QRect`. And note `ImCursorRectangle`
is in *widget* coordinates, unlike `QInputMethod::cursorRectangle`, which is in window
coordinates — easy to conflate.)

Any widget that supports input methods — `QLineEdit`, `QTextEdit`, custom editors —
reports its caret this way. A correctly positioned hardware cursor is the single largest
perceived-quality difference between a good TUI and a bad one, and matters for screen
readers.

### 5.6 L6 — Runtime

```cpp
namespace qtty {
class Application {
public:
    // Must be constructed before QApplication: sets QT_QPA_PLATFORM=offscreen,
    // QT_SCALE_FACTOR=1, and the bundled font dir.
    static void prepareEnvironment();

    Application(QApplication &, std::unique_ptr<ITerminalBackend>);
    void setTheme(const CellTheme &);
    int  exec();
};
}
```

`main()` becomes:

```cpp
int main(int argc, char **argv) {
    const bool tui = wantsTui(argc, argv);
    if (tui) qtty::Application::prepareEnvironment();
    QApplication app(argc, argv);

    MainScreen screen;                          // ← identical in both builds
    if (tui) {
        qtty::Application tty(app, makeBackend());
        screen.setAttribute(Qt::WA_DontShowOnScreen);
        screen.show();
        return tty.exec();
    }
    screen.show();
    return app.exec();
}
```

**On `Qt::WA_DontShowOnScreen` — the one place this design leans on undocumented
behaviour.** Its entire official documentation is *"Indicates that the widget is hidden
or is not a part of the viewable Desktop."* Everything else we rely on — polish, layout
activation, paint events, focus — is observed behaviour, confirmed in qtbase source
(`QWidgetPrivate::show_sys()` early-returns for this attribute after invalidating the
backingstore and setting `WA_Mapped`) and load-bearing for Qt's own offscreen testing.
It is stable in practice and has been for a decade, but it is not a contract, so it is
called out here rather than filed under G5.

Two corrections to the naive mental model:

- **A `QWindow` is still created**; what is suppressed is *mapping* it. "No native
  surface" is loose — say "never mapped".
- **A backingstore is still created**, and is in fact the mechanism by which paint
  events keep flowing. This closes OQ-2: at 200×60 cells × 8×16 px that is a
  1600×960 ARGB buffer, ≈6 MB per top-level, plus one per popup and dialog. §11's
  budget must account for widgets being painted twice per frame — once into the
  backingstore Qt insists on, once into our `CellPaintDevice` via `render()`. If that
  cost bites, the alternative is to harvest the backingstore image instead of calling
  `render()`, at the price of losing Channel A.

### 5.7 L4½ — GraphicsPlane: pixel overlays over the cell UI

*Requirement:* full-terminal graphics shown **over** the widget UI, with alpha, in
whatever mode the terminal supports — and the architecture must be prepared for it now
even where implementation comes later.

The design lands on friendly ground here, for one structural reason: **Qtty's cell
space is already pixel-addressed.** One cell ≡ CW×CH device pixels, the bundled font
is a real raster font Qt can draw with, and the compositor already produces a complete
`CellBuffer` each frame. So "UI + pixel overlay" is a compositing problem we can solve
entirely on our side of the backend seam whenever the terminal itself cannot.

**App-facing API** — one object, target-independent:

```cpp
namespace qtty {
class Overlay : public QObject {
public:
    void setImage(const QImage &rgba);        // straight or premultiplied alpha
    void setRect(const QRectF &cellRect);     // in cells; default = whole terminal
    void setOpacity(qreal);                   // multiplied into the image's alpha
    void setZ(int);                           // stacking among overlays
    void show(); void hide();
};
}
```

In the GUI build the same class renders through an ordinary top-level
`Qt::WA_TranslucentBackground` widget, so shared code manipulates one object in both
targets. Widgets that *contain* pixel content (plots, meters, video stills) get
`qtty::PixelSurface : QWidget` — paint into it with QPainter as usual; in the TUI build
the compositor harvests its `QImage` and hands it to the GraphicsPlane with the
widget's cell geometry instead of letting Channel B mangle it through `drawPixmap`.

**Three delivery strategies, selected by `Capabilities::graphics`:**

| Mode | Who blends | Mechanism |
|---|---|---|
| `KittyAlpha` | the terminal | `presentOverlay()`: ship the RGBA image with a z above the text plane; text stays live text — selection, copy, and screen readers keep working under translucent regions. The only true native path. |
| `Sixel` / `ITerm2` / `Kitty` (no alpha-over-text) | **GraphicsPlane** | Software composite: rasterize the `CellBuffer` with QPainter using the bundled font (measured: **3.8 ms** for 60×16 → 600×304 px), `SourceOver`-blend all overlays (**0.2 ms**), hand the finished frame to `presentPixels()` for encoding. The terminal shows one image; correctness is trivial because we control every pixel. |
| `NoGraphics` / `Halfblocks` | **GraphicsPlane, in cells** | Pure L2 transform on the composed `CellBuffer`: opaque overlay pixels become blocks, translucent ones shade empty cells (`░▒▓`) while **UI text under them survives**, transparent ones leave the UI untouched. Reaches all four legacy backends through the ordinary `present()` with zero backend changes. With the colour model of §6, this upgrades to half-block (`▀`) rendering at 2× vertical resolution with fg/bg per cell. |

*Spike-validated (§16.2):* both non-native paths run today in `spike/spike3.cpp` — the
half-block fallback output and the composited PNG are reproduced there, and the PNG is
byte-identical to what a sixel encoder would ship.

**Cell-anchored placements — pixel images that scroll with text.**

Full-terminal overlays are not the only mode. Inline content (chat stickers, avatars,
thumbnails) wants images *anchored to cells inside a scrolling view*, moving with the
text. Modern terminals make this genuinely possible — real pixels, not mosaic — and the
mechanism slots into L2 cleanly:

```cpp
struct CellImage {          // lives alongside cells in the frame, diffed like them
    quint64 imageKey;       // content hash → upload-once per backend
    QRect   cellRect;       // anchor + span, in cells
    int     z = 0;
};
```

Three delivery tiers, again by capability:

- **Kitty protocol** (kitty, Ghostty, WezTerm, recent Konsole): the strong path, two
  ways. Regular placements scroll with text by spec ("graphics should also scroll with
  the text, automatically"), and pixel data is transmitted **once** per image ID —
  re-placing a sticker after a scroll is a few bytes, so a chat view scrolls stickers
  at full frame rate over ssh. Stronger still are *Unicode placeholders*: the image is
  represented by ordinary text cells (U+10EEEE, image ID in the foreground colour,
  tile row/column in combining diacritics), which makes a sticker literally part of
  the text flow — anything that moves text moves it, including tmux. That maps 1:1
  onto `CellBuffer`: a placement is a run of placeholder cells, and our existing
  diff/damage machinery handles scrolling with no special cases at all.
- **Sixel / iTerm2**: images paint into the text flow at the cursor and scroll with it,
  but there are no handles — moving means re-emitting. In an alt-screen app "scroll"
  is our repaint anyway, so the plane re-emits placements at their new cells; encoded
  bytes are cached per `imageKey`, so the cost is transmission only. Policy for slow
  links: during fast scrolling, placements degrade to their mosaic fallback and the
  real pixels are drawn when scrolling settles (~100 ms debounce).
- **Fallback (`NoGraphics`/`Halfblocks`)**: the sticker becomes a half-block mosaic —
  `▀` with fg/bg per cell gives 2 pixels per cell, so a 8×4-cell sticker is a 16×8
  colour thumbnail. Pixelated, but it scrolls perfectly because it *is* cells. And
  emoji need none of this tier-picking: they are Unicode text, rendered by the
  terminal's own emoji font in a width-2 cell — the `Cell` grapheme-cluster model
  (§5.2) already carries them, in every terminal, scrolling natively.

The API insight: **the GUI code path is already the TUI API.** A chat delegate or
`QLabel` draws its sticker with `QPainter::drawPixmap()`; in the TUI build that call
arrives at `CellPaintEngine::drawPixmap()`, which — instead of the ▒ placeholder —
registers a `CellImage` at the pixmap's cell rect. Scrolling the view moves the widget,
the next frame renders placements at their new cells, and each tier does its thing.
No `qtty::` types appear in shared chat code at all; `Overlay`/`PixelSurface` remain
for the cases (full-screen graphics, owner-drawn surfaces) where explicitness is wanted.

**Document and item-view integration (chat-style widgets).** Both vanilla idioms for
text-with-images funnel into the engine unchanged: a `QTextDocument` inline image
(`addResource()` / `QTextImageFormat`) and a delegate's `QPainter::drawPixmap()` both
terminate in `CellPaintEngine::drawPixmap()`, so chat code keeps vanilla Qt image APIs
end to end — resource loading (`QTextBrowser::loadResource`), caching, and models are
all upstream of paint and untouched; `QPixmap::cacheKey()` supplies `imageKey`.
The single discipline is the grid: images embedded in the *text flow* must have
cell-multiple sizes or subsequent lines leave the cell rows (the inverse of F8). Two
GUI-invisible accommodations cover it: size assets via `qtty::cells(w,h)` (just a
`QSize` on the desktop), or run documents through `qtty::alignTextDocument()`, which
rounds every `QTextImageFormat` up to cell multiples. Delegate-based views need only
scroll discipline (per-item, or per-pixel with a CH step), which `GridStyle` pushes as
the default. For shared chat implementations the delegate route is preferred: its
`sizeHint()`/`paint()` pair is exactly what Qtty already grids, while the document
route leans on `QTextLayout` line heights — supported, but it is where sub-cell
surprises live (§8.2).

**Design consequences booked now (the "prepared for" part):**

- `Capabilities` carries a `GraphicsMode`, not a bool; backends report the best mode
  their terminal negotiation finds. Legacy backends report `NoGraphics` and are done.
- `IGraphicsOutput` is a separate optional interface, so it never burdens a backend
  that doesn't implement it, and never blocks Phase 1 consolidation.
- The frame loop gains one hook: after cell composition, `GraphicsPlane::compose()`
  runs when any overlay is visible. Invisible-overlay cost is one branch.
- Damage discipline: an overlay's dirty rect is unioned into the frame damage in cell
  units, so partial updates stay possible on protocols that support them (kitty),
  while sixel-class terminals simply repaint the overlay's bounding region.
- `CellPaintEngine::drawPixmap()` is the funnel for inline images: placement
  registration on graphics terminals, mosaic on the rest, `imageKey` caching so pixel
  data crosses the wire once per image per session.
- What is *not* promised: per-pixel alpha over live text on sixel-class terminals —
  physically impossible there; the software composite is the honest maximum, at the
  cost that overlaid regions are pixels, not selectable text, while an overlay is up.

---

## 6. Colour and theming

`CellTheme` maps `QPalette` roles to terminal colours, with three quantisation targets
selected by `Capabilities::color`:

- **TrueColor** — pass `QColor` through as 24-bit SGR.
- **Xterm256** — nearest match in the 6×6×6 cube plus greyscale ramp, in CIELAB, not RGB.
- **Ansi16** — *not* nearest-match. Explicit hand-authored role→index mapping, because
  nearest-match on 16 colours produces unreadable pairings. This table is a design
  artifact, reviewed like code.

Contrast is enforced, not hoped for: after mapping, assert a minimum luminance delta
between `fg` and `bg` in every emitted cell, and log violations in debug builds. A GUI
palette that relies on subtle greys will produce invisible text otherwise.

---

## 7. Adaptation: one view, two very different canvases

The honest problem with G1: a five-tab, forty-field preferences dialog is a good GUI and
an unusable 80×24 TUI. Qtty addresses this in three tiers, and the design deliberately
makes tier 3 pleasant rather than pretending it is unnecessary.

**Tier 1 — free.** Style metrics differ between targets, so the same layout compacts
automatically. A dialog with generous desktop padding tightens to single-cell gutters
with no code change. This covers the majority of simple forms.

**Tier 2 — declarative hints.** Shared code annotates intent without branching on target:

```cpp
qtty::setPriority(advancedGroup, qtty::Priority::Optional);   // dropped if space-poor
qtty::setCompact(toolbar, qtty::Compact::IconsToLetters);
w->setProperty("qtty.cells", QSize(20, 1));
```

`GridStyle` and a `CompactionPass` consume these during layout activation. Hints are
no-ops in the GUI build, so shared code stays honest.

*Measured (§16.1, F6):* reflow itself is free — after every resize all children land
re-aligned, and stretches redistribute exactly as on the desktop. The real Tier-boundary
is the layout **minimum**: a resize below `minimumSizeHint()` is refused, and content
overflows the terminal instead of compacting. Small terminals therefore need an explicit
policy — drop `Priority::Optional` widgets, then scroll the root — rather than trust
that layouts will squeeze.

**Tier 3 — explicit variants.** When a screen genuinely cannot compact, split at the
view boundary only: shared model, shared controller, two `buildUi()` bodies selected at
build or run time. The library must make this cheap and visible rather than shameful.

**Enforcement.** Shared view code must never hardcode margins, spacing, or fixed pixel
sizes — those are the calls that make a GUI layout unportable. A CI check bans
`setContentsMargins`, `setSpacing`, `setFixedSize`, and `setFixedWidth` under
`src/ui/shared/`, with an explicit `// qtty-allow:` escape comment requiring a reason.

---

## 8. Hard problems

Ordered by expected pain. These are the reasons this design could fail; each has a
mitigation and a detection point.

### 8.1 Popups and top-levels — *highest risk*

`QMenu`, combo dropdowns, tooltips and dialogs are separate top-level windows. Under the
offscreen plugin they exist as platform windows we must discover, order, and composite
ourselves, and their activation/grab semantics assume a window manager that is not there.

*Mitigation:* `Compositor` treats `QApplication::activePopupWidget()` and
`activeModalWidget()` as an explicit z-ordered stack rather than trusting window flags;
popups get `WA_DontShowOnScreen` too and are positioned by us, clamped to the terminal
rectangle (a menu opening at x=78 must flip left, which the desktop code never had to do).

*Measured (§16.1, F7):* internally-created popups do **not** inherit
`WA_DontShowOnScreen` — `QComboBoxPrivateContainer` arrives with the attribute unset.
Harmless under the offscreen platform, but the runtime stamps it anyway via a global
`QEvent::Show`/`ChildAdded` filter so behaviour never depends on the platform being
windowless. Discovery is easy: `combo->view()->window()` and `activePopupWidget()` both
report it correctly, it composites with its items visible, and synthetic
Down/Down/Enter selects the right item and closes it.

*Detection:* this is spike gate 2 (§12, Phase 0). If popups do not composite sanely, the
whole approach is in question and we fall back (§13.2).

### 8.2 Text layout below the cell

`QTextDocument`, rich-text `QLabel`, eliding, and `QTextLayout` line-breaking all
position glyphs in sub-cell pixels. Channel B snaps them to cells, which produces
collisions and overlaps in dense text.

*Mitigation:* `QLabel` with plain text is fine (Channel A). Rich text is not supported.

*Revised after Phase 0.5 (§16.1, F8):* this problem is smaller than predicted. When the
document font's line height equals CH, `QTextLayout` positions every line on a cell row
and plain-text `QTextEdit` renders **perfectly** through Channel B with no assistance;
mixed-size rich text flattens to the base row but stays legible. Display is therefore
solved by construction (force the document font). What remains unverified is the
*editing* surface — cursor drawing, selection highlight, partial-line scrolling — so
`QTextEdit` moves from "replace wholesale" to "replace the interaction layer if editing
proves bad"; a read-only log/text view needs nothing at all.

### 8.3 Focus and activation without a window manager

`activateWindow()`, focus-out on popup dismissal, and modal blocking all depend on
platform behaviour the offscreen plugin implements minimally.

*Mitigation:* `InputRouter` owns focus policy explicitly and treats Qt's focus state as
the model, not the authority. Modal handling is enforced at the router: input outside
`activeModalWidget()` is dropped before dispatch.

### 8.4 Widget support matrix

Coverage is declared, not discovered. Each widget is classified:

| Class | Widgets | Meaning |
|---|---|---|
| **Native (Channel A)** | `QPushButton`, `QLabel` (plain), `QCheckBox`, `QRadioButton`, `QLineEdit`, `QComboBox`, `QGroupBox`, `QTabWidget`, `QScrollArea`, `QSplitter`, `QProgressBar`, `QSlider`, `QMenu`, `QMenuBar`, `QDialogButtonBox`, `QListView`, `QTreeView`, `QTableView` | Style-drawn, reviewed, snapshot-tested |
| **Generic (Channel B)** | Custom `paintEvent` widgets, third-party | Renders legibly, no guarantees |
| **Replaced** | `QTextEdit`, `QPlainTextEdit`, `QCalendarWidget` | Cell-native substitute with matching API subset |
| **Unsupported** | `QGraphicsView`, OpenGL widgets, `QWebEngineView` | Renders a labelled placeholder box |

Item views work because they paint through `QStyledItemDelegate`; a `CellItemDelegate`
gives Channel A coverage for the common roles and inherits everything else.

### 8.5 Scrollbars and inherently pixel-sized affordances

A 16-pixel scrollbar becomes one cell. `PM_ScrollBarExtent = CW` plus a Channel A
renderer drawing `▲ █ ░ ▼`. Sub-cell thumb positions round, so a 1000-row list has a
thumb that moves in visible jumps — acceptable, documented.

### 8.6 Icons and pixmaps

On graphics-capable terminals `drawPixmap` *is* honoured — it funnels into a
cell-anchored placement (§5.7), which is how chat stickers, avatars and thumbnails get
real pixels that scroll with their view. On `NoGraphics` terminals, and for tiny images
where pixels cannot read (a 16 px icon in a 1-cell space), a substitution registry maps
`QIcon::name()` (or an attached `qtty.glyph` property) to a glyph or short string;
unregistered small icons render as a single placeholder cell in debug and nothing in
release, while larger unregistered images fall back to half-block mosaic.

### 8.7 Runtime weight

The TUI build links QtWidgets, QtGui, and a font stack to draw text on a terminal.
For desktop-class products this is irrelevant (Qt is already a dependency). It would be
disqualifying for a small static ssh-only binary; that use case is a non-goal.

---

## 9. Testing

The test strategy is unusually important here, because it is also the mechanism by which
four divergent TUIs are safely merged.

**Snapshot tests are the backbone.** `NullBackend` plus the offscreen platform renders a
widget tree to a `CellBuffer` with zero terminal involvement; the buffer serialises to
UTF-8 text plus an attribute plane. Fixtures are human-readable and diff beautifully in
review:

```
┌─ Preferences ────────────────────────┐
│ [x] Enable telemetry                 │
│ ( ) Daily    (o) Weekly              │
│                     < OK > <Cancel>  │
└──────────────────────────────────────┘
```

**Characterisation first.** Before porting any product, capture snapshots of its
*existing* TUI screens. Those fixtures define "no regression" for the migration and are
the only defensible way to merge four implementations without silently losing behaviour
that someone depended on.

**Invariant tests.** `GridGuard` runs as an assertion in every test. Wide-cluster
continuation handling, damage-region correctness (render twice, diff must be empty), and
palette contrast are property-tested.

**Differential tests.** The same model driven through GUI and TUI builds must produce
the same observable state after the same event script — catching logic that accidentally
lives in the view.

**Tooling.** `qtty-inspect` dumps the widget tree with cell geometry alongside the
rendered buffer; `qtty-replay` feeds a recorded input script to a screen and emits
frames as text, making bug reports reproducible.

---

## 10. Repository layout

```
qtty/
  include/qtty/           public headers
  src/core/               CellBuffer, Cell, colour quantisation      (no Qt GUI dep)
  src/grid/               GridMetrics, GridStyle, GridGuard, fonts
  src/render/             CellPaintDevice, CellPaintEngine, Compositor
  src/runtime/            Application, InputRouter, FrameScheduler
  src/graphics/           graphics encoders, placements, Overlay     (§17.3)
  src/widget/             replaced widgets (editor, calendar)
  src/backend/
    ansi/                 built-in backend
    termpaint/            reference backend
    legacy/               adapters over existing implementations
    null/                 CI backend
  test/                   the suite; text fixtures in test/snapshot/
  tool/inspect/, tool/replay/       qtty-inspect, qtty-replay
  tool/style_gate.py, tool/hooks/   shared style gate and commit-msg hook
  example/chat/           canonical example: dual-frontend chat with stickers
                          (frontend selection, packaging variants — §16.4)
  doc/design.md           this document
  spike/                  the Phase-0 spikes exactly as run (§16); standalone
```

CMake, C++17, Qt 5.15 and Qt 6 in parallel (the four products are not on one version;
the style and paint-engine APIs used here are stable across both). Backends are optional
targets so no product links another product's terminal code.

### 10.1 Namespace and global-state contract

Proven incidentally by the example (§16.4): shared app code contains zero qtty symbols,
so the entire clash surface is the frontend shim in `main.cpp`. To keep it that way in
any project — including one that already has its own `Cell`, `Overlay`, or `Style` —
the library commits to:

- **Everything public lives in `namespace Qtty`.** (Capitalised, matching the
  Qt-ecosystem convention — `Qt::`, `KIO::`, `QXlsx::`; file-system names stay
  lowercase `qtty`, the KDE repo/namespace pattern.) No types, functions, or globals at
  global scope. The spike core deliberately breaks this (`CW`, `CH`, `g_qttyFocus`,
  `Cell` unqualified) for brevity; the library wraps all of it. No `using namespace`
  in any public header.
- **No public macros** except include guards and, if ever needed, `QTTY_`-prefixed
  ones. Macros are the one C++ clash no namespace can fix.
- **Prefixed string namespaces** everywhere strings act as identifiers: dynamic
  properties (`"qtty.cells"`, `"qtty.glyph"`), `Q_DECLARE_INTERFACE` IDs
  (`"org.qtty.*"`), settings keys.
- **Qt singleton ownership is declared, not grabbed.** The real collision risk is not
  names but Qt's process-wide singletons. In TUI mode qtty owns: the application
  style (an app's custom style is not lost — it becomes `GridStyle`'s proxy base),
  the application font, `QT_QPA_PLATFORM`/scaling env vars, and one global event
  filter (popup stamping, GridGuard). An app that must also touch these in TUI mode
  goes through qtty's API rather than `QApplication` directly.
- **In GUI mode qtty touches none of the above** — G2's inertness, restated as a
  testable rule: a GUI build with the library linked but inactive must be
  byte-identical in behaviour to one without it.

---

## 11. Performance budget

Terminals are slow and often remote. Targets:

- Frame budget 16 ms local, 50 ms over ssh; `FrameScheduler` coalesces to whichever applies.
- Damage-driven: a keystroke in a text field must repaint that widget's cells, not the screen.
- `CellBuffer::diff` is the only full-screen scan per frame; O(cells) with early row skip.
- Emit DEC 2026 synchronised-output brackets when available to eliminate tearing.
- Budget enforced by a benchmark test on a 200×60 grid with a 5000-row table.
- **Double-paint overhead is expected** (§5.6): Qt paints into a backingstore we do not
  use, then we `render()` the same widgets into cells. *Measured (§16.1, F9): a full
  `render()` of a dialog into 80×24 costs **0.16 ms** — two orders of magnitude inside
  the 16 ms budget — and one keystroke in a `QLineEdit` dirties exactly **1 cell of
  400**. The double-paint concern is real but irrelevant at these magnitudes; diffing
  full frames is cheaper than tracking damage regions precisely.*

---

## 12. Migration plan

**Phase 0 — Spike. ✅ DONE (§16).** Both gates passed; five corrections folded back into
this document. Original plan retained below for the record:

1. *Render gate.* `CellPaintDevice`/`CellPaintEngine` dumping to stdout. Take a real
   existing dialog containing a `QTreeView` and a `QComboBox`;
   `setAttribute(WA_DontShowOnScreen); show(); render(&painter);`. Is it legible?
2. *Popup gate.* Open a `QMenu` on that dialog and route a synthetic click through it.
   Does it composite and dispatch?

Gate 2 is the real decision point (§8.1). Also resolve OQ-1 (shortcuts) here.

**Phase 1 — Backend consolidation (independent of everything else).** Define
`ITerminalBackend`; adapt all four existing implementations behind it; each product keeps
its current TUI but now calls through the interface. Ships value even if Phases 2+ are
abandoned: one interface, four implementations, a shared diff/damage layer, and a
regression harness.

**Phase 2 — Library core.** L2–L6 plus the snapshot harness and `qtty-inspect`.
Exit criterion: the Phase 0 dialog renders and is fully interactive under snapshot test.

**Phase 3 — First product, first screen.** Pick the *simplest* screen in the product with
the most maintainable existing TUI (best fallback if it goes badly). Characterisation
snapshots first, then delete the old screen's TUI code when the shared one matches.
Exit criterion: one screen, one source, both targets, in production.

**Phase 4 — Roll out.** Screen by screen, product by product, deleting legacy TUI code as
each lands. Expect Tier 3 variants (§7) for the two or three densest screens; that is a
success condition, not a failure.

**Phase 5 — Retire.** Once all four products are on Qtty, collapse the four backend
adapters onto one (termpaint or the best of ours). Optional; the interface makes it a
non-urgent cleanup rather than a blocker.

---

## 13. Risks

| # | Risk | L | I | Mitigation | Trigger to act |
|---|---|---|---|---|---|
| R1 | Popups don't composite acceptably | M | H | §8.1; fall back to 13.2 | Phase 0 gate 2 |
| R2 | **Offscreen plugin is documented as "only fully supported on X11"** | H | H | See §13.3 — scope decision required before Phase 2 | Now |
| R2b | Offscreen plugin behaves differently across Qt versions; absent from some packaged builds | M | M | CI matrix Qt 5.15/6.x; vendor-check the plugin's presence at startup | Any version-specific snapshot diff |
| R3 | Font metrics not integral on some backend | L | H | Startup assert; bundled font; documented hard failure | Assert fires |
| R4 | Shared screens degrade the GUI to suit the TUI | M | H | G2 is a review rule; GUI snapshot tests; Tier 3 is encouraged | Any GUI regression traced to a Tier 2 hint |
| R5 | Channel B output too poor for a heavily custom-painted product | M | M | Widen Channel A / `ICellPainted` per widget | >30% of a screen's cells from Channel B |
| R6 | Effort exceeds the value of unifying four TUIs | M | M | Phase 1 ships value standalone; phases are independently abandonable | Phase 2 slips >50% |

### 13.3 Platform scope — decide before Phase 2

Qt's own documentation states the offscreen plugin is *"only fully supported on X11."*
That is a material constraint on a design whose foundation it is, and it forces an
explicit scope decision:

- **If the TUI is a Linux/server feature** (the likely case — TUIs exist for ssh and
  headless operation), this costs nothing. Declare Linux/X11 the supported target for
  the *terminal* build; GUI builds are unaffected on every platform.
- **If any product needs a Windows or macOS terminal build**, offscreen is not a safe
  foundation there and that product goes to §13.2, or we accept a per-platform spike.

Note the constraint is on the *plugin's completeness*, not on Qt: this bounds where the
TUI runs, never where the GUI runs. Phase 0 should include one smoke test on macOS to
find out how bad "not fully supported" actually is in our usage.

### 13.2 Fallback

If gate 2 fails, or R5 dominates: adopt **Tui Widgets** as the terminal view engine
behind a thin façade (`ui::Button` → `QPushButton` or `Tui::ZButton`), sharing at the
model/controller layer instead of the view layer. Phase 1's backend work is *not* wasted
— Tui Widgets accepts a custom terminal connection, so our backends still plug in.
Vendor it in-tree (Boost licence permits it) given the project's small size.

---

## 14. Open questions

- ~~**OQ-1.** Does a synthetic `QKeyEvent` participate in Qt's shortcut map?~~
  **Closed: no.** `InputRouter` resolves `QAction` shortcuts itself. See §5.5, §16 F3.
- ~~**OQ-2.** Does the offscreen plugin create backingstores for `WA_DontShowOnScreen`
  top-levels?~~ **Closed:** yes — it is the mechanism that keeps paint events flowing.
  ≈6 MB per top-level at 200×60, and widgets are painted twice per frame. See §5.6, §11.
- **OQ-3.** Qt 5.15 and Qt 6 in one codebase, or Qt 6 only with the two laggard products
  upgrading first? Affects Phase 3 sequencing.
- **OQ-4.** Do any of the four products need the TUI where QtWidgets cannot be linked
  (embedded, serial console)? If yes, that product goes to 13.2 permanently and the
  answer changes Phase 4's scope.
- **OQ-5.** Accessibility: is a `QAccessible` bridge worth it, given the terminal already
  exposes text to screen readers when the cursor is placed correctly (§5.5)?

---

## 15. Decision record

| ID | Decision | Rejected alternative | Why |
|---|---|---|---|
| D1 | Reuse real QtWidgets classes | Parallel Qt-like TUI toolkit | One view codebase; sharing at 100% not 80% |
| D2 | In-box `offscreen` platform plugin | Custom curses QPA plugin | QPA has no source/binary compatibility guarantee; per-release maintenance |
| D3 | Semantic style channel + paint-engine fallback | Paint engine only | Style knows *what* it draws; box-drawing and controls need that |
| D4 | Style-driven gridding | Subclass every widget | Reaches Qt-internal children we never construct |
| D5 | Explicit `render()` on a damage schedule | Backingstore paint events | Public API, controls frame rate, and preserves Channel A — at the cost of double painting (§5.6) |
| D6 | Backend interface, four adapters | Adopt termpaint immediately | Preserves accumulated bug fixes; consolidation becomes optional |
| D7 | 1 cell = 8×16 px, bundled fixed-metric font | Sub-pixel metrics with rounding | Rounding once at the style beats rounding everywhere |
| D8 | No Qt fork | Patched qtbase | LGPLv3 publication duty + rebuild per release across four products |

---

## 16. Phase 0 results (executed)

Run on **Qt 6.4.2, `-platform offscreen`, Linux/X11-less container**, DejaVu Sans Mono
at 16 px (measured advance 10 px, height 19 px → CW=10, CH=19). Spike source:
`spike/spike.cpp`, `spike/focus.cpp`. ~450 lines total, built in one afternoon.

### Gate 1 — render a real dialog: **PASS**

A `QDialog` containing `QCheckBox`, two `QRadioButton`s, a `QComboBox`, a `QTreeWidget`
with a header and scrollbar, and a `QDialogButtonBox` — `WA_DontShowOnScreen`, `show()`,
`render()` into `CellPaintDevice`:

```
 [x]
     Enable telemetry
 ( )                    (o)
     Daily                  Weekly
 ▒
  Alpha
 ┌──────────────────────────────────────────┐
 │Name                                     ▒│
 │  host    …                              ││
 └  port    …                              ▒┘
                              <OK>   <Cancel>
```

Layouts activated normally (`isVisible=1`, `WA_Mapped=1`, dialog sized 480×323), and
**11 of 13 child widgets came out exactly cell-aligned** with only the style overrides
listed in §5.3 — confirming the "1 cell = K px + style-supplied metrics" premise. The two
misaligned ones are `QHeaderView` and `QScrollBar` internals (§16, F5).

### Gate 2 — popups: **PASS**, and easier than feared

`QMenu::popup()` with `WA_DontShowOnScreen` produced a live popup that
`QApplication::activePopupWidget()` reported correctly, appeared in `topLevelWidgets()`,
had sane geometry, and composited over the dialog by rendering it a second time at its
own origin. A synthetic press/release pair on `menu.actionGeometry(action).center()`
**triggered the action**. §8.1's "highest risk" rating is now over-stated for menus;
the residual risk is tooltips, nested submenus, and off-screen clamping.

### The five corrections

| # | Finding | Consequence |
|---|---|---|
| **F1** | `p->device()` is the **QWidget**, not our paint device, during a `paintEvent` | Channel A must detect via `p->paintEngine()`. Silent failure mode — it just never fires. §5.4 |
| **F2** | Neither `transform()` nor `combinedTransform()` carries `render()`'s redirection offset | Style-level coords must come from `w->mapTo(w->window(), …)`. §5.4 |
| **F3** | Synthetic `QKeyEvent`s never reach `QShortcutMap` — 0 hits across Widget/Window/Application contexts and both targets | `InputRouter` owns a shortcut table. §5.5 |
| **F4** | No window ever activates ⇒ `qApp->focusWidget()` null and `hasFocus()` false **forever** — but `window->focusWidget()` and `focusNextPrevChild()` work perfectly | Keep Qt's focus chain, replace only "active window". `State_HasFocus` must be injected. §5.5 |
| **F5** | `QHeaderView`/`QScrollBar` ignore some style metrics and self-size | A small set of widgets needs `ICellPainted` or fixed sizing; not a systemic problem |

### Confirmed as designed

- `QPaintEngine(AllFeatures)` **does** prevent text→path emulation: 11 `drawTextItem`
  calls vs 1 `drawPath` on a full dialog. Declaring features narrowly would have
  destroyed Channel B's text handling, exactly as predicted.
- `devicePixelRatio() == 1` under offscreen, unconditionally.
- A `QWindow` **and** a platform window are created (`win->handle()` non-null) but never
  shown — §5.6's revised wording is right, and OQ-2 is settled.
- Channel A reached **14 style-drawn elements** on one dialog once F1/F2 were fixed:
  checkboxes, radios, buttons, frames. The two-channel model works as intended.

### What Phase 0 did *not* test

Text-heavy widgets (`QTextEdit`), item-view editors and delegates, `QSplitter`,
resize/reflow, colour quantisation, real terminal I/O, and macOS/Windows offscreen
behaviour (§13.3). Vertical text centring within multi-cell widgets is visibly off by
one row in the dump above and needs a baseline calibration pass.

### Verdict

**Proceed.** No gate failed, and the two genuine surprises (F3, F4) cost a bounded amount
of `InputRouter` code rather than threatening the architecture. The design's core
bet — that Qt's layout, focus chain, and style system survive being pointed at a cell
grid — is now measured rather than assumed.

### 16.1 Phase 0.5 — interactivity and the untested list (executed)

Second spike (`spike/spike2.cpp`, sharing `spike/qtty_core.h`), same environment.
Five questions from "what Phase 0 did not test", five answers:

| # | Question | Result |
|---|---|---|
| **F6** | Resize/reflow | **PASS.** After resizes to 64×14, 30×8, and 80×24 cells, all children re-land aligned and stretch factors redistribute correctly (button row lands at exactly `height-4` when space allows). The boundary found: a resize below `minimumSizeHint()` is refused and content overflows — small terminals need a drop-optional-then-scroll policy (§7), not faith in layout compression. |
| **F7** | Internally-created popup (`QComboBox`) | **PASS.** `QComboBoxPrivateContainer` discovered via `combo->view()->window()` and reported by `activePopupWidget()`; composites with all items visible; synthetic Down/Down/Enter selects the right item and closes the popup. One trap: it does **not** inherit `WA_DontShowOnScreen` — the runtime must stamp popups via a global Show/ChildAdded filter (§8.1). |
| **F8** | `QTextEdit` through Channel B | **Better than designed for.** With the document font's line height equal to CH, plain text renders *perfectly* — every `QTextLayout` line lands on a cell row. Rich text flattens but stays legible. §8.2/§8.4 downgraded from "replace wholesale" to "replace the interaction layer if needed". |
| **F9** | Frame cost / damage | **PASS with headroom.** Full `render()` into 80×24: **0.16 ms/frame**. One keystroke in a `QLineEdit`: **1 cell of 400** changed. The §5.6 double-paint concern is real but numerically irrelevant; full-frame render + diff is a viable frame loop with no damage tracking at all. |
| **F10** | Focus injection (`g_qttyFocus`) | **PASS.** With `InputRouter`-owned focus consulted by `GridStyle`, moving focus to a button changes exactly the button's cells to reverse-video — confirming the F4 mitigation is a few lines, not a subsystem. |

Updated verdict: unchanged — proceed — but with more evidence and two design
simplifications banked (no damage-region tracking needed at v1; no cell-native text
editor needed for read-only text).

### 16.2 Phase 0.6 — graphics overlay plane (executed)

Third spike (`spike/spike3.cpp`) validating §5.7's two non-native delivery paths:

- **Half-block/shade fallback (pure L2):** a translucent radial-gradient disc plus an
  opaque block composited over a live dialog rendering. UI text remains readable under
  the translucent region (`░▒▓` fills only empty cells), the opaque region covers it —
  exactly the intended semantics, implemented as a ~25-line `CellBuffer` transform
  requiring nothing from any backend.
- **Software composite for pixel terminals:** rasterizing the 60×16 `CellBuffer` to a
  600×304 px `QImage` with QPainter costs **3.8 ms**; `SourceOver`-blending the overlay
  costs **0.2 ms**. The resulting PNG (`spike/composite.png`) is the exact frame a
  sixel/kitty/iTerm2 encoder would ship. Combined with F9's 0.16 ms cell render, a
  full graphics frame fits comfortably inside even the 16 ms local budget; encoders
  are the only cost not yet measured.

Conclusion: the graphics plane is *prepared for* in the strong sense — the seams are in
the interfaces (`Capabilities::GraphicsMode`, `IGraphicsOutput`), and the two hardest
compositing paths already run. Remaining graphics work is protocol encoding (sixel RLE,
kitty chunking, iTerm2 base64) and terminal negotiation, all confined to backends.

---

## 17. Widget coverage: remaining effort

Estimate basis: the three spikes (≈700 lines, roughly three working days of effort all
told) produced working checkbox, radio, button, combo + popup, menu + click dispatch,
line edit text entry, tree view (rough), dialog frames, resize/reflow, focus injection,
and both graphics composite paths. The numbers below extrapolate from that measured
pace, for one developer who knows the codebase, to "production-quality in the TUI with
snapshot tests" — not to demo-quality.

### 17.1 Shared infrastructure (gates everything; build first)

| Component | Content | Est. |
|---|---|---|
| L2 CellBuffer, colours, damage | grapheme clusters, wide cells, `Color` model, diff | 3–4 d |
| InputRouter | key/mouse dispatch, shortcut table (F3), focus ownership (F4), modal/popup routing, popup attribute stamping (F7) | 4–5 d |
| Compositor + FrameScheduler | top-level z-order, popup clamping, frame loop on a real pty via `QSocketNotifier` | 3–4 d |
| GridStyle hardening | full `pixelMetric` audit, `sizeFromContents` for every ContentsType, baseline calibration (the off-by-one row centring seen in §16) | 3–4 d |
| Theming + quantisation | §6: palette roles, 256/16-colour mapping, contrast assert | 3 d |
| Snapshot harness + `qtty-replay` | fixture format, characterisation runner | 2–3 d |
| **Subtotal** | | **18–23 d** |

### 17.2 Widget tier (Channel A quality)

| Widget group | Notes | Est. |
|---|---|---|
| QLabel, QPushButton, QCheckBox, QRadioButton, QGroupBox, QFrame | mostly done in spike; polish + tests | 2 d |
| QLineEdit | cursor, selection, horizontal scroll-in-field, `ImCursorRectangle` wiring | 2–3 d |
| QComboBox | popup done; editable variant, clamping | 1–2 d |
| QMenu, QMenuBar | submenus, mnemonics, edge flip | 2–3 d |
| Item views (QListView/QTreeView/QTableView) + CellItemDelegate + QHeaderView fix (F5) | the biggest single chunk; header/scrollbar self-sizing fought the style in spikes | 5–7 d |
| QScrollBar + QScrollArea (Channel A `▲█░▼`) | F5 sibling | 2 d |
| QTabWidget/QTabBar | | 2 d |
| QProgressBar, QSlider, QSpinBox | | 2 d |
| QSplitter | 1-cell handle, drag via mouse events | 1–2 d |
| QTextEdit interaction layer | display free (F8); cursor/selection/scroll if editing needed | 0–4 d |
| QDialog/QMessageBox/QDialogButtonBox | modality via InputRouter | 1–2 d |
| **Subtotal** | | **20–29 d** |

### 17.3 Graphics plane (beyond what §16.2 already runs)

| Component | Est. |
|---|---|
| `Overlay`/`PixelSurface` API + GUI-build twins | 2–3 d |
| Half-block colour upgrade (needs §6 colours) | 1–2 d |
| Sixel encoder + negotiation | 2–3 d |
| Kitty protocol (incl. alpha-over-text path) | 2–3 d |
| iTerm2 inline images | 1 d |
| **Subtotal** | | **8–12 d** |

### 17.4 Total

**≈ 46–64 developer-days (9–13 weeks single-handed)** to "commonly used widgets work
well, graphics overlays work everywhere, snapshot-tested." The infrastructure tier is
strictly sequential; the widget tier parallelises well across two people after it, which
compresses the calendar to ~6–8 weeks. This buys the library itself; per-product screen
migration (Phases 3–4) is on top and scales with each product's screen count.

Confidence: moderate-high for §17.1/§17.2 (extrapolated from measured spike pace on the
same code paths), moderate for §17.3 (encoders are well-documented but terminal
negotiation is fiddly across emulators).

### 16.3 Phase 0.7 — chat view with scrolling stickers (executed)

Fourth spike (`spike/spike4.cpp`): a vanilla-Qt chat — `QListView` +
`QAbstractListModel` + `QStyledItemDelegate`, delegate drawing author/text with
`QPainter::drawText` and stickers with `QPainter::drawPixmap`. **Zero `qtty::` types in
the app-side code.** The `CellPaintEngine::drawPixmap` funnel (now in the shared core)
converts each sticker into a `CellImage` placement; the view scrolls per-pixel with a
CH single step.

Results across three scroll positions (0, 6, 12 rows):

- **Placements track the text flow exactly.** The smiley at cells (3,7) moves to (3,1)
  after a 6-row scroll; the heart enters at (3,13) half-clipped by the viewport —
  the real partial-visibility case, handled by buffer clipping in the mosaic tier and
  needing a crop in the kitty tier.
- **Upload-once works as designed.** Two unique `QPixmap::cacheKey()`s across three
  frames and five sticker sightings; every subsequent sighting is "re-place by id"
  (~30 bytes on the kitty protocol) rather than pixel retransmission — including the
  same sticker sent twice in the conversation, which shares one key automatically.
- **The mosaic fallback is recognisable** (smiley and heart both read clearly in `░▒▓█`
  at 10×4 cells), and the PNG composites (`chat_scroll_*.png`) show the
  pixel-graphics-tier result: antialiased stickers sitting in the scrolling text flow.
- **Delegate discipline confirmed cheap:** cell-multiple `sizeHint()` came out as one
  `rows * CH` expression, and `QFrame::NoFrame` was needed on the view — the default
  frame offsets the viewport by PM_DefaultFrameWidth in *both* axes, breaking row
  alignment (a `GridStyle` fix for the library: frame widths must be CH-safe or zero).

Conclusion: §5.7's cell-anchored placement design and the "GUI code path is the TUI
API" claim are both demonstrated working. The remaining kitty-tier work is protocol
encoding and viewport cropping, both backend-side.

### 16.4 Phase 0.8 — the shippable example (executed)

The chat spike was promoted to `example/chat/` — the example that ships with the
library — and extended with the frontend-selection and packaging story:

- **`chat.h`** — the entire application (model, delegate, window, sticker assets),
  100% vanilla Qt Widgets, zero `qtty::` types, compiled identically into every
  variant. The three grid disciplines are marked `[grid]` inline: font-derived sizes,
  per-pixel scrolling with a line-height step, cell-multiple sticker assets.
- **`main.cpp`** — frontend selection best practice. Precedence: explicit `--tui`/
  `--gui` flag → `argv[0]` name suffix (`chat-tui` symlink) → autodetect (no
  `DISPLAY`/`WAYLAND_DISPLAY` and stdout is a tty ⇒ TUI). The one sharp edge is
  ordering, documented in the file header:
  `prepareEnvironment()` → `QApplication` → `setup()` → shared widgets → `exec()`.
- **`CMakeLists.txt`** — three targets from one source set: `chat` (dual-frontend,
  the one to ship), `chat-gui` (`-DQTTY_NO_TUI`), `chat-tui` (`-DQTTY_NO_GUI`).
  The variants strip code paths, not dependencies — GUI-first apps already link
  QtWidgets, so the recommended packaging is the single dual binary, optionally with
  a `chat-tui` symlink.
- **`qtty_mini.h`** — a minimal stand-in runtime (alt-screen ANSI backend, raw-mode
  input through `QSocketNotifier` into the Qt event loop, 256-colour placement
  mosaic, hardware cursor via `ImCursorRectangle`) so the example runs in a real
  terminal *today*; its `prepareEnvironment`/`setup`/`exec` surface is the intended
  `qtty::Application` API, so example code ports to the real library unchanged.

Verified: all three variants build and exit clean; the TUI, fed a scripted session,
accepts typed text into the `QLineEdit` (synthetic events through the F4 focus rule),
converts `:)`/`<3` into sticker placements rendered as colour mosaic, scrolls on
arrow keys, places the terminal cursor in the input field, and restores the screen on
Ctrl-D. The `argv[0]` symlink path was verified by invoking through `mychat-tui` with
stdout piped: the process entered the alt screen immediately, proving name-based
selection overrides autodetection.
