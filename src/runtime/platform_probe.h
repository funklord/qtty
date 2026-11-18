// src/runtime/platform_probe.h -- is the platform plugin qtty forces actually
// there?
//
// design.md section 13's R2b names two mitigations: a CI matrix, which
// `make test-versions` is, and "vendor-check the plugin's presence at
// startup", which nothing did until this file -- and which nothing recorded
// as outstanding either, so the row read as taken. project.md 8.783.
//
// WHAT A USER MEETS WITHOUT IT. prepare_environment() FORCES
// QT_QPA_PLATFORM=offscreen, so on a packaged Qt that ships no offscreen
// plugin the program dies in Qt's own abort -- "no Qt platform plugin could
// be initialized", with a list of what IS available. That message is not
// bad; what it cannot say is that the platform it failed on was qtty's
// choice rather than the user's, which is the one fact that tells somebody
// what to install.
//
// A WARNING AND NEVER A REFUSAL, for a measured reason: a statically linked
// Qt ships no plugin FILES at all, so a file-presence test refuses a build
// that works. The predicate below therefore answers only the case it can be
// sure of, and the caller prints rather than exits -- Qt's own machinery is
// still what decides whether the platform loads.
#pragma once
#include <QDir>
#include <QString>

namespace Qtty {

// True only when the plugins directory IS there, carries no offscreen
// platform plugin, and the build links no static plugins. Each clause is a
// way of not knowing:
//
//   - a directory that does not exist means Qt is installed somewhere this
//     cannot see, which is not evidence about the plugin;
//   - a static build has no files to find, so an empty directory says
//     nothing about what is linked in.
//
// Pure, and takes both facts as arguments, so the three ways of not knowing
// are testable without a Qt that is missing anything.
inline bool offscreen_plugin_absent(const QString &plugins_dir,
                                    bool static_plugins_present) {
	if (static_plugins_present) return false;
	QDir dir(plugins_dir + QStringLiteral("/platforms"));
	if (!dir.exists()) return false;
	// The name Qt gives it on each platform it is built for. Matched as a
	// prefix of the base name rather than exactly, because the suffix is the
	// platform's (`.so`, `.dylib`, `.dll`) and a debug build adds its own.
	const QStringList files = dir.entryList(QDir::Files);
	for (const QString &f : files)
		if (f.contains(QStringLiteral("qoffscreen"))) return false;
	return true;
}

}   // namespace Qtty
