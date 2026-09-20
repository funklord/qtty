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
// A STAND-IN NOTIFICATION DAEMON, which is a second service rather than
// part of the tray: org.freedesktop.Notifications is what a desktop's
// bubbles come from, and the StatusNotifierItem specification has no
// notification method at all. It records what it was told so the check can
// read the arguments back rather than merely observing that a call
// happened.
class Notifier : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
	// Whether this stand-in claims to support actions. Both states are
	// exercised, because the whole of message_clicked()'s contract is that
	// it fires only where a click can be reported.
	QStringList caps;
	QString app, summary, body, icon;
	QStringList actions;
	uint replaces = 0;
	int timeout = 0;
	int notifies = 0;
	uint next = 1;
	// Read from the main thread while the daemon thread writes, so every
	// field goes through this. A test that raced here would fail rarely
	// and blame the library.
	mutable QMutex lock;
	bool ready = false;
	uint last_id() { QMutexLocker g(&lock); return next - 1; }
	QString summary_of() { QMutexLocker g(&lock); return summary; }
	QString body_of() { QMutexLocker g(&lock); return body; }
	QString icon_of() { QMutexLocker g(&lock); return icon; }
	QString app_of() { QMutexLocker g(&lock); return app; }
	QStringList actions_of() { QMutexLocker g(&lock); return actions; }
	int timeout_of() { QMutexLocker g(&lock); return timeout; }
	uint replaces_of() { QMutexLocker g(&lock); return replaces; }
	void set_caps(const QStringList &c) { QMutexLocker g(&lock); caps = c; }
signals:
	// What the daemon broadcasts when the user clicks a bubble's button.
	// It is a BROADCAST: every program on the bus sees every click, which
	// is why the library filters on the ids it sent.
	void ActionInvoked(uint id, const QString &action);
public slots:
	// Emitted from the daemon's own thread, so the signal leaves on the
	// daemon's connection -- which is the whole reason the thread exists.
	void emit_action(uint id, const QString &action) {
		emit ActionInvoked(id, action);
	}
	QStringList GetCapabilities() { QMutexLocker g(&lock); return caps; }
	uint Notify(const QString &a, uint r, const QString &ic, const QString &s,
	            const QString &b, const QStringList &acts, const QVariantMap &,
	            int t) {
		QMutexLocker g(&lock);
		app = a; replaces = r; icon = ic; summary = s; body = b;
		actions = acts; timeout = t; ++notifies;
		return next++;
	}
};

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

	// -- NOTIFICATIONS, which are a second service and are asked about
	//    separately. Before the daemon exists there is none, asserted first
	//    so the positive below cannot pass on a hardcoded true.
	check(!Qtty::SystemTrayIcon::messages_available(),
	      "with no notification daemon, no messages are reported");
	check(!tray.show_message(QStringLiteral("Backup"), QStringLiteral("done")),
	      "and sending one says so rather than claiming the user was told");

	Notifier daemon;
	bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
	                   &daemon, QDBusConnection::ExportAllContents);
	check(bus.registerService(QStringLiteral("org.freedesktop.Notifications")),
	      "a stand-in notification daemon takes its name on the bus");
	check(Qtty::SystemTrayIcon::messages_available(),
	      "and now messages are reported available");

	int clicks = 0;
	QObject::connect(&tray, &Qtty::SystemTrayIcon::message_clicked,
	                 [&] { ++clicks; });

	// A daemon WITHOUT actions, which is the conservative half: the bubble
	// goes out, no action list is sent, and no click can be reported.
	daemon.set_caps(QStringList());
	check(tray.show_message(QStringLiteral("Backup"),
	                        QStringLiteral("finished"),
	                        QStringLiteral("drive-harddisk"), 5000),
	      "a notification from a program with no display");
	check(daemon.summary_of() == QStringLiteral("Backup")
	      && daemon.body_of() == QStringLiteral("finished")
	      && daemon.icon_of() == QStringLiteral("drive-harddisk")
	      && daemon.timeout_of() == 5000,
	      "and it carried the title, the body, the icon and the timeout");
	check(daemon.app_of() == QStringLiteral("qtty-tray-check"),
	      "under the application's own name, which is what the desktop "
	      "groups and mutes by");
	check(daemon.actions_of().isEmpty(),
	      "with no actions asked of a daemon that has none, which is what "
	      "keeps message_clicked() from being a signal that cannot fire");

	// The REPLACES id, which is why a program reporting progress does not
	// leave a column of bubbles behind it.
	const uint first = daemon.last_id();
	check(tray.show_message(QStringLiteral("Backup"),
	                        QStringLiteral("still going")),
	      "a second notification is sent");
	check(daemon.replaces_of() == first,
	      "and replaces the first rather than stacking on it");

	// A daemon WITH actions, and a second icon object because the
	// capability is asked once per object -- which is a property of the
	// daemon and not of the program, so asking again would be a round trip
	// per message.
	daemon.set_caps(QStringList() << QStringLiteral("actions"));
	Qtty::SystemTrayIcon acting;
	int acting_clicks = 0;
	QObject::connect(&acting, &Qtty::SystemTrayIcon::message_clicked,
	                 [&] { ++acting_clicks; });
	check(acting.show_message(QStringLiteral("Update"),
	                          QStringLiteral("ready to install")),
	      "a notification to a daemon that has actions");
	check(daemon.actions_of().contains(QStringLiteral("default")),
	      "carries a default action, without which no click could be "
	      "reported at all");
	const uint mine = daemon.last_id();
	// THE CLICK IS SENT FROM A SECOND CONNECTION, and that is not
	// fussiness. D-Bus does not loop a signal back to the connection that
	// emitted it, so a stand-in daemon sharing the library's connection
	// can never deliver ActionInvoked to it -- measured: the bubble went
	// out, the click was emitted, and the program heard nothing. A real
	// daemon is another process, so the fixture has to be another
	// connection or it is testing a shape the world does not have.
	//
	// A SIGNAL rather than the whole daemon, because a blocking call whose
	// answer must come from the same thread is a deadlock: moving the
	// daemon's object to the second connection made Notify itself time
	// out. A signal is fire-and-forget and has no such problem.
	// THE CLICK IS DELIVERED BY HAND, and the reason is a limit of the
	// fixture rather than of the feature. D-Bus does not loop a signal
	// back to the connection that emitted it, so a stand-in daemon sharing
	// this program's connection cannot deliver ActionInvoked to it --
	// measured: the bubble went out, the click was emitted, and nothing
	// arrived. Moving the daemon to its own connection deadlocks instead,
	// Notify being a blocking call whose answer would have to come from
	// the same thread, and moving it to a thread of its own crashed this
	// tool before it printed a line.
	//
	// So what is exercised here is the SLOT and its filters, which is
	// where the logic is; the bus delivery is the one line
	// QDBusConnection::connect() returns true for, and proving it needs a
	// second PROCESS. Recorded rather than left to look proven.
	const auto click = [&](uint id, const QString &action) {
		QMetaObject::invokeMethod(&acting, "on_action_invoked",
		                          Qt::DirectConnection,
		                          Q_ARG(uint, id), Q_ARG(QString, action));
		QCoreApplication::processEvents();
	};
	click(mine, QStringLiteral("default"));
	check(acting_clicks == 1, "and clicking it reaches the program");
	// THE FILTER, which is what makes the signal a statement about THIS
	// program: the broadcast carries every bubble's id, including other
	// programs'.
	click(mine + 4000, QStringLiteral("default"));
	click(mine, QStringLiteral("something-else"));
	check(acting_clicks == 1,
	      "while another program's notification and another button are "
	      "not this program's click");
	check(clicks == 0,
	      "and the icon that sent no action gets no click either, which is "
	      "the contract its capability check exists to keep");

	printf("test-tray: notifications checked against a stand-in daemon\n");

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
