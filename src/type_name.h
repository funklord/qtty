// src/type_name.h -- what a diagnostic calls a widget, when the widget's own
// class does not say.
//
// INTERNAL. Not shipped in include/qtty/: it answers a question only this
// library's warnings ask.
//
// `QObject::metaObject()->className()` is the nearest class that declared
// Q_OBJECT, which is the right answer when the subject IS a Qt class and
// the wrong one whenever an application wrote the widget. Q_OBJECT is
// needed for signals, slots and properties, and a widget that implements an
// interface and draws itself needs none of the three -- so a plot, a meter
// or a custom view often declares no Q_OBJECT at all, and this tree's own
// fixtures do not either.
//
// Measured 2026-10-26: the both-interfaces warning in cell_paint.cpp named
// every such widget `QWidget`, which is nothing an author can search their
// own tree for. GridGuard's off-grid warning had the same fault, and it is
// the diagnostic this project most relies on -- its whole job is to name a
// widget whose geometry an application can correct.
//
// typeid() reads the real dynamic type, and __cxa_demangle() turns the
// Itanium ABI's spelling into the class's own. The mangled form is the
// fallback and is still recognisable, being the name behind a length
// prefix, so a compiler with no demangler degrades to `8MyWidget` rather
// than to silence.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdlib>
#include <typeinfo>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif

namespace Qtty {

inline QString dynamic_type_name(const QObject *o) {
	if (!o) return QStringLiteral("(null)");
	const char *const raw = typeid(*o).name();
#if defined(__GNUG__)
	int status = 0;
	char *const plain = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
	if (plain) {
		const QString out = QString::fromLatin1(status == 0 ? plain : raw);
		::free(plain);
		return out;
	}
#endif
	return QString::fromLatin1(raw);
}

// The key a "told about this class once" set wants. Not the QMetaObject: a
// widget with no Q_OBJECT shares its base's, so a second offending class
// would be found already told and never reported -- which is how a
// diagnostic goes quiet about everything after the first.
inline QByteArray dynamic_type_key(const QObject *o) {
	return o ? QByteArray(typeid(*o).name()) : QByteArray();
}

} // namespace Qtty
