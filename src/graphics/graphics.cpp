// src/graphics/graphics.cpp -- encoders, negotiation, rasterizer, halfblocks.
#include "qtty/graphics.h"
#include "kitty_diacritics.h"
#include <QBuffer>
#include <QUrl>
#include <QPixmap>
#include <QTextFragment>
#include <QTextCursor>
#include <QTextBlock>
#include "qtty/grid.h"
#include <QFontMetrics>
#include <QPainter>
#include <QBuffer>

namespace Qtty {

// ---- negotiation -----------------------------------------------------------
Capabilities::GraphicsMode detect_graphics_mode() {
	// Explicit override wins -- the contract with cooperating terminals
	// (doc/beerssh.md): the terminal exports QTTY_GRAPHICS naming the best
	// mode it implements. Also the testing hook.
	const QByteArray force = qgetenv("QTTY_GRAPHICS").toLower();
	if (!force.isEmpty()) {
		if (force == "none")        return Capabilities::NoGraphics;
		if (force == "halfblocks")  return Capabilities::Halfblocks;
		if (force == "sixel")       return Capabilities::Sixel;
		if (force == "iterm2")      return Capabilities::ITerm2;
		if (force == "kitty")       return Capabilities::Kitty;
		if (force == "kitty-alpha") return Capabilities::KittyAlpha;
	}
	const QByteArray term = qgetenv("TERM").toLower();
	const QByteArray prog = qgetenv("TERM_PROGRAM").toLower();

	// There was a beerssh special case here, assuming KittyAlpha from $TERM
	// alone -- "provisional until beerssh publishes its capability set". It is
	// removed, and the reason is worth keeping.
	//
	// This function is now only reached when the terminal answered NOTHING
	// (negotiate_graphics), so every branch in it is a guess. A guess that
	// says yes to kitty costs a screenful of escape sequences, and this one
	// said yes on behalf of a terminal that had just proved silent. It also
	// assumed ALPHA over text, which beerssh has never claimed and which the
	// protocol has no query for -- an unverified guess in the most expensive
	// direction available.
	//
	// It is unnecessary as well as unsafe: beerssh answers the kitty query
	// (measured, doc/beerssh.md), so the measured path decides and hands back
	// the tier without any of this. A silent beerssh now falls to half-blocks,
	// which is the safe direction and the honest one.
	if (!qgetenv("KITTY_WINDOW_ID").isEmpty() || term.contains("kitty")
	    || term.contains("ghostty"))
		return Capabilities::KittyAlpha;          // kitty protocol, alpha over text
	if (prog.contains("wezterm"))
		return Capabilities::Kitty;               // kitty protocol, no alpha-over-text
	if (prog.contains("iterm"))
		return Capabilities::ITerm2;
	if (term.contains("sixel") || term.contains("mlterm") || term.contains("foot")
	    || prog.contains("mintty"))
		return Capabilities::Sixel;
	return Capabilities::Halfblocks;              // every colour terminal
}

// ---- sixel -----------------------------------------------------------------
QByteArray encode_sixel(const QImage &src) {
	const QImage img = src.convertToFormat(QImage::Format_ARGB32);
	const int w = img.width(), h = img.height();

	// Quantise via the xterm-256 cube; collect used registers.
	QVector<int> idx(w * h, -1);                  // -1 = transparent
	bool used[256] = {};
	for (int y = 0; y < h; ++y) {
		const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
		for (int x = 0; x < w; ++x) {
			if (qAlpha(line[x]) < 128) continue;
			// to_xterm256() returns -1 only for a Default colour, and the
			// colour here is constructed with Color::rgb(), so it always
			// takes the Lab match and always lands in 16..255. The clamp
			// that stood here could not fire, and had it ever fired it would
			// have written white into the pixel rather than reporting -- a
			// silent wrong answer in the one path nothing else reads.
			const int c = Color::rgb(line[x]).to_xterm256();
			idx[y * w + x] = c;
			used[c] = true;
		}
	}

	QByteArray out = "\033P0;1;0q";               // P2=1: untouched -> transparent
	out += "\"1;1;" + QByteArray::number(w) + ';' + QByteArray::number(h);

	// xterm256_rgb() rather than a copy of it. This carried its own 16-colour
	// table, its own grey ramp and its own cube arithmetic, all identical to
	// the public function in color.h -- verified index by index, 0 of 256
	// differing, before the copy was removed. Two copies of one table is the
	// parallel-copy hazard code-style.md names: correct one and the other
	// stays wrong, silently, in a path nothing else exercises. The sixel
	// encoder is exactly such a path, since nothing but a sixel terminal
	// reads its output.
	for (int c = 0; c < 256; ++c)
		if (used[c]) {
			const QRgb v = xterm256_rgb(c);
			out += '#' + QByteArray::number(c) + ";2;"
			     + QByteArray::number(qRed(v)   * 100 / 255) + ';'
			     + QByteArray::number(qGreen(v) * 100 / 255) + ';'
			     + QByteArray::number(qBlue(v)  * 100 / 255);
		}

	for (int band = 0; band < h; band += 6) {
		bool first_color = true;
		for (int c = 0; c < 256; ++c) {
			if (!used[c]) continue;
			// does this colour appear in the band?
			bool present = false;
			for (int y = band; y < qMin(band + 6, h) && !present; ++y)
				for (int x = 0; x < w; ++x)
					if (idx[y * w + x] == c) { present = true; break; }
			if (!present) continue;
			if (!first_color) out += '$';          // carriage return within band
			first_color = false;
			out += '#' + QByteArray::number(c);
			int run_char = -1, run_len = 0;
			auto flush = [&] {
				if (run_len <= 0) return;
				if (run_len > 3) out += '!' + QByteArray::number(run_len) + char(run_char);
				else for (int i = 0; i < run_len; ++i) out += char(run_char);
			};
			for (int x = 0; x < w; ++x) {
				int bits = 0;
				for (int dy = 0; dy < 6 && band + dy < h; ++dy)
					if (idx[(band + dy) * w + x] == c) bits |= 1 << dy;
				const int ch = 63 + bits;         // '?' + bits
				if (ch == run_char) ++run_len;
				else { flush(); run_char = ch; run_len = 1; }
			}
			flush();
		}
		out += '-';                               // next band
	}
	out += "\033\\";
	return out;
}

// ---- kitty -----------------------------------------------------------------
static QByteArray kitty_chunks(const QByteArray &ctrl, const QByteArray &payload) {
	QByteArray out;
	const int N = 4096;
	for (int off = 0; off < payload.size(); off += N) {
		const bool last = off + N >= payload.size();
		out += "\033_G";
		if (off == 0) out += ctrl + (payload.size() > N ? ",m=1" : "");
		else out += QByteArray("m=") + (last ? "0" : "1");
		out += ';';
		out += payload.mid(off, N);
		out += "\033\\";
	}
	if (payload.isEmpty()) out += "\033_G" + ctrl + ";\033\\";
	return out;
}

QByteArray encode_kitty_image(quint32 id, const QImage &src, int z) {
	const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
	QByteArray raw(reinterpret_cast<const char *>(img.constBits()),
	               int(img.sizeInBytes()));
	QByteArray ctrl = "a=T,f=32,q=2"
	    ",i=" + QByteArray::number(id)
	    + ",s=" + QByteArray::number(img.width())
	    + ",v=" + QByteArray::number(img.height());
	if (z) ctrl += ",z=" + QByteArray::number(z);
	return kitty_chunks(ctrl, raw.toBase64());
}

QByteArray kitty_place(quint32 id, int z, const QRect &source) {
	QByteArray ctrl = "a=p,q=2,i=" + QByteArray::number(id);
	// x/y/w/h select a rectangle of the stored image. Emitted only when the
	// placement is actually cropped, so an ordinary re-place stays the ~30
	// bytes section 16.3 measured.
	if (!source.isNull() && !source.isEmpty())
		ctrl += ",x=" + QByteArray::number(source.x())
		      + ",y=" + QByteArray::number(source.y())
		      + ",w=" + QByteArray::number(source.width())
		      + ",h=" + QByteArray::number(source.height());
	if (z) ctrl += ",z=" + QByteArray::number(z);
	return "\033_G" + ctrl + ";\033\\";
}

CroppedPlacement crop_placement(const QRect &cell_rect, QSize image, QSize grid) {
	const QRect visible = cell_rect.intersected(QRect(QPoint(0, 0), grid));
	if (visible.isEmpty() || cell_rect.isEmpty() || image.isEmpty())
		return {QRect(), QRect()};
	if (visible == cell_rect)
		return {cell_rect, QRect(QPoint(0, 0), image)};

	// The image spans cell_rect, so a cell maps to a fixed block of pixels.
	// Derived from the image and the rect rather than from GridMetrics: an
	// image placed into N cells is N cells wide whatever the cell size is,
	// and taking the ratio here keeps this function testable without a
	// configured grid.
	const int px_w = image.width()  / cell_rect.width();
	const int px_h = image.height() / cell_rect.height();
	QRect source((visible.left() - cell_rect.left()) * px_w,
	             (visible.top()  - cell_rect.top())  * px_h,
	             visible.width()  * px_w,
	             visible.height() * px_h);
	// Integer division above can leave a remainder, so the last visible cell
	// would ask for pixels past the edge. Clamp rather than round, because
	// asking a terminal for a source rectangle outside the image is a
	// protocol error on kitty and a crash risk in QImage::copy.
	source = source.intersected(QRect(QPoint(0, 0), image));
	if (source.isEmpty()) return {QRect(), QRect()};
	return {visible, source};
}

QByteArray kitty_delete_all() { return "\033_Ga=d,d=a,q=2;\033\\"; }

// ---- iTerm2 ----------------------------------------------------------------
QByteArray encode_iterm2(const QImage &img, int w_cells, int h_cells) {
	QByteArray png;
	QBuffer buf(&png);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	return "\033]1337;File=inline=1;width=" + QByteArray::number(w_cells)
	     + ";height=" + QByteArray::number(h_cells)
	     + ";preserveAspectRatio=0:" + png.toBase64() + "\a";
}

// ---- rasterizer ------------------------------------------------------------
void rasterize_into(QImage &dst, const CellBuffer &frame, const QFont &font,
                    const QRect &cells) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	if (dst.isNull()) return;
	QRect r = cells.intersected(QRect(0, 0, frame.cols(), frame.rows()));
	if (r.isEmpty()) return;
	// Leftwards to the start of a wide cluster the rect cuts in half. The
	// continuation cell carries no glyph, so a region beginning there paints
	// nothing and leaves the previous frame showing through -- the one way a
	// smaller repaint can be WRONG rather than merely partial.
	int from = r.left();
	for (int y = r.top(); y <= r.bottom(); ++y)
		while (from > 0 && frame.at(from, y).width == 0) --from;
	r.setLeft(from);

	const QRgb default_bg = qRgb(16, 20, 24), default_fg = qRgb(215, 218, 220);
	QPainter p(&dst);
	QFontMetrics fm(font);
	p.fillRect(r.x() * cw, r.y() * ch, r.width() * cw, r.height() * ch,
	           QColor::fromRgb(default_bg));
	for (int y = r.top(); y <= r.bottom(); ++y)
		for (int x = r.left(); x <= r.right(); ++x) {
			const Cell &c = frame.at(x, y);
			if (c.width == 0) continue;
			QRgb fg = c.fg.kind() == Color::Rgb ? c.fg.value() : default_fg;
			QRgb bg = c.bg.kind() == Color::Rgb ? c.bg.value() : default_bg;
			if (c.attrs & Attr::Reverse) std::swap(fg, bg);
			if (bg != default_bg || (c.attrs & Attr::Reverse))
				p.fillRect(x * cw, y * ch, cw * c.width, ch, QColor::fromRgb(bg));
			if (c.ch != QStringLiteral(" ")) {
				QFont f = font;
				f.setBold(c.attrs & Attr::Bold);
				f.setItalic(c.attrs & Attr::Italic);
				f.setUnderline(c.attrs & Attr::Underline);
				p.setFont(f);
				p.setPen(QColor::fromRgb(fg));
				p.drawText(x * cw, y * ch + fm.ascent(), c.ch);
			}
		}
	p.end();
}

QImage rasterize(const CellBuffer &frame, const QFont &font) {
	QImage img(frame.cols() * GridMetrics::cw(),
	           frame.rows() * GridMetrics::ch(),
	           QImage::Format_ARGB32_Premultiplied);
	img.fill(qRgb(16, 20, 24));
	rasterize_into(img, frame, font, QRect(0, 0, frame.cols(), frame.rows()));
	return img;
}

// ---- halfblock fallback ----------------------------------------------------
static QRgb blend(QRgb over, int a, QRgb under) {
	return qRgb((qRed(over)   * a + qRed(under)   * (255 - a)) / 255,
	            (qGreen(over) * a + qGreen(under) * (255 - a)) / 255,
	            (qBlue(over)  * a + qBlue(under)  * (255 - a)) / 255);
}

QByteArray encode_kitty_virtual(quint32 id, const QImage &img, int cols, int rows) {
	// Transmit and create the virtual placement in one escape, which the
	// specification allows: a=T with U=1. q=2 is quiet mode, so the terminal
	// answers nothing -- a reply here would be read as input by whatever host
	// application the placeholders are being printed through.
	QByteArray png;
	QBuffer buf(&png);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	buf.close();
	const QByteArray b64 = png.toBase64();

	QByteArray head = "\033_Ga=T,U=1,q=2,f=100,i=" + QByteArray::number(id)
	                + ",c=" + QByteArray::number(cols)
	                + ",r=" + QByteArray::number(rows);
	QByteArray out;
	const int chunk = 4096;
	for (int off = 0; off < b64.size(); off += chunk) {
		const QByteArray part = b64.mid(off, chunk);
		const bool more = off + chunk < b64.size();
		out += off == 0 ? head : QByteArray("\033_G");
		out += ",m=" + QByteArray::number(more ? 1 : 0) + ';' + part + "\033\\";
	}
	return out;
}

void compose_kitty_placeholders(CellBuffer &frame, quint32 id, const QRect &cell_rect) {
	const bool wide_id = id >= (1u << 24);
	const int msb = int((id >> 24) & 0xFF);
	for (int r = 0; r < cell_rect.height(); ++r) {
		for (int c = 0; c < cell_rect.width(); ++c) {
			const int X = cell_rect.x() + c, Y = cell_rect.y() + r;
			if (X < 0 || Y < 0 || X >= frame.cols() || Y >= frame.rows()) continue;
			if (r >= KITTY_DIACRITIC_COUNT || c >= KITTY_DIACRITIC_COUNT) continue;
			if (wide_id && msb >= KITTY_DIACRITIC_COUNT) continue;
			QString cluster;
			cluster += QString::fromUcs4(reinterpret_cast<const char32_t *>(
			    &KITTY_PLACEHOLDER), 1);
			cluster += QString::fromUcs4(&KITTY_DIACRITICS[r], 1);
			cluster += QString::fromUcs4(&KITTY_DIACRITICS[c], 1);
			if (wide_id) cluster += QString::fromUcs4(&KITTY_DIACRITICS[msb], 1);
			Cell &cell = frame.at(X, Y);
			cell.ch = cluster;
			cell.fg = Color::rgb(qRgb(int((id >> 16) & 0xFF),
			                          int((id >> 8) & 0xFF), int(id & 0xFF)));
			cell.attrs = {};
			cell.width = 1;
		}
	}
}

int align_text_document(QTextDocument *doc, QSize cell_px) {
	if (!doc || !cell_px.isValid() || cell_px.width() <= 0 || cell_px.height() <= 0)
		return 0;
	const int cw = cell_px.width(), ch = cell_px.height();
	const auto round_up = [](double v, int step) {
		return double((int(v) + step - 1) / step * step);
	};

	// Collected first, applied afterwards. Editing a fragment's format through
	// a cursor changes the document, and the block iterator being walked is
	// not guaranteed to survive that.
	struct Edit { int start, end; QTextImageFormat format; };
	QVector<Edit> edits;

	for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
		for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
			const QTextFragment f = it.fragment();
			if (!f.isValid()) continue;
			const QTextCharFormat cf = f.charFormat();
			if (!cf.isImageFormat()) continue;
			QTextImageFormat img = cf.toImageFormat();

			double w = img.width(), h = img.height();
			if (w <= 0 || h <= 0) {
				// Unset in the format, so the layout would use the resource's
				// natural size and undo any rounding. Resolve it and write it
				// back explicitly.
				const QVariant res = doc->resource(QTextDocument::ImageResource,
				                                  QUrl(img.name()));
				QSize natural;
				if (res.canConvert<QImage>()) natural = res.value<QImage>().size();
				else if (res.canConvert<QPixmap>()) natural = res.value<QPixmap>().size();
				if (natural.isEmpty()) continue;      // unknowable: leave it
				if (w <= 0) w = natural.width();
				if (h <= 0) h = natural.height();
			}
			const double aw = round_up(w, cw), ah = round_up(h, ch);
			if (aw == img.width() && ah == img.height()) continue;
			img.setWidth(aw);
			img.setHeight(ah);
			edits.append({f.position(), f.position() + f.length(), img});
		}
	}

	for (const Edit &e : edits) {
		QTextCursor cur(doc);
		cur.setPosition(e.start);
		cur.setPosition(e.end, QTextCursor::KeepAnchor);
		cur.setCharFormat(e.format);
	}
	return int(edits.size());
}

QSize cells(QSize image_px, QSize cell_px) {
	if (!cell_px.isValid() || cell_px.width() <= 0 || cell_px.height() <= 0)
		return QSize();                 // nothing was measured; say so
	if (image_px.width() <= 0 || image_px.height() <= 0) return QSize();
	return QSize((image_px.width() + cell_px.width() - 1) / cell_px.width(),
	             (image_px.height() + cell_px.height() - 1) / cell_px.height());
}

void compose_halfblocks(CellBuffer &frame, const QImage &src, const QRect &cell_rect,
                       QRgb under) {
	// A null image has nothing to composite, and asking it for pixels is not
	// quiet about it. The sampling below clamps to width() - 1, which is -1
	// when there is no width, and QImage::pixel() answers an out-of-range
	// coordinate with a qWarning and the value 12345 -- so a null placement
	// drew nothing and printed TWO WARNINGS PER CELL while doing it. In a TUI
	// that is stderr, which is the terminal: a 40x20 placement puts 1600 lines
	// of "QImage::pixel: coordinate (-1,-1) out of range" through the screen
	// the library is drawing.
	//
	// The library's own routes are already guarded -- the overlay registry
	// skips a null image and the pixel-surface harvest skips a zero-size
	// widget -- so this is the public entry point answering for itself.
	// compose_halfblocks() is declared in qtty/graphics.h and an application
	// may call it, or append a CellImage of its own to CellBuffer::images.
	if (src.isNull()) return;
	const QImage img = src.convertToFormat(QImage::Format_ARGB32);
	const QRgb under_default = under;
	for (int cy = 0; cy < cell_rect.height(); ++cy)
		for (int cx = 0; cx < cell_rect.width(); ++cx) {
			const int X = cell_rect.x() + cx, Y = cell_rect.y() + cy;
			if (X < 0 || Y < 0 || X >= frame.cols() || Y >= frame.rows()) continue;
			// two vertical samples per cell (2x vertical resolution)
			auto sample = [&](double fy) -> QRgb {
				int sx = qMin(int((cx + 0.5) * img.width() / cell_rect.width()), img.width() - 1);
				int sy = qMin(int((cy + fy) * img.height() / cell_rect.height()), img.height() - 1);
				return img.pixel(sx, sy);
			};
			const QRgb top = sample(0.25), bot = sample(0.75);
			const int alpha_top = qAlpha(top), alpha_bot = qAlpha(bot);
			if (alpha_top < 40 && alpha_bot < 40) continue;                  // transparent: untouched
			Cell &cell = frame.at(X, Y);
			if (alpha_top > 200 && alpha_bot > 200) {                        // opaque: 2 pixels
				cell.ch = QStringLiteral("▀");
				cell.fg = Color::rgb(QRgb(top | 0xFF000000));
				cell.bg = Color::rgb(QRgb(bot | 0xFF000000));
				cell.attrs = {}; cell.width = 1;
			} else if (alpha_top > 200 || alpha_bot > 200) {                 // half-covered edge
				const bool top_half = alpha_top > 200;
				cell.ch = top_half ? QStringLiteral("▀") : QStringLiteral("▄");
				cell.fg = Color::rgb(QRgb((top_half ? top : bot) | 0xFF000000));
				// keep whatever bg is behind the uncovered half
				cell.attrs = {}; cell.width = 1;
			} else {                                           // translucent: tint bg,
				const int a = qMax(alpha_top, alpha_bot);                    // glyph stays readable
				const QRgb under = cell.bg.kind() == Color::Rgb ? cell.bg.value()
				                                                : under_default;
				cell.bg = Color::rgb(blend(top, a, under));
			}
		}
}

} // namespace Qtty
