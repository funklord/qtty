// src/widget/cell_item_delegate.cpp -- Qtty::CellItemDelegate: the item-view
// data roles in Channel A (sections 8.4, 8.6 and 17.2).
#include "qtty/delegate.h"
#include "../cell_geometry.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QWidget>

namespace Qtty {

// Display width, and elision to a cell budget. Cluster-aware because a cell is
// a grapheme cluster and a wide one occupies two (section 5.2): counting
// QChars would put a CJK label one cell past the column it was given.
static int text_cells(const QString &s) {
	int n = 0;
	for (const QString &cluster : to_clusters(s)) n += cluster_width(cluster);
	return n;
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
	// to it: text written over a row the style has already filled must carry
	// the same attributes, or the fill and the text disagree about what the
	// row is. GridStyle maps State_Selected to Attr::Reverse, and so does
	// this; State_Enabled goes through with_state(), which both now share
	// rather than each keeping a copy.
	//
	// Measured before that was true: a disabled item came out
	// "2........22222222222" in the attribute plane -- the padding dim,
	// because CE_ItemViewItem fills the whole item through with_state(), and
	// the label not dim, because this wrote over it. One row carrying both
	// answers, and the state a user needs -- that the row cannot be chosen --
	// shown everywhere except on the word they are reading.
	//
	// Qt::FontRole joins them because it is DATA and this delegate is what
	// carries data the style cannot lay out. A model marking a row bold is
	// the ordinary way an item view says one row is different, and it arrives
	// in the option as a font; text written straight into the buffer takes
	// nothing from a font unless it is asked to. Measured: a bold QLabel
	// comes out bold, because that text goes through QPainter and
	// CellPaintEngine reads the painter's font -- and a bold item came out
	// plain.
	//
	// The current item's underline joins them for the same reason, and it is
	// the half a fill alone cannot carry: the style underlines the whole item
	// and this then writes the label over the middle of it, so without this
	// the mark would appear everywhere on the row EXCEPT on the word the user
	// is looking at.
	Attrs mark = (opt.state & QStyle::State_Selected) ? Attrs(Attr::Reverse)
	                                                  : Attrs();
	if (item_view_current(&opt, widget)) mark |= Attr::Underline;
	const Attrs attrs = with_state(&opt, mark) | attrs_for_font(opt.font);

	// Qt::ForegroundRole and Qt::BackgroundRole, which reached nothing. Both
	// arrive in the option -- the first as the palette's Text brush, the
	// second as backgroundBrush -- and both are DATA, which is what this
	// delegate carries.
	//
	// Deferred once as a design question and that was wrong: the project had
	// already decided it somewhere else under a different name. A colour with
	// no palette role behind it passes through as the application's own,
	// which is the rule CellPaintEngine has always applied -- so a QLabel
	// given a red palette comes out red while the same red on a model row
	// came out as nothing. fg_for() and bg_for() are that one rule, shared
	// rather than copied.
	//
	// An unset role costs nothing: the option's Text brush is then the
	// application palette's own, which matches a role, and the theme answers
	// Color::Default for it under the default theme. So a plain row is still
	// written with no colour at all.
	const Color fg = fg_for(opt.palette.color(QPalette::Text).rgba());
	Color bg;
	if (opt.backgroundBrush.style() != Qt::NoBrush)
		bg = bg_for(opt.backgroundBrush.color().rgba());

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
		buffer.text(col, row, box, fg, bg, attrs);
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
		buffer.text(x, row, s, fg, bg, attrs);
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
