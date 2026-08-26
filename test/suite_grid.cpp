// suite_grid -- alignment/reflow (F6), focus injection (F10), synthetic input (F4).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_grid() {
	fails = 0;
	const int cw = GridMetrics::cw();

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

	for (QSize s : {QSize(64, 14), QSize(80, 24), QSize(40, 10)}) {
		dlg.resize(GridMetrics::cells(s.width(), s.height()));
		QCoreApplication::processEvents();
		bool ok = true;
		for (QWidget *c : dlg.findChildren<QWidget *>()) {
			if (c->geometry().isNull() || !c->isVisible()) continue;
			if (!GridMetrics::isAligned(c->geometry())) ok = false;
		}
		CHECK(ok, qPrintable(QStringLiteral("aligned at %1x%2").arg(s.width()).arg(s.height())));
	}

	edit->setFocus(Qt::OtherFocusReason);
	QCoreApplication::processEvents();
	CHECK(dlg.focusWidget() == edit, "window->focusWidget() tracks setFocus (F4)");

	Qtty::CellBuffer f1(40, 10), f2(40, 10);
	Qtty::setFocusWidget(nullptr); Qtty::renderOnce(dlg, f1);
	Qtty::setFocusWidget(b1);      Qtty::renderOnce(dlg, f2);
	Qtty::setFocusWidget(nullptr);
	int d = f2.diffCells(f1);
	CHECK(d > 0 && d <= b1->width() / cw * 2, "focus injection dirties only the button");

	QKeyEvent k(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("x"));
	QApplication::sendEvent(edit, &k);
	CHECK(edit->text() == QStringLiteral("x"), "synthetic key edits QLineEdit");

	return fails;
}
