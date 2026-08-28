// suite_graphics -- section 17.3: encoders, negotiation, halfblock composite, overlays.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n", m); ++fails; } } while (0)

// ---- round trip: decode what the encoders emit, independently --------------
//
// project.md section 7.3 records the gap these close. The encoder checks in
// the suite below assert byte structure -- a header is present, a terminator
// is present, a size appears -- and byte structure cannot tell a well-formed
// stream from a correct one. An encoder emitting a syntactically perfect
// stream of the wrong pixels passes every one of them. So the bytes are
// parsed back to pixels here, by code that shares nothing with
// src/graphics/graphics.cpp, and the pixels are compared against the source.
//
// Every check below was confirmed able to fail, by breaking encode_sixel,
// encode_kitty_image and encode_iterm2 one at a time in a scratch copy of the
// tree and watching the round trip go red: a dropped sixel band, a sixel run
// length one too long, a green/blue swap in the sixel colour registers, a
// red/blue swap in the kitty RGBA payload, and an iTerm2 PNG shifted one
// column left. Four of the five leave every structural check in this file
// passing, which is the whole argument for the round trip existing.

// The fixture's six colours are all exact xterm-256 cube entries: every
// channel is one of the cube levels 0, 95, 135, 175, 215, 255, so Color's
// CIELAB match picks each of them back out of the palette unchanged and the
// sixel palette step costs nothing. That is what lets the sixel tolerance be
// 3 rather than 47 -- see the note beside it.
static const QRgb fixture_red     = qRgb(255,   0,   0);
static const QRgb fixture_blue    = qRgb(  0,   0, 255);
static const QRgb fixture_amber   = qRgb(255, 215,  95);
static const QRgb fixture_green   = qRgb(  0, 175,   0);
static const QRgb fixture_steel   = qRgb(135, 175, 215);
static const QRgb fixture_magenta = qRgb(215,   0, 215);

// A solid block round-trips through almost any encoder bug, so every part of
// this 13 x 14 image is doing a job:
//
//   - 13 x 14 is neither square nor a multiple of 6. A row/column
//     transposition therefore changes the raster dimensions, and the last
//     sixel band is a partial one -- rows 12 and 13 of a six-row band.
//   - Rows 0 to 4 are red for columns 0 to 5 and blue for 6 to 12: a vertical
//     edge inside a band, with runs of six and seven identical columns either
//     side. encode_sixel takes its "!" run-length path above three, so both
//     sides of the edge go through the RLE encoder.
//   - Row 5 is amber and row 6 is green. Those are the last row of band 0 and
//     the first row of band 1, so an off-by-one in the band arithmetic swaps
//     them, duplicates one or drops one.
//   - Rows 7 to 11, columns 0 to 6 are fully transparent: sixel's P2=1
//     untouched path, and a real alpha channel for kitty and for PNG.
//   - Rows 7 to 11, columns 7 to 12 are steel blue (135, 175, 215), whose
//     three channels are three different cube levels. A red/green or
//     red/blue channel swap moves it 40 or 80 units and cannot hide inside
//     the tolerance.
//   - Rows 12 and 13 are magenta with one green pixel at (2, 12). Its
//     transpose (12, 2) sits inside the blue block, so a decoder or an
//     encoder reading x for y puts blue where green belongs.
static QImage round_trip_fixture() {
	QImage img(13, 14, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	for (int y = 0; y <= 4; ++y)
		for (int x = 0; x < img.width(); ++x)
			img.setPixel(x, y, x < 6 ? fixture_red : fixture_blue);
	for (int x = 0; x < img.width(); ++x) {
		img.setPixel(x, 5, fixture_amber);
		img.setPixel(x, 6, fixture_green);
	}
	for (int y = 7; y <= 11; ++y)
		for (int x = 7; x < img.width(); ++x)
			img.setPixel(x, y, fixture_steel);
	for (int y = 12; y <= 13; ++y)
		for (int x = 0; x < img.width(); ++x)
			img.setPixel(x, y, fixture_magenta);
	img.setPixel(2, 12, fixture_green);
	return img;
}

// A pixel buffer reconstructed from a sixel stream. A pixel no band ever
// painted stays 0 -- alpha zero -- because that is what P2=1 means: untouched
// is transparent, not black.
struct SixelImage {
	int w = 0, h = 0;
	bool ok = false;                              // ST reached, stream understood
	QVector<QRgb> px;
	QRgb at(int x, int y) const { return px[y * w + x]; }
};

// Parse the subset of DEC sixel that encode_sixel emits: the DCS header, the
// raster attributes, "#c;2;r;g;b" register definitions and "#c" selections,
// "!n" run lengths, "$" carriage return within a band, "-" band advance, ST.
// Anything else means the stream is not what this encoder is documented to
// produce, and the parse is refused rather than half-decoded.
static SixelImage decode_sixel(const QByteArray &in) {
	SixelImage img;
	int p = 0;
	const auto literal = [&](const char *s) {
		const QByteArray want(s);
		if (in.mid(p, want.size()) != want) return false;
		p += want.size();
		return true;
	};
	const auto number = [&] {
		int v = -1;
		while (p < in.size() && in[p] >= '0' && in[p] <= '9')
			v = (v < 0 ? 0 : v) * 10 + (in[p++] - '0');
		return v;
	};
	const auto semicolon = [&] {
		if (p < in.size() && in[p] == ';') { ++p; return true; }
		return false;
	};

	if (!literal("\033P0;1;0q") || !literal("\""))  return img;
	if (number() < 0 || !semicolon())               return img;   // pan
	if (number() < 0 || !semicolon())               return img;   // pad
	const int w = number();
	if (!semicolon())                               return img;
	const int h = number();
	if (w <= 0 || h <= 0)                           return img;
	img.w = w;
	img.h = h;
	img.px.fill(0, w * h);

	QVector<QRgb> reg(256, 0);
	int color = 0, x = 0, band = 0;
	const auto put = [&](char c, int count) {
		const int bits = c - 63;                  // '?' is six zero bits
		for (int i = 0; i < count; ++i, ++x) {
			if (x >= w) continue;                 // past the raster: discard
			for (int dy = 0; dy < 6; ++dy)
				if ((bits & (1 << dy)) && band + dy < h)
					img.px[(band + dy) * w + x] = reg[color];
		}
	};

	while (p < in.size()) {
		const char c = in[p];
		if (c == '\033') { img.ok = literal("\033\\"); break; }
		if (c == '#') {
			++p;
			const int n = number();
			if (n < 0 || n > 255) return img;
			if (p < in.size() && in[p] == ';') {  // a definition, not a select
				++p;
				const int space = number();       // 2 = RGB, and in percent
				if (!semicolon()) return img;
				const int r = number();
				if (!semicolon()) return img;
				const int g = number();
				if (!semicolon()) return img;
				const int b = number();
				if (space != 2 || r < 0 || g < 0 || b < 0) return img;
				reg[n] = qRgb(r * 255 / 100, g * 255 / 100, b * 255 / 100);
			}
			color = n;                            // selecting does not move x
			continue;
		}
		if (c == '$') { x = 0; ++p; continue; }           // return within band
		if (c == '-') { x = 0; band += 6; ++p; continue; } // next band
		if (c == '!') {
			++p;
			const int n = number();
			if (n <= 0 || p >= in.size() || in[p] < '?' || in[p] > '~') return img;
			put(in[p++], n);
			continue;
		}
		if (c >= '?' && c <= '~') { put(c, 1); ++p; continue; }
		return img;
	}
	return img;
}

// The payloads of a kitty APC chain, concatenated in the order sent.
struct KittyStream {
	QByteArray ctrl;                              // the first chunk's keys
	QByteArray payload;                           // every chunk's payload
	int chunks = 0;
	bool ok = false;
};

// Walk the chain. A continuation chunk carries nothing but its m= key, so
// anything else in one means the chain was not built the way the protocol
// says, and the stream is refused rather than silently half-decoded.
static KittyStream split_kitty(const QByteArray &in) {
	KittyStream s;
	int p = 0;
	while (p < in.size()) {
		if (in.mid(p, 3) != "\033_G") return s;
		p += 3;
		const int semi = in.indexOf(';', p);
		const int st = in.indexOf("\033\\", p);
		if (semi < 0 || st < 0 || semi > st) return s;
		const QByteArray ctrl = in.mid(p, semi - p);
		if (s.chunks == 0) s.ctrl = ctrl;
		else if (ctrl != "m=1" && ctrl != "m=0") return s;
		s.payload += in.mid(semi + 1, st - semi - 1);
		++s.chunks;
		p = st + 2;
	}
	s.ok = s.chunks > 0;
	return s;
}

// One comma-separated k=v out of a kitty control block, or -1.
static int kitty_key(const QByteArray &ctrl, const char *key) {
	const QList<QByteArray> keys = ctrl.split(',');
	for (const QByteArray &kv : keys) {
		const int eq = kv.indexOf('=');
		if (eq > 0 && kv.left(eq) == key) return kv.mid(eq + 1).toInt();
	}
	return -1;
}

// The strong comparison, with no tolerance at all: kitty carries raw RGBA and
// iTerm2 carries PNG, and both are lossless. Returns -1 for a size
// disagreement, otherwise the number of pixels that differ. A pixel that is
// fully transparent on both sides has no colour to disagree about, since PNG
// is free to store anything under an alpha of zero.
static int exact_mismatches(const QImage &want, const QImage &got) {
	if (want.size() != got.size()) return -1;
	int wrong = 0;
	for (int y = 0; y < want.height(); ++y)
		for (int x = 0; x < want.width(); ++x) {
			const QRgb a = want.pixel(x, y), b = got.pixel(x, y);
			if (qAlpha(a) == 0 && qAlpha(b) == 0) continue;
			if (a != b) ++wrong;
		}
	return wrong;
}

int suite_graphics() {
	fails = 0;
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();

	// ---- negotiation (env-driven, restored afterwards) ----
	{
		auto save = [](const char *k) { return qgetenv(k); };
		QByteArray old_kitty = save("KITTY_WINDOW_ID"), old_term = save("TERM"),
		           old_prog = save("TERM_PROGRAM");
		auto setenvs = [](QByteArray k, QByteArray t, QByteArray p) {
			if (k.isEmpty()) qunsetenv("KITTY_WINDOW_ID"); else qputenv("KITTY_WINDOW_ID", k);
			if (t.isEmpty()) qunsetenv("TERM"); else qputenv("TERM", t);
			if (p.isEmpty()) qunsetenv("TERM_PROGRAM"); else qputenv("TERM_PROGRAM", p);
		};
		setenvs("1", "xterm-kitty", "");
		CHECK(detect_graphics_mode() == Capabilities::KittyAlpha, "kitty -> KittyAlpha");
		setenvs("", "xterm-ghostty", "");
		CHECK(detect_graphics_mode() == Capabilities::KittyAlpha, "ghostty -> KittyAlpha");
		setenvs("", "xterm-256color", "iTerm.app");
		CHECK(detect_graphics_mode() == Capabilities::ITerm2, "iTerm -> ITerm2");
		setenvs("", "foot", "");
		CHECK(detect_graphics_mode() == Capabilities::Sixel, "foot -> Sixel");
		setenvs("", "xterm-256color", "");
		CHECK(detect_graphics_mode() == Capabilities::Halfblocks, "plain xterm -> Halfblocks");
		setenvs("", "beerssh", "");
		CHECK(detect_graphics_mode() == Capabilities::KittyAlpha,
		      "beerssh recognised (provisional KittyAlpha)");
		qputenv("QTTY_GRAPHICS", "sixel");
		CHECK(detect_graphics_mode() == Capabilities::Sixel,
		      "QTTY_GRAPHICS override beats TERM heuristics");
		qputenv("QTTY_GRAPHICS", "none");
		CHECK(detect_graphics_mode() == Capabilities::NoGraphics,
		      "QTTY_GRAPHICS=none disables graphics");
		qunsetenv("QTTY_GRAPHICS");
		setenvs(old_kitty, old_term, old_prog);
	}

	// ---- sixel encoder: structure ----
	{
		QImage img(12, 12, QImage::Format_ARGB32);
		img.fill(Qt::transparent);
		for (int y = 0; y < 6; ++y)
			for (int x = 0; x < 12; ++x) img.setPixel(x, y, qRgb(255, 0, 0));
		for (int y = 6; y < 12; ++y)
			for (int x = 0; x < 12; ++x) img.setPixel(x, y, qRgb(0, 0, 255));
		QByteArray six = encode_sixel(img);
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
		QByteArray k = encode_kitty_image(7, img);
		CHECK(k.startsWith("\033_G"), "kitty APC introducer");
		CHECK(k.contains("a=T") && k.contains("f=32") && k.contains("i=7"),
		      "kitty transmit-and-display control keys");
		CHECK(k.contains("s=64") && k.contains("v=64"), "kitty dimensions");
		CHECK(k.contains(",m=1") && k.contains("m=0"),
		      "payload > 4096 chunks with m=1/m=0");
		QByteArray place = kitty_place(7);
		CHECK(place.size() < 40 && place.contains("a=p") && place.contains("i=7"),
		      "re-place by id is ~30 bytes (upload-once)");
		CHECK(kitty_delete_all().contains("a=d"), "delete-all helper");
	}

	// ---- iTerm2 encoder ----
	{
		QImage img(20, 20, QImage::Format_ARGB32);
		img.fill(Qt::green);
		QByteArray it = encode_iterm2(img, 4, 2);
		CHECK(it.startsWith("\033]1337;File=inline=1"), "iTerm2 OSC header");
		CHECK(it.contains("width=4") && it.contains("height=2"), "iTerm2 cell sizing");
		CHECK(it.endsWith("\a"), "iTerm2 BEL terminator");
	}

	// ---- round trip: sixel ----
	//
	// The tolerance is 3 per channel, and it is a measurement rather than a
	// fudge. The fixture's colours are exact xterm-256 cube entries, so the
	// palette step is lossless for them and the only lossy step left is the
	// register definition, which DEC specifies in percent. encode_sixel writes
	// v * 100 / 255 and this decoder reads back v * 255 / 100, both
	// truncating; over the six levels the fixture uses -- 0, 95, 135, 175,
	// 215, 255 -- the residuals are 0, 1, 3, 2, 1, 0. So 3 is the worst the
	// round trip can honestly produce and anything above it is a real
	// disagreement. A colour off the cube would need about 47, half the
	// widest gap between cube levels, and a tolerance that wide hides a
	// swapped channel -- which is why the fixture does not use one.
	{
		const QImage src = round_trip_fixture();
		const SixelImage dec = decode_sixel(encode_sixel(src));
		const bool geometry = dec.ok && dec.w == src.width()
		                   && dec.h == src.height();
		CHECK(geometry, "sixel round-trip: stream parses, raster attributes say 13x14");

		int untouched_wrong = 0, painted_wrong = 0, worst = 0;
		if (geometry)
			for (int y = 0; y < src.height(); ++y)
				for (int x = 0; x < src.width(); ++x) {
					const QRgb want = src.pixel(x, y), got = dec.at(x, y);
					const bool opaque = qAlpha(want) >= 128;
					if (!opaque && qAlpha(got) != 0) ++untouched_wrong;
					else if (opaque && qAlpha(got) == 0) ++painted_wrong;
					else if (opaque)
						worst = qMax(worst, qMax(qAbs(qRed(want)   - qRed(got)),
						             qMax(qAbs(qGreen(want) - qGreen(got)),
						                  qAbs(qBlue(want)  - qBlue(got)))));
				}
		CHECK(geometry && untouched_wrong == 0,
		      "sixel round-trip: transparent source pixels stay unpainted (P2=1)");
		CHECK(geometry && painted_wrong == 0 && worst <= 3,
		      "sixel round-trip: every opaque pixel within 3/255 of the source");

		// Named on its own because an off-by-one in the band arithmetic is
		// the sixel bug, and "row 5 is amber, row 6 is green" says which end
		// moved where a whole-image count only says a pixel is wrong.
		const auto within = [](QRgb got, QRgb want) {
			return qAlpha(got) != 0 && qAbs(qRed(got)   - qRed(want))   <= 3
			                        && qAbs(qGreen(got) - qGreen(want)) <= 3
			                        && qAbs(qBlue(got)  - qBlue(want))  <= 3;
		};
		CHECK(geometry && within(dec.at(6, 5), fixture_amber)
		      && within(dec.at(6, 6), fixture_green),
		      "sixel round-trip: band-boundary stripes stay on rows 5 and 6");
	}

	// ---- round trip: kitty ----
	{
		const QImage src = round_trip_fixture();
		const KittyStream s = split_kitty(encode_kitty_image(3, src));
		const auto b64 = QByteArray::fromBase64Encoding(s.payload,
		        QByteArray::AbortOnBase64DecodingErrors);
		const int sw = kitty_key(s.ctrl, "s"), sv = kitty_key(s.ctrl, "v");
		const bool raw = s.ok && kitty_key(s.ctrl, "f") == 32
		    && b64.decodingStatus == QByteArray::Base64DecodingStatus::Ok
		    && sw == src.width() && sv == src.height()
		    && b64.decoded.size() == sw * sv * 4;
		CHECK(raw, "kitty round-trip: f=32 payload is s*v*4 base64 RGBA bytes");
		CHECK(raw && exact_mismatches(src,
		          QImage(reinterpret_cast<const uchar *>(b64.decoded.constData()),
		                 sw, sv, QImage::Format_RGBA8888)) == 0,
		      "kitty round-trip: every pixel exact, alpha included");

		// The same over a payload big enough to be chunked: 48 x 40 of RGBA
		// is 10240 base64 bytes, three chunks of 4096. Reassembling them out
		// of order, or losing one, leaves a stream that still carries a
		// header, a terminator and the right m= keys.
		QImage wide(48, 40, QImage::Format_ARGB32);
		for (int y = 0; y < wide.height(); ++y)
			for (int x = 0; x < wide.width(); ++x)
				wide.setPixel(x, y, qRgb(x * 5, y * 6, (x * 3 + y * 7) & 0xFF));
		const KittyStream big = split_kitty(encode_kitty_image(4, wide));
		const auto wb = QByteArray::fromBase64Encoding(big.payload,
		        QByteArray::AbortOnBase64DecodingErrors);
		const bool chunked = big.ok && big.chunks == 3
		    && wb.decodingStatus == QByteArray::Base64DecodingStatus::Ok
		    && wb.decoded.size() == 48 * 40 * 4;
		CHECK(chunked, "kitty round-trip: 48x40 arrives as three reassembled chunks");
		CHECK(chunked && exact_mismatches(wide,
		          QImage(reinterpret_cast<const uchar *>(wb.decoded.constData()),
		                 48, 40, QImage::Format_RGBA8888)) == 0,
		      "kitty round-trip: chunk order preserved, every pixel exact");
	}

	// ---- round trip: iTerm2 ----
	{
		const QImage src = round_trip_fixture();
		const QByteArray it = encode_iterm2(src, 7, 3);
		const int colon = it.indexOf(':');
		const auto b64 = QByteArray::fromBase64Encoding(
		        it.mid(colon + 1, it.size() - colon - 2),
		        QByteArray::AbortOnBase64DecodingErrors);
		QImage got;
		const bool decoded = colon > 0
		    && b64.decodingStatus == QByteArray::Base64DecodingStatus::Ok
		    && b64.decoded.startsWith("\211PNG\r\n\032\n")
		    && got.loadFromData(b64.decoded, "PNG");
		CHECK(decoded && got.size() == src.size(),
		      "iTerm2 round-trip: payload is a 13x14 PNG");
		CHECK(decoded && exact_mismatches(src,
		          got.convertToFormat(QImage::Format_ARGB32)) == 0,
		      "iTerm2 round-trip: every pixel exact, alpha included");
	}

	// ---- rasterizer ----
	{
		CellBuffer b(8, 2);
		b.text(1, 0, QStringLiteral("Hi"), Color::rgb(qRgb(255, 0, 0)));
		QImage px = rasterize(b, QGuiApplication::font());
		CHECK(px.size() == QSize(8 * cw, 2 * ch), "rasterizer emits cols*cw x rows*ch");
		bool red_seen = false;
		for (int y = 0; y < ch && !red_seen; ++y)
			for (int x = cw; x < 3 * cw; ++x) {
				QRgb p = px.pixel(x, y);
				if (qRed(p) > 150 && qGreen(p) < 100) { red_seen = true; break; }
			}
		CHECK(red_seen, "rasterizer draws coloured glyphs");
	}

	// ---- halfblock composite: opacity semantics (section 5.7) ----
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
		compose_halfblocks(frame, ov, QRect(0, 0, 10, 4));
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
		// The overlay's GUI twin is a top-level widget placed against the
		// first visible top-level it can find (section 5.7), so with no window
		// open at all it keeps Qt's default 640x480 -- which is off the grid,
		// and what GridGuard reported here. An application always has a window
		// by the time it shows an overlay, so the test has one too, sized in
		// cells like every other window in the suite. Its cell rect then
		// resolves against a grid-aligned origin.
		QWidget base;
		base.setAttribute(Qt::WA_DontShowOnScreen);
		base.resize(GridMetrics::cells(20, 6));
		base.show();
		QCoreApplication::processEvents();

		CHECK(Overlay::visible_overlays().isEmpty(), "registry starts empty");
		Overlay o;
		QImage img(4 * cw, 2 * ch, QImage::Format_ARGB32);
		img.fill(QColor(255, 255, 0, 200));
		o.set_image(img);
		o.set_rect(QRectF(2, 1, 4, 2));
		CHECK(Overlay::visible_overlays().isEmpty(), "hidden overlay not listed");
		o.show();
		CHECK(Overlay::visible_overlays().size() == 1, "shown overlay listed");
		o.set_opacity(0.5);
		const QImage half = o.image();
		CHECK(qAlpha(half.pixel(1, 1)) < 130, "opacity multiplies into alpha");
		Overlay o2;
		o2.set_image(img); o2.set_z(-1); o2.show();
		CHECK(Overlay::visible_overlays().first() == &o2, "overlays z-ordered");
		o.hide(); o2.hide();
		CHECK(Overlay::visible_overlays().isEmpty(), "hidden overlays leave registry");
	}


	// ---- viewport cropping (section 16.3) ------------------------------------
	//
	// The case section 16.3 measured and left open: a sticker scrolled so that
	// it is half out of view. Its cell_rect runs past the grid, and every pixel
	// tier placed it whole -- kitty and sixel drawing outside the terminal, and
	// a placement scrolled off the top positioned at a negative row. Only the
	// mosaic tier was safe, because it composites into the CellBuffer.
	{
		// 4 cells wide, 4 tall, at 10x19 per cell.
		QImage art(40, 76, QImage::Format_ARGB32);
		art.fill(qRgb(200, 40, 40));

		const QSize grid(20, 10);
		// Fully visible: nothing is cropped, and the source is the whole image.
		CroppedPlacement whole = crop_placement(QRect(2, 2, 4, 4), art.size(), grid);
		CHECK(whole.cells == QRect(2, 2, 4, 4) && whole.source == QRect(0, 0, 40, 76),
		      "a placement inside the grid is not cropped");

		// Off the bottom: rows 8 and 9 are visible, 10 and 11 are not.
		CroppedPlacement bottom = crop_placement(QRect(2, 8, 4, 4), art.size(), grid);
		CHECK(bottom.cells == QRect(2, 8, 4, 2),
		      "a placement past the bottom keeps only the visible rows");
		CHECK(bottom.source == QRect(0, 0, 40, 38),
		      "and takes the matching top half of the image");

		// Off the top, which is the scrolled case and the one that produced a
		// negative row before.
		CroppedPlacement top = crop_placement(QRect(2, -2, 4, 4), art.size(), grid);
		CHECK(top.cells == QRect(2, 0, 4, 2),
		      "a placement above the grid starts at row 0, never negative");
		CHECK(top.source == QRect(0, 38, 40, 38),
		      "and takes the matching bottom half of the image");

		// Off the right edge.
		CroppedPlacement right = crop_placement(QRect(18, 2, 4, 4), art.size(), grid);
		CHECK(right.cells == QRect(18, 2, 2, 4) && right.source == QRect(0, 0, 20, 76),
		      "a placement past the right edge keeps only the visible columns");

		// Wholly off screen: nothing to draw at all.
		CHECK(crop_placement(QRect(2, 30, 4, 4), art.size(), grid).cells.isEmpty(),
		      "a placement entirely off the grid is dropped");

		// kitty carries the crop as a source rectangle rather than as different
		// pixels, so the upload stays whole and upload-once survives a crop.
		const QByteArray plain = kitty_place(7);
		CHECK(!plain.contains(",x=") && !plain.contains(",w="),
		      "an uncropped re-place carries no source rectangle");
		const QByteArray cropped = kitty_place(7, 0, QRect(0, 38, 40, 38));
		CHECK(cropped.contains("x=0") && cropped.contains("y=38")
		      && cropped.contains("w=40") && cropped.contains("h=38"),
		      "a cropped re-place carries x/y/w/h into the stored image");
		CHECK(cropped.size() < 64,
		      "and is still small enough to be the upload-once path");
	}
	// The terminal's background reaches the compositor. It was a constant --
	// qRgb(16, 20, 24) -- for the whole life of this function, which haloes
	// every translucent edge darkly on a light terminal, and the value was
	// always askable: OSC 11 is in the startup query.
	//
	// Asserted as a difference rather than against a colour, because what
	// matters is that the argument is consulted at all. Pinning one value
	// would pass with the parameter ignored if the expectation happened to be
	// the old constant.
	{
		QImage translucent(4, 4, QImage::Format_ARGB32);
		translucent.fill(QColor(200, 0, 0, 120));       // half-covered: tints bg
		Qtty::CellBuffer dark(4, 2), light(4, 2);
		Qtty::compose_halfblocks(dark, translucent, QRect(0, 0, 4, 2), qRgb(0, 0, 0));
		Qtty::compose_halfblocks(light, translucent, QRect(0, 0, 4, 2),
		                         qRgb(255, 255, 255));
		CHECK(dark.at(0, 0).bg != light.at(0, 0).bg,
		      "a translucent pixel composites against the terminal's background");
	}

	// The other half of section 5.7's pair: an image in the TEXT FLOW must be
	// a cell multiple or every line after it leaves the cell rows -- which
	// compounds down the document rather than showing as one wrong picture.
	{
		QTextDocument doc;
		QImage art(25, 30, QImage::Format_ARGB32);
		art.fill(Qt::red);
		doc.addResource(QTextDocument::ImageResource,
		                QUrl(QStringLiteral("art://one")), art);
		QTextCursor cur(&doc);
		cur.insertText(QStringLiteral("before "));
		QTextImageFormat sized;
		sized.setName(QStringLiteral("art://one"));
		sized.setWidth(25);
		sized.setHeight(30);
		cur.insertImage(sized);
		cur.insertText(QStringLiteral(" after"));

		// Unsized in the format: the layout would use the resource's natural
		// size and undo the rounding, so it has to be resolved and written
		// back rather than skipped.
		QTextImageFormat unsized;
		unsized.setName(QStringLiteral("art://one"));
		cur.insertImage(unsized);

		// A name that resolves to nothing at all. Rounding an unknown would
		// be inventing a number, so it is left exactly as it was.
		QTextImageFormat missing;
		missing.setName(QStringLiteral("art://absent"));
		cur.insertImage(missing);

		const int changed = Qtty::align_text_document(&doc, QSize(10, 19));
		CHECK(changed == 2, "both resolvable images are aligned and the third is not");

		QVector<QSizeF> got;
		for (QTextBlock b = doc.begin(); b != doc.end(); b = b.next())
			for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
				const QTextFragment f = it.fragment();
				if (f.isValid() && f.charFormat().isImageFormat()) {
					const QTextImageFormat i = f.charFormat().toImageFormat();
					got.append(QSizeF(i.width(), i.height()));
				}
			}
		CHECK(got.size() == 3, "the document still has its three images");
		// 25 -> 30 and 30 -> 38: rounded UP, because an image given less room
		// than it needs is cropped and a picture missing its last row is
		// worse than one with a gap under it.
		CHECK(got.value(0) == QSizeF(30, 38), "an explicit size is rounded up");
		CHECK(got.value(1) == QSizeF(30, 38),
		      "and a natural size is resolved and then rounded");
		CHECK(got.value(2) == QSizeF(0, 0), "an unresolvable image is left alone");

		// Idempotent, which is what makes it safe to call on a document that
		// has already been through it -- the obvious way to use it is on
		// every document, once, wherever they are built.
		CHECK(Qtty::align_text_document(&doc, QSize(10, 19)) == 0,
		      "running it again changes nothing");
		CHECK(Qtty::align_text_document(nullptr, QSize(10, 19)) == 0,
		      "and a null document is not a crash");
	}

	// section 5.7's image sizing, which needs the cell measured rather than
	// assumed: a half-block pixel is one cell wide and half a cell tall, so
	// treating a cell as square squashes every picture on a terminal whose
	// cells are not 1:2.
	{
		CHECK(Qtty::cells(QSize(100, 190), QSize(10, 19)) == QSize(10, 10),
		      "an exact fit is exactly that many cells");
		// Rounded up: given four cells an image needing four and a half is
		// cropped, and a picture missing its last row is worse than one with
		// a gap under it.
		CHECK(Qtty::cells(QSize(105, 200), QSize(10, 19)) == QSize(11, 11),
		      "and a partial cell is rounded up rather than cropped");
		// The discriminating pair. The same image on two terminals whose
		// cells differ must not get the same footprint -- that is the whole
		// reason the cell size is asked for.
		CHECK(Qtty::cells(QSize(160, 160), QSize(10, 20))
		          != Qtty::cells(QSize(160, 160), QSize(8, 16)),
		      "a different cell size gives a different footprint");
		// An unmeasured cell yields nothing rather than a plausible guess.
		CHECK(!Qtty::cells(QSize(100, 100), QSize()).isValid(),
		      "and with no cell size measured it answers nothing");
	}

	return fails;
}
