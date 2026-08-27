// suite_render -- Gate-1 regression as a snapshot (section 9).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

namespace {

// A widget that draws itself in cells rather than in pixels. Inherits QWidget
// first and the interface second, which is the only legal order: two QObject
// bases are illegal, which is why ICellPainted is not one.
class CellDrawn : public QWidget, public Qtty::ICellPainted {
public:
	using QWidget::QWidget;
	mutable int calls = 0;
	void paint_cells(Qtty::CellBuffer &buffer, const QRect &cells) const override {
		++calls;
		buffer.text(cells.left(), cells.top(), QStringLiteral("CELLS"));
	}
	// Ordinary painting, which must NOT happen while a cell render is running.
	// If it does it lands underneath, and a cell the interface left alone
	// shows Channel B output through it.
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.drawText(rect(), Qt::AlignLeft, QStringLiteral("PIXELS"));
	}
};

bool buffer_has(const Qtty::CellBuffer &b, const QString &needle) {
	return b.to_text().contains(needle);
}

} // namespace

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
	// The stretch is not decoration. This dialog is 12 cells tall and its
	// content needs 9, and QBoxLayout hands the slack to the items when
	// nothing can absorb it -- in shares that are not cell multiples, which
	// puts every row off the grid (measured: y = 33, 85, 137 against ch=19,
	// and a row at y=33 renders into row 1 rather than row 2). A stretch
	// gives the slack somewhere to go. GridGuard reports the difference.
	v->addStretch();
	v->addWidget(bb);
	dlg.setAttribute(Qt::WA_DontShowOnScreen);
	dlg.resize(GridMetrics::cells(48, 12));
	dlg.show();
	QCoreApplication::processEvents();

	const QString got = Qtty::test::snapshot_of(dlg, 52, 14);
	int r = Qtty::test::check_snapshot(QStringLiteral(QTTY_SOURCE_DIR),
	                                  QStringLiteral("prefs_dialog"), got, record);
	if (!r && !record) printf("PASS: snapshot matches\n");

	// ---- ICellPainted (section 5.3, risk R5) ---------------------------------
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(20, 4));
		auto *drawn = new CellDrawn(&host);
		drawn->setGeometry(0, 0, GridMetrics::cw() * 20, GridMetrics::ch() * 2);
		host.show();
		QCoreApplication::processEvents();

		Qtty::CellBuffer buf(20, 4);
		Qtty::render_once(host, buf);

		if (drawn->calls == 0) {
			printf("FAIL: ICellPainted widget was never asked to paint cells\n");
			++r;
		} else printf("PASS: an ICellPainted widget paints itself in cells\n");

		if (buffer_has(buf, QStringLiteral("CELLS")))
			printf("PASS: what it drew reached the buffer\n");
		else { printf("FAIL: what it drew reached the buffer\n"); ++r; }

		// The discriminating half. Channel B would put "PIXELS" there, and a
		// filter that painted cells WITHOUT consuming the event would leave
		// both -- passing the two checks above while still being wrong.
		if (!buffer_has(buf, QStringLiteral("PIXELS")))
			printf("PASS: its ordinary painting was skipped, not overdrawn\n");
		else { printf("FAIL: its ordinary painting was skipped, not overdrawn\n"); ++r; }

		// section 10.1's inertness rule, made testable. Outside a cell render
		// there is no active CellPaintDevice, so the filter must stand down and
		// the widget must paint the way it would with qtty absent. A filter
		// that consumed paint events unconditionally would take an
		// ICellPainted widget's rendering away in the GUI build -- the exact
		// failure the rule exists to forbid, and one nothing else here would
		// catch, because every other check runs inside a render.
		const int before = drawn->calls;
		QImage offscreen(drawn->size(), QImage::Format_ARGB32);
		offscreen.fill(Qt::transparent);
		drawn->render(&offscreen);
		if (drawn->calls == before)
			printf("PASS: outside a cell render the interface is not consulted\n");
		else { printf("FAIL: outside a cell render the interface is not consulted\n"); ++r; }
	}
	return r;
}
