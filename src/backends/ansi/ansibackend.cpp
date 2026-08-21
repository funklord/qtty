#include "ansibackend.h"
#include <QSocketNotifier>
#include <QImage>
#include <QCoreApplication>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>

namespace Qtty {

AnsiBackend::AnsiBackend() {
    winsize ws{};
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        cells_ = QSize(ws.ws_col, ws.ws_row);
    else
        cells_ = QSize(80, 24);                     // piped/CI fallback
    resume();
    notifier_ = new QSocketNotifier(0, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { readInput(); });
}

AnsiBackend::~AnsiBackend() { suspend(); }

Capabilities AnsiBackend::capabilities() const {
    Capabilities c;
    c.color = Capabilities::Xterm256;
    c.graphics = Capabilities::Halfblocks;          // mosaic tier (§5.7)
    return c;
}

QSize AnsiBackend::size() const { return cells_; }

void AnsiBackend::resume() {
    if (active_) return;
    if (isatty(0) && tcgetattr(0, &saved_) == 0) {
        termios t = saved_;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        rawOk_ = true;
    }
    printf("\033[?1049h\033[?25l");
    fflush(stdout);
    active_ = true;
}

void AnsiBackend::suspend() {
    if (!active_) return;
    printf("\033[0m\033[?1049l\033[?25h");
    fflush(stdout);
    if (rawOk_) tcsetattr(0, TCSANOW, &saved_);
    active_ = false;
}

// ---- output ----------------------------------------------------------------
struct Sgr { int fg = -2, bg = -2; Attrs attrs; };

static void emitSgr(QByteArray &out, const Cell &c, Sgr &cur) {
    const int fg = c.fg.toXterm256();
    const int bg = c.bg.toXterm256();
    if (fg == cur.fg && bg == cur.bg && c.attrs == cur.attrs) return;
    out += "\033[0m";
    if (fg >= 0) out += "\033[38;5;" + QByteArray::number(fg) + 'm';
    if (bg >= 0) out += "\033[48;5;" + QByteArray::number(bg) + 'm';
    if (c.attrs & Attr::Bold)      out += "\033[1m";
    if (c.attrs & Attr::Dim)       out += "\033[2m";
    if (c.attrs & Attr::Italic)    out += "\033[3m";
    if (c.attrs & Attr::Underline) out += "\033[4m";
    if (c.attrs & Attr::Reverse)   out += "\033[7m";
    if (c.attrs & Attr::Strike)    out += "\033[9m";
    cur = {fg, bg, c.attrs};
}

void AnsiBackend::present(const CellBuffer &frame, const QRegion &) {
    // Full-frame emission: measured cheap (§16.1 F9); damage-limited output
    // arrives with DEC 2026 bracketing in Phase 2 polish.
    CellBuffer composed = frame;                     // mosaic composites in-place
    for (const CellImage &ci : frame.images) {
        QImage img = ci.pixmap.toImage();
        for (int cy = 0; cy < ci.cellRect.height(); ++cy)
            for (int cx = 0; cx < ci.cellRect.width(); ++cx) {
                int X = ci.cellRect.x() + cx, Y = ci.cellRect.y() + cy;
                if (X < 0 || Y < 0 || X >= composed.cols() || Y >= composed.rows()) continue;
                int tx = qMin(cx * img.width()  / ci.cellRect.width()
                              + img.width()  / (2 * ci.cellRect.width()),  img.width() - 1);
                int ty = qMin(cy * img.height() / ci.cellRect.height()
                              + img.height() / (2 * ci.cellRect.height()), img.height() - 1);
                QRgb px = img.pixel(tx, ty);
                if (qAlpha(px) < 40) continue;
                composed.putCluster(X, Y,
                    qAlpha(px) > 200 ? QStringLiteral("█") : QStringLiteral("▓"),
                    Color::rgb(px));
            }
    }

    QByteArray out = "\033[H";
    Sgr cur;
    for (int y = 0; y < composed.rows(); ++y) {
        for (int x = 0; x < composed.cols(); ++x) {
            const Cell &c = composed.at(x, y);
            if (c.width == 0) continue;              // continuation of wide cell
            emitSgr(out, c, cur);
            out += c.ch.toUtf8();
        }
        if (y < composed.rows() - 1) out += "\033[0m\r\n", cur = Sgr{};
    }
    fwrite(out.constData(), 1, out.size(), stdout);
    fflush(stdout);
}

void AnsiBackend::setCursor(std::optional<QPoint> cell, CursorShape shape) {
    if (cell && shape != CursorShape::Hidden)
        printf("\033[%d;%dH\033[?25h", cell->y() + 1, cell->x() + 1);
    else
        printf("\033[?25l");
    fflush(stdout);
}

// ---- input decoding --------------------------------------------------------
void AnsiBackend::readInput() {
    char buf[256];
    ssize_t n = ::read(0, buf, sizeof buf);
    if (n <= 0) {                                     // EOF: quit politely
        if (sink_) sink_->onKey({Qt::Key_D, QString(), true, false, false});
        return;
    }
    pending_.append(buf, n);
    while (!pending_.isEmpty()) { if (!decodeOne()) break; }
}

bool AnsiBackend::decodeOne() {
    if (!sink_) { pending_.clear(); return false; }
    unsigned char c = pending_[0];
    if (c == 0x1b) {                                  // ESC sequences
        if (pending_.size() < 2) return false;
        if (pending_[1] == '[') {
            if (pending_.size() < 3) return false;
            char fin = pending_[2];
            int consumed = 3;
            KeyEvent k;
            switch (fin) {
            case 'A': k.qtKey = Qt::Key_Up; break;
            case 'B': k.qtKey = Qt::Key_Down; break;
            case 'C': k.qtKey = Qt::Key_Right; break;
            case 'D': k.qtKey = Qt::Key_Left; break;
            case 'H': k.qtKey = Qt::Key_Home; break;
            case 'F': k.qtKey = Qt::Key_End; break;
            case 'Z': k.qtKey = Qt::Key_Tab; k.shift = true; break;
            case '3': k.qtKey = Qt::Key_Delete;  consumed = 4; break;   // 3~
            case '5': k.qtKey = Qt::Key_PageUp;  consumed = 4; break;   // 5~
            case '6': k.qtKey = Qt::Key_PageDown; consumed = 4; break;  // 6~
            default:  k.qtKey = 0; break;
            }
            if (pending_.size() < consumed) return false;
            pending_.remove(0, consumed);
            if (k.qtKey) sink_->onKey(k);
            return true;
        }
        pending_.remove(0, 2);                        // Alt-<char>
        KeyEvent k; k.alt = true; k.text = QString(QChar(pending_.isEmpty() ? 0 : c));
        return true;
    }
    pending_.remove(0, 1);
    KeyEvent k;
    if (c == '\r' || c == '\n')       k.qtKey = Qt::Key_Return;
    else if (c == 0x7f || c == 0x08)  k.qtKey = Qt::Key_Backspace;
    else if (c == '\t')               k.qtKey = Qt::Key_Tab;
    else if (c < 0x20) { k.qtKey = Qt::Key_A + (c - 1); k.ctrl = true; }   // ^A..^Z
    else { k.qtKey = 0; k.text = QString(QChar(c)); }
    sink_->onKey(k);
    return true;
}

} // namespace Qtty
