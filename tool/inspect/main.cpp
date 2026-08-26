// qtty-inspect -- dump a widget tree with cell geometry beside its rendering (section 9).
// Currently inspects a built-in sample; Phase 2 adds loading .ui files.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

int main(int argc, char **argv) {
	Qtty::prepareEnvironment();
	QApplication app(argc, argv);
	Qtty::setup(app);
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	QDialog dlg;
	auto *v = new QVBoxLayout(&dlg);
	auto *chk = new QCheckBox("Enable telemetry", &dlg);
	chk->setChecked(true);
	v->addWidget(chk);
	auto *edit = new QLineEdit("status: connected", &dlg);
	v->addWidget(edit);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	v->addWidget(bb);
	dlg.setAttribute(Qt::WA_DontShowOnScreen);
	dlg.resize(GridMetrics::cells(48, 10));
	dlg.show();
	QCoreApplication::processEvents();

	printf("widget tree (cell geometry, CW=%d CH=%d):\n", cw, ch);
	for (QWidget *c : dlg.findChildren<QWidget *>()) {
		if (c->geometry().isNull() || !c->isVisible()) continue;
		QRect g = c->geometry();
		printf("  %-24s cells %3d,%2d %3dx%-2d  %s\n",
		       c->metaObject()->className(),
		       g.x() / cw, g.y() / ch, g.width() / cw, g.height() / ch,
		       GridMetrics::isAligned(g) ? "aligned" : "MISALIGNED");
	}
	Qtty::CellBuffer buf(52, 12);
	Qtty::renderOnce(dlg, buf);
	printf("\nrendering:\n%s", qPrintable(buf.toText()));
	return 0;
}
