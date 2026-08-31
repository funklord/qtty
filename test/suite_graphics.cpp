// suite_graphics -- section 17.3: encoders, negotiation, halfblock composite, overlays.
#include <qtty/qtty.h>
#include <QtWidgets>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <cstdio>

using namespace Qtty;

static int fails = 0;
// The failure carries the condition that was false, not only the sentence.
// A message that cannot separate the hypotheses it will generate guarantees
// the guessing: twice in one day an assertion here had to be diagnosed by
// adding a temporary print, which is the proof that what it printed was not
// enough. Named by the beerssh session, which paid two container runs and
// three wrong theories for the same lesson.
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); \
                         else { printf("FAIL: %s\n      condition: %s\n", \
                                       m, #c); ++fails; } } while (0)

// Counts what Qt says while a call is being made. A named function rather
// than a lambda because qInstallMessageHandler takes a plain function
// pointer, so a capture is not available anyway and the counter has to be a
// file static either way.
static int g_messages = 0;
static void count_message(QtMsgType, const QMessageLogContext &, const QString &) {
	++g_messages;
}

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
		// $TERM=beerssh used to be a special case returning KittyAlpha --
		// "provisional until beerssh publishes its capability set". It is
		// gone, and this asserts the absence rather than deleting the case
		// quietly.
		//
		// Two reasons it had to go. This function is now reached only when
		// the terminal answered NOTHING, so every branch is a guess -- and
		// that one said yes to kitty on behalf of a terminal that had just
		// proved silent, which is the direction that costs a screenful of
		// escape sequences. It also assumed ALPHA over text, which beerssh
		// never claimed and the protocol has no query for.
		//
		// It is unnecessary too: beerssh answers the kitty query, so the
		// measured path decides and never reaches here. Verified against the
		// real terminal, not only here -- graphics on gives Kitty, and
		// --term-features=none gives half-blocks.
		setenvs("", "beerssh", "");
		CHECK(detect_graphics_mode() == Capabilities::Halfblocks,
		      "a silent terminal gets no tier from its name alone");
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
		{
			// A WIDE cluster through the rasteriser, which nothing had sent
			// it: the background fill for a cell is cw wide for an ordinary
			// one and twice that for a wide one, and only the narrow case had
			// ever run. A wide glyph filled to one cell leaves half of itself
			// standing on the previous cell's colour.
			CellBuffer w(6, 1);
			w.put_cluster(0, 0, QStringLiteral("\u6f22"));
			w.at(0, 0).bg = Color::rgb(qRgb(0, 0, 200));
			const QImage wide = rasterize(w, QGuiApplication::font());
			// Sampled at the far edge of the SECOND cell, which is the half
			// that a width of one would leave unpainted.
			const QRgb far = wide.pixel(2 * cw - 1, ch / 2);
			CHECK(qBlue(far) > 150 && qRed(far) < 100,
			      "a wide cluster's background covers both of its cells");
		}
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

	// kitty's Unicode placeholders, checked against the WORKED EXAMPLES in
	// kitty's own specification rather than against a decoder written here.
	// A round trip through our own decoder would agree with itself and be
	// wrong together; these two literals come from the protocol document, so
	// they are the one witness this file cannot supply for itself.
	//
	// The spec prints, for image id 42 in a 2x2:
	//   \e[38;5;42m \U10EEEE\U0305\U0305  \U10EEEE\U0305\U030D
	//   \e[38;5;42m \U10EEEE\U030D\U0305  \U10EEEE\U030D\U030D
	// so a cell is the placeholder, then the ROW diacritic, then the COLUMN.
	{
		const auto cluster = [](std::initializer_list<char32_t> cps) {
			QString out;
			for (char32_t c : cps) out += QString::fromUcs4(&c, 1);
			return out;
		};
		Qtty::CellBuffer frame(4, 3);
		Qtty::compose_kitty_placeholders(frame, 42, QRect(0, 0, 2, 2));

		CHECK(frame.at(0, 0).ch == cluster({0x10EEEE, 0x0305, 0x0305}),
		      "placeholder (0,0) matches the specification's example");
		CHECK(frame.at(1, 0).ch == cluster({0x10EEEE, 0x0305, 0x030D}),
		      "and (0,1), which is row 0 column 1");
		CHECK(frame.at(0, 1).ch == cluster({0x10EEEE, 0x030D, 0x0305}),
		      "and (1,0), so the row diacritic comes first");
		CHECK(frame.at(1, 1).ch == cluster({0x10EEEE, 0x030D, 0x030D}),
		      "and (1,1)");
		// The id travels in the foreground colour: 42 is 0x00002A.
		CHECK(frame.at(0, 0).fg.kind() == Qtty::Color::Rgb
		      && (frame.at(0, 0).fg.value() & 0xFFFFFF) == 42u,
		      "the image id is carried in the foreground colour");

		// The specification's second example: id 33554474 = 42 + (2 << 24)
		// needs a third diacritic for the most significant byte, because the
		// colour carries only 24 bits. U+030E is the diacritic for 2.
		Qtty::CellBuffer wide(4, 3);
		Qtty::compose_kitty_placeholders(wide, 33554474u, QRect(0, 0, 2, 2));
		CHECK(wide.at(0, 0).ch == cluster({0x10EEEE, 0x0305, 0x0305, 0x030E}),
		      "a big id adds the most-significant-byte diacritic");
		CHECK(wide.at(1, 0).ch == cluster({0x10EEEE, 0x0305, 0x030D, 0x030E}),
		      "on every cell, not only the first");

		// One grapheme cluster per cell, which is what makes a placement
		// ordinary text that any Unicode-aware program moves correctly -- the
		// whole reason this mode exists.
		CHECK(frame.at(0, 0).width == 1,
		      "a placeholder cell is one cell wide");

		// The transmit is quiet, or the terminal answers into whatever host
		// application the placeholders are being printed through.
		const QImage art(2, 2, QImage::Format_ARGB32);
		const QByteArray tx = Qtty::encode_kitty_virtual(42, art, 2, 2);
		CHECK(tx.startsWith("\033_Ga=T,U=1,q=2,"),
		      "the transmit creates a virtual placement quietly");
		CHECK(tx.contains(",c=2,r=2"), "sized in cells, not pixels");
		CHECK(tx.endsWith("\033\\"), "and is a well-formed APC string");
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

	// design.md section 5.7's three overlay delivery strategies, none of which
	// had a test. Overlay itself is covered -- the registry, the opacity, the
	// z-order -- and the compositor path that CONSUMES that registry was not:
	// a correct class and an unexercised connection, which is the shape that
	// keeps turning up here. Found by measuring coverage across the library
	// rather than by suspecting this file: compositor.cpp was the lowest at
	// 78%, and this block was most of the gap.
	{
		struct Recorder : Qtty::ITerminalBackend, Qtty::IGraphicsOutput {
			Qtty::Capabilities caps;
			int pixels = 0, overlays = 0, cleared = 0, frames = 0;
			bool want_placement = false;
			QImage last_pixels, last_overlay;
			QPoint last_cell;
			int last_z = 0;
			QSize size() const override { return QSize(20, 6); }
			Qtty::Capabilities capabilities() const override { return caps; }
			void present(const Qtty::CellBuffer &, const QRegion &) override { ++frames; }
			void set_cursor(std::optional<QPoint>, Qtty::CursorShape) override {}
			void set_event_sink(Qtty::ITerminalEventSink *) override {}
			void resume() override {}
			void suspend() override {}
			void present_pixels(const QImage &f, const QRegion &) override {
				++pixels; last_pixels = f;
			}
			void present_overlay(int, const QImage &i, QPoint c, int z) override {
				++overlays; last_overlay = i; last_cell = c; last_z = z;
			}
			void clear_overlay(int) override { ++cleared; }
		};

		// Reap anything an earlier case left to deleteLater() before
		// compositing. Compositor::compose() walks EVERY top-level, so a
		// widget merely awaiting deletion is still drawn and still resized --
		// and GridGuard then reports its default 640x480 as off the grid,
		// which is a fault in this suite's housekeeping rather than in the
		// code under test. Found because this is the first block here that
		// composites at all.
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();

		const auto drive = [&](Qtty::Capabilities::GraphicsMode mode, Recorder &rec) {
			rec.caps.graphics = mode;
			QWidget win;
			win.setAttribute(Qt::WA_DontShowOnScreen);
			win.resize(GridMetrics::cells(20, 6));
			win.show();
			QCoreApplication::processEvents();
			Qtty::InputRouter router(&win);
			Qtty::Compositor comp(&win, &router);
			Qtty::FrameScheduler sched(&rec, &comp, &win);
			if (rec.want_placement) {
				auto *plot = new Qtty::PixelSurface(&win);
				plot->setGeometry(0, 0, GridMetrics::cw() * 3, GridMetrics::ch() * 2);
				plot->show();
				QCoreApplication::processEvents();
			}
			sched.render_now();
			// The coalescing path, which render_now() bypasses. A frame ASKED
			// for rather than taken is how every frame after the first one
			// arrives, and it was the other half of this file's coverage gap.
			sched.request_frame();
			for (int k = 0; k < 40 && rec.frames < 2; ++k)
				QCoreApplication::processEvents();
		};

		QImage art(GridMetrics::cw() * 4, GridMetrics::ch() * 2,
		           QImage::Format_ARGB32);
		art.fill(QColor(200, 40, 40, 255));
		Overlay ov;
		ov.set_image(art);
		ov.set_rect(QRectF(1, 1, 4, 2));
		ov.set_z(3);
		ov.show();
		CHECK(Overlay::visible_overlays().size() == 1, "the overlay is registered");

		// KittyAlpha: the terminal blends. Text stays live text underneath,
		// which is the only tier where that is true and the reason the table
		// calls it the one native path.
		Recorder alpha;
		drive(Qtty::Capabilities::KittyAlpha, alpha);
		CHECK(alpha.overlays == 1 && alpha.pixels == 0,
		      "KittyAlpha ships the overlay to the terminal, unrasterised");
		CHECK(alpha.last_cell == QPoint(1, 1) && alpha.last_z == 3,
		      "with its own cell position and z");

		// Sixel, iTerm2 and plain Kitty cannot blend over text, so the plane
		// composites in software and hands over one finished frame.
		for (auto mode : {Qtty::Capabilities::Sixel, Qtty::Capabilities::ITerm2,
			                  Qtty::Capabilities::Kitty}) {
			Recorder soft;
			// With a PLACEMENT in the frame as well as an overlay: the software
			// path draws both onto the rasterised frame, and an overlay-only
			// case leaves the placement half of it unrun.
			soft.want_placement = true;
			drive(mode, soft);
			const bool ok = soft.pixels == 1 && soft.overlays == 0
			             && soft.last_pixels.size()
			                == QSize(20 * GridMetrics::cw(), 6 * GridMetrics::ch());
			printf("%s: tier %d composites in software into one full frame\n",
			       ok ? "PASS" : "FAIL", int(mode));
			if (!ok) ++fails;
		}

		// NoGraphics and Halfblocks: a pure L2 transform, so the overlay
		// becomes cells and nothing is shipped as pixels at all. This is the
		// tier that reaches every backend without backend changes.
		Recorder blocks;
		drive(Qtty::Capabilities::Halfblocks, blocks);
		CHECK(blocks.pixels == 0 && blocks.overlays == 0,
		      "Halfblocks ships no pixels, being cells already");

		ov.hide();
		CHECK(Overlay::visible_overlays().isEmpty(), "and it deregisters on hide");
	}

	// The mosaic tier in the three configurations nothing reached, found by
	// rendering them and reading the cells (project.md section 0d).
	{
		// The half-covered edge: one of a cell's two vertical samples is
		// opaque and the other is not. Both branches above it were covered --
		// wholly opaque, and translucent over text -- and this one was
		// measured unreachable by the suite rather than by the code, which is
		// section 7.9's coverage residue rather than a missing feature.
		//
		// The image is eight rows over two cell rows, so each cell samples
		// image rows at a quarter and three quarters of its own height. Paint
		// the first two rows and the last two and the edge falls INSIDE a
		// cell in both directions, which is what the earlier version of this
		// got wrong: painting the top HALF of the image covers cell row 0
		// entirely and takes the opaque branch.
		QImage img(4, 8, QImage::Format_ARGB32);
		img.fill(Qt::transparent);
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 4; ++x)
				img.setPixelColor(x, y, QColor(255, 0, 0, 255));
		for (int y = 6; y < 8; ++y)
			for (int x = 0; x < 4; ++x)
				img.setPixelColor(x, y, QColor(0, 255, 0, 255));
		CellBuffer edge(4, 2);
		edge.text(0, 0, QStringLiteral("abcd"));
		edge.text(0, 1, QStringLiteral("efgh"));
		compose_halfblocks(edge, img, QRect(0, 0, 4, 2), qRgb(0, 0, 0));
		CHECK(edge.at(0, 0).ch == QStringLiteral("▀")
		      && edge.at(0, 1).ch == QStringLiteral("▄"),
		      "a half-covered cell takes the block of the covered half");
		CHECK(edge.at(0, 0).fg.kind() == Color::Rgb
		      && qRed(edge.at(0, 0).fg.value()) > 200
		      && edge.at(0, 1).fg.kind() == Color::Rgb
		      && qGreen(edge.at(0, 1).fg.value()) > 200,
		      "and the colour of that half, not of the empty one");
		// The uncovered half keeps the background behind it. Asserted because
		// the opaque branch one line up DOES set bg, so a half-covered cell
		// falling into it would still draw a block of the right colour and
		// paint the empty half solid.
		CHECK(edge.at(0, 0).bg.kind() == Color::Default
		      && edge.at(0, 1).bg.kind() == Color::Default,
		      "and leaves the uncovered half showing what is behind it");

		// A placement clipped at the top-left must show the BOTTOM-RIGHT of
		// its image. Compositing the whole thing and then the same thing
		// shifted two cells off-screen, the surviving cells have to equal the
		// corresponding cells of the unclipped frame -- which is what says
		// the image was cropped rather than moved. A gradient, because a flat
		// fill cannot tell the two apart.
		QImage grad(4, 4, QImage::Format_ARGB32);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				grad.setPixelColor(x, y, QColor(x * 60, y * 60, 0, 255));
		CellBuffer whole(4, 4), clipped(4, 4);
		compose_halfblocks(whole, grad, QRect(0, 0, 4, 4), qRgb(0, 0, 0));
		compose_halfblocks(clipped, grad, QRect(-2, -2, 4, 4), qRgb(0, 0, 0));
		CHECK(clipped.at(0, 0).fg == whole.at(2, 2).fg
		      && clipped.at(1, 1).fg == whole.at(3, 3).fg,
		      "a placement clipped at the top-left shows its bottom-right");
		CHECK(clipped.at(2, 0).ch == QStringLiteral(" ")
		      && clipped.at(0, 2).ch == QStringLiteral(" "),
		      "and nothing beyond where the placement ends");

		// A null image draws nothing, QUIETLY. The sampling clamps to
		// width() - 1, which is -1 when there is no width, and
		// QImage::pixel() answers an out-of-range coordinate with a qWarning
		// -- so this used to print two warnings per cell, into the stderr
		// that in a TUI is the terminal being drawn. Counting the messages is
		// the only way to see it: the buffer is untouched either way, so
		// every assertion about the CELLS passes against the defect.
		g_messages = 0;
		QtMessageHandler previous = qInstallMessageHandler(count_message);
		CellBuffer quiet(4, 2);
		quiet.text(0, 0, QStringLiteral("abcd"));
		compose_halfblocks(quiet, QImage(), QRect(0, 0, 4, 2), qRgb(0, 0, 0));
		qInstallMessageHandler(previous);
		CHECK(g_messages == 0 && quiet.at(0, 0).ch == QStringLiteral("a"),
		      "a null image composites nothing and says nothing");
	}


	return fails;
}
