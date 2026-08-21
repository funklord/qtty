// suite_render — Gate-1 regression as a snapshot (§9).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using qtty::GridMetrics;

int suite_render(bool record) {
    QDialog dlg;
    auto *v = new QVBoxLayout(&dlg);
    auto *chk = new QCheckBox("Enable telemetry", &dlg);
    chk->setChecked(true);
    v->addWidget(chk);
    auto *h = new QHBoxLayout;
    auto *r1 = new QRadioButton("Daily", &dlg);
    auto *r2 = new QRadioButton("Weekly", &dlg);
    r2->setChecked(true);
    h->addWidget(r1); h->addWidget(r2);
    v->addLayout(h);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(bb);
    dlg.setAttribute(Qt::WA_DontShowOnScreen);
    dlg.resize(GridMetrics::cells(48, 12));
    dlg.show();
    QCoreApplication::processEvents();

    const QString got = qtty::test::snapshotOf(dlg, 52, 14);
    int r = qtty::test::checkSnapshot(QStringLiteral(QTTY_SOURCE_DIR),
                                      QStringLiteral("prefs_dialog"), got, record);
    if (!r && !record) printf("PASS: snapshot matches\n");
    return r;
}
