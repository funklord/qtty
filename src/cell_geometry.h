// src/cell_geometry.h -- the Channel A coordinate rules, in one place.
//
// INTERNAL. Not shipped in include/qtty/, because nothing outside the library
// needs it and a header that leaves the tree is a promise about its contents.
//
// These three were file-static in grid_style.cpp and were copied into
// cell_item_delegate.cpp when that arrived, which is the parallel-copy hazard
// code-style.md names -- and the worst instance of it, because two of them
// encode measurements rather than preferences. cell_target() is finding F1:
// during a paintEvent the painter's DEVICE is the QWidget, so the cell target
// is found through the paint ENGINE, and getting it wrong fails silently by
// never entering Channel A at all. cells_of() is finding F2: neither
// transform() nor combinedTransform() carries the offset QWidget::render()
// applies, so the position comes from the widget.
//
// A second copy of a rule that was arrived at by measurement is the kind that
// drifts: somebody corrects one, the other keeps the old answer, and the
// symptom is Channel A quietly not firing in half the tree.
#pragma once
#include <QPainter>
#include <QRect>
#include <QString>
#include <QWidget>
#include "qtty/cell.h"
#include "qtty/grid.h"
#include "qtty/paint.h"

namespace Qtty {

// The cell device being painted into, or null when this is an ordinary GUI
// paint. Measured F1: p->device() is the QWidget during a paintEvent, so the
// question has to be asked of the engine.
inline CellPaintDevice *cell_target(QPainter *p) {
	if (auto *e = dynamic_cast<CellPaintEngine *>(p->paintEngine())) return e->device();
	return nullptr;
}

// A widget-space rectangle in buffer cells. Measured F2: the redirection
// offset QWidget::render() applies is carried by neither transform() nor
// combinedTransform(), so the origin comes from the widget being painted --
// p->device() when there is one, the style's widget otherwise -- plus the
// compositor's origin.
inline QRect cells_of(const QRect &r, QPainter *p, CellPaintDevice *dev,
                      const QWidget *w) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QWidget *paint_widget = dynamic_cast<QWidget *>(p->device());
	const QWidget *m = paint_widget ? paint_widget : w;
	QPoint tl = m ? m->mapTo(m->window(), r.topLeft()) : r.topLeft();
	tl += dev->origin;
	return QRect(qRound(tl.x() / double(cw)), qRound(tl.y() / double(ch)),
	             qMax(1, qRound(r.width() / double(cw))),
	             qMax(1, qRound(r.height() / double(ch))));
}

// Shorten `s` to fit `cells` columns, counting grapheme clusters and their
// display width rather than QChar units.
//
// This is the delegate's version, kept over GridStyle's, because a
// differential run over 143 cases found the two disagreeing on 9 and the
// other one wrong on every one of them. It ended with `out.chop(1)`, which
// removes one QChar rather than one cluster, and it reserved no cell for the
// marker -- so a five-character CJK string elided to a budget of 3 came back
// as a lone ellipsis using 1 of its 3 cells, and at a budget of 1 it came
// back EMPTY, rendering a truncated string as nothing at all. Chopping a
// QChar would also have split a surrogate pair, which is an invalid string
// rather than a short one.
//
// Two implementations of one rule disagreeing is what surfaced it. Neither
// had a test that asked about a wide cluster.
inline QString elide_to_cells(const QString &s, int cells) {
	if (cells <= 0) return {};
	int width = 0;
	for (const QString &cluster : to_clusters(s)) width += cluster_width(cluster);
	if (width <= cells) return s;
	int used = 0;
	QString out;
	for (const QString &cluster : to_clusters(s)) {
		const int w = cluster_width(cluster);
		if (used + w > cells - 1) break;         // one cell reserved for U+2026
		out += cluster;
		used += w;
	}
	// U+2026 as a code point, never a character literal: QLatin1Char takes a
	// char, so a UTF-8 ellipsis there is a multichar constant truncating to a
	// broken bar.
	out += QChar(0x2026);
	return out;
}

// A mnemonic marker is Qt's, not the label's: "&Save" is drawn "Save" with the
// S underlined, and "&&" is a literal ampersand. Every style that draws text
// itself has to do this, because Qt does it inside drawItemText() via
// Qt::TextShowMnemonic and a style that writes the string straight out never
// gets there.
//
// Measured: CE_PushButtonLabel wrote QStyleOptionButton::text unchanged, so a
// QPushButton("&Save") rendered "<&Save>". The check box and the group box
// were right precisely because GridStyle does NOT override their labels and
// they fall through to the base style. InputRouter owns the matching half of
// this concept -- mnemonic_of() finds the marked letter so Alt-s can reach the
// button -- which is the sharper reason the ampersand had to go: the library
// asks applications to write "&Save" and was then drawing the ampersand.
//
// The underline is not drawn. A terminal has one underline attribute and
// design.md spends it on other things; the mnemonic is discoverable by the
// Alt key rather than by the glyph. That is a limitation, not an oversight.
inline QString strip_mnemonic(const QString &s) {
	QString out;
	out.reserve(s.size());
	for (int i = 0; i < s.size(); ++i) {
		if (s.at(i) != QLatin1Char('&')) { out += s.at(i); continue; }
		if (i + 1 < s.size() && s.at(i + 1) == QLatin1Char('&')) {
			out += QLatin1Char('&');             // "&&" is one literal ampersand
			++i;
			continue;
		}
		// A lone trailing '&' is kept: it marks nothing, and dropping it would
		// silently edit a label that meant to end in one.
		if (i + 1 >= s.size()) out += QLatin1Char('&');
	}
	return out;
}

} // namespace Qtty
