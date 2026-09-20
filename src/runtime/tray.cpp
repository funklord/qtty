// The StatusNotifierItem half of qtty/tray.h.
//
// The specification is small and its useful part is smaller: a service name
// the desktop can find, an object exporting some properties, and a handful of
// methods it calls when the user does something. Everything here is either
// that or a reason.
#include "qtty/tray.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>

namespace Qtty {

// The pixmap form the specification asks for: width, height, and ARGB32 in
// NETWORK byte order. Not a QImage's native order, and getting that wrong
// gives a recognisable picture in the wrong colours rather than an error,
// which is why it is spelled out rather than memcpy'd.
struct ArgbIcon {
	int width = 0;
	int height = 0;
	QByteArray data;
};

QDBusArgument &operator<<(QDBusArgument &a, const ArgbIcon &i) {
	a.beginStructure();
	a << i.width << i.height << i.data;
	a.endStructure();
	return a;
}

const QDBusArgument &operator>>(const QDBusArgument &a, ArgbIcon &i) {
	a.beginStructure();
	a >> i.width >> i.height >> i.data;
	a.endStructure();
	return a;
}

using ArgbIconList = QList<ArgbIcon>;

} // namespace Qtty

Q_DECLARE_METATYPE(Qtty::ArgbIcon)
Q_DECLARE_METATYPE(Qtty::ArgbIconList)

namespace Qtty {

static ArgbIcon to_argb(const QIcon &icon, int size) {
	ArgbIcon out;
	const QImage img = icon.pixmap(size, size).toImage()
	                       .convertToFormat(QImage::Format_ARGB32);
	if (img.isNull()) return out;
	out.width = img.width();
	out.height = img.height();
	out.data.resize(img.width() * img.height() * 4);
	char *p = out.data.data();
	for (int y = 0; y < img.height(); ++y) {
		const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
		for (int x = 0; x < img.width(); ++x) {
			const QRgb c = row[x];
			*p++ = char(qAlpha(c));
			*p++ = char(qRed(c));
			*p++ = char(qGreen(c));
			*p++ = char(qBlue(c));
		}
	}
	return out;
}

namespace {
const char *kWatcherService = "org.kde.StatusNotifierWatcher";
const char *kWatcherPath    = "/StatusNotifierWatcher";
const char *kItemInterface  = "org.kde.StatusNotifierItem";
const char *kItemPath       = "/StatusNotifierItem";
// The notification daemon, which is a different service from the tray and
// is asked about separately: a machine can run one and not the other.
const char *kNotifyService  = "org.freedesktop.Notifications";
const char *kNotifyPath     = "/org/freedesktop/Notifications";

} // namespace

// The exported object. Its PROPERTIES are what a tray host reads to draw the
// icon, and its SLOTS are what the host calls when the user acts -- so this
// class is the whole wire surface, and everything public in tray.h is a
// setter for one of these properties.
class TrayAdaptor : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
	Q_PROPERTY(QString Category READ category)
	Q_PROPERTY(QString Id READ id)
	Q_PROPERTY(QString Title READ title)
	Q_PROPERTY(QString Status READ status)
	Q_PROPERTY(QString IconName READ icon_name)
	Q_PROPERTY(Qtty::ArgbIconList IconPixmap READ icon_pixmap)
	Q_PROPERTY(QString ToolTipTitle READ tool_tip)
	Q_PROPERTY(bool ItemIsMenu READ item_is_menu)
public:
	QString category() const { return QStringLiteral("ApplicationStatus"); }
	QString id() const { return id_; }
	QString title() const { return title_; }
	QString status() const { return status_; }
	QString icon_name() const { return icon_name_; }
	ArgbIconList icon_pixmap() const { return pixmaps_; }
	QString tool_tip() const { return tool_tip_; }
	// False, so the desktop delivers Activate rather than opening a menu we
	// do not export. A dbusmenu is a second specification and is not here;
	// an application gets the click and puts up its own window.
	bool item_is_menu() const { return false; }

	QString id_ = QStringLiteral("qtty");
	QString title_;
	QString status_ = QStringLiteral("Active");
	QString icon_name_;
	QString tool_tip_;
	ArgbIconList pixmaps_;

	// Reported through a plain callback rather than a Qt signal, and that is
	// not a style choice. Every signal on an object exported with
	// ExportAllContents is offered to the bus, and QtDBus cannot marshal an
	// enum nobody registered -- so a signal here printed two warnings on
	// every tray icon a program created:
	//
	//     Skipped method "acted" : Type not registered with QtDBus
	//     QDBusAbstractAdaptor: Cannot relay signal ...
	//
	// A library that talks on the user's terminal for its own internal
	// reasons is the noise this project spends its time removing, and the
	// activation is not part of the wire protocol anyway. The three
	// notifications below ARE, and stay signals.
	std::function<void(SystemTrayIcon::ActivationReason)> acted;

public slots:
	void Activate(int, int)          { if (acted) acted(SystemTrayIcon::Trigger); }
	void SecondaryActivate(int, int) { if (acted) acted(SystemTrayIcon::MiddleClick); }
	void ContextMenu(int, int)       { if (acted) acted(SystemTrayIcon::Context); }
	void Scroll(int, const QString &) {}

signals:
	// The specification's change notifications. A host that has already read
	// the properties re-reads them when these fire.
	void NewIcon();
	void NewToolTip();
	void NewStatus(const QString &);
	// Title is a separate property with a separate notification, and
	// set_tool_tip() moves BOTH -- it writes the tooltip and the title from
	// the one string, because a tray icon's title is what a panel shows
	// when it will not show a tooltip. Emitting only NewToolTip left every
	// host that reads Title showing the value it read at registration for
	// ever, with the property correct underneath it, which is why no
	// property read can see it: the property agrees either way.
	//
	// NOT DEFENDED BY A CHECK, and that is worth knowing rather than
	// assuming. `make test-tray` reads these properties and does not
	// listen for the notifications; an attempt to make it listen is
	// written up in project.md section 8.45 and was withdrawn because it
	// could not be made to answer the same way twice. What stands behind
	// this line is the specification and a measurement taken by hand --
	// with it, a subscriber saw the title notification; without it, that
	// count was zero while the other three stayed one.
	void NewTitle();
};

struct SystemTrayIcon::Private {
	TrayAdaptor adaptor;
	QString service;
	bool visible = false;
	bool registered = false;
	// The notification half. `sent` is what makes message_clicked() a
	// statement about THIS object: ActionInvoked is broadcast with the id
	// of whatever bubble was clicked, so without it a program would report
	// a click on another program's notification.
	bool asked_actions = false;
	bool has_actions = false;
	uint last_message = 0;
	QSet<uint> sent;
};

static bool watcher_present()
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) return false;
	if (!bus.interface()) return false;
	const QDBusReply<bool> has =
	    bus.interface()->isServiceRegistered(QLatin1String(kWatcherService));
	return has.isValid() && has.value();
}

bool SystemTrayIcon::is_available()
{
	// Two questions, and both have to be yes: a session bus at all, and
	// somebody on it willing to draw an icon. A bus with no watcher is the
	// ordinary state of a server, and answering true there would have an
	// application hide itself into nothing.
	return watcher_present();
}

SystemTrayIcon::SystemTrayIcon(QObject *parent)
    : QObject(parent), d_(std::make_unique<Private>())
{
	qDBusRegisterMetaType<ArgbIcon>();
	qDBusRegisterMetaType<QList<ArgbIcon>>();
	d_->adaptor.id_ = QCoreApplication::applicationName().isEmpty()
	                      ? QStringLiteral("qtty")
	                      : QCoreApplication::applicationName();
	d_->adaptor.acted = [this](ActivationReason r) { emit activated(r); };
}

SystemTrayIcon::~SystemTrayIcon() { hide(); }

void SystemTrayIcon::set_icon_name(const QString &name)
{
	d_->adaptor.icon_name_ = name;
	emit d_->adaptor.NewIcon();
}

void SystemTrayIcon::set_icon(const QIcon &icon)
{
	// Three sizes, which is what panels ask for in practice; the host picks.
	d_->adaptor.pixmaps_.clear();
	for (int size : {16, 22, 24}) {
		const ArgbIcon a = to_argb(icon, size);
		if (a.width > 0) d_->adaptor.pixmaps_ << a;
	}
	emit d_->adaptor.NewIcon();
}

void SystemTrayIcon::set_tool_tip(const QString &text)
{
	d_->adaptor.tool_tip_ = text;
	d_->adaptor.title_ = text;
	emit d_->adaptor.NewToolTip();
	emit d_->adaptor.NewTitle();
}

void SystemTrayIcon::set_status(const QString &s)
{
	d_->adaptor.status_ = s;
	emit d_->adaptor.NewStatus(s);
}

bool SystemTrayIcon::is_visible() const { return d_->visible; }

void SystemTrayIcon::show()
{
	if (d_->visible) return;
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) return;

	// The service name the specification prescribes: the interface name, the
	// process id, and an instance number, so two icons from one program do
	// not collide.
	if (!d_->registered) {
		static int instance = 0;
		d_->service = QStringLiteral("%1-%2-%3")
		                  .arg(QLatin1String(kItemInterface))
		                  .arg(QCoreApplication::applicationPid())
		                  .arg(++instance);
		if (!bus.registerObject(QLatin1String(kItemPath), &d_->adaptor,
		                        QDBusConnection::ExportAllContents))
			return;
		if (!bus.registerService(d_->service)) return;
		d_->registered = true;
	}

	QDBusInterface watcher(QLatin1String(kWatcherService),
	                       QLatin1String(kWatcherPath),
	                       QLatin1String(kWatcherService), bus);
	const QDBusReply<void> reply =
	    watcher.call(QStringLiteral("RegisterStatusNotifierItem"), d_->service);
	d_->visible = reply.isValid();
}

bool SystemTrayIcon::messages_available()
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) return false;
	if (!bus.interface()) return false;
	// The same shape as is_available() above and for the same reason: a bus
	// with nobody listening is the ordinary state of a server, and an
	// application that told the user something into nothing has not told
	// them.
	const QDBusReply<bool> has =
	    bus.interface()->isServiceRegistered(QLatin1String(kNotifyService));
	return has.isValid() && has.value();
}

bool SystemTrayIcon::show_message(const QString &title, const QString &body,
                                  const QString &icon_name, int timeout_ms)
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) return false;
	QDBusInterface notifier(QLatin1String(kNotifyService),
	                        QLatin1String(kNotifyPath),
	                        QLatin1String(kNotifyService), bus);
	if (!notifier.isValid()) return false;

	// ACTIONS ONLY WHERE THE DAEMON HAS THEM. Notify takes a list, and a
	// daemon that does not implement actions either ignores it or refuses
	// the call -- so the capability is asked for rather than assumed, and
	// message_clicked() is a signal an application only gets where a click
	// can actually be reported. Asked once: the answer is a property of the
	// daemon and the daemon does not change under a running program.
	if (!d_->asked_actions) {
		d_->asked_actions = true;
		const QDBusReply<QStringList> caps =
		    notifier.call(QStringLiteral("GetCapabilities"));
		d_->has_actions = caps.isValid()
		               && caps.value().contains(QStringLiteral("actions"));
		if (d_->has_actions) {
			// One connection, not one per message. ActionInvoked carries
			// the id of the notification it belongs to, so the slot filters
			// on the ids this object sent rather than reporting a click on
			// somebody else's bubble.
			bus.connect(QLatin1String(kNotifyService),
			            QLatin1String(kNotifyPath),
			            QLatin1String(kNotifyService),
			            QStringLiteral("ActionInvoked"), this,
			            SLOT(on_action_invoked(uint, QString)));
		}
	}

	QStringList actions;
	if (d_->has_actions)
		actions << QStringLiteral("default") << QStringLiteral("Open");

	// The replaces_id is the LAST id this object was given, so a program
	// that reports progress replaces its own bubble rather than stacking a
	// column of them -- which is what a desktop application does and what
	// QSystemTrayIcon's single balloon looks like.
	const QDBusReply<uint> reply = notifier.call(
	    QStringLiteral("Notify"), d_->adaptor.id_, d_->last_message,
	    icon_name, title, body, actions, QVariantMap(), timeout_ms);
	if (!reply.isValid()) return false;
	d_->last_message = reply.value();
	d_->sent.insert(d_->last_message);
	return true;
}

void SystemTrayIcon::on_action_invoked(uint id, const QString &action)
{
	// The id filter is the point: the signal is a broadcast and every
	// program on the bus sees every click. The action is checked too, so a
	// desktop that offers more than one button later cannot turn a
	// different choice into this signal.
	if (!d_->sent.contains(id)) return;
	if (action != QStringLiteral("default")) return;
	emit message_clicked();
}

void SystemTrayIcon::hide()
{
	if (!d_->registered) return;
	// There is no Unregister in the specification -- a host watches for the
	// service disappearing instead -- so dropping the NAME is how an icon is
	// taken away. The object stays exported so show() can put it back.
	QDBusConnection::sessionBus().unregisterService(d_->service);
	d_->registered = false;
	d_->visible = false;
}

} // namespace Qtty

#include "tray.moc"
