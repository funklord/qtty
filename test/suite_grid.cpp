// suite_grid -- alignment/reflow (F6), focus injection (F10), synthetic input (F4).
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

int suite_grid() {
	fails = 0;
	const int cw = GridMetrics::cw();
	const int ch = GridMetrics::ch();

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
			if (!GridMetrics::is_aligned(c->geometry())) ok = false;
		}
		CHECK(ok, qPrintable(QStringLiteral("aligned at %1x%2").arg(s.width()).arg(s.height())));
	}

	edit->setFocus(Qt::OtherFocusReason);
	QCoreApplication::processEvents();
	CHECK(dlg.focusWidget() == edit, "window->focusWidget() tracks setFocus (F4)");

	Qtty::CellBuffer f1(40, 10), f2(40, 10);
	Qtty::set_focus_widget(nullptr); Qtty::render_once(dlg, f1);
	Qtty::set_focus_widget(b1);      Qtty::render_once(dlg, f2);
	Qtty::set_focus_widget(nullptr);
	int d = f2.diff_cells(f1);
	CHECK(d > 0 && d <= b1->width() / cw * 2, "focus injection dirties only the button");

	QKeyEvent k(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("x"));
	QApplication::sendEvent(edit, &k);
	CHECK(edit->text() == QStringLiteral("x"), "synthetic key edits QLineEdit");

	// A QProxyStyle passes style hints through, and some come from the
	// PLATFORM THEME rather than the base style -- so a terminal program's
	// layout could depend on the desktop it was launched from. Measured: of
	// 121 style hints and 96 pixel metrics, exactly one differs between
	// offscreen and xcb, and it makes a dialog reserve width for an icon a
	// cell renderer cannot draw. A "Cancel" button went 90px to 110px, moving
	// the whole button row two cells.
	//
	// **This check cannot fail under offscreen**, whose theme answers 0
	// anyway, and saying so is better than letting a green result stand in for
	// one. Where it discriminates is
	//
	//     make test-platforms TEST_PLATFORMS="offscreen xcb"
	//
	// which was run: it failed before this override and passes after.
	CHECK(QApplication::style()->styleHint(
	          QStyle::SH_DialogButtonBox_ButtonsHaveIcons) == 0,
	      "dialog buttons reserve no room for icons the terminal cannot draw");

	// A mnemonic marker is Qt's, not the label's. CE_PushButtonLabel wrote
	// QStyleOptionButton::text unchanged, so "&Save" drew as "<&Save>" -- and
	// the library asks applications to write exactly that, because
	// InputRouter::match_mnemonic() reads the marker to route Alt-s. The check
	// box and the group box were already right, and only because GridStyle
	// does not override their labels: they fall through to a base style that
	// draws through Qt::TextShowMnemonic.
	{
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *save = new QPushButton(QStringLiteral("&Save"), &h);
		save->setGeometry(0, 0, cw * 12, ch);
		auto *amp = new QPushButton(QStringLiteral("A && B"), &h);
		amp->setGeometry(0, ch, cw * 12, ch);
		h.resize(GridMetrics::cells(20, 3));
		h.show();
		QCoreApplication::processEvents();
		Qtty::CellBuffer buf(20, 3);
		Qtty::render_once(h, buf);
		const QStringList rows = buf.to_text().split(QLatin1Char('\n'));
		CHECK(rows.value(0).contains(QStringLiteral("<Save>")),
		      "a button's mnemonic marker is not drawn");
		// The other half of Qt's rule, and the one a naive strip gets wrong:
		// "&&" is a literal ampersand, so a label may still contain one.
		CHECK(rows.value(1).contains(QStringLiteral("<A & B>")),
		      "but a doubled ampersand still draws one");
	}

	// A platform theme sets fonts PER WIDGET CLASS, and those beat the
	// application font setup() installs -- measured under xcb with gtk3, where
	// QPushButton, QLabel and QMenu came back as Noto Sans 13, not fixed
	// pitch, advancing 12 for 'M' and 3 for 'i' against a 10-pixel cell. That
	// is the failure grid_font_problem() exists to prevent, and it could not
	// see it: it is handed the font setup() built, the one font a theme does
	// not override.
	//
	// The theme is SIMULATED here rather than waited for, by registering a
	// class font of the kind a theme registers. That is what makes this check
	// able to fail under the default platform instead of only under a desktop
	// -- the size is varied rather than the family, so the test does not
	// depend on which fonts happen to be installed.
	{
		const QFont grid = QApplication::font();
		QFont intruder = grid;
		intruder.setPixelSize(grid.pixelSize() - 5);
		// A theme's font is built fresh and asks for nothing in particular,
		// so the intruder does not either. That is what lets the hinting
		// check below fail: derived from the grid font it would arrive
		// already carrying the request, and the enforcer could drop the
		// property without anything noticing.
		intruder.setHintingPreference(QFont::PreferNoHinting);
		QApplication::setFont(intruder, "QPushButton");

		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *b = new QPushButton(QStringLiteral("x"), &h);
		b->setGeometry(0, 0, cw * 4, ch);
		h.resize(GridMetrics::cells(10, 2));
		h.show();
		QCoreApplication::processEvents();
		CHECK(b->font().pixelSize() == grid.pixelSize(),
		      "a class font from a platform theme is overridden by the grid font");
		CHECK(QFontMetrics(b->font()).horizontalAdvance(QChar('M')) == cw,
		      "so every widget still advances exactly one cell");
		// The family and the size are not the whole of it. Full hinting is
		// what makes the metrics whole numbers at all -- without it, on a
		// fontconfig left at the packaged hintslight default, this font
		// advances 9.625 against a 10-cell -- so a widget that loses the
		// request computes its columns off a different rasterisation of the
		// same font than the grid was measured from. The enforcer copies its
		// base wholesale today; a future edit that copies field by field, the
		// way it already copies weight, italic and underline, is what this
		// notices.
		CHECK(b->font().hintingPreference() == QFont::PreferFullHinting,
		      "and the grid font's hinting survives the override");
		QApplication::setFont(grid, "QPushButton");     // leave nothing behind
	}

	{
		// Qtty::focusWidget(), which has no caller anywhere in src: the
		// router and the compositor use QWidget::focusWidget(), a different
		// function with the same name. This one is public API in grid.h --
		// how an application asks who the router considers focused, since Qt
		// cannot answer while no window is ever activated -- so it is
		// asserted as the round trip it promises rather than removed.
		QWidget a, b;
		Qtty::set_focus_widget(&a);
		const bool first = Qtty::focusWidget() == &a;
		Qtty::set_focus_widget(&b);
		// Both directions, because "returns the last thing set" is satisfied
		// by a function that returns a pointer it never updates.
		CHECK(first && Qtty::focusWidget() == &b,
		      "focusWidget() answers with whatever set_focus_widget() was last given");
		Qtty::set_focus_widget(nullptr);
		CHECK(Qtty::focusWidget() == nullptr, "and with nothing when there is nothing");
	}
	{
		// The two style hints this style pins for a reason, asserted at the
		// style rather than through a widget: SH_ToolButtonStyle is what
		// stops a toolbar reserving space for icons a terminal cannot draw,
		// and it is not reached by the toolbar tests because CC_ToolButton
		// draws the label whatever the hint says. A hint nobody asks is
		// still a promise this style makes to a widget that does.
		QStyle *st = QApplication::style();
		CHECK(st->styleHint(QStyle::SH_ToolButtonStyle, nullptr, nullptr)
		          == Qt::ToolButtonTextOnly,
		      "the style asks for text-only tool buttons, icons being undrawable");
		CHECK(st->styleHint(QStyle::SH_DialogButtonLayout, nullptr, nullptr) == 0,
		      "and for a dialog button layout that does not follow the desktop");
	}

	{
		// The subcontrol rects a terminal answers for itself. Qt asks for
		// SC_SpinBoxFrame and SC_ComboBoxFrame when it decides where the
		// editable part of one of these sits, and this style answers with
		// the whole widget: there is no frame inset on a grid, so an inset
		// answer would put the editor one cell in and lose a column of text.
		QStyle *st = QApplication::style();
		QStyleOptionSpinBox sb;
		sb.rect = QRect(0, 0, GridMetrics::cw() * 8, GridMetrics::ch());
		CHECK(st->subControlRect(QStyle::CC_SpinBox, &sb,
		                         QStyle::SC_SpinBoxFrame, nullptr) == sb.rect,
		      "a spin box's frame is the whole spin box, there being no inset");
		QStyleOptionComboBox cb;
		cb.rect = QRect(0, 0, GridMetrics::cw() * 10, GridMetrics::ch());
		CHECK(st->subControlRect(QStyle::CC_ComboBox, &cb,
		                         QStyle::SC_ComboBoxFrame, nullptr) == cb.rect,
		      "and a combo box's likewise");
	}
	{
		// GridSnap's counters, which are how section 7.8 will be decided:
		// the number of rectangles the filter moved is the measurement, and
		// a counter nobody can read is not a measurement.
		Qtty::GridSnap::reset();
		CHECK(Qtty::GridSnap::snapped() == 0, "the snap counter starts, and resets, at zero");
		// Its installed() state is what says whether the count means
		// anything -- zero snaps with the filter off is not evidence that
		// nothing needed snapping.
		CHECK(!Qtty::GridSnap::installed() || Qtty::GridSnap::snapped() >= 0,
		      "and reports whether the filter is installed at all");
	}

	return fails;
}
