// suite_loops -- the fixtures that need a nested QEventLoop: a context menu
// through QMenu::exec(), the whole of drag and drop, ActionsContextMenu and
// a file dialog. Split out of suite_router on 2026-10-29 (8.629).
//
// ITS POSITION IN main.cpp IS LOAD-BEARING and is the reason this file
// exists. On Qt 5 one QCoreApplication::quit() issued while no loop is
// running latches a thread quit, and every later QEventLoop::exec() returns
// at once (8.464) -- so these fixtures were dead on that arm and stepped
// over since 2026-10-20: six blocks, 23 checks.
//
// The quit is not written in any test. It comes from the library:
// InputRouter::on_key() calls qApp->quit() on a quit key and
// on_terminal_lost() calls it unconditionally. Measured 2026-10-29 by
// instrumenting both sites, suite_router reaches them FOURTEEN times, the
// first within a hundred lines of its output -- and the checks around them
// are about exactly that ("Ctrl+C in a text field copies rather than
// quitting", "a read-only field has nothing to copy, so it quits too"). So
// there is no late region to relocate, which is what 8.627 had hoped.
//
// A nested loop still runs at suite_router's ENTRY on Qt 5, at 10 ms, and
// none of the five suites before it quits. Running these blocks from a
// suite registered BEFORE router therefore gives them a process that has
// not quit, which is all they ever needed: no child process, no clearing.
// 8.625 is why clearing in process was rejected -- the only way to clear
// the latch is to run the event loop, and doing that at a block boundary
// dispatches posted events from fixtures already torn down and segfaults.
//
// IF SOMEBODY REORDERS main.cpp's TABLE these fail loudly rather than
// quietly: a latched exec() returns at once, the fixture observes nothing,
// and the checks go red. That is the right failure for an ordering
// dependency and is why this is a comment rather than a guard.
#include <qtty/qtty.h>
#include "src/qt_compat.h"
#include <qtty/drag.h>
#include <qtty/windows.h>
#include <QtWidgets>
#include "src/runtime/handover.h"
#include <QTemporaryDir>
#include <QShortcut>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
// The same shape every suite here uses: the failure carries the condition
// that was false, not only the sentence.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

int suite_loops() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	// PROCESS-WIDE STATE RESTORED ON THE WAY OUT, and this was measured
	// rather than foreseen. One fixture below calls
	// set_keyboard_conventions(true), which runtime.h marks process-wide --
	// and this suite runs BEFORE suite_router, so the setting leaked
	// forward and took four of that suite's checks with it: "Tab advances
	// focus chain", two scroll-area ones, and "F6 does not move between
	// windows until an application asks for the terminal conventions",
	// which names the leak in its own sentence.
	//
	// Suite order was a property nobody had to think about while these
	// blocks lived inside router; moving them made it one. A suite that
	// changes global state and runs first is a suite that rewrites its
	// successors' fixtures.
	const bool had_conventions = keyboard_conventions();

	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(30, 8));
		host.show();
		QCoreApplication::processEvents();
		InputRouter mr(&host);
		Compositor mc(&host, &mr);
		CellBuffer mb(30, 8);
		mc.compose(mb);

		int chosen = 0, drawn_while_up = 0, gave_up = 0;
		for (int round = 0; round < 2; ++round) {      // twice: one proves
			QMenu menu(&host);                         // only the first
			menu.addAction(QStringLiteral("Alpha"));
			QAction *bravo = menu.addAction(QStringLiteral("Bravo"));
			QTimer driver;
			int tries = 0;
			driver.setInterval(5);
			QObject::connect(&driver, &QTimer::timeout, [&] {
				CellBuffer inner(30, 8);
				mc.compose(inner);
				if (inner.to_text().contains(QStringLiteral("Bravo")))
					++drawn_while_up;
				mr.on_key({Qt::Key_Down, QString(), false, false, false});
				mr.on_key({Qt::Key_Down, QString(), false, false, false});
				mr.on_key({Qt::Key_Return, QStringLiteral("\r"), false,
					       false, false});
				if (++tries > 200) { gave_up = 1; menu.close(); }
			});
			driver.start();
			if (menu.exec(QPoint(0, 0)) == bravo) ++chosen;
			driver.stop();
			QCoreApplication::processEvents();
		}
		CHECK(chosen == 2 && !gave_up,
		      "a context menu run with exec() returns the action the keys "
		      "chose, twice in a row -- the idiom an application writes, "
		      "and a nested loop inside the router's own dispatch");
		CHECK(drawn_while_up > 0 && mr.popups().isEmpty(),
		      "and the terminal drew the menu while that loop was spinning, "
		      "then the stack unwound to nothing");
	}
	{
		struct Target : QWidget {
			int enters = 0, moves = 0, drops = 0, leaves = 0;
			QString got;
			explicit Target(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
			void dragEnterEvent(QDragEnterEvent *e) override {
				++enters; e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragMoveEvent(QDragMoveEvent *e) override {
				++moves; e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
			void dropEvent(QDropEvent *e) override {
				++drops; got = e->mimeData()->text();
				e->setDropAction(Qt::MoveAction); e->accept();
			}
		};
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *t = new Target(&h);
		t->setGeometry(0, 0, cw * 10, ch * 2);
		h.resize(GridMetrics::cells(12, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		auto *mime = new QMimeData;
		mime->setText(QStringLiteral("payload"));
		auto *drag = new QDrag(&h);
		drag->setMimeData(mime);

		// The drag is started from a timer rather than inline, because
		// exec_drag() does not return until the drop -- exactly as
		// QDrag::exec() does not. The mouse that drives it therefore has to
		// come from inside the nested loop, which is where a real one comes
		// from too.
		Qt::DropAction acted = Qt::IgnoreAction;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
		});
		acted = Qtty::exec_drag(drag, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();

		printf("info: a drag saw enter=%d move=%d drop=%d leave=%d, payload"
		       " \"%s\", action %d\n", t->enters, t->moves, t->drops,
		       t->leaves, qPrintable(t->got), int(acted));
		CHECK(t->enters == 1 && t->moves >= 1,
		      "a drag reaches the widget under the pointer");
		CHECK(t->drops == 1 && t->got == QStringLiteral("payload"),
		      "and the drop carries the payload");
		CHECK(acted == Qt::MoveAction,
		      "and the action the target chose is what exec_drag returns");

		// Escape ABANDONS a drag, which is what it does on every desktop.
		// drag_cancel() existed, was exported, and had no caller anywhere --
		// so a drag could be started and not given up, with the pointer
		// captured and every widget under it being offered something the
		// user had changed their mind about.
		//
		// The pair is the assertion: the target must have been offered the
		// drag (or cancelling proves nothing, an unstarted drag cancels
		// trivially) and must NOT have received a drop.
		auto *mime2 = new QMimeData;
		mime2->setText(QStringLiteral("abandoned"));
		auto *drag2 = new QDrag(&h);
		drag2->setMimeData(mime2);
		const int drops_before = t->drops, enters_before = t->enters;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_key({Qt::Key_Escape, {}, false, false, false});
		});
		// A bound, because exec_drag() does not return until the drag ends
		// and this check exists precisely to test the thing that ends it.
		// Without it a broken Escape does not fail the check -- it HANGS the
		// suite, which running-code.md says is worse than no gate at all.
		// Measured: with the cancel disabled the run stopped at 212 checks
		// and was killed by the suite's own timeout, and the sabotage harness
		// reported "the code was broken and nothing noticed" because a hang
		// produces neither a PASS nor a FAIL line.
		//
		// The rescue is not a workaround that hides the defect: it records
		// that it was NEEDED, and that is what the check asserts. If Escape
		// works the timer finds no drag and does nothing.
		bool needed_rescue = false;
		// A STACK TIMER rather than singleShot(), because this one must not
		// outlive the block. The lambda captures a local by reference and
		// the drag normally ends promptly, so a fired-later timer wrote
		// through a dangling reference into a dead frame -- and only when
		// some OTHER drag happened to be active, which with five of these
		// in one file is reachable. Destroying it here is what makes the
		// capture safe: the connection goes with it.
		QTimer rescue1;
		rescue1.setSingleShot(true);
		QObject::connect(&rescue1, &QTimer::timeout, [&] {
			if (Qtty::drag_active()) { needed_rescue = true; Qtty::drag_cancel(); }
		});
		rescue1.start(2000);
		const Qt::DropAction gave_up =
		    Qtty::exec_drag(drag2, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();
		printf("info: an abandoned drag entered %d more, dropped %d more,"
		       " returned %d\n", t->enters - enters_before,
		       t->drops - drops_before, int(gave_up));
		CHECK(t->enters > enters_before,
		      "an abandoned drag was offered to the target first");
		CHECK(t->drops == drops_before && gave_up == Qt::IgnoreAction
		      && !needed_rescue,
		      "and Escape abandons it without a drop");

		// CROSSING from one widget to another, which coverage said had
		// never happened: every drag this suite had run saw one target or
		// none. The order is the whole of it, and drag.cpp says why -- the
		// widget being left hears the leave BEFORE the widget being entered
		// hears the enter, because a target that highlights on enter and
		// clears on leave otherwise ends up with two widgets both looking
		// like the drop site.
		//
		// Asserted as a SEQUENCE rather than as two counters. Counters are
		// satisfied by either order, which is exactly the fault.
		{
			QStringList order;
			struct Watched : QWidget {
				QStringList *log = nullptr;
				QString name;
				explicit Watched(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					*log << name + QStringLiteral(":enter");
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override {
					*log << name + QStringLiteral(":leave");
				}
				void dropEvent(QDropEvent *e) override {
					*log << name + QStringLiteral(":drop");
					e->setDropAction(Qt::MoveAction); e->accept();
				}
			};
			QWidget h2;
			h2.setAttribute(Qt::WA_DontShowOnScreen);
			auto *left = new Watched(&h2);
			left->log = &order; left->name = QStringLiteral("left");
			left->setGeometry(0, 0, cw * 5, ch * 2);
			auto *right = new Watched(&h2);
			right->log = &order; right->name = QStringLiteral("right");
			right->setGeometry(cw * 6, 0, cw * 5, ch * 2);
			h2.resize(GridMetrics::cells(12, 4));
			h2.show();
			QCoreApplication::processEvents();
			InputRouter r2(&h2);

			auto *mime3 = new QMimeData;
			mime3->setText(QStringLiteral("crossing"));
			auto *drag3 = new QDrag(&h2);
			drag3->setMimeData(mime3);
			QTimer::singleShot(0, [&] {
				r2.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(8, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(8, 0), 1, false, true, false, 0});
			});
			// The same rescue the abandoned-drag check carries, and for the
			// reason recorded there: a broken drag does not fail this, it
			// HANGS the suite, and a hang produces neither a PASS nor a FAIL.
			bool rescued = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue2;
			rescue2.setSingleShot(true);
			QObject::connect(&rescue2, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) { rescued = true; Qtty::drag_cancel(); }
			});
			rescue2.start(2000);
			Qtty::exec_drag(drag3, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: crossing between two targets gave %s\n",
			       qPrintable(order.join(QStringLiteral(" "))));
			const int lv = order.indexOf(QStringLiteral("left:leave"));
			const int en = order.indexOf(QStringLiteral("right:enter"));
			CHECK(!rescued && lv >= 0 && en >= 0 && lv < en,
			      "a drag crossing from one widget to another tells the one "
			      "it left before the one it reached, so two widgets are "
			      "never both lit as the drop site");
		}

		// And a drop on a widget that was OFFERED the drag and refused it.
		// The else arm: it hears a leave, because it heard an enter, and
		// exec_drag reports IgnoreAction rather than a hopeful default.
		// Never run either -- every drag here had accepted.
		{
			struct Refuser : QWidget {
				int enters = 0, leaves = 0, drops = 0;
				explicit Refuser(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				// Offered and declined, which is not the same as a widget
				// that takes no drops at all: this one is asked and says no,
				// so it is the branch where `over` is set and
				// `over_accepts` is false.
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->ignore();
				}
				void dragMoveEvent(QDragMoveEvent *e) override { e->ignore(); }
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override { ++drops; e->ignore(); }
			};
			QWidget h3;
			h3.setAttribute(Qt::WA_DontShowOnScreen);
			auto *no = new Refuser(&h3);
			no->setGeometry(0, 0, cw * 10, ch * 2);
			h3.resize(GridMetrics::cells(12, 4));
			h3.show();
			QCoreApplication::processEvents();
			InputRouter r3(&h3);

			auto *mime4 = new QMimeData;
			mime4->setText(QStringLiteral("refused"));
			auto *drag4 = new QDrag(&h3);
			drag4->setMimeData(mime4);
			QTimer::singleShot(0, [&] {
				r3.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r3.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
				r3.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
			});
			bool rescued2 = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue3;
			rescue3.setSingleShot(true);
			QObject::connect(&rescue3, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) { rescued2 = true; Qtty::drag_cancel(); }
			});
			rescue3.start(2000);
			const Qt::DropAction refused =
			    Qtty::exec_drag(drag4, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: a refusing target saw enter=%d leave=%d drop=%d,"
			       " and the drag returned %d\n", no->enters, no->leaves,
			       no->drops, int(refused));
			CHECK(!rescued2 && no->enters >= 1 && no->drops == 0
			      && refused == Qt::IgnoreAction,
			      "a widget offered a drag and refusing it gets no drop, and "
			      "the drag reports that nothing was taken");
			// STEPPED OVER ON QT 5, and this is the ONE of the 23 recovered
			// checks that does not pass there -- named precisely rather
			// than hidden behind the blanket guard this file replaced.
			//
			// SETTLED 2026-10-29, and NEITHER SIDE IS WRONG: Qt 5 drops the
			// event in transit. drag_drop_at() does send the leave -- "the
			// target still hears a leave, because it heard an enter" -- and
			// this fixture is right to expect it. Measured with no qtty in
			// the picture, a QDragLeaveEvent through
			// QApplication::sendEvent() on Qt 5 reaches neither the widget
			// nor its event filters:
			//
			//   Qt 5  sendEvent : filter 0, QWidget::event 0, override 0
			//   Qt 5  direct    : filter 0, QWidget::event 1, override 1
			//   Qt 6  sendEvent : filter 1, QWidget::event 1, override 1
			//
			// So QApplication::notify() discards it, and the widget handles
			// it perfectly when reached. QWidget::event() is protected, so
			// the library has no public route to it for an arbitrary
			// widget: on Qt 5 a synthesised drag-leave cannot be delivered
			// at all. 8.630 records what that costs an application -- a
			// drop target that highlights on enter keeps the highlight.
			//
			// Stepped over rather than deleted, because the check is
			// correct and becomes true the moment Qt 5 is dropped or a
			// route is found. Reproduction: remove this guard, run the
			// Qt 5 arm, and read the `info:` line above.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
			CHECK(no->leaves >= 1,
			      "and it still hears the leave, because it heard the enter");
#else
			printf("SKIP: and it still hears the leave, because it heard the"
			       " enter\n      Qt 5's QApplication::notify() discards a"
			       " synthesised QDragLeaveEvent before the widget or its"
			       " filters see it, so the library cannot deliver one"
			       " there -- project.md 8.630\n");
#endif
		}

		// A SECOND DRAG WHILE ONE IS RUNNING, which exec_drag() refuses --
		// and the refusal carries an ownership promise its own comment
		// states: "The QDrag is still consumed, so a caller's ownership rule
		// is the same either way." Branch coverage named this: the `g_live`
		// test had never been true, so neither the refusal nor the
		// consumption had ever run. A missing deleteLater() there would leak
		// in every caller that follows the documented rule, invisibly.
		//
		// Asked from INSIDE the first drag's nested loop, which is the only
		// place the question exists -- a timer is how this fixture reaches
		// into that loop, as the cancel checks above do.
		{
			auto *mime5 = new QMimeData;
			mime5->setText(QStringLiteral("first"));
			auto *drag5 = new QDrag(&h);
			drag5->setMimeData(mime5);
			auto *mime6 = new QMimeData;
			mime6->setText(QStringLiteral("second"));
			auto *drag6 = new QDrag(&h);
			drag6->setMimeData(mime6);
			QPointer<QDrag> watch(drag6);
			Qt::DropAction refused_with = Qt::TargetMoveAction;
			bool asked = false;
			QTimer inner;
			inner.setSingleShot(true);
			QObject::connect(&inner, &QTimer::timeout, [&] {
				asked = true;
				refused_with = Qtty::exec_drag(drag6, Qt::CopyAction);
				// And end the outer one, which is this block's own business
				// rather than the question being asked.
				Qtty::drag_cancel();
			});
			QTimer rescue_second;
			rescue_second.setSingleShot(true);
			QObject::connect(&rescue_second, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) Qtty::drag_cancel();
			});
			inner.start(0);
			rescue_second.start(2000);
			Qtty::exec_drag(drag5, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			printf("info: a second drag inside the first returned %d and the"
			       " QDrag it was handed is %s\n", int(refused_with),
			       watch.isNull() ? "deleted" : "still alive");
			CHECK(asked && refused_with == Qt::IgnoreAction,
			      "a second drag started while one is running is refused"
			      " rather than answered with a nested loop inside a nested"
			      " loop");
			CHECK(watch.isNull(),
			      "and the QDrag it was handed is consumed anyway, so a"
			      " caller's ownership rule is the same whether it was"
			      " refused or run");
		}

		// A DRAG ACROSS A HANDOVER, which is the worst of the family this
		// session has been closing. exec_drag() runs a NESTED event loop and
		// only two things end it: a release and Escape. A user who lets go
		// while a shell or an editor has the terminal has that release
		// delivered THERE, so the loop runs on and the application is in a
		// permanent drag -- every motion going to drag_move_to(), the
		// indicator following the pointer, and Escape the only way out of a
		// state whose cause the user cannot see. 8.447 records that
		// qApp->quit() does not end a nested loop either, so not even a
		// terminal loss clears it.
		//
		// CANCELLED rather than dropped, which is the decision in the fix:
		// the release went somewhere else, so where the user meant to drop
		// is not known, and dropping at the last position seen would perform
		// an action they may never have asked for.
		//
		// The rescue timer is this fixture's own pattern and the reason is
		// recorded above: without it a broken cancel does not fail the check,
		// it HANGS the suite, and a hang produces neither a PASS nor a FAIL.
		// It records that it was NEEDED, and that is what the check asserts.
		{
			struct Handing4 : ITerminalBackend {
				int handed = 0;
				QSize size() const override { return QSize(12, 4); }
				Capabilities capabilities() const override { return Capabilities(); }
				void present(const CellBuffer &, const QRegion &) override {}
				void set_cursor(std::optional<QPoint>, CursorShape) override {}
				void set_event_sink(ITerminalEventSink *) override {}
				void suspend() override {}
				void resume() override {}
				int handovers() const override { return handed; }
			};
			Handing4 back_end;
			Compositor comp(&h, &r);
			FrameScheduler sched(&back_end, &comp, &h);
			auto *mime4 = new QMimeData;
			mime4->setText(QStringLiteral("interrupted"));
			auto *drag4 = new QDrag(&h);
			drag4->setMimeData(mime4);
			const int drops_before = t->drops, enters_before = t->enters;
			QTimer::singleShot(0, [&] {
				// The target is offered the drag first, or cancelling proves
				// nothing -- an unstarted drag cancels trivially.
				r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				back_end.handed = 1;
				sched.render_now();
			});
			bool needed_rescue = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue4;
			rescue4.setSingleShot(true);
			QObject::connect(&rescue4, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) {
					needed_rescue = true;
					Qtty::drag_cancel();
				}
			});
			rescue4.start(2000);
			const Qt::DropAction ended =
			    Qtty::exec_drag(drag4, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: a drag interrupted by a handover entered %d more,"
			       " dropped %d more, returned %d, rescued %d\n",
			       t->enters - enters_before, t->drops - drops_before,
			       int(ended), int(needed_rescue));
			CHECK(t->enters > enters_before,
			      "a drag interrupted by a handover was offered to the"
			      " target first, so the cancel below is not a trivial one");
			CHECK(ended == Qt::IgnoreAction && t->drops == drops_before
			      && !needed_rescue,
			      "and a handover abandons it without a drop, the release"
			      " that would have ended it having gone to whoever had the"
			      " terminal");
		}

		// THE WALK UP THE WIDGET TREE, which line coverage could not see:
		// `drop_target` was 100% of lines and its loop had never taken the
		// step that makes it a loop. Branch coverage says which direction --
		// the `acceptDrops()` test had never been false, so every drag this
		// suite ran landed on a widget that was itself the target.
		//
		// That is the case the function exists for and its comment says so:
		// a label inside a drop area is not itself a target, and the area
		// is. It is also the ordinary shape of a real drop target -- a panel
		// of labels, an item view's viewport -- so the untested path is the
		// common one and the tested one was the special case.
		{
			struct Area : QWidget {
				int enters = 0, drops = 0;
				QString got;
				explicit Area(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dropEvent(QDropEvent *e) override {
					++drops; got = e->mimeData()->text();
					e->setDropAction(Qt::CopyAction); e->accept();
				}
			};
			QWidget h4;
			h4.setAttribute(Qt::WA_DontShowOnScreen);
			auto *area = new Area(&h4);
			area->setGeometry(0, 0, cw * 10, ch * 2);
			// The child takes no drops and is what the pointer is actually
			// over. Without the walk it is the target, and a target that
			// accepts nothing means the drop goes nowhere.
			auto *inner = new QLabel(QStringLiteral("label"), area);
			inner->setGeometry(0, 0, cw * 5, ch);
			h4.resize(GridMetrics::cells(12, 4));
			h4.show();
			QCoreApplication::processEvents();
			InputRouter r4(&h4);

			auto *mime5 = new QMimeData;
			mime5->setText(QStringLiteral("through the child"));
			auto *drag5 = new QDrag(&h4);
			drag5->setMimeData(mime5);
			QTimer::singleShot(0, [&] {
				r4.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r4.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r4.on_mouse({QPoint(2, 0), 1, false, true, false, 0});
			});
			bool rescued3 = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue5;
			rescue5.setSingleShot(true);
			QObject::connect(&rescue5, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) { rescued3 = true; Qtty::drag_cancel(); }
			});
			rescue5.start(2000);
			const Qt::DropAction took =
			    Qtty::exec_drag(drag5, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: a drop over a child gave the area enter=%d drop=%d"
			       " \"%s\"\n", area->enters, area->drops,
			       qPrintable(area->got));
			CHECK(!rescued3 && area->enters >= 1 && area->drops == 1
			      && area->got == QStringLiteral("through the child")
			      && took == Qt::CopyAction,
			      "a drop over a child that takes no drops reaches the "
			      "ancestor that does, which is how every real drop area is "
			      "built");
		}

		// AND OFF THE END OF IT: released where nothing accepts anything.
		// The other direction of the same loop -- it had never run out of
		// parents, so `drop_target` had never returned null and no drag had
		// ever been over nothing. It is what a user does to change their
		// mind: drag out of the drop area and let go over the background.
		{
			struct Area : QWidget {
				int enters = 0, leaves = 0, drops = 0;
				explicit Area(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
				void dragEnterEvent(QDragEnterEvent *e) override {
					++enters; e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					e->setDropAction(Qt::CopyAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override { ++drops; e->accept(); }
			};
			// The HOST counts drag events and takes no drops, and that is
			// what makes this check discriminate. Asserting only that the
			// area got no drop passes against a `drop_target` that returns
			// the widget under the pointer instead of null: the host is then
			// the target, it ignores the enter, `over_accepts` stays false,
			// and the drop is refused anyway -- same leave, same
			// IgnoreAction, same everything a check would look at. Measured:
			// that sabotage passed until the host was watched.
			// NOT asserted here: that nothing was offered the drag over the
			// background. It cannot be, and the reason is worth the lines
			// because the obvious check passes in both worlds.
			//
			// `drop_target` returning null and returning the widget under
			// the pointer differ only in a value nothing can observe. With
			// the walk sabotaged to return `under`, the enter IS sent -- a
			// probe in `drag_move_to` shows `enter -> QWidget accepts=0` --
			// and Qt delivers it nowhere: `QWidget::event()` does not
			// dispatch a drag event to a widget with no drop support, and an
			// event filter on the receiver does not see it either, both
			// measured. The else arm then sends that widget a leave, equally
			// unseen. Same leave to the area, same absent drop, same
			// IgnoreAction.
			//
			// So a "the background was not offered the drag" check would be
			// a check that cannot fail, which this project holds to be worse
			// than none. What defends the walk is the sibling check above,
			// where the ancestor DOES accept drops and the difference is
			// visible.
			QWidget h5;
			h5.setAttribute(Qt::WA_DontShowOnScreen);
			auto *area = new Area(&h5);
			area->setGeometry(0, 0, cw * 4, ch);
			h5.resize(GridMetrics::cells(12, 4));
			h5.show();
			QCoreApplication::processEvents();
			InputRouter r5(&h5);

			auto *mime6 = new QMimeData;
			mime6->setText(QStringLiteral("changed my mind"));
			auto *drag6 = new QDrag(&h5);
			drag6->setMimeData(mime6);
			QTimer::singleShot(0, [&] {
				r5.on_mouse({QPoint(1, 0), 1, false, false, true, 0});
				r5.on_mouse({QPoint(9, 3), 1, false, false, true, 0});
				r5.on_mouse({QPoint(9, 3), 1, false, true, false, 0});
			});
			bool rescued4 = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue6;
			rescue6.setSingleShot(true);
			QObject::connect(&rescue6, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) { rescued4 = true; Qtty::drag_cancel(); }
			});
			rescue6.start(2000);
			const Qt::DropAction nowhere =
			    Qtty::exec_drag(drag6, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: dragged off the area: enter=%d leave=%d drop=%d,"
			       " returned %d\n", area->enters, area->leaves,
			       area->drops, int(nowhere));
			CHECK(!rescued4 && area->enters >= 1 && area->leaves >= 1
			      && area->drops == 0 && nowhere == Qt::IgnoreAction,
			      "dragging off the drop area and letting go over the "
			      "background drops nothing, and the area is told it was "
			      "left");

		}
	}
	// The fifth policy, which needs a nested loop to observe.
	{
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		auto *hv = new QVBoxLayout(&host);
		auto *own = new QLineEdit(QStringLiteral("text"));
		hv->addWidget(own);
		own->setContextMenuPolicy(Qt::ActionsContextMenu);
		int fired = 0;
		auto *sharpen = new QAction(QStringLiteral("&Sharpen"), own);
		QObject::connect(sharpen, &QAction::triggered,
		                 [&fired] { ++fired; });
		own->addAction(sharpen);
		own->addAction(new QAction(QStringLiteral("&Blur"), own));
		host.resize(GridMetrics::cells(30, 8));
		host.show();
		QCoreApplication::processEvents();
		InputRouter ar(&host);
		Compositor ac(&host, &ar);
		Qtty::set_current_window(&host);
		own->setFocus();
		QCoreApplication::processEvents();

		int tries = 0, popups = -1;
		bool drawn = false;
		QTimer poke;
		poke.setInterval(5);
		QObject::connect(&poke, &QTimer::timeout, [&] {
			if (++tries > 40) {
				poke.stop();
				QApplication::closeAllWindows();
				return;
			}
			if (ar.input_popups().isEmpty()) return;
			popups = int(ar.input_popups().size());
			CellBuffer ab(30, 8);
			ac.compose(ab);
			drawn = ab.to_text().contains(QStringLiteral("Sharpen"));
			poke.stop();
			ar.on_key({Qt::Key_Down, QString(), false, false, false});
			ar.on_key({Qt::Key_Return, QString(), false, false, false});
		});
		poke.start();
		ar.on_key({Qt::Key_Menu, QString(), false, false, false});
		QCoreApplication::processEvents();
		CHECK(popups == 1 && drawn && fired == 1,
		      "and ActionsContextMenu opens the menu QT builds from"
		      " the widget's actions -- on the popup stack, drawn,"
		      " and reachable by key, through a nested QMenu::exec()"
		      " inside this router's own dispatch");
		host.hide();
		QCoreApplication::processEvents();
	}
	// The file dialog's directory moves WITH it -- the whole if/else below
	// came from suite_router verbatim, because the guard sat inside the
	// else and nothing after it used `tmp`. The checks assert the dialog
	// LISTS alpha.txt and beta.txt, so a wrongly built directory fails
	// loudly rather than passing quietly.
	QTemporaryDir tmp;
	if (!tmp.isValid()) {
		printf("FAIL: could not make a directory for the file dialog\n");
		++fails;
	} else {
		for (const char *name : { "alpha.txt", "beta.txt" }) {
			QFile f(tmp.filePath(QString::fromLatin1(name)));
			f.open(QIODevice::WriteOnly);
			f.close();
		}
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(80, 24));
		host.show();
		QCoreApplication::processEvents();
		InputRouter fr(&host);
		QFileDialog dlg(&host, QStringLiteral("Open"), tmp.path());
		dlg.setOption(QFileDialog::DontUseNativeDialog, true);
		dlg.setFileMode(QFileDialog::ExistingFile);
		dlg.setAttribute(Qt::WA_DontShowOnScreen);
		dlg.resize(GridMetrics::cells(60, 18));
		bool listed = false;
		QTimer::singleShot(0, &dlg, [&] {
			QCoreApplication::processEvents();
			Compositor fc(&host, &fr);
			// WAITED FOR, because QFileSystemModel fills itself on
			// another thread and a frame composed before it has
			// finished holds an empty list. The first version of
			// this check composed once and passed -- from THIS
			// directory, by the timing it happened to get; run from
			// elsewhere the same check failed, which is a fixture
			// that works for a reason it does not state. Bounded,
			// so a dialog that never lists anything fails rather
			// than hanging.
			for (int spin = 0; spin < 200 && !listed; ++spin) {
				QCoreApplication::processEvents();
				QThread::msleep(5);
				QCoreApplication::processEvents();
				CellBuffer probe(80, 24);
				fc.compose(probe);
				const QString seen = probe.to_text();
				listed = seen.contains(QStringLiteral("alpha.txt"))
				      && seen.contains(QStringLiteral("beta.txt"));
			}
			for (QChar c : QStringLiteral("alpha.txt"))
				fr.on_key({0, QString(c), false, false, false});
			QCoreApplication::processEvents();
			fr.on_key({Qt::Key_Return, QString(), false, false, false});
			QCoreApplication::processEvents();
			if (dlg.isVisible()) dlg.reject();
		});
		const int answer = dlg.exec();
		const QStringList picked = dlg.selectedFiles();
		CHECK(listed,
		      "a file dialog draws the directory it was opened on, "
		      "names and all -- the largest composite widget an "
		      "application opens, and the one nothing here had tried");
		CHECK(answer == QDialog::Accepted && picked.size() == 1
		      && picked.first().endsWith(QStringLiteral("alpha.txt")),
		      "and a name typed into it and accepted with Return "
		      "reaches the application, so Open works with no mouse "
		      "anywhere in it");
		GridGuard::reset();
	}
	// COPIED, not moved: suite_router's later checks share this host and
	// router, so the fixture stays there too. Verbatim from that file, and
	// trimmed to the SETUP -- the first copy ran to the guard and brought
	// two of router's own checks with it, so they ran twice and count-check
	// caught the tree at 2475 where project.md said 2472. A copied fixture
	// that reaches past its setup duplicates assertions silently; the gate
	// is what made it a number instead of a surprise. 8.629.
	{
		set_keyboard_conventions(true);
		QWidget host;
		host.setAttribute(Qt::WA_DontShowOnScreen);
		host.resize(GridMetrics::cells(40, 12));
		auto *behind = new QLineEdit(&host);
		behind->setGeometry(0, 0, 20 * cw, ch);
		host.show();
		QCoreApplication::processEvents();
		InputRouter dr(&host);
		behind->setFocus();
		set_focus_widget(behind);
		{
			QDialog dlg(&host);
			auto *field = new QLineEdit(&dlg);
			field->setGeometry(0, 0, 20 * cw, ch);
			dlg.setAttribute(Qt::WA_DontShowOnScreen);
			dlg.resize(GridMetrics::cells(30, 4));
			bool typed_in = false;
			QTimer::singleShot(0, &dlg, [&] {
				QCoreApplication::processEvents();
				dr.on_key({0, QStringLiteral("z"), false, false, false});
				QCoreApplication::processEvents();
				typed_in = field->text() == QStringLiteral("z");
				dlg.accept();
			});
			dlg.exec();
			CHECK(typed_in,
			      "and the same through exec(), where a lost keystroke is "
			      "what an application would actually ship");
		}
	}
	// ---- a drag whose source dies under it -------------------------------
	//
	// THE QDrag BELONGS TO THE WIDGET THE DRAG STARTED FROM, which is what
	// drag.h asks for: exec_drag() is a drop-in for QDrag::exec(), so what an
	// application brings to it is whatever it wrote for QDrag -- and the
	// spelling in Qt's own documentation is `new QDrag(this)` inside a mouse
	// handler. That parents the QDrag to the SOURCE WIDGET and ties its
	// lifetime to it, and exec_drag() then runs a nested event loop --
	// arbitrary application code, for as long as the button is held down.
	//
	// The sequence an application performs to reach it is ordinary. An item
	// is dragged out of a panel; the drop handler takes it; the panel is now
	// empty and closes itself, or the dialog it lived in is dismissed.
	// `delete panel` runs inside dropEvent(), the QDrag parented to it dies
	// with it, drag_drop_at() quits the loop, and exec_drag() resumes at
	// `drag->deleteLater()` holding freed memory. THAT IS THE LOUD ONE: a
	// heap-use-after-free on the QDrag, which the sanitized suite aborts on.
	//
	// The same death can arrive EARLIER, while the loop is still running, and
	// that half is why a null test at the end alone would not do. The
	// live-drag record keeps the QMimeData the QDrag OWNS and deletes, so
	// every move after the source died handed the target a freed payload --
	// and a drop target reads its payload, that being what a drop target is
	// for. The quiet version of that one is the worse: a QMimeData
	// reallocated where the old one stood answers text() with somebody
	// else's string and nothing crashes at all.
	//
	// Ending the drag is also the only answer that TERMINATES. A guard that
	// merely returned from drag_move_to() and drag_drop_at() would leave the
	// nested loop with nothing left to quit it, and a hang is worse than a
	// crash -- it produces neither a PASS nor a FAIL. `!rescued` is what
	// asserts the drag ended on its own, and the rescue timers are the ones
	// the drag checks above already carry, for that reason.
	{
		struct Taker : QWidget {
			int drops = 0;
			QString got;
			QWidget **kill = nullptr;
			explicit Taker(QWidget *p) : QWidget(p) { setAcceptDrops(true); }
			void dragEnterEvent(QDragEnterEvent *e) override {
				e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dragMoveEvent(QDragMoveEvent *e) override {
				e->setDropAction(Qt::MoveAction); e->accept();
			}
			void dropEvent(QDropEvent *e) override {
				++drops;
				got = e->mimeData()->text();
				e->setDropAction(Qt::MoveAction); e->accept();
				// The panel the item came out of closes behind it, which
				// is what takes the QDrag parented to it.
				if (kill && *kill) { delete *kill; *kill = nullptr; }
			}
		};
		QWidget h;
		h.setAttribute(Qt::WA_DontShowOnScreen);
		auto *t = new Taker(&h);
		t->setGeometry(0, 0, cw * 10, ch * 2);
		auto *panel = new QWidget(&h);
		panel->setGeometry(0, ch * 2, cw * 10, ch);
		h.resize(GridMetrics::cells(12, 4));
		h.show();
		QCoreApplication::processEvents();
		InputRouter r(&h);

		auto *mime = new QMimeData;
		mime->setText(QStringLiteral("torn out"));
		auto *drag = new QDrag(panel);       // Qt's own `new QDrag(this)`
		drag->setMimeData(mime);
		QWidget *doomed = panel;
		t->kill = &doomed;
		QTimer::singleShot(0, [&] {
			r.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
			r.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
		});
		bool rescued = false;
		// A STACK TIMER rather than singleShot(), because this one must not
		// outlive the block. The lambda captures a local by reference and
		// the drag normally ends promptly, so a fired-later timer wrote
		// through a dangling reference into a dead frame -- and only when
		// some OTHER drag happened to be active, which with five of these
		// in one file is reachable. Destroying it here is what makes the
		// capture safe: the connection goes with it.
		QTimer rescue7;
		rescue7.setSingleShot(true);
		QObject::connect(&rescue7, &QTimer::timeout, [&] {
			if (Qtty::drag_active()) { rescued = true; Qtty::drag_cancel(); }
		});
		rescue7.start(2000);
		const Qt::DropAction took =
		    Qtty::exec_drag(drag, Qt::CopyAction | Qt::MoveAction);
		QCoreApplication::processEvents();
		printf("info: a drop that closed the drag source: drops=%d payload"
		       " \"%s\", the source is %s, exec_drag returned %d\n",
		       t->drops, qPrintable(t->got), doomed ? "alive" : "gone",
		       int(took));
		CHECK(!rescued && t->drops == 1 && doomed == nullptr
		      && took == Qt::MoveAction,
		      "a drop handler that closes the drag source, deleting the "
		      "QDrag parented to it, leaves exec_drag returning the action "
		      "the target took rather than deleting a QDrag already gone");

		// AND THE SAME DEATH DURING THE LOOP, which is the other half. A
		// dragMoveEvent handler that closes the source -- a panel that
		// collapses as its last item leaves it, a dialog dismissed while
		// the button is still down -- kills the QDrag in the MIDDLE of the
		// drag rather than at the end of it, and what the unfixed code
		// does next is hand the target another move, and then a drop,
		// carrying the QMimeData the QDrag deleted on its way out.
		//
		// Asserted as "nothing more was delivered" rather than by reading
		// the payload. A fixture that dereferenced the freed QMimeData
		// itself would be diagnosing its own line rather than the
		// library's, and under the sanitizers it would abort before any of
		// this could print.
		{
			struct Fragile : QWidget {
				int moves = 0, drops = 0, leaves = 0, after_death = 0;
				QWidget **kill = nullptr;
				explicit Fragile(QWidget *p) : QWidget(p) {
					setAcceptDrops(true);
				}
				void dragEnterEvent(QDragEnterEvent *e) override {
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragMoveEvent(QDragMoveEvent *e) override {
					++moves;
					if (kill && *kill) { delete *kill; *kill = nullptr; }
					else ++after_death;
					e->setDropAction(Qt::MoveAction); e->accept();
				}
				void dragLeaveEvent(QDragLeaveEvent *) override { ++leaves; }
				void dropEvent(QDropEvent *e) override {
					++drops; e->setDropAction(Qt::MoveAction); e->accept();
				}
			};
			QWidget h2;
			h2.setAttribute(Qt::WA_DontShowOnScreen);
			auto *f = new Fragile(&h2);
			f->setGeometry(0, 0, cw * 10, ch * 2);
			auto *panel2 = new QWidget(&h2);
			panel2->setGeometry(0, ch * 2, cw * 10, ch);
			h2.resize(GridMetrics::cells(12, 4));
			h2.show();
			QCoreApplication::processEvents();
			InputRouter r2(&h2);

			auto *mime2 = new QMimeData;
			mime2->setText(QStringLiteral("half way"));
			auto *drag2 = new QDrag(panel2);
			drag2->setMimeData(mime2);
			QWidget *doomed2 = panel2;
			f->kill = &doomed2;
			// FOUR events, not three: the first motion is the enter, which
			// returns before any move is delivered, so the kill happens on
			// the second and the third is the one that would carry a freed
			// payload.
			QTimer::singleShot(0, [&] {
				r2.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r2.on_mouse({QPoint(4, 1), 1, false, false, true, 0});
				r2.on_mouse({QPoint(5, 1), 1, false, false, true, 0});
				r2.on_mouse({QPoint(5, 1), 1, false, true, false, 0});
			});
			bool rescued2 = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue8;
			rescue8.setSingleShot(true);
			QObject::connect(&rescue8, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) {
					rescued2 = true; Qtty::drag_cancel();
				}
			});
			rescue8.start(2000);
			const Qt::DropAction stopped =
			    Qtty::exec_drag(drag2, Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: the source died mid-drag: moves=%d after_death=%d"
			       " leave=%d drop=%d, returned %d\n", f->moves,
			       f->after_death, f->leaves, f->drops, int(stopped));
			CHECK(!rescued2 && doomed2 == nullptr && f->after_death == 0
			      && f->drops == 0 && f->leaves >= 1
			      && stopped == Qt::IgnoreAction,
			      "a drag whose source is destroyed while the loop is "
			      "still running ends there and tells the target it was "
			      "left, rather than offering it another move and a drop "
			      "carrying a QMimeData the QDrag freed");

			// AND THE DEATH INSIDE THE MOVE THE RELEASE ITSELF SENDS,
			// which neither of the two above reaches. drag_drop_at()
			// delivers a move before the drop, and its own comment says
			// why: a target that has never seen this position has to
			// decide on it before being asked to take a drop there. So
			// the last handler run before every drop is one the
			// application wrote, and it can close the source just as the
			// drop handler can -- at which point the drop below it would
			// carry a QMimeData that no longer exists.
			//
			// A guard at the TOP of drag_drop_at() cannot see this: it
			// has already run. What catches it is the second look, after
			// the move and before the drop, and this is the check that
			// holds that one -- two events, so the only move in the whole
			// drag is the one the release sends.
			QWidget h_late;
			h_late.setAttribute(Qt::WA_DontShowOnScreen);
			auto *f_late = new Fragile(&h_late);
			f_late->setGeometry(0, 0, cw * 10, ch * 2);
			auto *panel_late = new QWidget(&h_late);
			panel_late->setGeometry(0, ch * 2, cw * 10, ch);
			h_late.resize(GridMetrics::cells(12, 4));
			h_late.show();
			QCoreApplication::processEvents();
			InputRouter r_late(&h_late);

			auto *mime_late = new QMimeData;
			mime_late->setText(QStringLiteral("on the way up"));
			auto *drag_late = new QDrag(panel_late);
			drag_late->setMimeData(mime_late);
			QWidget *doomed_late = panel_late;
			f_late->kill = &doomed_late;
			QTimer::singleShot(0, [&] {
				r_late.on_mouse({QPoint(2, 0), 1, false, false, true, 0});
				r_late.on_mouse({QPoint(4, 1), 1, false, true, false, 0});
			});
			bool rescued_late = false;
			// A STACK TIMER rather than singleShot(), because this one must not
			// outlive the block. The lambda captures a local by reference and
			// the drag normally ends promptly, so a fired-later timer wrote
			// through a dangling reference into a dead frame -- and only when
			// some OTHER drag happened to be active, which with five of these
			// in one file is reachable. Destroying it here is what makes the
			// capture safe: the connection goes with it.
			QTimer rescue9;
			rescue9.setSingleShot(true);
			QObject::connect(&rescue9, &QTimer::timeout, [&] {
				if (Qtty::drag_active()) {
					rescued_late = true; Qtty::drag_cancel();
				}
			});
			rescue9.start(2000);
			const Qt::DropAction ended =
			    Qtty::exec_drag(drag_late,
			                    Qt::CopyAction | Qt::MoveAction);
			QCoreApplication::processEvents();
			printf("info: the source died in the release's own move:"
			       " moves=%d leave=%d drop=%d, returned %d\n",
			       f_late->moves, f_late->leaves, f_late->drops,
			       int(ended));
			CHECK(!rescued_late && doomed_late == nullptr
			      && f_late->moves == 1 && f_late->drops == 0
			      && f_late->leaves >= 1 && ended == Qt::IgnoreAction,
			      "and a source closed by the last move before the button "
			      "came up takes the drop with it, rather than being "
			      "handed one whose payload the QDrag has already freed");
		}
		GridGuard::reset();
	}

	set_keyboard_conventions(had_conventions);
	return fails;
}
