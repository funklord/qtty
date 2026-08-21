// suite_graphics — §17.3: encoders, negotiation, halfblock composite, overlays.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

int suite_graphics() {
    fails = 0;
    const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

    // ---- negotiation (env-driven, restored afterwards) ----
    {
        auto save = [](const char *k) { return qgetenv(k); };
        QByteArray oldKitty = save("KITTY_WINDOW_ID"), oldTerm = save("TERM"),
                   oldProg = save("TERM_PROGRAM");
        auto setenvs = [](QByteArray k, QByteArray t, QByteArray p) {
            if (k.isEmpty()) qunsetenv("KITTY_WINDOW_ID"); else qputenv("KITTY_WINDOW_ID", k);
            if (t.isEmpty()) qunsetenv("TERM"); else qputenv("TERM", t);
            if (p.isEmpty()) qunsetenv("TERM_PROGRAM"); else qputenv("TERM_PROGRAM", p);
        };
        setenvs("1", "xterm-kitty", "");
        CHECK(detectGraphicsMode() == Capabilities::KittyAlpha, "kitty -> KittyAlpha");
        setenvs("", "xterm-ghostty", "");
        CHECK(detectGraphicsMode() == Capabilities::KittyAlpha, "ghostty -> KittyAlpha");
        setenvs("", "xterm-256color", "iTerm.app");
        CHECK(detectGraphicsMode() == Capabilities::ITerm2, "iTerm -> ITerm2");
        setenvs("", "foot", "");
        CHECK(detectGraphicsMode() == Capabilities::Sixel, "foot -> Sixel");
        setenvs("", "xterm-256color", "");
        CHECK(detectGraphicsMode() == Capabilities::Halfblocks, "plain xterm -> Halfblocks");
        setenvs(oldKitty, oldTerm, oldProg);
    }

    // ---- sixel encoder: structure ----
    {
        QImage img(12, 12, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 12; ++x) img.setPixel(x, y, qRgb(255, 0, 0));
        for (int y = 6; y < 12; ++y)
            for (int x = 0; x < 12; ++x) img.setPixel(x, y, qRgb(0, 0, 255));
        QByteArray six = encodeSixel(img);
        CHECK(six.startsWith("\033P0;1;0q"), "sixel DCS header with P2=1 transparency");
        CHECK(six.endsWith("\033\\"), "sixel ST terminator");
        CHECK(six.contains("\"1;1;12;12"), "sixel raster attributes carry size");
        CHECK(six.count('#') >= 4, "sixel defines and selects colour registers");
        CHECK(six.contains('-'), "sixel band separators present");
        CHECK(six.contains('!'), "sixel RLE used for runs");
    }

    // ---- kitty encoder: chunking + upload-once helpers ----
    {
        QImage img(64, 64, QImage::Format_RGBA8888);
        img.fill(QColor(10, 200, 50, 255));
        QByteArray k = encodeKittyImage(7, img);
        CHECK(k.startsWith("\033_G"), "kitty APC introducer");
        CHECK(k.contains("a=T") && k.contains("f=32") && k.contains("i=7"),
              "kitty transmit-and-display control keys");
        CHECK(k.contains("s=64") && k.contains("v=64"), "kitty dimensions");
        CHECK(k.contains(",m=1") && k.contains("m=0"),
              "payload > 4096 chunks with m=1/m=0");
        QByteArray place = kittyPlace(7);
        CHECK(place.size() < 40 && place.contains("a=p") && place.contains("i=7"),
              "re-place by id is ~30 bytes (upload-once)");
        CHECK(kittyDeleteAll().contains("a=d"), "delete-all helper");
    }

    // ---- iTerm2 encoder ----
    {
        QImage img(20, 20, QImage::Format_ARGB32);
        img.fill(Qt::green);
        QByteArray it = encodeITerm2(img, 4, 2);
        CHECK(it.startsWith("\033]1337;File=inline=1"), "iTerm2 OSC header");
        CHECK(it.contains("width=4") && it.contains("height=2"), "iTerm2 cell sizing");
        CHECK(it.endsWith("\a"), "iTerm2 BEL terminator");
    }

    // ---- rasterizer ----
    {
        CellBuffer b(8, 2);
        b.text(1, 0, QStringLiteral("Hi"), Color::rgb(qRgb(255, 0, 0)));
        QImage px = rasterize(b, QGuiApplication::font());
        CHECK(px.size() == QSize(8 * cw, 2 * ch), "rasterizer emits cols*cw x rows*ch");
        bool redSeen = false;
        for (int y = 0; y < ch && !redSeen; ++y)
            for (int x = cw; x < 3 * cw; ++x) {
                QRgb p = px.pixel(x, y);
                if (qRed(p) > 150 && qGreen(p) < 100) { redSeen = true; break; }
            }
        CHECK(redSeen, "rasterizer draws coloured glyphs");
    }

    // ---- halfblock composite: opacity semantics (§5.7) ----
    {
        CellBuffer frame(10, 4);
        frame.text(1, 1, QStringLiteral("KEEP"));
        // opaque left half, translucent right half
        QImage ov(10 * cw, 4 * ch, QImage::Format_ARGB32);
        ov.fill(Qt::transparent);
        QPainter p(&ov);
        p.fillRect(6 * cw, 0, 4 * cw, 4 * ch, QColor(255, 0, 0, 255));   // opaque
        p.fillRect(0, 0, 6 * cw, 4 * ch, QColor(0, 80, 255, 90));        // translucent
        p.end();
        composeHalfblocks(frame, ov, QRect(0, 0, 10, 4));
        CHECK(frame.at(1, 1).ch == QStringLiteral("K"),
              "text under translucent overlay survives");
        CHECK(frame.at(1, 1).bg.kind() == Color::Rgb,
              "translucent overlay tints the cell background");
        CHECK(frame.at(7, 1).ch == QStringLiteral("▀"),
              "opaque overlay becomes half-block pixels");
        CHECK(frame.at(7, 1).fg.kind() == Color::Rgb
              && qRed(frame.at(7, 1).fg.value()) > 200,
              "half-block carries the overlay colour");
    }

    // ---- Overlay API + registry ----
    {
        CHECK(Overlay::visibleOverlays().isEmpty(), "registry starts empty");
        Overlay o;
        QImage img(4 * cw, 2 * ch, QImage::Format_ARGB32);
        img.fill(QColor(255, 255, 0, 200));
        o.setImage(img);
        o.setRect(QRectF(2, 1, 4, 2));
        CHECK(Overlay::visibleOverlays().isEmpty(), "hidden overlay not listed");
        o.show();
        CHECK(Overlay::visibleOverlays().size() == 1, "shown overlay listed");
        o.setOpacity(0.5);
        const QImage half = o.image();
        CHECK(qAlpha(half.pixel(1, 1)) < 130, "opacity multiplies into alpha");
        Overlay o2;
        o2.setImage(img); o2.setZ(-1); o2.show();
        CHECK(Overlay::visibleOverlays().first() == &o2, "overlays z-ordered");
        o.hide(); o2.hide();
        CHECK(Overlay::visibleOverlays().isEmpty(), "hidden overlays leave registry");
    }

    return fails;
}
