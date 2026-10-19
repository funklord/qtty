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
// below is handed both. Hashed by their components, which is what Qt 6's own
// overloads do -- and declared at global scope because that is where argument
// lookup for a Qt type looks. Guarded to Qt 5 so a later Qt adding them
// cannot collide with these.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QRect>
#include <QSize>
inline uint qHash(const QSize &s, uint seed = 0) {
	return qHash(s.width(), seed) ^ qHash(s.height(), seed + 1);
}
inline uint qHash(const QRect &r, uint seed = 0) {
	return qHash(r.x(), seed) ^ qHash(r.y(), seed + 1)
	     ^ qHash(r.width(), seed + 2) ^ qHash(r.height(), seed + 3);
}
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
