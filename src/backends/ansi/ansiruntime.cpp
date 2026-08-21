#include "ansiruntime.h"
#include "qtty/application.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include <QtWidgets>
#include <QSocketNotifier>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>

namespace qtty {
namespace detail {

static int to256(QRgb c) {
    int r = qRed(c) * 5 / 255, g = qGreen(c) * 5 / 255, b = qBlue(c) * 5 / 255;
    return 16 + 36 * r + 6 * g + b;
}

struct FrameCell { QString ch; int fg = -1; bool rev = false, bold = false; };

AnsiRuntime::AnsiRuntime(QWidget *win) : win_(win) {
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    winsize ws{};
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) { cols_ = ws.ws_col; rows_ = ws.ws_row; }
    else { cols_ = 80; rows_ = 24; }                 // piped/CI fallback
    if (isatty(0) && tcgetattr(0, &saved_) == 0) {
        termios t = saved_;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        rawOk_ = true;
    }
    printf("\033[?1049h\033[?25l");                  // alt screen, hide cursor
    win_->setAttribute(Qt::WA_DontShowOnScreen);
    win_->resize(cols_ * cw, rows_ * ch);
    win_->show();
    notifier_ = new QSocketNotifier(0, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { readInput(); });
    render();
}

AnsiRuntime::~AnsiRuntime() {
    printf("\033[0m\033[?1049l\033[?25h");
    fflush(stdout);
    if (rawOk_) tcsetattr(0, TCSANOW, &saved_);
}

void AnsiRuntime::readInput() {
    char buf[256];
    ssize_t n = ::read(0, buf, sizeof buf);
    if (n <= 0) { quit_ = true; qApp->quit(); return; }
    pending_.append(buf, n);
    while (!pending_.isEmpty()) { if (!step()) break; }
    render();
}

bool AnsiRuntime::step() {
    // The InputRouter rules, measured: shortcuts must be resolved here, never
    // via QShortcutMap (F3); key events go to window->focusWidget() (F4).
    auto key = [&](int k, const QString &txt = {}) {
        QWidget *target = win_->focusWidget() ? win_->focusWidget() : win_;
        setFocusWidget(win_->focusWidget());
        QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier, txt);
        QApplication::sendEvent(target, &ev);
    };
    auto scroll = [&](int lines) {
        if (auto *area = win_->findChild<QAbstractScrollArea *>())
            area->verticalScrollBar()->setValue(
                area->verticalScrollBar()->value() + lines * GridMetrics::ch());
    };
    unsigned char c = pending_[0];
    if (c == 0x03 || c == 0x04) { quit_ = true; qApp->quit(); return false; }   // ^C ^D
    if (c == 0x1b) {
        if (pending_.size() < 3) return false;
        char fin = pending_[2];
        pending_.remove(0, 3);
        if      (fin == 'A') scroll(-1);
        else if (fin == 'B') scroll(+1);
        else if (fin == 'D') key(Qt::Key_Left);
        else if (fin == 'C') key(Qt::Key_Right);
        else if (fin == '5') { pending_.remove(0, 1); scroll(-5); }
        else if (fin == '6') { pending_.remove(0, 1); scroll(+5); }
        return true;
    }
    pending_.remove(0, 1);
    if (c == '\r' || c == '\n') key(Qt::Key_Return);
    else if (c == 0x7f || c == 0x08) key(Qt::Key_Backspace);
    else if (c == '\t') key(Qt::Key_Tab);
    else if (c >= 0x20) key(0, QString(QChar(c)));
    QCoreApplication::processEvents();
    return true;
}

void AnsiRuntime::render() {
    if (quit_) return;
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
    QCoreApplication::processEvents();

    CellBuffer buf(cols_, rows_);
    QVector<CellImage> placements;
    renderOnce(*win_, buf, &placements);

    QVector<FrameCell> fb(cols_ * rows_);
    for (int y = 0; y < rows_; ++y)
        for (int x = 0; x < cols_; ++x) {
            const Cell &c = buf.at(x, y);
            fb[y * cols_ + x] = {c.ch, -1, c.rev, c.bold};
        }
    for (const CellImage &ci : placements) {           // NoGraphics tier mosaic
        QImage img = ci.pixmap.toImage();
        for (int cy = 0; cy < ci.cellRect.height(); ++cy)
            for (int cx = 0; cx < ci.cellRect.width(); ++cx) {
                int X = ci.cellRect.x() + cx, Y = ci.cellRect.y() + cy;
                if (X < 0 || Y < 0 || X >= cols_ || Y >= rows_) continue;
                int tx = qMin(cx * img.width()  / ci.cellRect.width()  + img.width()  / (2 * ci.cellRect.width()),  img.width() - 1);
                int ty = qMin(cy * img.height() / ci.cellRect.height() + img.height() / (2 * ci.cellRect.height()), img.height() - 1);
                QRgb px = img.pixel(tx, ty);
                if (qAlpha(px) < 40) continue;
                fb[Y * cols_ + X] = {qAlpha(px) > 200 ? QStringLiteral("█") : QStringLiteral("▓"),
                                     to256(px), false, false};
            }
    }

    QByteArray out = "\033[H";
    int curFg = -2; bool curRev = false, curBold = false;
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            FrameCell &f = fb[y * cols_ + x];
            if (f.fg != curFg || f.rev != curRev || f.bold != curBold) {
                out += "\033[0m";
                if (f.fg >= 0) out += "\033[38;5;" + QByteArray::number(f.fg) + "m";
                if (f.rev)  out += "\033[7m";
                if (f.bold) out += "\033[1m";
                curFg = f.fg; curRev = f.rev; curBold = f.bold;
            }
            out += f.ch.toUtf8();
        }
        if (y < rows_ - 1) out += "\r\n";
    }
    if (QWidget *fw = win_->focusWidget()) {           // hardware cursor (§5.5)
        QVariant v = fw->inputMethodQuery(Qt::ImCursorRectangle);
        if (v.isValid()) {
            QPoint g = fw->mapTo(win_, v.toRect().topLeft());
            out += "\033[" + QByteArray::number(g.y() / ch + 1) + ';'
                 + QByteArray::number(g.x() / cw + 1) + "H\033[?25h";
        }
    }
    fwrite(out.constData(), 1, out.size(), stdout);
    fflush(stdout);
}

} // namespace detail
} // namespace qtty
