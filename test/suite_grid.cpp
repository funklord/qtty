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

	return fails;
}
