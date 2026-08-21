// test_grid — alignment, reflow, focus injection, inertness (§16 F4/F6, §10.1).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using qtty::GridMetrics;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv) {
    qtty::prepareEnvironment();
    QApplication app(argc, argv);

    // §10.1 inertness: before setup(), the library must have changed nothing.
    CHECK(!QString::fromLatin1(app.style()->metaObject()->className())
              .contains(QStringLiteral("GridStyle")),
          "inert before setup(): application style untouched");

    qtty::setup(app);
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    CHECK(cw > 0 && ch > 0, "setup(): metrics measured from font");
    CHECK(app.devicePixelRatio() == 1.0, "offscreen DPR is exactly 1");

    QDialog dlg;
    auto *v = new QVBoxLayout(&dlg);
    auto *edit = new QLineEdit(&dlg);
    auto *b1 = new QPushButton("OK", &dlg);
    auto *b2 = new QPushButton("Cancel", &dlg);
    auto *h = new QHBoxLayout;
    h->addStretch(1); h->addWidget(b1); h->addWidget(b2);
    v->addWidget(edit); v->addStretch(1); v->addLayout(h);
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.resize(GridMetrics::cells(40, 10));
    dlg.show();
    QCoreApplication::processEvents();

    // reflow keeps children on the grid at every size (F6)
    for (QSize s : {QSize(64, 14), QSize(80, 24), QSize(40, 10)}) {
        dlg.resize(GridMetrics::cells(s.width(), s.height()));
        QCoreApplication::processEvents();
        bool allAligned = true;
        for (QWidget *c : dlg.findChildren<QWidget *>()) {
            if (c->geometry().isNull() || !c->isVisible()) continue;
            if (!GridMetrics::isAligned(c->geometry())) allAligned = false;
        }
        CHECK(allAligned, qPrintable(QStringLiteral("all children aligned at %1x%2")
                                     .arg(s.width()).arg(s.height())));
    }

    // focus chain works without an active window (F4)
    edit->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(dlg.focusWidget() == edit, "window->focusWidget() tracks setFocus()");

    // focus injection changes exactly the focused button's cells (F10)
    qtty::CellBuffer f1(40, 10), f2(40, 10);
    qtty::setFocusWidget(nullptr); qtty::renderOnce(dlg, f1);
    qtty::setFocusWidget(b1);      qtty::renderOnce(dlg, f2);
    qtty::setFocusWidget(nullptr);
    int d = f2.diffCells(f1);
    CHECK(d > 0 && d <= b1->width() / cw * 2, "focus injection dirties only the button");

    // synthetic text entry reaches the focus widget (F4 core input path)
    QKeyEvent k(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("x"));
    QApplication::sendEvent(edit, &k);
    CHECK(edit->text() == QStringLiteral("x"), "synthetic key edits QLineEdit");

    printf(failures ? "%d FAILURE(S)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
