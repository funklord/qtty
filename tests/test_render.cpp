// test_render — Gate-1 regression as a snapshot test (§9).
// Renders the §16 preferences dialog and compares against the committed
// fixture. Run with --record to (re)write the fixture after a reviewed
// rendering change.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using qtty::GridMetrics;

static QDialog *makeDialog() {
    auto *dlg = new QDialog;
    auto *v = new QVBoxLayout(dlg);
    auto *chk = new QCheckBox("Enable telemetry", dlg);
    chk->setChecked(true);
    v->addWidget(chk);
    auto *h = new QHBoxLayout;
    auto *r1 = new QRadioButton("Daily", dlg);
    auto *r2 = new QRadioButton("Weekly", dlg);
    r2->setChecked(true);
    h->addWidget(r1); h->addWidget(r2);
    v->addLayout(h);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    v->addWidget(bb);
    dlg->resize(GridMetrics::cells(48, 12));
    return dlg;
}

int main(int argc, char **argv) {
    qtty::prepareEnvironment();
    QApplication app(argc, argv);
    qtty::setup(app);

    QDialog *dlg = makeDialog();
    dlg->setAttribute(Qt::WA_DontShowOnScreen);
    dlg->show();
    QCoreApplication::processEvents();

    qtty::CellBuffer buf(52, 14);
    qtty::renderOnce(*dlg, buf);
    const QString got = buf.toText();

    const QString fixturePath = QStringLiteral(QTTY_SOURCE_DIR "/tests/snapshots/prefs_dialog.txt");
    if (argc > 1 && !qstrcmp(argv[1], "--record")) {
        QFile f(fixturePath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(got.toUtf8());
        printf("recorded %s\n", qPrintable(fixturePath));
        return 0;
    }

    QFile f(fixturePath);
    if (!f.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "FAIL: fixture missing (%s); run with --record\n",
                qPrintable(fixturePath));
        return 1;
    }
    const QString want = QString::fromUtf8(f.readAll());
    if (got != want) {
        fprintf(stderr, "FAIL: snapshot mismatch\n--- want ---\n%s--- got ---\n%s",
                qPrintable(want), qPrintable(got));
        return 1;
    }
    printf("PASS: snapshot matches (%d cells)\n", buf.cols() * buf.rows());
    return 0;
}
