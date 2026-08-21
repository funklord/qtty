// Qtty Phase 0 spike — falsify or validate the design's foundations.
//
// Gate 1: does a real QWidget tree render legibly through a custom QPaintEngine?
// Gate 2: do popups (QMenu) composite and dispatch?
// OQ-1:   does a synthetic QKeyEvent reach Qt's shortcut map?
// Plus:   backingstore existence, DPR, layout activation, text->path emulation.

#include <QtWidgets>
#include <QPaintEngine>
#include <QPaintDevice>
#include <QTextItem>
#include <cstdio>

static int CW = 8, CH = 16;   // measured at startup

// ---------------------------------------------------------------- CellBuffer
struct Cell { QString ch = QStringLiteral(" "); bool rev = false; bool bold = false; };

class CellBuffer {
public:
    CellBuffer(int cols, int rows) : c_(cols), r_(rows), d_(cols * rows) {}
    int cols() const { return c_; } int rows() const { return r_; }
    Cell &at(int x, int y) { static Cell junk; if (x<0||y<0||x>=c_||y>=r_) return junk; return d_[y*c_+x]; }
    void fill(const QRect &r, const Cell &v) {
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) at(x,y) = v;
    }
    void text(int x, int y, const QString &s, bool rev=false, bool bold=false) {
        for (int i = 0; i < s.size(); ++i) { Cell &c = at(x+i,y); c.ch = QString(s[i]); c.rev=rev; c.bold=bold; }
    }
    QString toText() const {
        QString out;
        for (int y = 0; y < r_; ++y) {
            QString line;
            for (int x = 0; x < c_; ++x) line += d_[y*c_+x].ch;
            while (line.endsWith(' ')) line.chop(1);
            out += line + '\n';
        }
        return out;
    }
private:
    int c_, r_; QVector<Cell> d_;
};

// ------------------------------------------------- CellPaintDevice / Engine
class CellPaintEngine;

class CellPaintDevice : public QPaintDevice {
public:
    CellPaintDevice(CellBuffer &b);
    ~CellPaintDevice() override;
    QPaintEngine *paintEngine() const override;
    int metric(PaintDeviceMetric m) const override;
    CellBuffer &buffer() const { return buf_; }
    QPoint origin;                       // widget-space offset, in pixels
private:
    CellBuffer &buf_;
    CellPaintEngine *eng_;
};

class CellPaintEngine : public QPaintEngine {
public:
    // Declare a rich feature set so QPainter does NOT emulate text as paths.
    CellPaintEngine()
        : QPaintEngine(QPaintEngine::AllFeatures) {}

    bool begin(QPaintDevice *pdev) override { dev_ = static_cast<CellPaintDevice*>(pdev); return true; }
    bool end() override { dev_ = nullptr; return true; }
    Type type() const override { return QPaintEngine::User; }

    void updateState(const QPaintEngineState &s) override {
        if (s.state() & DirtyPen)   pen_   = s.pen();
        if (s.state() & DirtyBrush) brush_ = s.brush();
        if (s.state() & DirtyFont)  font_  = s.font();
        if (s.state() & DirtyTransform) xf_ = s.transform();
        if (s.state() & DirtyClipRegion) { clip_ = s.clipRegion(); hasClip_ = true; }
        if (s.state() & DirtyClipPath)   { clip_ = QRegion(s.clipPath().boundingRect().toRect()); hasClip_ = true; }
    }

    void drawTextItem(const QPointF &p, const QTextItem &ti) override {
        ++textCalls;
        QPointF q = xf_.map(p) + QPointF(dev_->origin);
        QFontMetricsF fm(ti.font());
        int col = qRound(q.x() / CW);
        int row = qRound((q.y() - fm.ascent()) / CH);
        dev_->buffer().text(col, row, ti.text(), false, ti.font().bold());
    }

    void drawRects(const QRectF *rects, int n) override { for (int i=0;i<n;++i) fill(rects[i]); }
    void drawRects(const QRect  *rects, int n) override { for (int i=0;i<n;++i) fill(QRectF(rects[i])); }

    void drawLines(const QLineF *l, int n) override { ++lineCalls; for (int i=0;i<n;++i) line(l[i]); }
    void drawLines(const QLine  *l, int n) override { ++lineCalls; for (int i=0;i<n;++i) line(QLineF(l[i])); }

    void drawPath(const QPainterPath &path) override {
        ++pathCalls;
        // If text ever arrives here, feature declaration failed. Track it.
        fill(path.boundingRect(), /*outlineOnly=*/true);
    }
    void drawPixmap(const QRectF &r, const QPixmap &, const QRectF &) override {
        ++pixmapCalls;
        QRect c = toCells(r);
        if (c.isValid()) dev_->buffer().text(c.left(), c.top(), QStringLiteral("▒"));
    }
    void drawPolygon(const QPointF *pts, int n, PolygonDrawMode) override {
        QPolygonF p; for (int i=0;i<n;++i) p << pts[i];
        fill(p.boundingRect(), true);
    }

    int textCalls = 0, lineCalls = 0, pathCalls = 0, pixmapCalls = 0, fillCalls = 0;
    CellPaintDevice *device() const { return dev_; }

private:
    QRect toCells(const QRectF &r) const {
        QRectF m = xf_.mapRect(r).translated(dev_->origin);
        return QRect(qRound(m.left()/CW), qRound(m.top()/CH),
                     qMax(1, qRound(m.width()/CW)), qMax(1, qRound(m.height()/CH)));
    }
    void fill(const QRectF &r, bool outlineOnly = false) {
        ++fillCalls;
        QRect c = toCells(r);
        if (!c.isValid() || c.width() > 400 || c.height() > 200) return;
        if (outlineOnly || brush_.style() == Qt::NoBrush) { box(c); return; }
        // Solid fill: only paint background-ish fills as blanks, don't erase text.
        Cell blank; blank.ch = QStringLiteral(" ");
        if (c.width() > 1 && c.height() > 1) dev_->buffer().fill(c, blank);
    }
    void box(const QRect &c) {
        if (c.width() < 2 || c.height() < 2) return;
        CellBuffer &b = dev_->buffer();
        for (int x = c.left()+1; x < c.right(); ++x) { b.at(x,c.top()).ch = "─"; b.at(x,c.bottom()).ch = "─"; }
        for (int y = c.top()+1; y < c.bottom(); ++y) { b.at(c.left(),y).ch = "│"; b.at(c.right(),y).ch = "│"; }
        b.at(c.left(),c.top()).ch="┌"; b.at(c.right(),c.top()).ch="┐";
        b.at(c.left(),c.bottom()).ch="└"; b.at(c.right(),c.bottom()).ch="┘";
    }
    void line(const QLineF &l) {
        QLineF m(xf_.map(l.p1()) + QPointF(dev_->origin), xf_.map(l.p2()) + QPointF(dev_->origin));
        CellBuffer &b = dev_->buffer();
        if (qAbs(m.dy()) < CH/2.0) {           // horizontal
            int y = qRound(m.y1()/CH);
            for (int x = qRound(qMin(m.x1(),m.x2())/CW); x <= qRound(qMax(m.x1(),m.x2())/CW); ++x)
                if (b.at(x,y).ch == " ") b.at(x,y).ch = "─";
        } else if (qAbs(m.dx()) < CW/2.0) {    // vertical
            int x = qRound(m.x1()/CW);
            for (int y = qRound(qMin(m.y1(),m.y2())/CH); y <= qRound(qMax(m.y1(),m.y2())/CH); ++y)
                if (b.at(x,y).ch == " ") b.at(x,y).ch = "│";
        }
    }
    CellPaintDevice *dev_ = nullptr;
    QPen pen_; QBrush brush_; QFont font_; QTransform xf_; QRegion clip_; bool hasClip_ = false;
};

CellPaintDevice::CellPaintDevice(CellBuffer &b) : buf_(b), eng_(new CellPaintEngine) {}
CellPaintDevice::~CellPaintDevice() { delete eng_; }
QPaintEngine *CellPaintDevice::paintEngine() const { return eng_; }
int CellPaintDevice::metric(PaintDeviceMetric m) const {
    switch (m) {
    case PdmWidth:              return buf_.cols() * CW;
    case PdmHeight:             return buf_.rows() * CH;
    case PdmWidthMM:            return buf_.cols() * CW / 4;
    case PdmHeightMM:           return buf_.rows() * CH / 4;
    case PdmNumColors:          return 256;
    case PdmDepth:              return 24;
    case PdmDpiX: case PdmPhysicalDpiX: return 96;
    case PdmDpiY: case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio:   return 1;
    case PdmDevicePixelRatioScaled: return 1 * QPaintDevice::devicePixelRatioFScale();
    default: return 0;
    }
}

// ------------------------------------------------------------------ GridStyle
class GridStyle : public QProxyStyle {
public:
    GridStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override {
        switch (m) {
        case PM_LayoutLeftMargin: case PM_LayoutRightMargin:  return CW;
        case PM_LayoutTopMargin:  case PM_LayoutBottomMargin: return CH;
        case PM_LayoutHorizontalSpacing: return CW;
        case PM_LayoutVerticalSpacing:   return 0;
        case PM_ScrollBarExtent:         return CW;
        case PM_DefaultFrameWidth:       return CW;
        case PM_ButtonMargin:            return CW;
        case PM_FocusFrameHMargin: case PM_FocusFrameVMargin: return 0;
        case PM_MenuPanelWidth: case PM_MenuBarPanelWidth:    return CW;
        case PM_IndicatorWidth:  return 3*CW;
        case PM_IndicatorHeight: return CH;
        case PM_ExclusiveIndicatorWidth:  return 3*CW;
        case PM_ExclusiveIndicatorHeight: return CH;
        default: {
            int v = QProxyStyle::pixelMetric(m, o, w);
            return v; // deliberately unsnapped here; see findings
        }}
    }
    QSize sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &cs,
                           const QWidget *w) const override {
        QSize s = QProxyStyle::sizeFromContents(t, o, cs, w);
        return QSize(((s.width()+CW-1)/CW)*CW, ((s.height()+CH-1)/CH)*CH);
    }
    // Channel A: semantic drawing when the target is a cell device.
    static CellPaintDevice *cellTarget(QPainter *p) {
        // NOT p->device(): during a paintEvent the device is the QWidget itself.
        // The engine is ours regardless of redirection.
        if (auto *e = dynamic_cast<CellPaintEngine*>(p->paintEngine())) return e->device();
        return nullptr;
    }
    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                       const QWidget *w) const override {
        if (auto *dev = cellTarget(p)) {
            QRect c = cells(opt->rect, p, dev, w);
            switch (pe) {
            case PE_IndicatorCheckBox:
                dev->buffer().text(c.left(), c.top(),
                    (opt->state & State_On) ? "[x]" : "[ ]");
                ++chanA; return;
            case PE_IndicatorRadioButton:
                dev->buffer().text(c.left(), c.top(),
                    (opt->state & State_On) ? "(o)" : "( )");
                ++chanA; return;
            case PE_FrameWindow: case PE_Frame: case PE_FrameGroupBox:
            case PE_PanelMenu:   case PE_FrameMenu:
                drawBox(dev->buffer(), c); ++chanA; return;
            default: break;
            }
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }
    void drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                     const QWidget *w) const override {
        if (auto *dev = cellTarget(p)) {
            QRect c = cells(opt->rect, p, dev, w);
            if (ce == CE_PushButtonBevel || ce == CE_PushButtonLabel) {
                if (auto *b = qstyleoption_cast<const QStyleOptionButton*>(opt)) {
                    if (ce == CE_PushButtonLabel) {
                        QRect bw = w ? cells(w->rect(), p, dev, w) : c;
                        QString t = "<" + b->text + ">";
                        c = bw;
                        dev->buffer().text(c.left(), c.top(), t, opt->state & State_HasFocus);
                        ++chanA;
                    }
                    return;
                }
            }
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }
    static int chanA;
private:
    static QRect cells(const QRect &r, QPainter *p, CellPaintDevice *dev,
                       const QWidget *w) {
        // Neither transform() nor combinedTransform() carries the redirection
        // offset applied by QWidget::render(). Map through the widget instead.
        QPoint tl = w ? w->mapTo(w->window(), r.topLeft())
                      : p->combinedTransform().map(r.topLeft());
        tl += dev->origin;
        return QRect(qRound(tl.x()/double(CW)), qRound(tl.y()/double(CH)),
                     qMax(1,qRound(r.width()/double(CW))), qMax(1,qRound(r.height()/double(CH))));
    }
    static void drawBox(CellBuffer &b, const QRect &c) {
        if (c.width() < 2 || c.height() < 2) return;
        for (int x=c.left()+1;x<c.right();++x){ b.at(x,c.top()).ch="─"; b.at(x,c.bottom()).ch="─"; }
        for (int y=c.top()+1;y<c.bottom();++y){ b.at(c.left(),y).ch="│"; b.at(c.right(),y).ch="│"; }
        b.at(c.left(),c.top()).ch="┌"; b.at(c.right(),c.top()).ch="┐";
        b.at(c.left(),c.bottom()).ch="└"; b.at(c.right(),c.bottom()).ch="┘";
    }
};
int GridStyle::chanA = 0;

// ------------------------------------------------------------------- helpers
static void renderWidget(QWidget *w, CellBuffer &buf, QPoint originPx = QPoint()) {
    CellPaintDevice dev(buf);
    dev.origin = originPx;
    QPainter p(&dev);
    w->render(&p, QPoint(), QRegion(),
              QWidget::RenderFlags(QWidget::DrawWindowBackground | QWidget::DrawChildren));
    p.end();
}

static void hr(const char *t) { printf("\n\033[1m=== %s ===\033[0m\n", t); }

// ------------------------------------------------------------ the test dialog
class Dialog : public QDialog {
public:
    Dialog() {
        setWindowTitle("Preferences");
        auto *v = new QVBoxLayout(this);

        auto *chk = new QCheckBox("Enable telemetry", this);
        chk->setChecked(true);
        v->addWidget(chk);

        auto *h = new QHBoxLayout;
        auto *r1 = new QRadioButton("Daily", this);
        auto *r2 = new QRadioButton("Weekly", this); r2->setChecked(true);
        h->addWidget(r1); h->addWidget(r2);
        v->addLayout(h);

        combo = new QComboBox(this);
        combo->addItems({"Alpha", "Beta", "Gamma"});
        v->addWidget(combo);

        tree = new QTreeWidget(this);
        tree->setHeaderLabels({"Name", "Value"});
        for (const char *n : {"host", "port", "user"}) {
            auto *it = new QTreeWidgetItem(tree);
            it->setText(0, n); it->setText(1, "…");
        }
        tree->setFixedHeight(5 * CH);
        v->addWidget(tree);

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        v->addWidget(bb);

        resize(48 * CW, 16 * CH);
    }
    QComboBox *combo; QTreeWidget *tree;
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);

    // --- font: require integral metrics -------------------------------------
    hr("FONT METRICS");
    QFont f("DejaVu Sans Mono");
    f.setPixelSize(16);
    f.setStyleHint(QFont::Monospace, QFont::PreferMatch);
    QFontMetrics fm(f);
    printf("requested DejaVu Sans Mono @16px -> family=%s  advance('M')=%d  height=%d  ascent=%d\n",
           qPrintable(QFontInfo(f).family()), fm.horizontalAdvance('M'), fm.height(), fm.ascent());
    bool integral = true;
    for (char c : {'i','M','W','0','.'})
        if (fm.horizontalAdvance(c) != fm.horizontalAdvance('M')) integral = false;
    printf("fixed-advance across i/M/W/0/. : %s\n", integral ? "YES" : "NO");
    CW = fm.horizontalAdvance('M'); CH = fm.height();
    printf("=> CW=%d CH=%d\n", CW, CH);
    app.setFont(f);
    app.setStyle(new GridStyle);

    // --- DPR ----------------------------------------------------------------
    hr("DEVICE PIXEL RATIO");
    printf("qApp->devicePixelRatio() = %.3f   primaryScreen dpr = %.3f  logicalDpi = %.1f\n",
           app.devicePixelRatio(), QGuiApplication::primaryScreen()->devicePixelRatio(),
           QGuiApplication::primaryScreen()->logicalDotsPerInch());

    // --- GATE 1 -------------------------------------------------------------
    hr("GATE 1: render a real dialog through CellPaintEngine");
    Dialog dlg;
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.show();
    QCoreApplication::processEvents();

    printf("after show(): isVisible=%d isHidden=%d WA_Mapped=%d layout activated (size=%dx%d)\n",
           dlg.isVisible(), dlg.isHidden(), dlg.testAttribute(Qt::WA_Mapped),
           dlg.size().width(), dlg.size().height());
    printf("child geometries (px / cells):\n");
    for (QWidget *c : dlg.findChildren<QWidget*>()) {
        if (c->geometry().isNull() || !c->isVisible()) continue;
        QRect g = c->geometry();
        printf("  %-22s %4d,%3d %4dx%-4d   cells %3d,%2d %3dx%-2d  %s\n",
               c->metaObject()->className(), g.x(), g.y(), g.width(), g.height(),
               g.x()/CW, g.y()/CH, g.width()/CW, g.height()/CH,
               (g.x()%CW || g.y()%CH || g.width()%CW || g.height()%CH) ? "MISALIGNED" : "aligned");
    }

    // backingstore check
    hr("BACKINGSTORE");
    QWindow *win = dlg.windowHandle();
    printf("windowHandle() = %p   (a QWindow exists: %s)\n", (void*)win, win ? "YES" : "no");
    if (win) printf("  win->isVisible()=%d  win->handle()=%p (platform window %s)\n",
                    win->isVisible(), (void*)win->handle(), win->handle() ? "created" : "NOT created");

    CellBuffer buf(60, 20);
    renderWidget(&dlg, buf);
    printf("\n--- rendered ---\n%s--- end ---\n", qPrintable(buf.toText()));

    // --- GATE 2 -------------------------------------------------------------
    hr("GATE 2: popup (QMenu)");
    QMenu menu("Options");
    QAction *aNew  = menu.addAction("New");
    QAction *aOpen = menu.addAction("Open");
    menu.addSeparator();
    QAction *aQuit = menu.addAction("Quit");
    int fired = 0;
    QObject::connect(aOpen, &QAction::triggered, [&]{ fired = 1; });

    menu.setAttribute(Qt::WA_DontShowOnScreen);
    menu.popup(QPoint(10 * CW, 6 * CH));
    QCoreApplication::processEvents();

    printf("activePopupWidget() = %s\n",
           qApp->activePopupWidget() ? qApp->activePopupWidget()->metaObject()->className() : "(null)");
    printf("topLevelWidgets(): ");
    for (QWidget *w : qApp->topLevelWidgets())
        printf("%s%s ", w->metaObject()->className(), w->isVisible() ? "*" : "");
    printf("\nmenu geometry %dx%d at %d,%d  (visible=%d)\n",
           menu.width(), menu.height(), menu.x(), menu.y(), menu.isVisible());

    // composite popup over the dialog
    renderWidget(&menu, buf, menu.pos());
    printf("\n--- dialog + composited popup ---\n%s--- end ---\n", qPrintable(buf.toText()));

    // dispatch a synthetic click onto the "Open" item
    QRect openRect = menu.actionGeometry(aOpen);
    QPoint local = openRect.center();
    QPointF globalF = menu.mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), globalF,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), globalF,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&menu, &press);
    QApplication::sendEvent(&menu, &release);
    QCoreApplication::processEvents();
    printf("synthetic click on 'Open' -> action fired: %s\n", fired ? "YES" : "NO");

    menu.close();
    QCoreApplication::processEvents();

    // --- OQ-1 + activation ---------------------------------------------------
    hr("ACTIVATION & FOCUS under offscreen + WA_DontShowOnScreen");
    printf("baseline: focusWidget=%s activeWindow=%s\n",
           qApp->focusWidget() ? qApp->focusWidget()->metaObject()->className() : "(null)",
           qApp->activeWindow() ? qApp->activeWindow()->metaObject()->className() : "(null)");

    // path 1: QWidget::activateWindow()
    dlg.activateWindow(); QCoreApplication::processEvents();
    printf("after activateWindow():        activeWindow=%s\n",
           qApp->activeWindow() ? qApp->activeWindow()->metaObject()->className() : "(null)");

    // path 2: QWindow::requestActivate()
    if (dlg.windowHandle()) { dlg.windowHandle()->requestActivate(); QCoreApplication::processEvents(); }
    printf("after requestActivate():       activeWindow=%s  focusWindow=%s\n",
           qApp->activeWindow() ? qApp->activeWindow()->metaObject()->className() : "(null)",
           qApp->focusWindow() ? "set" : "(null)");

    // path 3: explicit focus on a widget
    auto *edit = new QLineEdit(&dlg);
    edit->setGeometry(0, 0, 20*CW, CH);
    edit->show();
    edit->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    printf("after setFocus():              focusWidget=%s  edit->hasFocus()=%d\n",
           qApp->focusWidget() ? qApp->focusWidget()->metaObject()->className() : "(null)",
           edit->hasFocus());

    // core input path: does a synthetic key reach a focused QLineEdit?
    hr("CORE INPUT PATH: synthetic keys into QLineEdit");
    for (const QString &ch : {QStringLiteral("h"), QStringLiteral("i")}) {
        QKeyEvent k(QEvent::KeyPress, ch.at(0).unicode(), Qt::NoModifier, ch);
        QApplication::sendEvent(edit, &k);
    }
    QCoreApplication::processEvents();
    printf("QLineEdit text after 2 synthetic keys: \"%s\"  %s\n",
           qPrintable(edit->text()), edit->text() == "hi" ? "(OK)" : "(FAILED)");

    hr("OQ-1: shortcut map");
    int scFired = 0;
    QAction *save = new QAction("Save", &dlg);
    save->setShortcut(QKeySequence("Ctrl+S"));
    dlg.addAction(save);
    QObject::connect(save, &QAction::triggered, [&]{ scFired++; });

    for (auto ctx : {Qt::WidgetShortcut, Qt::WindowShortcut, Qt::ApplicationShortcut}) {
        save->setShortcutContext(ctx);
        int before = scFired;
        QWidget *target = qApp->focusWidget() ? qApp->focusWidget() : &dlg;
        { QKeyEvent k(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, "\x13");
          QApplication::sendEvent(target, &k); }
        QCoreApplication::processEvents();
        const char *n = ctx==Qt::WidgetShortcut ? "WidgetShortcut"
                      : ctx==Qt::WindowShortcut ? "WindowShortcut" : "ApplicationShortcut";
        printf("  ctx=%-20s sendEvent(focusWidget) -> +%d\n", n, scFired - before);
    }
    // and via the QWindow (the path a real platform plugin uses)
    save->setShortcutContext(Qt::ApplicationShortcut);
    int before2 = scFired;
    if (dlg.windowHandle()) {
        QKeyEvent k(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, "\x13");
        QApplication::sendEvent(dlg.windowHandle(), &k);
    }
    QCoreApplication::processEvents();
    printf("  ctx=ApplicationShortcut  sendEvent(QWindow)     -> +%d\n", scFired - before2);
    printf("  => %s\n", scFired ? "shortcut map IS reachable" :
           "shortcut map NOT reachable; InputRouter must resolve QActions itself");

    // --- engine call census -------------------------------------------------
    hr("PAINT ENGINE CENSUS (did features stop text->path emulation?)");
    CellBuffer probe(60, 20);
    { CellPaintDevice dev(probe); QPainter p(&dev);
      dlg.render(&p, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawWindowBackground|QWidget::DrawChildren));
      p.end();
      auto *e = static_cast<CellPaintEngine*>(dev.paintEngine());
      printf("drawTextItem=%d  drawLines=%d  drawPath=%d  drawPixmap=%d  fills=%d\n",
             e->textCalls, e->lineCalls, e->pathCalls, e->pixmapCalls, e->fillCalls);
      printf("Channel A (style-drawn) hits = %d\n", GridStyle::chanA);
      printf("%s\n", e->textCalls > 0
             ? "=> text arrives as text (AllFeatures prevented path emulation)"
             : "=> WARNING: no drawTextItem calls; text was emulated as paths");
    }
    return 0;
}
