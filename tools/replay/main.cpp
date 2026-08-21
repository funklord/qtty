// qtty-replay — scripted input → text frames (§9). Makes bug reports
// reproducible: a script drives the UI through the real InputRouter and each
// `frame` line emits the composed cell buffer.
//
// Script (stdin or file argument), one command per line:
//   text <string>     type characters
//   key <name>        Tab | Return | Backspace | Up | Down | Left | Right
//   ctrl <letter>     e.g. "ctrl s"
//   click <col> <row> mouse press+release at cell
//   frame             print the composed frame between markers
//
// Drives the built-in sample UI; applications link libqtty and reuse
// InputRouter/Compositor the same way for their own screens.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QTextStream>
#include <cstdio>

using namespace Qtty;

static int keyByName(const QString &n) {
    static const QHash<QString, int> map = {
        {"tab", Qt::Key_Tab}, {"return", Qt::Key_Return}, {"enter", Qt::Key_Return},
        {"backspace", Qt::Key_Backspace}, {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
        {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
        {"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
    };
    return map.value(n.toLower(), 0);
}

int main(int argc, char **argv) {
    prepareEnvironment();
    QApplication app(argc, argv);
    setup(app);

    // sample UI: form + scrolling list (replace with your screen when linking)
    QWidget win;
    auto *v = new QVBoxLayout(&win);
    v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto *edit = new QLineEdit(&win);
    edit->setPlaceholderText("type here");
    auto *list = new QListView(&win);
    auto *model = new QStringListModel(&win);
    QStringList rows;
    for (int i = 0; i < 40; ++i) rows << QStringLiteral("item %1").arg(i);
    model->setStringList(rows);
    list->setModel(model);
    list->setFrameShape(QFrame::NoFrame);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    v->addWidget(edit); v->addWidget(list, 1);
    win.setAttribute(Qt::WA_DontShowOnScreen);
    win.resize(GridMetrics::cells(48, 14));
    win.show();
    edit->setFocus();
    QCoreApplication::processEvents();

    InputRouter router(&win);
    Compositor comp(&win, &router);

    QFile file;
    if (argc > 1) { file.setFileName(QString::fromLocal8Bit(argv[1])); file.open(QIODevice::ReadOnly); }
    else file.open(stdin, QIODevice::ReadOnly);
    QTextStream in(&file);

    int frameNo = 0;
    while (!in.atEnd()) {
        const QString lineRaw = in.readLine();
        const QString line = lineRaw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(QLatin1Char(' '));
        const QString cmd = parts[0].toLower();
        if (cmd == QLatin1String("text") && parts.size() > 1) {
            const QString t = lineRaw.mid(lineRaw.indexOf(QLatin1Char(' ')) + 1);
            for (const QString &cl : toClusters(t))
                router.onKey({0, cl, false, false, false});
        } else if (cmd == QLatin1String("key") && parts.size() == 2) {
            router.onKey({keyByName(parts[1]), QString(), false, false, false});
        } else if (cmd == QLatin1String("ctrl") && parts.size() == 2) {
            router.onKey({Qt::Key_A + (parts[1].at(0).toLower().unicode() - 'a'),
                          QString(), true, false, false});
        } else if (cmd == QLatin1String("click") && parts.size() == 3) {
            QPoint cell(parts[1].toInt(), parts[2].toInt());
            router.onMouse({cell, 1, true, false, false, 0});
            router.onMouse({cell, 1, false, true, false, 0});
        } else if (cmd == QLatin1String("frame")) {
            CellBuffer buf(48, 14);
            comp.compose(buf);
            printf("--- frame %d ---\n%s--- end ---\n", frameNo++, qPrintable(buf.toText()));
        } else {
            fprintf(stderr, "qtty-replay: unknown command '%s'\n", qPrintable(cmd));
        }
    }
    return 0;
}
