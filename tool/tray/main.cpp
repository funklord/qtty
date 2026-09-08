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

	// set_icon(), the PIXMAP half of this interface, which until now was
	// public API that nothing in the tree ever called -- not the suite, not
	// this gate, not the example. Its sibling set_icon_name() is exercised
	// above, and a reviewer reading the header would have found the pair
	// complete, because it is: what was missing is a caller.
	//
	// It is also the half that matters most. An icon by NAME is drawn by
	// the desktop from its own theme; an icon by PIXMAP is the case for a
	// program that encodes state as SHAPE rather than colour, which is the
	// accessible way to do it and the reason to send pixels at all.
	{
		QImage art(22, 22, QImage::Format_ARGB32);
		art.fill(Qt::transparent);
		// One opaque pixel of a colour whose four channels are all
		// different, so a byte order that is wrong in ANY way -- BGRA,
		// RGBA, a swapped alpha -- reads differently. A grey or a
		// primary would survive several wrong orders unchanged.
		art.setPixelColor(3, 5, QColor(0x11, 0x22, 0x33, 0xf4));
		tray.set_icon(QIcon(QPixmap::fromImage(art)));
		QCoreApplication::processEvents();

		const QDBusReply<QDBusVariant> r =
		    item.call(QStringLiteral("Get"),
		              QStringLiteral("org.kde.StatusNotifierItem"),
		              QStringLiteral("IconPixmap"));
		int count = 0, w22 = 0, h22 = 0;
		bool bytes_ok = false;
		// CONST, and the distinction is not style: QDBusArgument's
		// beginArray() has a writing overload and a reading one, and a
		// non-const object selects the writer -- which aborts the process
		// with "write from a read-only object" rather than returning an
		// error. Demarshalling wants the const overloads throughout.
		if (r.isValid()) {
			const QDBusArgument arg =
			    r.value().variant().value<QDBusArgument>();
			arg.beginArray();
			while (!arg.atEnd()) {
				int width = 0, height = 0;
				QByteArray data;
				arg.beginStructure();
				arg >> width >> height >> data;
				arg.endStructure();
				++count;
				if (width != 22) continue;
				w22 = width;
				h22 = height;
				// The specification says ARGB32 in NETWORK byte order, so
				// the pixel at (3,5) must read A,R,G,B in that order.
				const int at = (5 * width + 3) * 4;
				bytes_ok = data.size() == width * height * 4
				           && at + 3 < data.size()
				           && quint8(data[at])     == 0xf4
				           && quint8(data[at + 1]) == 0x11
				           && quint8(data[at + 2]) == 0x22
				           && quint8(data[at + 3]) == 0x33;
			}
			arg.endArray();
		}
		printf("info: the desktop is offered %d pixmap size(s)\n", count);
		check(count == 3 && w22 == 22 && h22 == 22,
		      "a pixmap icon is offered at the three sizes panels ask for");
		check(bytes_ok,
		      "and its pixels go out as ARGB32 in network byte order");
	}

	// A click in the panel is a method call on that object.
	QDBusInterface act(w.last_item, QStringLiteral("/StatusNotifierItem"),
	                   QStringLiteral("org.kde.StatusNotifierItem"), bus);
	act.call(QStringLiteral("Activate"), 0, 0);
	QCoreApplication::processEvents();
	check(activations == 1 && why == Qtty::SystemTrayIcon::Trigger,
	      "and a click in the panel reaches the program");

	// THE OTHER TWO BUTTONS. The header promises this enum is
	// QSystemTrayIcon's "so a switch on the reason ports unchanged", and an
	// application that ports such a switch has three arms this adaptor can
	// reach: Trigger, MiddleClick and Context. One of the three was
	// checked.
	//
	// They are not exotic. A right-click on a tray icon is how nearly every
	// one of them is used at all, and it arrives as a different D-Bus
	// method rather than as a parameter -- so the three are three separate
	// slots, and a wrong constant in any of them sends the application down
	// the wrong arm of its switch with nothing to say so. Method calls
	// rather than signals, which is what makes this the reliable half of
	// this gate.
	const struct { const char *method; Qtty::SystemTrayIcon::ActivationReason r;
	               const char *what; } buttons[] = {
		{ "SecondaryActivate", Qtty::SystemTrayIcon::MiddleClick, "middle" },
		{ "ContextMenu",       Qtty::SystemTrayIcon::Context,     "right"  },
	};
	for (const auto &b : buttons) {
		const int before = activations;
		why = Qtty::SystemTrayIcon::Unknown;
		act.call(QString::fromLatin1(b.method), 0, 0);
		QCoreApplication::processEvents();
		if (activations == before + 1 && why == b.r)
			printf("PASS: a %s click in the panel reaches the program as "
			       "its own reason\n", b.what);
		else {
			printf("FAIL: a %s click in the panel reaches the program as "
			       "its own reason\n"
			       "      condition: %d activation(s), reason %d\n",
			       b.what, activations - before, int(why));
			++failures;
		}
	}

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
