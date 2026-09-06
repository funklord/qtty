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

signals:
	// The desktop reports WHICH gesture, and the reasons above mirror
	// QSystemTrayIcon's so an existing slot needs no rewriting.
	void activated(Qtty::SystemTrayIcon::ActivationReason reason);

private:
	struct Private;
	std::unique_ptr<Private> d_;
};

} // namespace Qtty
