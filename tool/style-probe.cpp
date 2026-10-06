// Does setup() keep the application's own style as GridStyle's proxy base?
//
// design.md section 12 promises it does -- "an app's custom style is not lost,
// it becomes GridStyle's proxy base" -- and 0b carried the opposite as a
// flagged contradiction until the copyright holder settled it in the
// document's favour. 8.380 has the fix and the measurement.
//
// A binary of its own because the suite CANNOT hold this. setup() runs once
// per process and test/main.cpp has already called it, so no in-process check
// can exercise a style chosen BEFORE setup at all. Not shipped and in no
// .pro, the way tool/install-probe.cpp and tool/screen-probe.cpp are not: a
// binary only a gate uses does not belong in an install.
//
// IT CARRIES ITS OWN CONTROL, and the control is about discrimination rather
// than about firing. Asking for the style that is also the fallback proves
// nothing -- the base would read correct however the code behaved -- so this
// picks a key that is NOT the fallback and refuses to report anything when
// the Qt it is built against offers no such key.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

int main(int argc, char **argv) {
	Qtty::prepare_environment();
	QApplication app(argc, argv);

	// The fallback setup() uses when it cannot rebuild what it found. A key
	// equal to it cannot discriminate, so it is not a candidate.
	const QString fallback = QStringLiteral("fusion");
	QString want;
	for (const QString &k : QStyleFactory::keys())
		if (k.toLower() != fallback) { want = k; break; }
	if (want.isEmpty()) {
		printf("style-probe: SKIPPED -- this Qt offers only %s, so a wrapped"
		       " base cannot be told from the fallback\n",
		       qPrintable(fallback));
		return 0;
	}

	QStyle *const chosen = QStyleFactory::create(want);
	if (!chosen) {
		fprintf(stderr, "style-probe: QStyleFactory lists %s and will not"
		        " create it, so nothing was measured\n", qPrintable(want));
		return 1;
	}
	app.setStyle(chosen);
	const QString before = app.style()->name();
	if (before.toLower() == fallback) {
		fprintf(stderr, "style-probe: asked for %s and got %s, which is the"
		        " fallback -- this cannot discriminate\n",
		        qPrintable(want), qPrintable(before));
		return 1;
	}

	Qtty::setup(app);

	auto *const proxy = qobject_cast<QProxyStyle *>(app.style());
	if (!proxy) {
		fprintf(stderr, "style-probe: setup() left a %s rather than a proxy,"
		        " so there is no base to read\n",
		        app.style() ? app.style()->metaObject()->className() : "null");
		return 1;
	}
	const QString base = proxy->baseStyle() ? proxy->baseStyle()->name()
	                                        : QString();
	printf("style-probe: chose %s before setup(); the proxy base is %s\n",
	       qPrintable(before), base.isEmpty() ? "none" : qPrintable(base));
	if (base != before) {
		fprintf(stderr, "style-probe: design.md section 12 says an"
		        " application's style becomes the proxy base, and %s was"
		        " replaced by %s\n", qPrintable(before),
		        base.isEmpty() ? "nothing" : qPrintable(base));
		return 1;
	}
	return 0;
}
