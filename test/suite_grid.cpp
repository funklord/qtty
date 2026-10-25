// suite_grid -- alignment/reflow (F6), focus injection (F10), synthetic input (F4).
#include <qtty/qtty.h>
#include "src/cell_geometry.h"
#include <QtWidgets>
#include <cstdio>

using Qtty::GridMetrics;
using Qtty::text_cells;

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

		// And with nothing when the widget is DESTROYED, which is a different
		// question from the line above and the one a bare pointer gets wrong.
		// This state is process-wide and refreshed only on input, so the gap
		// it must survive is a widget destroyed by something that is not
		// input -- a timer, a reply, an application closing its own dialog --
		// and FrameScheduler's idle tick renders in exactly that window. It
		// never crashed, because every internal use is a comparison; what a
		// stale pointer buys is a NEW widget at the same address comparing
		// equal and drawing itself focused while focus is elsewhere. Qt
		// reuses heap addresses freely.
		auto *gone = new QWidget;
		Qtty::set_focus_widget(gone);
		const bool held = Qtty::focusWidget() == gone;
		delete gone;
		CHECK(held && Qtty::focusWidget() == nullptr,
		      "and with nothing once the widget it named is destroyed");
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
		// nothing needed snapping. That sentence was here already and the
		// check under it closed nothing: `!installed() || snapped() >= 0` is
		// true for every state of both, because s_snapped starts at zero and
		// only ever increments, so the right operand cannot be false.
		// Removing GridSnap::install() from setup() left it green. Reported
		// by a peer session sweeping every tree for gates that pass having
		// examined nothing, and confirmed here before being taken.
		//
		// So the counter is shown COUNTING instead. The same four-button
		// layout suite_runtime uses is known to leave its children off the
		// grid with the filter removed, which is what makes a non-zero count
		// here mean the filter did the moving.
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *row = new QHBoxLayout(&host);
		for (int i = 0; i < 4; ++i)
			row->addWidget(new QPushButton(QStringLiteral("b%1").arg(i)));
		host.resize(GridMetrics::cells(40, 12));
		host.show();
		QCoreApplication::processEvents();
		row->activate();
		QCoreApplication::processEvents();
		CHECK(Qtty::GridSnap::installed() && Qtty::GridSnap::snapped() > 0,
		      "and counts the geometries the installed filter moved");
		// The eight violations that layout just produced are deliberate and
		// are discarded here, the way suite_router discards a splitter's.
		// They exist because of an ORDER that is the test binary's and not the
		// library's: main.cpp installs GridGuard AFTER Qtty::setup(), so the
		// guard is the most recently installed filter and runs FIRST -- it
		// sees what the layout assigned, before GridSnap moves it. In an
		// application the guard goes in inside setup(), ahead of GridSnap, and
		// reads the corrected geometry instead. Which is also why this is the
		// only place in the suite that has to say so: everything after
		// suite_runtime re-installs GridSnap runs in the other order.

		Qtty::GridGuard::reset();
	}

	{
		// CT_ToolButton handed an option that is NOT a QStyleOptionToolButton.
		// Qt always passes the subclass, so this is the defensive tail of that
		// case -- and it was the last uncovered line in the file. What it must
		// not do is fall back to a PIXEL height: the contract of every metric
		// here is a cell-high answer whatever it was handed.
		QStyle *style = QApplication::style();
		QStyleOption plain;
		const QSize got = style->sizeFromContents(QStyle::CT_ToolButton, &plain,
		                                          QSize(37, 91), nullptr);
		CHECK(got.height() == GridMetrics::ch(),
		      "an unrecognised tool-button option still measures one cell tall");
		// Not "the width it was given": `width` there is the PROXIED answer
		// rounded up to a whole cell, not the caller's contents size. The
		// first version of this check asserted the pass-through and failed,
		// which is the check finding the reader's mistake rather than the
		// code's. The invariant worth holding is the one the whole function
		// exists for.
		CHECK(got.width() % GridMetrics::cw() == 0,
		      "and answers a whole number of cells wide");
	}

	// EVERY CONTENTS TYPE, WHICH ONE DEFENSIVE CASE WAS STANDING IN FOR.
	// The block above asks CT_ToolButton with an option it cannot cast and
	// states the contract in as many words -- "the contract of every metric
	// here is a cell-high answer whatever it was handed" -- and nothing
	// asked the other twenty types at all. sizeFromContents() is the
	// surface every LAYOUT consumes, so a pixel answer here does not stay
	// in the style: it becomes a widget geometry, and GridSnap then rounds
	// it to the nearest cell, which is how the group box's children came to
	// draw through its own border in 8.428.
	//
	// Three questions, and only two of them are invariants. Measured
	// 2026-10-22 over 176 answers, 21 contents types at eight contents
	// sizes: 0 off the grid, 0 that shrink as the contents grow.
	//
	// THE THIRD IS NOT AN INVARIANT and is recorded so that nobody adds it.
	// 42 answers are SMALLER than the contents they were handed, and every
	// one is correct. Most are the height: a control holding one line of
	// text answers one cell tall whatever it is given, which is the first
	// case in that switch and deliberate. The rest are Qt's own semantics --
	// QCommonStyle's CT_HeaderSection and CT_MdiControls ignore
	// contentsSize entirely, and this style's CT_ToolButton and
	// CT_ItemViewItem answer a width counted in CHARACTERS, which is the
	// premise the whole file rests on. CT_SizeGrip answers 0x0 on purpose.
	//
	// The positive control is the base style, which fails the first
	// question on the same 176 asks -- so this is a comparison that can
	// come out either way rather than a number that cannot move.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStyle *st = QApplication::style();
		QStyle *plain = QStyleFactory::create(QStringLiteral("Fusion"));
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 10));
		host.show();
		QCoreApplication::processEvents();
		int sfc_asked = 0, base_askew = 0;
		QStringList sfc_askew, sfc_backward;
		const auto sweep = [&](const QString &name, QStyle::ContentsType ct,
		                       const QStyleOption *o) {
			QSize prev;
			bool first = true;
			for (QSize cs : { QSize(0, 0), QSize(1, 1), QSize(7, 3),
			                  QSize(10, 19), QSize(33, 19), QSize(100, 19),
			                  QSize(100, 40), QSize(101, 41) }) {
				const QSize g = st->sizeFromContents(ct, o, cs, &host);
				++sfc_asked;
				if (g.width() % cw != 0 || g.height() % ch != 0)
					sfc_askew << name + QStringLiteral(" -> %1x%2")
					                      .arg(g.width()).arg(g.height());
				if (!first && (g.width() < prev.width()
				               || g.height() < prev.height()))
					sfc_backward << name + QStringLiteral(" %1x%2 -> %3x%4")
					                         .arg(prev.width())
					                         .arg(prev.height())
					                         .arg(g.width()).arg(g.height());
				if (plain) {
					const QSize b = plain->sizeFromContents(ct, o, cs, &host);
					if (b.width() % cw != 0 || b.height() % ch != 0)
						++base_askew;
				}
				prev = g;
				first = false;
			}
		};
		QStyleOptionButton b;
		b.text = QStringLiteral("Hi there");
		b.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("pushbutton"), QStyle::CT_PushButton, &b);
		sweep(QStringLiteral("checkbox"), QStyle::CT_CheckBox, &b);
		sweep(QStringLiteral("radio"), QStyle::CT_RadioButton, &b);
		QStyleOptionToolButton tb;
		tb.text = QStringLiteral("Cut");
		tb.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("toolbutton"), QStyle::CT_ToolButton, &tb);
		QStyleOptionComboBox cb;
		cb.currentText = QStringLiteral("alpha");
		cb.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("combobox"), QStyle::CT_ComboBox, &cb);
		QStyleOptionSpinBox sp;
		sp.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("spinbox"), QStyle::CT_SpinBox, &sp);
		QStyleOptionFrame fr;
		fr.rect = QRect(0, 0, 200, ch);
		fr.lineWidth = 1;
		sweep(QStringLiteral("lineedit"), QStyle::CT_LineEdit, &fr);
		QStyleOptionTab tab;
		tab.text = QStringLiteral("One");
		tab.rect = QRect(0, 0, 100, ch);
		sweep(QStringLiteral("tab"), QStyle::CT_TabBarTab, &tab);
		QStyleOptionMenuItem mi;
		mi.text = QStringLiteral("Open");
		mi.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("menuitem"), QStyle::CT_MenuItem, &mi);
		sweep(QStringLiteral("menubaritem"), QStyle::CT_MenuBarItem, &mi);
		sweep(QStringLiteral("menu"), QStyle::CT_Menu, &mi);
		QStyleOptionViewItem vi;
		vi.text = QStringLiteral("Row");
		vi.rect = QRect(0, 0, 200, ch);
		vi.features = QStyleOptionViewItem::HasDisplay;
		sweep(QStringLiteral("item"), QStyle::CT_ItemViewItem, &vi);
		QStyleOptionHeader hd;
		hd.text = QStringLiteral("Col");
		hd.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("header"), QStyle::CT_HeaderSection, &hd);
		QStyleOptionSlider sl;
		sl.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("slider"), QStyle::CT_Slider, &sl);
		sweep(QStringLiteral("scrollbar"), QStyle::CT_ScrollBar, &sl);
		QStyleOptionProgressBar pb;
		pb.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("progressbar"), QStyle::CT_ProgressBar, &pb);
		QStyleOptionGroupBox gb;
		gb.text = QStringLiteral("T");
		gb.rect = QRect(0, 0, 200, 4 * ch);
		sweep(QStringLiteral("groupbox"), QStyle::CT_GroupBox, &gb);
		QStyleOption any;
		any.rect = QRect(0, 0, 200, ch);
		sweep(QStringLiteral("splitter"), QStyle::CT_Splitter, &any);
		sweep(QStringLiteral("tabwidget"), QStyle::CT_TabWidget, &any);
		sweep(QStringLiteral("mdicontrols"), QStyle::CT_MdiControls, &any);
		sweep(QStringLiteral("sizegrip"), QStyle::CT_SizeGrip, &any);
		sweep(QStringLiteral("menubar"), QStyle::CT_MenuBar, &any);
		delete plain;
		sfc_askew.sort();
		sfc_askew.removeDuplicates();
		sfc_backward.sort();
		printf("info: %d sizeFromContents answer(s): %d off the grid, %d"
		       " shrinking as the contents grow; the base style has %d off"
		       " the grid%s%s\n", sfc_asked, int(sfc_askew.size()),
		       int(sfc_backward.size()), base_askew,
		       sfc_askew.isEmpty() ? "" : ": ",
		       qPrintable(sfc_askew.join(QStringLiteral(", "))));
		CHECK(sfc_asked > 0 && base_askew > 0 && sfc_askew.isEmpty(),
		      "every size this style measures is a whole number of cells,"
		      " on every contents type, where the base style's are not");
		// THE SECOND CHECK IS NOT DEFENDED BY A SABOTAGE and says so.
		// Monotonicity here is held jointly: the base style's own answers
		// rise with the contents, and the rounding above is monotonic, so
		// both entries that break the rounding leave this green --
		// measured, not assumed. It is kept because two arms compute their
		// own width from a LABEL rather than through that rounding,
		// CT_TabBarTab and CT_ToolButton, and a threshold rule in either
		// -- which is exactly the shape 8.471 found in the tool button's
		// drawing, where three cells carried less than two -- would break
		// it where nothing else would notice.
		CHECK(sfc_backward.isEmpty(),
		      "and none of them shrinks as the contents it must hold grow,"
		      " which is the non-monotonicity 8.471 met in the drawing");
	}

	// AND THE SIZE HOLDS THE LABEL IT WILL BE DRAWN WITH, which the sweep
	// above cannot ask: every answer there is a whole number of cells and
	// rises with the contents, and both were true while four controls were
	// a cell or two short of their own text.
	//
	// Qt measures a label by the FONT's advance and a two-column glyph does
	// not advance by two cells, so a size rounded up from Qt's pixels is
	// short for wide text. Measured 2026-10-22 with a six-character CJK
	// label -- twelve cells -- each widget clipped to its own hint:
	//
	//     push button   13 given, 14 wanted, a character lost to `...`
	//     check box     15 given, 16 wanted, the last character lost
	//     radio         15 given, 16 wanted, the same
	//     tab           12 given, 14 wanted, TWO characters lost
	//
	// ASKED OF THE STYLE rather than of a rendered widget, because the
	// style is what this library owns: the width it answers must hold the
	// label plus the chrome it draws round it. The ASCII row is the
	// control, and it is the half that says the floor did not simply make
	// everything wider -- Qt's own width for an eight-cell ASCII button is
	// eleven cells where the floor is ten, so the floor must not show
	// there.
	//
	// A QLabel is deliberately absent: its sizeHint is Qt's own, built
	// from the same metrics and reaching no style, and the same measurement
	// found it losing a character too. That is section 0b's question about
	// what a font's wide glyphs must advance by.
	{
		const int cw = GridMetrics::cw();
		QStyle *st = QApplication::style();
		QString cjk;
		for (int k = 0; k < 6; ++k) cjk += QChar(0x4fdd + k);
		int short_of = 0, asked = 0;
		const auto ask = [&](const QString &what, QStyle::ContentsType ct,
		                     const QStyleOption *o, int chrome,
		                     const QString &label) {
			const QSize got = st->sizeFromContents(ct, o, QSize(), nullptr);
			const int want = (text_cells(label) + chrome) * cw;
			++asked;
			if (got.width() < want) {
				++short_of;
				printf("info:   %s: %d cell(s) for a label wanting %d\n",
				       qPrintable(what), got.width() / cw, want / cw);
			}
			return got.width();
		};
		for (int wide = 0; wide < 2; ++wide) {
			const QString label = wide ? cjk : QStringLiteral("SixChars");
			QStyleOptionButton b;
			b.text = label;
			b.rect = QRect(0, 0, 200, GridMetrics::ch());
			const int push = ask(QStringLiteral("push button"),
			                     QStyle::CT_PushButton, &b, 2, label);
			ask(QStringLiteral("check box"), QStyle::CT_CheckBox, &b, 4,
			    label);
			ask(QStringLiteral("radio"), QStyle::CT_RadioButton, &b, 4,
			    label);
			QStyleOptionTab tab;
			tab.text = label;
			tab.shape = QTabBar::RoundedNorth;
			tab.rect = QRect(0, 0, 200, GridMetrics::ch());
			ask(QStringLiteral("tab"), QStyle::CT_TabBarTab, &tab, 2, label);
			QStyleOptionToolButton tb;
			tb.text = label;
			tb.rect = QRect(0, 0, 200, GridMetrics::ch());
			ask(QStringLiteral("tool button"), QStyle::CT_ToolButton, &tb, 2,
			    label);
			// The menu bar item and the heading, which the same
			// measurement found short: four characters and an ellipsis
			// each. Their chrome is one leading cell, which is what
			// CE_MenuBarItem and CE_HeaderLabel budget for.
			QStyleOptionMenuItem mi;
			mi.text = label;
			mi.rect = QRect(0, 0, 200, GridMetrics::ch());
			ask(QStringLiteral("menu bar item"), QStyle::CT_MenuBarItem,
			    &mi, 1, label);
			// And the menu's own item, in its three chrome shapes, which
			// is what makes the model of that chrome asserted rather than
			// assumed: a plain item indents by one, a checkable one adds
			// two for the mark, and a submenu one adds a cell for its
			// arrow. Qt's own answer was a cell short of the plainest of
			// the three for a wide label.
			ask(QStringLiteral("menu item"), QStyle::CT_MenuItem, &mi, 1,
			    label);
			QStyleOptionMenuItem mc = mi;
			mc.checkType = QStyleOptionMenuItem::NonExclusive;
			ask(QStringLiteral("checkable item"), QStyle::CT_MenuItem, &mc,
			    3, label);
			QStyleOptionMenuItem ms = mi;
			ms.menuItemType = QStyleOptionMenuItem::SubMenu;
			ask(QStringLiteral("submenu item"), QStyle::CT_MenuItem, &ms, 2,
			    label);
			QStyleOptionHeader hd;
			hd.text = label;
			hd.orientation = Qt::Horizontal;
			hd.rect = QRect(0, 0, 200, GridMetrics::ch());
			ask(QStringLiteral("header"), QStyle::CT_HeaderSection, &hd, 1,
			    label);
			(void)push;
		}
		// THE CONTROL, and it has to be asked of a real widget. Asked of
		// the style with an empty contents size the base style has nothing
		// to work from, so the floor decides every answer and the control
		// says "AT the floor" about the ASCII case too -- which is this
		// fixture being unfaithful rather than the floor being wrong.
		// QPushButton::sizeHint() passes the contents size Qt measured,
		// which is the number the floor is meant to sit under.
		int above = 0, at = 0;
		for (int wide = 0; wide < 2; ++wide) {
			const QString label = wide ? cjk : QStringLiteral("SixChars");
			QPushButton b(label);
			b.setAttribute(Qt::WA_DontShowOnScreen);
			b.show();
			QCoreApplication::processEvents();
			const int hint = b.sizeHint().width();
			const int floor_px = (text_cells(label) + 2) * cw;
			if (hint > floor_px) ++above;
			if (hint == floor_px) ++at;
			printf("info:   a %s button hints %d cell(s), the floor is"
			       " %d\n", wide ? "CJK" : "ascii", hint / cw,
			       floor_px / cw);
		}
		printf("info: %d of %d label size(s) are short of the cells they"
		       " will be drawn in\n", short_of, asked);
		CHECK(asked == 20 && short_of == 0,
		      "every size this style answers for a control it draws a label"
		      " in holds that label in cells, wide characters included");
		CHECK(above == 1 && at == 1,
		      "and the floor decides the size only where Qt's own"
		      " measurement is short of it, an ascii button already being"
		      " wider than its label needs");
	}

	// THE CELL IS THE UNIT, ASKED OF EVERY ANSWER THIS STYLE GIVES. The
	// grid is this library's central promise, and the way it breaks is a
	// pixel constant: a 4, a 16, a 24 that means something at a 10x19 cell
	// and nothing at any other. `make layout` forbids those syntactically
	// and cannot see one computed, so this asks the question numerically --
	// double the cell and every answer doubles, or is zero, or is a count.
	//
	// Three surfaces, 72 answers between them, and the richest is this one:
	// the parts of a spin box, a scroll bar and a slider are where the
	// arithmetic actually lives.
	//
	// WHAT THIS DOES NOT COVER, said here so nobody reads it as more than
	// it is: the font. A second cell size needs a font whose metrics match
	// it -- section 7.9 counts 7 of 102 installed families giving the 10x19
	// this one assumes -- and these checks move the CELL while the font
	// stays, which is exactly right for arithmetic and exactly wrong for
	// anything that renders. Nothing below renders.
	//
	// The widget is rebuilt at each cell size and asked for its own option,
	// because a hand-built QStyleOptionSlider is a pair Qt never produces --
	// a fix was written and reverted on that in this tree.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		const auto in_cells = [](const QRect &r, int w, int h) {
			return QRect(r.x() / w, r.y() / h, r.width() / w, r.height() / h);
		};
		struct Probe : QSpinBox { using QSpinBox::QSpinBox;
			QStyleOptionSpinBox real() const {
				QStyleOptionSpinBox o; initStyleOption(&o); return o; } };
		struct Bar : QScrollBar { using QScrollBar::QScrollBar;
			QStyleOptionSlider real() const {
				QStyleOptionSlider o; initStyleOption(&o); return o; } };
		struct Sl : QSlider { using QSlider::QSlider;
			QStyleOptionSlider real() const {
				QStyleOptionSlider o; initStyleOption(&o); return o; } };
		int same = 0, differ = 0;
		const auto compare = [&](const char *what,
		                         std::function<QRect(int,int)> ask) {
			GridMetrics::set(cw, ch);
			const QRect a = in_cells(ask(cw, ch), cw, ch);
			GridMetrics::set(cw * 2, ch * 2);
			const QRect b = in_cells(ask(cw * 2, ch * 2), cw * 2, ch * 2);
			GridMetrics::set(cw, ch);
			if (a == b) { ++same; return; }
			++differ;
			printf("probe: %-26s %d,%d %dx%d -> %d,%d %dx%d\n", what,
			       a.x(), a.y(), a.width(), a.height(),
			       b.x(), b.y(), b.width(), b.height());
		};
		struct Part { QStyle::ComplexControl cc; QStyle::SubControl sc;
		              const char *name; int kind; };
		static const Part parts[] = {
			{ QStyle::CC_SpinBox, QStyle::SC_SpinBoxUp,        "spin up",    0 },
			{ QStyle::CC_SpinBox, QStyle::SC_SpinBoxDown,      "spin down",  0 },
			{ QStyle::CC_SpinBox, QStyle::SC_SpinBoxEditField, "spin edit",  0 },
			{ QStyle::CC_ScrollBar, QStyle::SC_ScrollBarSubLine, "bar sub",  1 },
			{ QStyle::CC_ScrollBar, QStyle::SC_ScrollBarSlider,  "bar thumb",1 },
			{ QStyle::CC_ScrollBar, QStyle::SC_ScrollBarAddLine, "bar add",  1 },
			{ QStyle::CC_Slider, QStyle::SC_SliderHandle,      "slider handle",2 },
			{ QStyle::CC_Slider, QStyle::SC_SliderGroove,      "slider groove",2 },
		};
		for (const Part &pt : parts) {
			compare(pt.name, [&](int w, int h) -> QRect {
				if (pt.kind == 0) {
					Probe sp; sp.setAttribute(Qt::WA_DontShowOnScreen);
					sp.resize(w * 12, h * 1); sp.show();
					QCoreApplication::processEvents();
					const QStyleOptionSpinBox o = sp.real();
					return QApplication::style()->subControlRect(
					    pt.cc, &o, pt.sc, &sp);
				}
				if (pt.kind == 1) {
					Bar b(Qt::Horizontal);
					b.setAttribute(Qt::WA_DontShowOnScreen);
					b.setRange(0, 100); b.setValue(50);
					b.resize(w * 12, h * 1); b.show();
					QCoreApplication::processEvents();
					const QStyleOptionSlider o = b.real();
					return QApplication::style()->subControlRect(
					    pt.cc, &o, pt.sc, &b);
				}
				Sl sl(Qt::Horizontal);
				sl.setAttribute(Qt::WA_DontShowOnScreen);
				sl.setRange(0, 10); sl.setValue(5);
				sl.resize(w * 12, h * 1); sl.show();
				QCoreApplication::processEvents();
				const QStyleOptionSlider o = sl.real();
				return QApplication::style()->subControlRect(
				    pt.cc, &o, pt.sc, &sl);
			});
		}
		printf("info: %d complex-control part(s) asked at two cell sizes:"
		       " %d the same in cells, %d not\n",
		       int(sizeof(parts) / sizeof(parts[0])), same, differ);
		CHECK(differ == 0 && same == int(sizeof(parts) / sizeof(parts[0])),
		      "every part of a spin box, a scroll bar and a slider lands on"
		      " the same cells when the cell doubles, there being no pixel"
		      " constant inside the arithmetic");
	}

	// A CELL OF ZERO, which was a CRASH rather than a bad answer.
	// GridMetrics::set() is public -- setup() is its caller but nothing
	// stops an application calling it -- and the library divides by those
	// two numbers in 83 places, plus the modulo in is_aligned() in the same
	// header. Measured 2026-10-21: after set(0, 0), set(0, 19) or
	// set(10, 0), is_aligned() died on SIGFPE before anything rendered.
	//
	// A negative pair was worse in kind though not in effect: it survived
	// and answered nonsense, cells(2, 2) coming back -20x-38 and
	// is_aligned() saying TRUE of a rectangle aligned to nothing. A wrong
	// answer outlives a stop.
	//
	// The cell is restored at the end, because this is process-wide state
	// and the sweeps below depend on it -- the same care the two-cell-size
	// sweep takes.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		int refused = 0, kept = 0, answered = 0;
		for (QPoint bad : { QPoint(0, 0), QPoint(0, 19), QPoint(10, 0),
		                    QPoint(-10, -19), QPoint(-1, 19) }) {
			GridMetrics::set(bad.x(), bad.y());
			++refused;
			if (GridMetrics::cw() == cw && GridMetrics::ch() == ch) ++kept;
			// Asked INSIDE the loop, which is the reproduction rather
			// than a tidier place to put it: is_aligned() is the call
			// that died, and it has to be made while the bad pair is
			// the one in force. Asked after the loop it would be safe
			// whatever the setter did, the last pair carrying no zero.
			const QSize two = GridMetrics::cells(2, 2);
			if (GridMetrics::is_aligned(QRect(0, 0, 2 * cw, 2 * ch))
			    && two.width() > 0 && two.height() > 0) ++answered;
		}
		GridMetrics::set(cw, ch);
		printf("info: %d of %d non-positive cell sizes were refused, and the"
		       " grid is still %dx%d\n", kept, refused, GridMetrics::cw(),
		       GridMetrics::ch());
		CHECK(refused == kept && GridMetrics::cw() == cw
		      && GridMetrics::ch() == ch,
		      "a cell of zero or less is refused and the working one kept,"
		      " because the whole library divides by it");
		CHECK(answered == refused,
		      "and the arithmetic that divides by it still answers, rather"
		      " than dividing by zero or measuring in negative pixels");
	}

	// AND THE PIXEL-METRIC SURFACE, WHICH NOTHING ASKED NUMERICALLY. The
	// sweep above covers sizeFromContents() and subControlRect(); the third
	// surface a style answers through is pixelMetric(), and no check in this
	// tree called it except as a helper. That is the surface
	// PM_MessageBoxIconSize was on when it answered Fusion's 48 -- 2.53 rows
	// -- and made a message box a fractional number of rows tall (8.466).
	//
	// PINNED AS MEASURED, NOT AS WANTED. A set of metrics answers a fixed
	// pixel number that does not move when the cell doubles, because
	// GridStyle's switch does not name them and the proxy base answers for
	// them. Today that is latent rather than live: sizeFromContents() rounds
	// every answer up to whole cells before a widget sees it, which is why
	// nothing breaks, and PM_MessageBoxIconSize escaped only because
	// QMessageBox reads a metric DIRECTLY to size a pixmap. Whether the icon
	// metrics should answer in cells is section 0b's question and is not
	// this check's to decide.
	//
	// ASSERTED IN BOTH DIRECTIONS, so the set cannot rot. A metric outside
	// it must scale, or be zero, or be a negative sentinel; a metric inside
	// it must still NOT scale. Without the second half the set would outlive
	// its subject -- a metric that started answering in cells would leave a
	// name behind that nobody could falsify by running anything.
	//
	// BY NAME, and not by count or by value, because the two Qt versions
	// differ in all three. Qt 5.15 carries PM_DialogButtons* and
	// PM_Default*Margin that Qt 6 removed, Qt 6 carries PM_LineEditIcon*
	// that Qt 5 lacks, and the shared ones disagree on the number --
	// PM_ScrollBarSliderMin is 26 on Qt 6 and 27 on Qt 5. So a name this Qt
	// does not have is skipped rather than failed, and the counts are
	// printed rather than asserted.
	//
	// Canonical names come from valueToKey(), because Qt 5's enum has two
	// keys for one value -- PM_MDISubWindowFrameWidth beside
	// PM_MdiSubWindowFrameWidth -- so a sweep keyed on the index would
	// otherwise ask one metric twice under two spellings.
	{
		// Measured 2026-10-20 against Qt 5.15.15 and Qt 6.8.2.
		static const char *const fixed_pixels[] = {
			"PM_ButtonIconSize",
			// GONE FROM QT 6'S ENUM, so keyToValue() skips these three
			// there and no conditional is needed: Qt 5's Fusion answers
			// 11, 9 and 6 pixels for them.
			"PM_DefaultChildMargin",
			"PM_DefaultLayoutSpacing",
			"PM_DefaultTopLevelMargin",
			// STILL IN QT 6'S ENUM AND ANSWERING 0 THERE, which is
			// grid-safe, against 31, 72 and 27 pixels on Qt 5. So these
			// three cannot be left to keyToValue() the way the three above
			// are -- the name resolves on both versions and only one of
			// them answers a fixed number. The conditional is a statement
			// about which Qt deprecated them, not about behaviour this
			// library chooses. Measured 2026-10-20 on both.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
			"PM_DialogButtonsButtonHeight",
			"PM_DialogButtonsButtonWidth",
			"PM_DialogButtonsSeparator",
#endif
			"PM_DockWidgetFrameWidth",
			"PM_DockWidgetHandleExtent",
			"PM_DockWidgetTitleBarButtonMargin",
			"PM_DockWidgetTitleMargin",
			"PM_IconViewIconSize",
			"PM_LargeIconSize",
			"PM_LineEditIconMargin",              // Qt 6 only
			"PM_LineEditIconSize",                // Qt 6 only
			"PM_ListViewIconSize",
			"PM_MaximumDragDistance",             // -1, a sentinel
			"PM_MdiSubWindowFrameWidth",
			"PM_MdiSubWindowMinimizedWidth",
			"PM_MenuButtonIndicator",
			"PM_ScrollBarSliderMin",
			"PM_SizeGripSize",
			"PM_SliderTickmarkOffset",
			"PM_SmallIconSize",
			"PM_SpinBoxFrameWidth",
			"PM_SpinBoxSliderHeight",
			"PM_SubMenuOverlap",                  // -1, a sentinel
			"PM_TabBarIconSize",
			"PM_TabBarScrollButtonWidth",
			"PM_TabBarTabOverlap",
			"PM_TabBar_ScrollButtonOverlap",
			"PM_TextCursorWidth",
			"PM_TitleBarButtonIconSize",
			"PM_TitleBarButtonSize",
			"PM_ToolTipLabelFrameWidth",
		};
		const auto pinned = [&](const QByteArray &name) {
			for (const char *n : fixed_pixels)
				if (name == n) return true;
			return false;
		};
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStyle *style = QApplication::style();
		const QMetaEnum me = QMetaEnum::fromType<QStyle::PixelMetric>();
		QSet<int> seen;
		QSet<QByteArray> fixed_here;
		QStringList unpinned;
		int scales = 0, zeroes = 0;
		for (int i = 0; i < me.keyCount(); ++i) {
			const int v = me.value(i);
			if (seen.contains(v)) continue;
			seen.insert(v);
			const QByteArray name = me.valueToKey(v);
			const auto ask = [&](int w, int h) {
				GridMetrics::set(w, h);
				const int r = style->pixelMetric(QStyle::PixelMetric(v),
				                                 nullptr, nullptr);
				GridMetrics::set(cw, ch);
				return r;
			};
			const int one = ask(cw, ch), two = ask(cw * 2, ch * 2);
			if (one == 0 && two == 0) { ++zeroes; continue; }
			if (two == 2 * one) { ++scales; continue; }
			fixed_here.insert(name);
			if (!pinned(name))
				unpinned << QString::fromLatin1(name)
				             + QStringLiteral(" (%1)").arg(one);
		}
		QStringList rotted;
		int pinned_here = 0;
		for (const char *n : fixed_pixels) {
			if (me.keyToValue(n) == -1) continue;   // not in this Qt
			++pinned_here;
			if (!fixed_here.contains(QByteArray(n)))
				rotted << QString::fromLatin1(n);
		}
		printf("info: %d pixel metric(s): %d scale with the cell, %d are"
		       " zero, %d answer a fixed number; %d of %d pinned name(s)"
		       " exist on this Qt\n", int(seen.size()), scales, zeroes,
		       int(fixed_here.size()), pinned_here,
		       int(sizeof(fixed_pixels) / sizeof(fixed_pixels[0])));
		if (!unpinned.isEmpty())
			printf("info: not pinned: %s\n",
			       qPrintable(unpinned.join(QStringLiteral(", "))));
		if (!rotted.isEmpty())
			printf("info: pinned but no longer fixed: %s\n",
			       qPrintable(rotted.join(QStringLiteral(", "))));
		CHECK(unpinned.isEmpty(),
		      "no pixel metric outside the pinned set answers a fixed number"
		      " of pixels, so a new one cannot arrive unnoticed on the one"
		      " surface that has already carried a defect");
		CHECK(rotted.isEmpty(),
		      "and every pinned metric this Qt has still answers one, so the"
		      " set cannot outlive what it describes");
	}

	// AND WHICH PARTS THIS STYLE SAYS EXIST, against the base style's answer.
	// A sub-control rect is a hit target and a drawing site, so a style that
	// CLAIMS a part the base declines has invented one, and a style that
	// DECLINES one the base claims has taken one away. Both are reachable
	// faults and neither shows in a frame.
	//
	// The lens is 8.475's, where it cost something: that change wrapped every
	// standard icon in an engine, and QIcon::isNull() asks the ENGINE -- so
	// 11 of the 81 StandardPixmap values Fusion has no icon for came back
	// non-null with a blank pixmap behind them, and a widget asking isNull()
	// would have reserved cells for nothing. The question that found it
	// generalises to every override: what did Qt decline to answer, and does
	// this style now answer it?
	//
	// PINNED AS MEASURED. Twenty differences, and each is this style being
	// right or a case already recorded:
	//
	//   groupbox bare/checkbox, bare/label, titled/checkbox   declined
	//     Fusion computes a 14x14 checkbox rect for a box that is not
	//     checkable and a 2x2 label rect for one with no title. There is no
	//     such part; declining is the honest answer and Fusion is the sloppy
	//     one here.
	//   spinbox/field   claimed
	//     The field keeps a one-cell floor below the width where Fusion gives
	//     up. 8.473 measured the consequence and found none -- the internal
	//     QLineEdit covers the field and takes the press, so nothing reads
	//     the rect.
	//   combobox/field   claimed
	//     Deliberate in 8.472: a combo keeps an edit field wherever two cells
	//     allow one, because a rect overlapping the arrow made a click on the
	//     value open the popup. Its own check asserts it.
	//   h+v scrollbar/subline, addline   declined
	//   h+v scrollbar/thumb              claimed
	//     At one cell this style gives the cell to the thumb and declines
	//     both arrows, where Fusion claims both arrows and declines the
	//     thumb. Measured: it draws the thumb, no click moves it, and the
	//     KEYBOARD steps it at every width -- which is this library's whole
	//     premise, so an indicator you work with the keys rather than a trap.
	//   v scrollbar/thumb   declined
	//     And at TWO cells the bar is two arrows with no track between
	//     them, so there is no thumb to answer for. It used to answer the
	//     add line's cell, which the disjointness check below now refuses.
	//     The horizontal bar makes no difference here because Fusion
	//     declines a 20-pixel thumb as well; at two ROWS it fits one.
	//   spinbox/up, down   declined
	//     Below three cells a spin box cannot hold two arrows and a value,
	//     and the value is what must survive -- a spin box whose number
	//     cannot be read is 8.473's worst case. Both arrows go rather than
	//     one, which is the scroll bar's answer one control along, and it
	//     keeps the keyboard as the way to step it.
	//   groupbox bare/contents, titled/contents, checkable/contents
	//   groupbox checkable/label   declined
	//     A one-cell group box has no interior: the contents rect is a cell
	//     of inset from each side, so it lies wholly outside a widget one
	//     cell wide, and a checkable box's title starts after a
	//     three-cell indicator and a gap. Both used to be answered
	//     OUTSIDE the box -- and the contents rect is what QGroupBox turns
	//     into its contents margins, so a narrow box laid its children
	//     past its own edge. Clipped to the widget now, which leaves them
	//     empty at that width, and the children were already zero cells
	//     wide there.
	//   toolbutton/button, menu   declined
	//     A menu button wants a cell for its arrow and a cell for the
	//     bracket the arrow sits inside, so at one cell both parts decline
	//     and at two the button's own part does. Fusion answers the rect
	//     less a 12-pixel indicator at every width, which is where the
	//     TWO-PIXEL-WIDE rect came from.
	//
	// ASSERTED IN BOTH DIRECTIONS, so the set cannot rot: nothing outside it
	// may differ, and everything inside it must still differ. The same set
	// holds on Qt 5.15.15 and Qt 6.8.2 -- 93 agreements and these 20 on both
	// -- so unlike the pixel metrics above this needs no conditional.
	{
		static const char *const allowed[] = {
			"combobox/field/claimed",
			"groupbox bare/contents/declined",
			"groupbox checkable/contents/declined",
			"groupbox checkable/label/declined",
			"groupbox titled/contents/declined",
			"groupbox bare/checkbox/declined",
			"groupbox bare/label/declined",
			"groupbox titled/checkbox/declined",
			"h scrollbar/addline/declined",
			"h scrollbar/subline/declined",
			"h scrollbar/thumb/claimed",
			"spinbox/down/declined",
			"spinbox/field/claimed",
			"spinbox/up/declined",
			"toolbutton/button/declined",
			"toolbutton/menu/declined",
			"v scrollbar/addline/declined",
			"v scrollbar/subline/declined",
			"v scrollbar/thumb/claimed",
			"v scrollbar/thumb/declined",
		};
		QStyle *plain = QStyleFactory::create(QStringLiteral("Fusion"));
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QSet<QString> seen_diffs;
		int agreed = 0;
		QStringList negative, overlapping, outside, offgrid;
		int pairs = 0;
		const auto cmp = [&](const QString &what, const QString &part,
		                     QStyle::ComplexControl cc, QStyle::SubControl sc,
		                     const QStyleOptionComplex *o, const QWidget *w) {
			if (!plain) return;
			const QRect gr = QApplication::style()
			                     ->subControlRect(cc, o, sc, w);
			// AND NO ANSWER HAS A NEGATIVE DIMENSION, folded in here
			// because this sweep already has the rect. A negative one lies
			// about its area -- -12 by -5 multiplies to a positive 60 --
			// which is 8.474's trap in CellBuffer from the other end, and
			// 8.477 found 18 of them on the sub-element surface.
			if (gr.width() < 0 || gr.height() < 0)
				negative << what + QLatin1Char('/') + part;
			// AND IT HAS TO BE INSIDE THE CONTROL IT IS PART OF, which is
			// a third question again: a rect can be positive, clear of its
			// neighbours and still describe cells the widget does not own.
			// The spin box's arrows were at cells -2..-1 of a one-cell box
			// until 8.479, so this is not hypothetical -- it is the one
			// invariant of that family nothing asks about.
			if (!gr.isEmpty() && !o->rect.contains(gr))
				outside << what + QLatin1Char('/') + part;
			if (!gr.isEmpty() && !GridMetrics::is_aligned(gr))
				offgrid << what + QLatin1Char('/') + part
				           + QStringLiteral(" -> %1x%2+%3+%4")
				             .arg(gr.width()).arg(gr.height())
				             .arg(gr.x()).arg(gr.y());
			const bool b = plain->subControlRect(cc, o, sc, w).isEmpty();
			const bool g = gr.isEmpty();
			if (b == g) { ++agreed; return; }
			seen_diffs.insert(what + QLatin1Char('/') + part
			                  + (b ? QStringLiteral("/claimed")
			                       : QStringLiteral("/declined")));
		};
		// AND NO TWO PARTS OF ONE CONTROL MAY CLAIM THE SAME CELL, which
		// the sweep above cannot see: emptiness and overlap are different
		// questions, and three of this style's controls were failing the
		// second one while passing the first.
		//
		// Measured 2026-10-21. The spin box's two arrows sat on the value's
		// cell below five cells and the up arrow was painted over by it; the
		// tool button's own part ran eight pixels into the menu arrow's cell
		// at every width and came back TWO PIXELS WIDE at one cell; and a
		// two-cell scroll bar answered a thumb at the add line's cell for a
		// bar drawn as two arrows with no track at all.
		//
		// It matters because of how the hit test resolves it: QCommonStyle
		// walks the sub-controls in BIT ORDER and takes the first whose rect
		// contains the point, so an overlap is settled by the numeric value
		// of an enumerator rather than by what the style drew. Two of the
		// three were right by that accident and one was wrong by it.
		//
		// Only parts that are meant to be disjoint. A slider's groove
		// CONTAINS its handle and a frame contains everything, so those
		// pairs are not a question -- which is why this is a list of sets
		// rather than SC_All.
		using Parts = QList<QPair<QString, QStyle::SubControl>>;
		const auto disjoint =
		    [&](const QString &what, QStyle::ComplexControl cc,
		        const QStyleOptionComplex *o, const QWidget *w,
		        const Parts &parts) {
			for (int i = 0; i < parts.size(); ++i)
				for (int j = i + 1; j < parts.size(); ++j) {
					const QRect a = QApplication::style()
					                    ->subControlRect(cc, o,
					                                     parts[i].second, w);
					const QRect b = QApplication::style()
					                    ->subControlRect(cc, o,
					                                     parts[j].second, w);
					++pairs;
					if (a.intersects(b))
						overlapping << what + QLatin1Char('/')
						               + parts[i].first + QLatin1Char('+')
						               + parts[j].first;
				}
		};
		for (int n : { 1, 2, 3, 6, 14 }) {
			{
				struct Sp : QSpinBox { using QSpinBox::QSpinBox;
					QStyleOptionSpinBox real() const {
						QStyleOptionSpinBox o; initStyleOption(&o);
						return o; } };
				Sp x;
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionSpinBox o = x.real();
				o.subControls = QStyle::SC_All;
				cmp(QStringLiteral("spinbox"), QStringLiteral("up"),
				    QStyle::CC_SpinBox, QStyle::SC_SpinBoxUp, &o, &x);
				cmp(QStringLiteral("spinbox"), QStringLiteral("down"),
				    QStyle::CC_SpinBox, QStyle::SC_SpinBoxDown, &o, &x);
				cmp(QStringLiteral("spinbox"), QStringLiteral("field"),
				    QStyle::CC_SpinBox, QStyle::SC_SpinBoxEditField, &o, &x);
				const Parts spin = {
					{ QStringLiteral("up"), QStyle::SC_SpinBoxUp },
					{ QStringLiteral("down"), QStyle::SC_SpinBoxDown },
					{ QStringLiteral("field"), QStyle::SC_SpinBoxEditField },
				};
				disjoint(QStringLiteral("spinbox"), QStyle::CC_SpinBox, &o,
				         &x, spin);
			}
			{
				struct Cb : QComboBox { using QComboBox::QComboBox;
					QStyleOptionComboBox real() const {
						QStyleOptionComboBox o; initStyleOption(&o);
						return o; } };
				Cb x;
				x.addItem(QStringLiteral("Hi"));
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionComboBox o = x.real();
				o.subControls = QStyle::SC_All;
				cmp(QStringLiteral("combobox"), QStringLiteral("arrow"),
				    QStyle::CC_ComboBox, QStyle::SC_ComboBoxArrow, &o, &x);
				cmp(QStringLiteral("combobox"), QStringLiteral("field"),
				    QStyle::CC_ComboBox, QStyle::SC_ComboBoxEditField, &o, &x);
				const Parts combo = {
					{ QStringLiteral("arrow"), QStyle::SC_ComboBoxArrow },
					{ QStringLiteral("field"), QStyle::SC_ComboBoxEditField },
				};
				disjoint(QStringLiteral("combobox"), QStyle::CC_ComboBox, &o,
				         &x, combo);
			}
			for (auto d : { Qt::Horizontal, Qt::Vertical }) {
				const QString side = d == Qt::Horizontal
				    ? QStringLiteral("h ") : QStringLiteral("v ");
				{
					struct Sl : QSlider { using QSlider::QSlider;
						QStyleOptionSlider real() const {
							QStyleOptionSlider o; initStyleOption(&o);
							return o; } };
					Sl x(d);
					x.setAttribute(Qt::WA_DontShowOnScreen);
					if (d == Qt::Horizontal) x.resize(n * cw, ch);
					else x.resize(cw, n * ch);
					x.show();
					QCoreApplication::processEvents();
					QStyleOptionSlider o = x.real();
					o.subControls = QStyle::SC_All;
					cmp(side + QStringLiteral("slider"),
					    QStringLiteral("handle"), QStyle::CC_Slider,
					    QStyle::SC_SliderHandle, &o, &x);
					cmp(side + QStringLiteral("slider"),
					    QStringLiteral("groove"), QStyle::CC_Slider,
					    QStyle::SC_SliderGroove, &o, &x);
				}
				{
					struct Sb : QScrollBar { using QScrollBar::QScrollBar;
						QStyleOptionSlider real() const {
							QStyleOptionSlider o; initStyleOption(&o);
							return o; } };
					Sb x(d);
					x.setRange(0, 100);
					x.setValue(50);
					x.setAttribute(Qt::WA_DontShowOnScreen);
					if (d == Qt::Horizontal) x.resize(n * cw, ch);
					else x.resize(cw, n * ch);
					x.show();
					QCoreApplication::processEvents();
					QStyleOptionSlider o = x.real();
					o.subControls = QStyle::SC_All;
					const QString bar = side + QStringLiteral("scrollbar");
					cmp(bar, QStringLiteral("subline"), QStyle::CC_ScrollBar,
					    QStyle::SC_ScrollBarSubLine, &o, &x);
					cmp(bar, QStringLiteral("thumb"), QStyle::CC_ScrollBar,
					    QStyle::SC_ScrollBarSlider, &o, &x);
					cmp(bar, QStringLiteral("addline"), QStyle::CC_ScrollBar,
					    QStyle::SC_ScrollBarAddLine, &o, &x);
					const Parts three = {
						{ QStringLiteral("subline"),
						  QStyle::SC_ScrollBarSubLine },
						{ QStringLiteral("addline"),
						  QStyle::SC_ScrollBarAddLine },
						{ QStringLiteral("thumb"),
						  QStyle::SC_ScrollBarSlider },
					};
					disjoint(bar, QStyle::CC_ScrollBar, &o, &x, three);
				}
			}
			for (int kind = 0; kind < 3; ++kind) {
				struct Gb : QGroupBox { using QGroupBox::QGroupBox;
					QStyleOptionGroupBox real() const {
						QStyleOptionGroupBox o; initStyleOption(&o);
						return o; } };
				Gb x;
				if (kind >= 1) x.setTitle(QStringLiteral("T"));
				if (kind == 2) x.setCheckable(true);
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, 4 * ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionGroupBox o = x.real();
				const QString g = kind == 0
				    ? QStringLiteral("groupbox bare")
				    : (kind == 1 ? QStringLiteral("groupbox titled")
				                 : QStringLiteral("groupbox checkable"));
				cmp(g, QStringLiteral("checkbox"), QStyle::CC_GroupBox,
				    QStyle::SC_GroupBoxCheckBox, &o, &x);
				cmp(g, QStringLiteral("label"), QStyle::CC_GroupBox,
				    QStyle::SC_GroupBoxLabel, &o, &x);
				cmp(g, QStringLiteral("contents"), QStyle::CC_GroupBox,
				    QStyle::SC_GroupBoxContents, &o, &x);
				const Parts box = {
					{ QStringLiteral("checkbox"),
					  QStyle::SC_GroupBoxCheckBox },
					{ QStringLiteral("label"), QStyle::SC_GroupBoxLabel },
					{ QStringLiteral("contents"),
					  QStyle::SC_GroupBoxContents },
				};
				disjoint(g, QStyle::CC_GroupBox, &o, &x, box);
			}
			// AND THE TOOL BUTTON, which was in neither this sweep nor the
			// negativity tally below it. 8.477 reported "the sub-control
			// surface was already clean at 120 of 120", and that was true of
			// the five controls it asked: SC_ToolButton came back TWO PIXELS
			// WIDE at one cell -- the base style's rect less a 12-pixel menu
			// indicator -- and nothing had ever asked it. A count inherits
			// its detector.
			{
				struct Tb : QToolButton { using QToolButton::QToolButton;
					QStyleOptionToolButton real() const {
						QStyleOptionToolButton o; initStyleOption(&o);
						return o; } };
				Tb x;
				x.setText(QStringLiteral("Go"));
				x.setPopupMode(QToolButton::MenuButtonPopup);
				QMenu m;
				m.addAction(QStringLiteral("One"));
				x.setMenu(&m);
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionToolButton o = x.real();
				o.subControls = QStyle::SC_All;
				cmp(QStringLiteral("toolbutton"), QStringLiteral("button"),
				    QStyle::CC_ToolButton, QStyle::SC_ToolButton, &o, &x);
				cmp(QStringLiteral("toolbutton"), QStringLiteral("menu"),
				    QStyle::CC_ToolButton, QStyle::SC_ToolButtonMenu, &o, &x);
				const Parts two = {
					{ QStringLiteral("button"), QStyle::SC_ToolButton },
					{ QStringLiteral("menu"), QStyle::SC_ToolButtonMenu },
				};
				disjoint(QStringLiteral("toolbutton"), QStyle::CC_ToolButton,
				         &o, &x, two);
			}
		}
		delete plain;
		QStringList unpinned, rotted;
		for (const QString &d : seen_diffs) {
			bool known = false;
			for (const char *a : allowed)
				if (d == QLatin1String(a)) known = true;
			if (!known) unpinned << d;
		}
		for (const char *a : allowed)
			if (!seen_diffs.contains(QLatin1String(a)))
				rotted << QString::fromLatin1(a);
		unpinned.sort();
		rotted.sort();
		printf("info: %d sub-control answer(s) agree with the base style and"
		       " %d differ; %d unpinned, %d pinned but gone\n", agreed,
		       int(seen_diffs.size()), int(unpinned.size()),
		       int(rotted.size()));
		if (!unpinned.isEmpty())
			printf("info: not pinned: %s\n",
			       qPrintable(unpinned.join(QStringLiteral(", "))));
		if (!rotted.isEmpty())
			printf("info: pinned but no longer differing: %s\n",
			       qPrintable(rotted.join(QStringLiteral(", "))));
		// `agreed > 0` is the vacuous-pass guard: a sweep that compared
		// nothing would report no differences and ask nothing.
		CHECK(agreed > 0 && unpinned.isEmpty(),
		      "no sub-control claims a part the base style declines, or"
		      " declines one it claims, except where this style does so"
		      " deliberately and says which");
		CHECK(rotted.isEmpty(),
		      "and every deliberate difference still happens, so that set"
		      " cannot outlive what it describes either");
		negative.sort();
		if (!negative.isEmpty())
			printf("info: negative sub-control rect(s): %s\n",
			       qPrintable(negative.join(QStringLiteral(", "))));
		CHECK(negative.isEmpty(),
		      "and no sub-control rect has a negative dimension, which would"
		      " lie about its area rather than merely be empty");

		// AND THE SUB-ELEMENT THAT SWEEP DOES NOT REACH. The tally above
		// walks CC_/SC_ pairs; SE_ItemViewItemText is a sub-ELEMENT and is
		// not in it. Its arithmetic subtracts an indent from the item's
		// width -- one cell, five when the item carries a check indicator --
		// and the guard against that going negative had never fired:
		// coverage put `opt->rect.width() > in` at 100 per cent one way, so
		// no fixture has an item narrower than its own indent.
		//
		// It is reachable by the one consumer this style has for it. The
		// comment at the arithmetic says GridStyle draws CE_ItemViewItem
		// itself and never asks this, so the only caller under a cell
		// device is the inline EDITOR -- a rename box over a narrow,
		// checkable column, which is an ordinary tree view squeezed.
		//
		// A wide item is the control: without it, a style that returned an
		// empty rect for everything would satisfy the narrow case.
		{
			QStyle *st = QApplication::style();
			const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
			QStyleOptionViewItem narrow;
			narrow.features |= QStyleOptionViewItem::HasCheckIndicator;
			narrow.rect = QRect(0, 0, cw * 3, ch);   // 3 cells, indent is 5
			const QRect thin =
			    st->subElementRect(QStyle::SE_ItemViewItemText, &narrow,
			                       nullptr);
			QStyleOptionViewItem wide = narrow;
			wide.rect = QRect(0, 0, cw * 20, ch);
			const QRect room =
			    st->subElementRect(QStyle::SE_ItemViewItemText, &wide,
			                       nullptr);
			printf("info: an item 3 cells wide with a check box gives a text"
			       " rect %dx%d, and one 20 cells wide gives %dx%d\n",
			       thin.width(), thin.height(), room.width(), room.height());
			CHECK(thin.width() >= 0 && thin.height() >= 0
			      && room.width() > 0 && room.width() < wide.rect.width(),
			      "an item narrower than its own indent still answers a text"
			      " rect with no negative dimension, while a wide one gets"
			      " the row less the indent");
		}
		overlapping.sort();
		overlapping.removeDuplicates();
		printf("info: %d disjoint pair(s) of parts asked, %d overlapping%s%s\n",
		       pairs, int(overlapping.size()),
		       overlapping.isEmpty() ? "" : ": ",
		       qPrintable(overlapping.join(QStringLiteral(", "))));
		// `pairs > 0` is the same vacuous-pass guard as `agreed > 0`, and it
		// has a positive control that needs no sabotage: the base style
		// overlaps 11 times at these same questions, so the comparison is
		// one that can fail and the number is not the question's absence.
		outside.sort();
		outside.removeDuplicates();
		if (!outside.isEmpty())
			printf("info: sub-control(s) outside their own control: %s\n",
			       qPrintable(outside.join(QStringLiteral(", "))));
		offgrid.sort();
		offgrid.removeDuplicates();
		printf("info: %d sub-control rect(s) off the grid%s%s\n",
		       int(offgrid.size()), offgrid.isEmpty() ? "" : ": ",
		       qPrintable(offgrid.join(QStringLiteral(", "))));
		CHECK(outside.isEmpty(),
		      "and no sub-control rect names cells outside the control it is"
		      " part of, which is neither empty nor negative nor overlapping"
		      " and is still not an answer");
		CHECK(pairs > 0 && overlapping.isEmpty(),
		      "and no two parts of one complex control that are meant to be"
		      " disjoint claim the same pixel, the hit test settling an"
		      " overlap by enumerator order rather than by what was drawn");
	}

	// AND THE SAME OF THE SUB-ELEMENT SURFACE, which is where they were.
	// QCommonStyle insets by PM_DefaultFrameWidth -- a whole CELL here
	// against Fusion's one pixel -- on all four sides, so on a widget too
	// small to hold it the subtraction goes past zero. Measured 2026-10-21,
	// each widget asked only for ITS OWN sub-elements: 18 of 90 answers came
	// back negative, including SE_PushButtonContents at EVERY width because
	// a one-row button cannot give up a row at the top and another at the
	// bottom.
	//
	// No pinned set and no allow-list, unlike the emptiness sweep above: a
	// rectangle with a negative side is never the right answer to anything,
	// so the assertion is simply that there are none.
	{
		const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
		QStyle *st = QApplication::style();
		QStringList bad, out, askew;
		int looked = 0;
		const auto look = [&](const QString &what, const QString &name,
		                      QStyle::SubElement se, const QStyleOption *o,
		                      const QWidget *w) {
			const QRect r = st->subElementRect(se, o, w);
			++looked;
			if (r.width() < 0 || r.height() < 0)
				bad << QStringLiteral("%1/%2 -> %3x%4").arg(what).arg(name)
				       .arg(r.width()).arg(r.height());
			// And the containment question, for the sub-control sweep's
			// reason above and asked here because this sweep already holds
			// the option it would be compared against.
			if (!o->rect.isEmpty() && !r.isEmpty() && !o->rect.contains(r))
				out << QStringLiteral("%1/%2 -> %3x%4+%5+%6").arg(what)
				       .arg(name).arg(r.width()).arg(r.height())
				       .arg(r.x()).arg(r.y());
			if (!o->rect.isEmpty() && !r.isEmpty()
			    && !GridMetrics::is_aligned(r))
				askew << QStringLiteral("%1/%2 -> %3x%4+%5+%6").arg(what)
				         .arg(name).arg(r.width()).arg(r.height())
				         .arg(r.x()).arg(r.y());
		};
		for (int n : { 1, 2, 3, 6, 14 }) {
			{
				struct B : QPushButton { using QPushButton::QPushButton;
					QStyleOptionButton real() const {
						QStyleOptionButton o; initStyleOption(&o);
						return o; } };
				B x(QStringLiteral("Hi"));
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionButton o = x.real();
				look(QStringLiteral("pushbutton"),
				     QStringLiteral("contents"),
				     QStyle::SE_PushButtonContents, &o, &x);
				look(QStringLiteral("pushbutton"), QStringLiteral("focus"),
				     QStyle::SE_PushButtonFocusRect, &o, &x);
				look(QStringLiteral("pushbutton"), QStringLiteral("bevel"),
				     QStyle::SE_PushButtonBevel, &o, &x);
			}
			{
				struct C : QCheckBox { using QCheckBox::QCheckBox;
					QStyleOptionButton real() const {
						QStyleOptionButton o; initStyleOption(&o);
						return o; } };
				C x(QStringLiteral("Hi"));
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionButton o = x.real();
				look(QStringLiteral("checkbox"),
				     QStringLiteral("indicator"),
				     QStyle::SE_CheckBoxIndicator, &o, &x);
				look(QStringLiteral("checkbox"), QStringLiteral("contents"),
				     QStyle::SE_CheckBoxContents, &o, &x);
				look(QStringLiteral("checkbox"), QStringLiteral("focus"),
				     QStyle::SE_CheckBoxFocusRect, &o, &x);
				look(QStringLiteral("checkbox"), QStringLiteral("click"),
				     QStyle::SE_CheckBoxClickRect, &o, &x);
			}
			{
				struct R : QRadioButton { using QRadioButton::QRadioButton;
					QStyleOptionButton real() const {
						QStyleOptionButton o; initStyleOption(&o);
						return o; } };
				R x(QStringLiteral("Hi"));
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionButton o = x.real();
				look(QStringLiteral("radio"), QStringLiteral("indicator"),
				     QStyle::SE_RadioButtonIndicator, &o, &x);
				look(QStringLiteral("radio"), QStringLiteral("contents"),
				     QStyle::SE_RadioButtonContents, &o, &x);
				look(QStringLiteral("radio"), QStringLiteral("click"),
				     QStyle::SE_RadioButtonClickRect, &o, &x);
			}
			{
				struct L : QLineEdit { using QLineEdit::QLineEdit;
					QStyleOptionFrame real() const {
						QStyleOptionFrame o; initStyleOption(&o);
						return o; } };
				L x(QStringLiteral("Hi"));
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionFrame o = x.real();
				look(QStringLiteral("lineedit"), QStringLiteral("contents"),
				     QStyle::SE_LineEditContents, &o, &x);
				look(QStringLiteral("lineedit"), QStringLiteral("frame"),
				     QStyle::SE_FrameContents, &o, &x);
				look(QStringLiteral("lineedit"), QStringLiteral("shaped"),
				     QStyle::SE_ShapedFrameContents, &o, &x);
			}
			{
				struct P : QProgressBar { using QProgressBar::QProgressBar;
					QStyleOptionProgressBar real() const {
						QStyleOptionProgressBar o; initStyleOption(&o);
						return o; } };
				P x;
				x.setValue(40);
				x.setAttribute(Qt::WA_DontShowOnScreen);
				x.resize(n * cw, ch);
				x.show();
				QCoreApplication::processEvents();
				QStyleOptionProgressBar o = x.real();
				look(QStringLiteral("progressbar"),
				     QStringLiteral("groove"),
				     QStyle::SE_ProgressBarGroove, &o, &x);
				look(QStringLiteral("progressbar"),
				     QStringLiteral("contents"),
				     QStyle::SE_ProgressBarContents, &o, &x);
				look(QStringLiteral("progressbar"), QStringLiteral("label"),
				     QStyle::SE_ProgressBarLabel, &o, &x);
			}
		}
		// AND ONE PASS WITH AN EMPTY OPTION RECT, which is the only place
		// the negative-side clamp can still be seen. A non-empty rect is
		// intersected with now, and QRect::intersected() of an invalid
		// rect is EMPTY rather than negative -- so the clip answers for
		// the clamp and removing the clamp left this check green.
		// Measured by the sabotage harness, which is what it is for.
		//
		// A hand-built option is the faithful instrument here, unlike
		// everywhere else in this sweep: the question is this function's
		// arithmetic on a rect it was handed, not what any widget asks
		// for. A real widget is still passed, because the implementation
		// reads it.
		{
			QPushButton host(QStringLiteral("Hi"));
			host.setAttribute(Qt::WA_DontShowOnScreen);
			host.resize(6 * cw, ch);
			host.show();
			QCoreApplication::processEvents();
			QStyleOptionFrame f;
			f.rect = QRect();
			f.lineWidth = 1;
			look(QStringLiteral("empty option"),
			     QStringLiteral("frame contents"),
			     QStyle::SE_FrameContents, &f, &host);
			look(QStringLiteral("empty option"),
			     QStringLiteral("shaped frame"),
			     QStyle::SE_ShapedFrameContents, &f, &host);
			QStyleOptionButton b;
			b.rect = QRect();
			look(QStringLiteral("empty option"),
			     QStringLiteral("push contents"),
			     QStyle::SE_PushButtonContents, &b, &host);
			look(QStringLiteral("empty option"), QStringLiteral("push focus"),
			     QStyle::SE_PushButtonFocusRect, &b, &host);
			look(QStringLiteral("empty option"), QStringLiteral("check box"),
			     QStyle::SE_CheckBoxIndicator, &b, &host);
		}
		bad.sort();
		printf("info: %d sub-element rect(s) asked, %d with a negative"
		       " dimension%s%s\n", looked, int(bad.size()),
		       bad.isEmpty() ? "" : ": ",
		       qPrintable(bad.join(QStringLiteral(", "))));
		// `looked > 0` for the reason every sweep here carries one: a loop
		// that asked nothing reports no faults.
		CHECK(looked > 0 && bad.isEmpty(),
		      "no sub-element rect this style hands out has a negative"
		      " dimension, however small the widget it was asked about");
		out.sort();
		out.removeDuplicates();
		printf("info: %d of them name cells outside the widget%s%s\n",
		       int(out.size()), out.isEmpty() ? "" : ": ",
		       qPrintable(out.join(QStringLiteral(", "))));
		// AND WHICH OF THEM ARE OFF THE GRID, pinned rather than
		// forbidden. `is_aligned()` is this library's own test and
		// GridGuard reports a WIDGET that fails it, so a rect this style
		// hands out that fails it is at least worth knowing about. Six do,
		// all on the check box and the radio button, and all from
		// QCommonStyle compounding the indicator's three cells with
		// PM_CheckBoxLabelSpacing and the focus margins -- which are
		// among the 29 metrics 8.476 measured as answering a fixed number
		// of pixels, and whether those should answer in cells is recorded
		// there as the copyright holder's.
		//
		// MEASURED FOR A CONSEQUENCE AND THERE IS NONE, which is why this
		// pins rather than asserts emptiness: rendering a check box and a
		// radio button at 4, 6, 8 and 14 cells and testing every cell's
		// centre against the click rect, **every cell carrying ink is a
		// cell a click reaches** at every width. The cells the click rect
		// stops short of are blank ones past the label.
		//
		// Asserted in both directions, like the emptiness set above: a
		// seventh cannot arrive unnoticed and one that goes away cannot
		// leave a stale entry behind.
		static const char *const askew_pinned[] = {
			"checkbox/click -> 62x19+0+0",
			"checkbox/contents -> 100x19+39+0",
			"checkbox/contents -> 20x19+39+0",
			"checkbox/focus -> 24x19+36+0",
			"checkbox/focus -> 26x19+36+0",
			"radio/click -> 63x19+0+0",
		};
		askew.sort();
		askew.removeDuplicates();
		printf("info: %d sub-element rect(s) off the grid%s%s\n",
		       int(askew.size()), askew.isEmpty() ? "" : ": ",
		       qPrintable(askew.join(QStringLiteral(", "))));
		QStringList askew_new, askew_gone;
		for (const QString &a : askew) {
			bool known = false;
			for (const char *k : askew_pinned)
				if (a == QLatin1String(k)) known = true;
			if (!known) askew_new << a;
		}
		for (const char *k : askew_pinned)
			if (!askew.contains(QLatin1String(k)))
				askew_gone << QString::fromLatin1(k);
		if (!askew_new.isEmpty())
			printf("info: not pinned: %s\n",
			       qPrintable(askew_new.join(QStringLiteral(", "))));
		if (!askew_gone.isEmpty())
			printf("info: pinned but gone: %s\n",
			       qPrintable(askew_gone.join(QStringLiteral(", "))));
		CHECK(askew_new.isEmpty() && askew_gone.isEmpty(),
		      "and the rects that are off the grid are exactly the six"
		      " measured ones, every cell of whose ink a click still"
		      " reaches");
		CHECK(out.isEmpty(),
		      "and none names cells outside the widget it was asked about,"
		      " which a positive rectangle clear of its neighbours can"
		      " still do");
	}

	return fails;
}
