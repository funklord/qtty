// The tray gate's two halves in one program: a stand-in for the desktop's
// StatusNotifierWatcher, and a Qtty::SystemTrayIcon published against it.
//
// It is a tool rather than a suite check because it needs a SESSION BUS, and
// the suite must run on a machine that has none -- a build server, a
// container, this project's own sanitizer and valgrind arms. `make test-tray`
// supplies one with dbus-run-session; nothing else in the tree needs it.
#include <qtty/qtty.h>
#include <qtty/tray.h>
#include <QtWidgets>
#include <QtDBus>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *what) {
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

// Enough of org.kde.StatusNotifierWatcher for a real item to register
// against it, and nothing more. What it records is what a desktop would.
class Watcher : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
	Q_PROPERTY(bool IsStatusNotifierHostRegistered READ host_registered)
public:
	bool host_registered() const { return true; }
	QString last_item;
public slots:
	void RegisterStatusNotifierItem(const QString &s) { last_item = s; }
	void RegisterStatusNotifierHost(const QString &) {}
};

int main(int argc, char **argv) {
	Qtty::prepare_environment();
	QApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("qtty-tray-check"));
	Qtty::setup(app);

	// `--require-bus` is the caller saying it has SUPPLIED one: `make
	// test-tray` runs this under dbus-run-session, so a missing bus there
	// is not a machine without D-Bus, it is the harness failing to do what
	// it promised. Skipping in that case reports success having measured
	// nothing, which is the vacuous pass this project keeps paying for --
	// so the guard's failure becomes the caller's failure instead.
	bool require_bus = false;
	for (int i = 1; i < argc; ++i)
		if (QLatin1String(argv[i]) == QLatin1String("--require-bus"))
			require_bus = true;

	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		if (require_bus) {
			printf("FAIL: --require-bus was given and there is no "
			       "session bus, so the gate measured nothing\n");
			return 1;
		}
		printf("SKIP: no session bus, so nothing here can be measured\n");
		return 0;
	}

	// Before the watcher exists there is no tray, and that is the case an
	// application asks about before hiding itself into one. Asserted FIRST,
	// so the positive below cannot pass by the property being hardcoded.
	check(!Qtty::SystemTrayIcon::is_available(),
	      "with nobody watching, no tray is reported");

	Watcher w;
	bus.registerObject(QStringLiteral("/StatusNotifierWatcher"), &w,
	                   QDBusConnection::ExportAllContents);
	check(bus.registerService(QStringLiteral("org.kde.StatusNotifierWatcher")),
	      "a stand-in desktop tray takes its name on the bus");
	check(Qtty::SystemTrayIcon::is_available(),
	      "and now a tray is reported");

	Qtty::SystemTrayIcon tray;
	int activations = 0;
	Qtty::SystemTrayIcon::ActivationReason why =
	    Qtty::SystemTrayIcon::Unknown;
	QObject::connect(&tray, &Qtty::SystemTrayIcon::activated,
	                 [&](Qtty::SystemTrayIcon::ActivationReason r) {
		                 ++activations; why = r;
	                 });
	tray.set_icon_name(QStringLiteral("drive-harddisk"));
	tray.set_tool_tip(QStringLiteral("md0 degraded"));
	tray.set_status(QStringLiteral("NeedsAttention"));
	tray.show();
	QCoreApplication::processEvents();

	check(tray.is_visible(), "an icon published from a program with no display");
	check(!w.last_item.isEmpty(), "and the desktop was told which name to read");

	// Read the properties back over the bus, as a tray host does. This is
	// the half that proves the icon is USABLE rather than merely announced:
	// a service can exist and export nothing.
	QDBusInterface item(w.last_item, QStringLiteral("/StatusNotifierItem"),
	                    QStringLiteral("org.freedesktop.DBus.Properties"), bus);
	const auto get = [&](const char *name) {
		const QDBusReply<QDBusVariant> r =
		    item.call(QStringLiteral("Get"),
		              QStringLiteral("org.kde.StatusNotifierItem"),
		              QString::fromLatin1(name));
		return r.isValid() ? r.value().variant().toString() : QString();
	};
	printf("info: the desktop reads IconName=%s Status=%s ToolTipTitle=%s\n",
	       qPrintable(get("IconName")), qPrintable(get("Status")),
	       qPrintable(get("ToolTipTitle")));
	check(get("IconName") == QStringLiteral("drive-harddisk")
	      && get("Status") == QStringLiteral("NeedsAttention")
	      && get("ToolTipTitle") == QStringLiteral("md0 degraded"),
	      "carrying the name, status and tooltip it was given");

	// A click in the panel is a method call on that object.
	QDBusInterface act(w.last_item, QStringLiteral("/StatusNotifierItem"),
	                   QStringLiteral("org.kde.StatusNotifierItem"), bus);
	act.call(QStringLiteral("Activate"), 0, 0);
	QCoreApplication::processEvents();
	check(activations == 1 && why == Qtty::SystemTrayIcon::Trigger,
	      "and a click in the panel reaches the program");

	// Hiding drops the NAME, which is how the specification says an icon
	// goes away -- there is no Unregister call to make.
	tray.hide();
	QCoreApplication::processEvents();
	check(!tray.is_visible()
	      && !bus.interface()->isServiceRegistered(w.last_item).value(),
	      "hiding takes the icon off the bus");

	printf("test-tray: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
#include "main.moc"
