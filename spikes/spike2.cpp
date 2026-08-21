// Qtty Phase 0.5 — the questions Phase 0 left open.
//
// A: resize/reflow            — do layouts re-land on the grid after resize?
// B: QComboBox popup          — internally-created popup: discover, composite, drive
// C: focus injection          — g_qttyFocus renders focus with no active window
// D: damage/diff              — is a keystroke's damage small? frame cost?
// E: QTextEdit via Channel B  — how bad is unassisted rich text, really?

#include "qtty_core.h"
#include <QElapsedTimer>

static int alignedCount(QWidget *root, int *total) {
    int ok = 0; *total = 0;
    for (QWidget *c : root->findChildren<QWidget*>()) {
        if (c->geometry().isNull() || !c->isVisible()) continue;
        ++*total;
        QRect g = c->geometry();
        if (!(g.x()%CW || g.y()%CH || g.width()%CW || g.height()%CH)) ++ok;
    }
    return ok;
}

static int diffCells(CellBuffer &a, CellBuffer &b) {
    if (a.cols() != b.cols() || a.rows() != b.rows()) return -1;
    int n = 0;
    for (int y = 0; y < a.rows(); ++y)
        for (int x = 0; x < a.cols(); ++x) {
            Cell &ca = a.at(x,y), &cb = b.at(x,y);
            if (ca.ch != cb.ch || ca.rev != cb.rev || ca.bold != cb.bold) ++n;
        }
    return n;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);

    QFont f("DejaVu Sans Mono"); f.setPixelSize(16);
    QFontMetrics fm(f);
    CW = fm.horizontalAdvance('M'); CH = fm.height();
    app.setFont(f);
    app.setStyle(new GridStyle);

    // ---------------------------------------------------------- A: resize
    hr("A: RESIZE / REFLOW");
    QDialog dlg;
    auto *v = new QVBoxLayout(&dlg);
    auto *edit  = new QLineEdit(&dlg);
    auto *combo = new QComboBox(&dlg);
    combo->addItems({"Alpha", "Beta", "Gamma"});
    auto *h = new QHBoxLayout;
    auto *b1 = new QPushButton("OK", &dlg);
    auto *b2 = new QPushButton("Cancel", &dlg);
    h->addStretch(1); h->addWidget(b1); h->addWidget(b2);
    v->addWidget(edit); v->addWidget(combo); v->addStretch(1); v->addLayout(h);
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.resize(40*CW, 10*CH);
    dlg.show();
    QCoreApplication::processEvents();

    int total = 0, ok = alignedCount(&dlg, &total);
    printf("at 40x10 cells: %d/%d children aligned\n", ok, total);
    for (QSize s : {QSize(64,14), QSize(30,8), QSize(80,24)}) {
        dlg.resize(s.width()*CW, s.height()*CH);
        QCoreApplication::processEvents();
        ok = alignedCount(&dlg, &total);
        printf("resize -> %dx%d cells: %d/%d aligned, button row at row %d (want %d)\n",
               s.width(), s.height(), ok, total,
               b1->geometry().y()/CH, s.height() - 4);
    }

    // ---------------------------------------------------------- B: combo popup
    hr("B: QComboBox POPUP (internally created — the style-not-subclass case)");
    combo->setFocus();
    combo->showPopup();
    QCoreApplication::processEvents();

    QWidget *container = combo->view() ? combo->view()->window() : nullptr;
    printf("popup container: %s  (activePopupWidget=%s)\n",
           container ? container->metaObject()->className() : "(none)",
           qApp->activePopupWidget() ? qApp->activePopupWidget()->metaObject()->className() : "(null)");
    if (container) {
        printf("container geometry %d,%d %dx%d px  visible=%d  WA_DontShowOnScreen=%d (inherited? %s)\n",
               container->x(), container->y(), container->width(), container->height(),
               container->isVisible(), container->testAttribute(Qt::WA_DontShowOnScreen),
               container->testAttribute(Qt::WA_DontShowOnScreen) ? "yes" : "NO - must be set by ChildAdded filter");
        CellBuffer buf(100, 30);
        renderWidget(&dlg, buf);
        renderWidget(container, buf, container->pos());
        printf("--- dialog + combo popup ---\n%s--- end ---\n", qPrintable(buf.toText()));
    }
    // drive it with keys: Down, Down, Enter -> should select Gamma
    QAbstractItemView *view = combo->view();
    for (int key : {Qt::Key_Down, Qt::Key_Down, Qt::Key_Return}) {
        QKeyEvent k(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(view, &k);
        QCoreApplication::processEvents();
    }
    printf("after Down,Down,Enter: currentText=\"%s\" (want Gamma)  popup closed=%s\n",
           qPrintable(combo->currentText()),
           (!combo->view()->window()->isVisible()) ? "YES" : "no");

    // ---------------------------------------------------------- C: focus injection
    hr("C: FOCUS INJECTION via g_qttyFocus (F4 mitigation)");
    dlg.resize(40*CW, 10*CH);
    QCoreApplication::processEvents();
    CellBuffer f1(40, 10), f2(40, 10);
    g_qttyFocus = nullptr;    renderWidget(&dlg, f1);
    g_qttyFocus = b1;         renderWidget(&dlg, f2);
    int fd = diffCells(f1, f2);
    bool revSeen = false;
    QRect bg = b1->geometry();
    for (int x = bg.x()/CW; x < (bg.x()+bg.width())/CW; ++x)
        if (f2.at(x, bg.y()/CH).rev) revSeen = true;
    printf("focus=none vs focus=OK-button: %d cells differ, reverse-video on button: %s\n",
           fd, revSeen ? "YES" : "no");
    g_qttyFocus = nullptr;

    // ---------------------------------------------------------- D: damage/diff
    hr("D: DAMAGE + FRAME COST");
    edit->setText("hello");
    CellBuffer d1(40, 10); renderWidget(&dlg, d1);
    edit->setText("hello!");
    CellBuffer d2(40, 10); renderWidget(&dlg, d2);
    printf("one keystroke in QLineEdit: %d of %d cells changed\n",
           diffCells(d1, d2), 40*10);

    QElapsedTimer t; t.start();
    const int N = 200;
    for (int i = 0; i < N; ++i) { CellBuffer fb(80, 24); renderWidget(&dlg, fb); }
    double perFrame = t.nsecsElapsed() / 1e6 / N;
    printf("full render() of dialog into 80x24: %.2f ms/frame (%d iterations)\n", perFrame, N);

    // ---------------------------------------------------------- E: QTextEdit
    hr("E: QTextEdit THROUGH CHANNEL B (expected: bad)");
    QTextEdit te;
    te.setAttribute(Qt::WA_DontShowOnScreen);
    te.setPlainText("The quick brown fox\njumps over the lazy dog.\nLine three is here.");
    te.resize(40*CW, 8*CH);
    te.show();
    QCoreApplication::processEvents();
    CellBuffer tb(44, 9);
    renderWidget(&te, tb);
    printf("--- QTextEdit, plain text ---\n%s--- end ---\n", qPrintable(tb.toText()));

    te.setHtml("<b>Bold</b> and <i>italic</i> and <span style='font-size:24px'>big</span> text.");
    QCoreApplication::processEvents();
    CellBuffer tb2(44, 9);
    renderWidget(&te, tb2);
    printf("--- QTextEdit, rich text ---\n%s--- end ---\n", qPrintable(tb2.toText()));

    return 0;
}
