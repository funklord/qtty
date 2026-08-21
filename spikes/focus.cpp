#include <QtWidgets>
#include <cstdio>
struct Probe : QDialog { using QDialog::focusNextPrevChild; };
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QDialog dlg;
    auto *v = new QVBoxLayout(&dlg);
    auto *e1 = new QLineEdit("one", &dlg);
    auto *e2 = new QLineEdit("two", &dlg);
    auto *b  = new QPushButton("go", &dlg);
    v->addWidget(e1); v->addWidget(e2); v->addWidget(b);
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.show();
    QCoreApplication::processEvents();

    e1->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    printf("qApp->focusWidget()      = %s\n", qApp->focusWidget() ? qApp->focusWidget()->metaObject()->className() : "(null)");
    printf("dlg.focusWidget()        = %s  (obj=%s)\n",
           dlg.focusWidget() ? dlg.focusWidget()->metaObject()->className() : "(null)",
           dlg.focusWidget() == e1 ? "e1 CORRECT" : "not e1");
    printf("e1->hasFocus()           = %d\n", e1->hasFocus());

    // does the focus CHAIN still navigate?
    bool ok = static_cast<Probe&>(dlg).focusNextPrevChild(true);
    printf("focusNextPrevChild(true) = %d -> dlg.focusWidget()=%s (%s)\n", ok,
           dlg.focusWidget() ? dlg.focusWidget()->metaObject()->className() : "(null)",
           dlg.focusWidget() == e2 ? "e2 CORRECT" : "not e2");
    ok = static_cast<Probe&>(dlg).focusNextPrevChild(true);
    printf("focusNextPrevChild(true) = %d -> dlg.focusWidget()=%s (%s)\n", ok,
           dlg.focusWidget() ? dlg.focusWidget()->metaObject()->className() : "(null)",
           dlg.focusWidget() == b ? "button CORRECT" : "not button");

    // shortcut with the window "active" forced via focusWidget of the window
    int fired = 0;
    QAction *a = new QAction(&dlg);
    a->setShortcut(QKeySequence("Ctrl+S"));
    a->setShortcutContext(Qt::ApplicationShortcut);
    dlg.addAction(a);
    QObject::connect(a, &QAction::triggered, [&]{ fired++; });
    QKeyEvent k(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, "\x13");
    QApplication::sendEvent(dlg.focusWidget() ? dlg.focusWidget() : &dlg, &k);
    QCoreApplication::processEvents();
    printf("shortcut via window focusWidget target -> fired=%d\n", fired);

    // manual QAction resolution (the fallback the design would use)
    int manual = 0;
    for (QAction *act : dlg.actions())
        if (act->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S)) { act->trigger(); manual++; }
    printf("manual QAction resolution              -> matched=%d fired=%d\n", manual, fired);
    return 0;
}
