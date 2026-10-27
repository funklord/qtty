// qt_compat.h -- the version conditionals, in one file.
//
// section 8.1 recorded for a long time that this tree has "no version conditionals
// anywhere", and that adding the first ones would settle by hand what
// belongs to the copyright holder. They were authorised on 2026-10-19 with
// the instruction to do the Qt 5.15 port, and they live here rather than at
// their call sites for the reason that section gives: a conditional is a
// statement about which versions are supported, and a statement is easier to
// read, to change and to withdraw when it is made once in one place.
//
// Seven constructs, re-measured 2026-10-19 by section 8.1's own command -- 32 error
// lines, the same count that section records -- and each is a spelling
// except where this file says otherwise.
#pragma once

#include <QtGlobal>
#include <QKeySequence>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEvent>
#include <QAction>

// Qt 6.6 added QPalette::Accent. There is no older equivalent and it cannot
// be aliased to Highlight: theme.cpp switches on both and its own comment
// gives Accent 12 "rather than Highlight's 4", so an alias would be a
// duplicate case rather than a fallback. The sites guard on this instead,
// and a Qt 5 build simply has no accent -- which is the state the default
// theme is already asserted to be in.
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#  define QTTY_HAS_PALETTE_ACCENT 1
#else
#  define QTTY_HAS_PALETTE_ACCENT 0
#endif

// Qt 6 added qHash for QRect and QSize; Qt 5 has neither, and hash_multi()
// below is handed both. Declared at global scope because that is where
// argument lookup for a Qt type looks, and guarded to Qt 5 so a later Qt
// adding them cannot collide with these.
//
// COMBINED, NOT XORED, and the difference was measured rather than argued.
// These read
//
//     qHash(s.width(), seed) ^ qHash(s.height(), seed + 1)
//
// and the comment above them claimed that was "what Qt 6's own overloads
// do". It is not. Qt 5's qHash(int, uint) is `uint(key) ^ seed` -- measured,
// qHash(7, 0) is 7 -- so the seeds fall straight back out of an XOR: for a
// QRect the four of them are 0^1^2^3, which is 0, leaving a bare
// x ^ y ^ w ^ h that ignores its seed entirely. Every QSize(a, b) collided
// with QSize(b, a), and QRect(0,0,10,10), QRect(0,0,5,5) and QRect(3,3,7,7)
// all hashed to 0. Over every cell rect a 200x60 terminal can hold at a few
// sizes -- 57600 of them -- Qt 5 produced 64 distinct values where Qt 6
// produced 57600, and an ImageEncodeKey built from them came to 1.6 per cent
// of its buckets.
//
// THAT WAS NEVER A CORRECTNESS FAULT, which is why it had gone unnoticed and
// why it is a small fix rather than an urgent one: QHash resolves a bucket
// collision with operator==, and ImageEncodeKey's compares all six of its
// fields. Both of its caches are also pruned every frame, so they hold what
// is on the screen and a short scan costs nothing. What it leaves is a trap
// for the next cache keyed on one of these, which would inherit 0.1 per cent
// and look fine.
//
// The shape is hash_multi()'s own, below: accumulate with the 31 multiplier.
// Measured on that same population it gives 57600 of 57600 standalone and
// through hash_multi, which is the whole of what is claimed for it. It is a
// BUCKETING hash over a handful of small integers and nothing more -- over
// 1.6 million rects with coordinates to 200 it falls to 12.3 per cent,
// because 31^3 is 29791 and the products start to overlap. Where a hash
// VALUE has to carry identity rather than choose a bucket, this is the wrong
// tool and src/render/content_hash.h is the right one; 8.616 is what that
// mistake costs.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QRect>
#include <QSize>
inline uint qHash(const QSize &s, uint seed = 0) {
	uint h = seed;
	h = h * 31 + uint(s.width());
	h = h * 31 + uint(s.height());
	return h;
}
inline uint qHash(const QRect &r, uint seed = 0) {
	uint h = seed;
	h = h * 31 + uint(r.x());
	h = h * 31 + uint(r.y());
	h = h * 31 + uint(r.width());
	h = h * 31 + uint(r.height());
	return h;
}
#endif

// WHETHER A NESTED QEventLoop CAN BE TRUSTED. On Qt 5, one
// QCoreApplication::quit() issued while no loop is running latches a thread
// quit that makes every later QEventLoop::exec() return immediately without
// running anything -- permanently, measured in 8.464 with a twelve-line
// reproduction that answers 0 ms on Qt 5 and 10 ms on Qt 6.
//
// The suite does exactly that in its quit-key checks, which exist BECAUSE
// the quit is unobservable, so every fixture needing a nested loop after
// them is dead there. Set by the copyright holder 2026-10-20: those
// fixtures step over on Qt 5 rather than failing.
//
// This is not a statement that nested loops are broken on Qt 5 -- they work
// in a process that has not issued a stray quit. It is a statement about
// this suite, which has.
//
// MEASURED, so the step-over is not taken on trust. Forcing this to 1 and
// running the Qt 5 arm un-skips 23 blocks, of which **19 fail**: the context
// menu through exec(), the whole of drag and drop, ActionsContextMenu and
// the file dialog. So the guard is substantively right and not merely
// convenient. It also disposes of the obvious objection, that a compile-time
// macro cannot see WHERE in the run it sits and so skips blocks that precede
// any quit: the context-menu block at suite_router.cpp:5989 is thousands of
// lines above the first set_quit_keys() and fails all the same, which places
// the latch earlier still. Reordering the quit-key checks would therefore
// recover nothing, and what it would take is a bigger restructuring than
// this macro. 8.618 carries the numbers and names the open question.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define QTTY_NESTED_LOOPS_USABLE 1
#else
#  define QTTY_NESTED_LOOPS_USABLE 0
#endif

namespace Qtty {

// Qt::ColorScheme IS NOT HERE. It is the one of the seven that appears in a
// PUBLIC header, so its alias lives in include/qtty/application.h beside the
// function that returns it -- a public declaration cannot name a type defined
// in this file. That section's judgement that it "is not a spelling" is
// softened there rather than here: on Qt 6 the alias IS Qt's own enum, so the
// API qtty offers does not change.

// Qt 6 replaced the combined int of a key chord with QKeyCombination. The
// combined int is what both versions can be built FROM, so one helper
// answers for both and the call sites stop naming either type.
inline int combined_key(Qt::KeyboardModifiers mods, Qt::Key key) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return QKeyCombination(mods, key).toCombined();
#else
	return int(mods) | int(key);
#endif
}

// And the other direction: an element of a QKeySequence is a
// QKeyCombination on Qt 6 and an int on Qt 5. Both convert to the combined
// int, which is what the caller compares.
inline int combined_of(const QKeySequence &seq, int at) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return seq[at].toCombined();
#else
	return seq[at];
#endif
}

// And its two halves. A combined int carries the modifiers in the high bits
// and the key in the low ones on both versions, which is what makes one
// spelling answer for both -- Qt 6's QKeyCombination accessors do the same
// masking behind a type.
inline Qt::KeyboardModifiers combined_mods(int combined) {
	return Qt::KeyboardModifiers(combined & int(Qt::KeyboardModifierMask));
}

inline int combined_key_only(int combined) {
	return combined & ~int(Qt::KeyboardModifierMask);
}

// Qt 6 narrowed QWidget::enterEvent from QEvent to QEnterEvent, so an
// override's signature differs by version and `override` is what catches it.
// One name for the parameter type lets a widget override it on both.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using EnterEvent = QEnterEvent;
#else
using EnterEvent = QEvent;
#endif

// Qt 6 renamed QMouseEvent::localPos() to position() and changed its return
// from QPointF to QPointF -- the name moved, not the meaning.
inline QPointF event_position(const QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position();
#else
	return e->localPos();
#endif
}

// And the same rename on a wheel event, whose position() arrived with it.
//
// NO CONDITIONAL, because there is nothing left to condition on: Qt 5.15
// has position() and deprecates posF() in favour of it --
// QT_DEPRECATED_VERSION_X_5_15("Use position()") in qevent.h -- and
// design.md section 10 states Qt 5.15 and Qt 6 as what this project
// supports. So one spelling serves both and the deprecated branch was
// costing a warning for nothing.
//
// It was costing it quietly, which is why it stood. The warning fires only
// when a translation unit including this header is recompiled, so an
// incremental Qt 5 build does not show it: of four Qt 5 runs in one
// afternoon, one reported it and three did not, the difference being
// whether grid_style.cpp had been touched. And 8.588's census of the
// warning-free build was taken on the Qt 6 arm, where this line does not
// exist. 8.603.
inline QPointF event_position(const QWheelEvent *e) {
	return e->position();
}

// WHAT QHash WANTS A qHash TO RETURN AND TAKE. Qt 6 looks for
// `size_t qHash(const T &, size_t)` and Qt 5 for `uint qHash(const T &,
// uint)`, both with a default seed. A type that gives only one of the two
// compiles against the other version by accident if at all -- measured in
// 8.475, where the size_t form built on Qt 6 and the Qt 5 library then
// failed with "no matching function for call to qHash" while the suite
// linked the stale library and reported a cache that was never in it.
//
// An alias rather than a conditional at each use, so a hashable type writes
// one function and no `#if` splits a brace.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using HashSeed = size_t;
#else
using HashSeed = uint;
#endif

// Qt 6 added qHashMulti. Combining by hand is what it does, and the shape
// here is Qt's own documented one rather than an invention: seed each value
// into the accumulator with the multiplier QHash uses.
template <typename... Args>
inline size_t hash_multi(size_t seed, const Args &...args) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return qHashMulti(seed, args...);
#else
	size_t h = seed;
	const size_t each[] = { size_t(qHash(args))... };
	for (size_t v : each) h = h * 31 + v;
	return h;
#endif
}

// Qt 6.1 added QStyle::name(), which answers the string QStyleFactory takes.
// Qt 5 has no such accessor, and section 8.1 notes that className() "is not the
// string a factory takes" -- QFusionStyle rather than Fusion. Deriving it is
// a HEURISTIC and is written as one: strip a leading Q and a trailing Style,
// which is how every name QStyleFactory::keys() returns is spelled. A style
// whose class is named otherwise answers something a factory will refuse,
// and the caller of this already handles a null from the factory.
inline QString style_name(const QStyle *s) {
	if (!s) return QString();
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
	return s->name();
#else
	QString n = QString::fromLatin1(s->metaObject()->className());
	if (n.startsWith(QLatin1Char('Q'))) n.remove(0, 1);
	if (n.endsWith(QLatin1String("Style"))) n.chop(5);
	return n;
#endif
}

// Qt 6 renamed QAction::associatedWidgets() to associatedObjects() and
// widened it from QWidget to QObject. Every caller here casts back to
// QWidget, so the narrower Qt 5 list is the same answer reached sooner.
inline QList<QObject *> associated_objects(const QAction *a) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return a->associatedObjects();
#else
	QList<QObject *> out;
	const auto widgets = a->associatedWidgets();
	out.reserve(widgets.size());
	for (QWidget *w : widgets) out.append(w);
	return out;
#endif
}

} // namespace Qtty
