// src/widget/cell_item_delegate.cpp -- Qtty::CellItemDelegate: the item-view
// data roles in Channel A (sections 8.4, 8.6 and 17.2).
#include "qtty/delegate.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QWidget>

namespace Qtty {

// Both of these are GridStyle's, and both are here rather than shared because
// they are file-static there. The rules they encode are measured ones and must
// not be re-derived: section 16 F1 says the paint ENGINE identifies a cell
// target, since inside a paintEvent the painter's device is the widget; F2
// says the offset comes from mapping through the widget being painted, because
// no transform on the painter carries render()'s redirection. WHICH widget is
// the part that bites for item views -- opt.rect is in the viewport's
// coordinates, which is p->device(), while the widget a delegate is handed is
// the view. Mapping through the view drops the header and frame offset.
static CellPaintDevice *cell_target(QPainter *p) {
	if (auto *e = dynamic_cast<CellPaintEngine *>(p->paintEngine())) return e->device();
	return nullptr;
}

static QRect cells_of(const QRect &r, QPainter *p, CellPaintDevice *dev, const QWidget *w) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QWidget *paint_widget = dynamic_cast<QWidget *>(p->device());
	const QWidget *m = paint_widget ? paint_widget : w;
	QPoint tl = m ? m->mapTo(m->window(), r.topLeft()) : r.topLeft();
	tl += dev->origin;
	return QRect(qRound(tl.x() / double(cw)), qRound(tl.y() / double(ch)),
	             qMax(1, qRound(r.width() / double(cw))),
	             qMax(1, qRound(r.height() / double(ch))));
}

// Display width, and elision to a cell budget. Cluster-aware because a cell is
// a grapheme cluster and a wide one occupies two (section 5.2): counting
// QChars would put a CJK label one cell past the column it was given.
static int text_cells(const QString &s) {
	int n = 0;
	for (const QString &cluster : to_clusters(s)) n += cluster_width(cluster);
	return n;
}

static QString elide_to_cells(const QString &s, int cells) {
	if (cells <= 0) return {};
	if (text_cells(s) <= cells) return s;
	int used = 0;
	QString out;
	for (const QString &cluster : to_clusters(s)) {
		const int w = cluster_width(cluster);
		if (used + w > cells - 1) break;
		out += cluster;
		used += w;
	}
	// U+2026 as a code point, never a character literal: QLatin1Char takes a
	// char, so a UTF-8 ellipsis there is a multichar constant that truncates
	// to a broken bar. GridStyle's elide() carries the same note and the same
	// scar.
	out += QChar(0x2026);
	return out;
}

CellItemDelegate::CellItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void CellItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
	CellPaintDevice *dev = cell_target(painter);
	if (!dev) {                                   // GUI path, untouched
		QStyledItemDelegate::paint(painter, option, index);
		return;
	}

	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);
	const QWidget *widget = opt.widget;

	// The frame belongs to the style. It is handed back the option with the
	// data stripped out, so CE_ItemViewItem paints the selection -- and
	// whatever it grows next -- while this delegate paints only what the
	// style could not lay out. Clearing the text matters: leaving it would
	// draw the label twice, at the style's position and then at this one.
	QStyleOptionViewItem frame = opt;
	frame.text.clear();
	frame.icon = QIcon();
	frame.features &= ~(QStyleOptionViewItem::HasDisplay
	                    | QStyleOptionViewItem::HasDecoration
	                    | QStyleOptionViewItem::HasCheckIndicator);
	QStyle *style = widget ? widget->style() : QApplication::style();
	style->drawControl(QStyle::CE_ItemViewItem, &frame, painter, widget);

	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QRect c = cells_of(opt.rect, painter, dev, widget);
	CellBuffer &buffer = dev->buffer();

	// The one place this does have to agree with the style rather than defer
	// to it: text written over a reverse-video row must carry the attribute
	// too, or the row's own reverse video hides it. GridStyle maps
	// State_Selected to Attr::Reverse, and so does this.
	const Attrs attrs = (opt.state & QStyle::State_Selected) ? Attrs(Attr::Reverse) : Attrs();

	int row = c.top();
	if (c.height() > 1) {
		if (opt.displayAlignment & Qt::AlignBottom)       row = c.bottom();
		else if (opt.displayAlignment & Qt::AlignVCenter) row = c.top() + (c.height() - 1) / 2;
	}
	int col = c.left() + indent_cells();

	if (opt.features & QStyleOptionViewItem::HasCheckIndicator) {
		QString box = QStringLiteral("[ ]");
		if (opt.checkState == Qt::Checked)                box = QStringLiteral("[x]");
		else if (opt.checkState == Qt::PartiallyChecked)  box = QStringLiteral("[-]");
		buffer.text(col, row, box, Color(), Color(), attrs);
		col += check_cells();
	}

	if ((opt.features & QStyleOptionViewItem::HasDecoration) && !opt.icon.isNull()) {
		const int dw = qMax(1, qRound(opt.decorationSize.width() / double(cw)));
		const int dh = qMax(1, qRound(opt.decorationSize.height() / double(ch)));
		// Through QPainter deliberately, rather than into the buffer.
		// CellPaintEngine::drawPixmap IS the section 8.6 funnel already: two
		// cells or more in each direction becomes a section 5.7 placement
		// carrying real pixels, and anything smaller substitutes a glyph. A
		// second copy of that decision here is a second answer to one
		// question, and the two would part company the first time the
		// graphics tier learned something.
		const QRect px(opt.rect.left() + (col - c.left()) * cw,
		               opt.rect.top() + (row - c.top()) * ch, dw * cw, dh * ch);
		painter->drawPixmap(px, opt.icon.pixmap(opt.decorationSize));
		col += dw + 1;
	}

	const int budget = c.right() - col + 1;
	if (budget > 0 && !opt.text.isEmpty()) {
		const QString s = elide_to_cells(opt.text, budget);
		const int width = text_cells(s);
		int x = col;
		if (opt.displayAlignment & Qt::AlignRight)        x = c.right() - width + 1;
		else if (opt.displayAlignment & Qt::AlignHCenter) x = col + (budget - width) / 2;
		buffer.text(x, row, s, Color(), Color(), attrs);
	}
}

// The width is derived from the row this delegate actually draws, rather than
// snapped up from the proxied answer. Snapping the base is what the first
// version did and it cannot be checked: GridStyle::sizeFromContents already
// returns a cell multiple for CT_ItemViewItem, so "the hint is a cell
// multiple" passes for a delegate that overrides nothing at all. Deriving it
// makes the hint say something -- a checkable item is exactly check_cells()
// wider than the same item without a check state -- and that is a difference a
// test can hold.
//
// The height is the proxy's, snapped, and at least one row: a decoration two
// cells tall is the thing that makes a row taller than a line of text, and the
// base hint already accounts for it.
QSize CellItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	const QSize base = QStyledItemDelegate::sizeHint(option, index);

	int cells = indent_cells();
	if (index.data(Qt::CheckStateRole).isValid()) cells += check_cells();
	if (index.data(Qt::DecorationRole).isValid())
		cells += qMax(1, qRound(option.decorationSize.width() / double(cw))) + 1;
	cells += text_cells(index.data(Qt::DisplayRole).toString());

	return QSize(cells * cw, qMax(ch, ((base.height() + ch - 1) / ch) * ch));
}

} // namespace Qtty
