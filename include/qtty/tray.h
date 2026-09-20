// qtty/tray.h -- a system tray icon for a program with no display.
//
// A terminal has no notification area of its own, and for a long time that
// sentence was taken to end the matter. It does not. The tray belongs to the
// DESKTOP the terminal is running on, and a modern desktop's tray is not a
// window at all -- it is a D-Bus object, org.kde.StatusNotifierItem, which a
// program without a display connection can publish perfectly well.
//
// QSystemTrayIcon cannot: Qt asks the PLATFORM for a tray implementation
// (QPlatformTheme::createPlatformSystemTrayIcon) and qtty's offscreen
// platform supplies none, so Qt falls back to its legacy XEmbed path and
// isSystemTrayAvailable() answers false however healthy the desktop is.
// Measured 2026-09-06, with a StatusNotifierWatcher present on the bus and
// Qt still answering false. So the missing piece was never the medium; it
// was that Qt asks a question qtty's platform cannot answer.
//
// The API is QSystemTrayIcon's, deliberately and as far as it goes, so an
// application writes one class and gets a tray in a windowed build and in a
// terminal build alike -- which is the whole point. What is NOT here is
// noted at each place rather than left to be discovered.
#pragma once
#include <QObject>
#include <QIcon>
#include <QString>
#include <memory>

namespace Qtty {

class SystemTrayIcon : public QObject {
	Q_OBJECT
public:
	// QSystemTrayIcon's, so a switch on the reason ports unchanged.
	enum ActivationReason { Unknown, Context, DoubleClick, Trigger, MiddleClick };
	Q_ENUM(ActivationReason)

	explicit SystemTrayIcon(QObject *parent = nullptr);
	~SystemTrayIcon() override;

	// Whether a tray exists to put an icon IN. False when there is no
	// session bus, or a bus with nobody watching -- a terminal on a server,
	// or a desktop with the tray turned off.
	//
	// An application is expected to ASK, and to show its window when the
	// answer is no. That is not a terminal special case: not every desktop
	// has a tray either, and a program that hides itself into one that is
	// not there has hidden itself nowhere.
	static bool is_available();

	// By NAME, which is the form to prefer. The desktop draws a themed icon
	// itself at whatever size its panel uses, so the picture never passes
	// through a cell grid and none of qtty's rendering applies to it.
	void set_icon_name(const QString &freedesktop_name);
	// By PIXMAP, for a program that draws its own. Sent as ARGB32 over the
	// bus, again drawn by the desktop.
	void set_icon(const QIcon &);
	void set_tool_tip(const QString &);
	// Active, Passive or NeedsAttention -- the desktop may show the last
	// more prominently. Spelled as the specification spells it.
	void set_status(const QString &);

	void show();
	void hide();
	bool is_visible() const;

	// A NOTIFICATION, which is QSystemTrayIcon::showMessage() and was the
	// most-used part of that class missing from this one. An application
	// that says "backup finished" got nothing here and had to write a
	// branch, which is the thing this header exists to avoid.
	//
	// It is a SECOND desktop service, not part of the tray: the
	// StatusNotifierItem specification has no notification method, and a
	// desktop's bubbles come from org.freedesktop.Notifications. So it is
	// asked about separately -- a machine can have one and not the other,
	// and a headless server usually has neither.
	static bool messages_available();

	// Returns whether the desktop took it. `icon_name` is a freedesktop
	// icon name, drawn by the desktop like the tray icon and never through
	// a cell grid; empty asks for none. A negative timeout leaves the
	// duration to the desktop, which is what QSystemTrayIcon's default
	// argument means too, and 0 asks for one that stays until dismissed.
	bool show_message(const QString &title, const QString &body,
	                  const QString &icon_name = QString(),
	                  int timeout_ms = -1);

signals:
	// The desktop reports WHICH gesture, and the reasons above mirror
	// QSystemTrayIcon's so an existing slot needs no rewriting.
	void activated(Qtty::SystemTrayIcon::ActivationReason reason);

	// The user clicked the notification, which is QSystemTrayIcon's
	// messageClicked().
	//
	// IT FIRES ONLY WHERE THE DESKTOP SUPPORTS ACTIONS, and that is asked
	// rather than assumed: the specification's Notify takes a list of
	// actions, a daemon that does not implement them says so in
	// GetCapabilities, and one that is not asked reports nothing when the
	// bubble is clicked. So an application must not treat the absence of
	// this signal as the absence of a click -- show_message() having
	// returned true is what says the notification went out.
	void message_clicked();

private slots:
	// The daemon's broadcast, filtered to the notifications this object
	// sent. Private because it is wire plumbing rather than API, and a slot
	// because QDBusConnection::connect() takes one.
	void on_action_invoked(uint id, const QString &action);

private:
	struct Private;
	std::unique_ptr<Private> d_;
};

} // namespace Qtty
