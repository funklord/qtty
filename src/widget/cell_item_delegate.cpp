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

	// Every write below goes straight into the buffer, so nothing else
	// bounds them: the budget is the ITEM's rectangle, and an item is
	// entitled to be wider than the view showing it. GridStyle installs this
	// same clip on each of its three entry points -- but the drawControl()
	// above installs and tears down its own before returning, so it does not
	// cover this. Measured: a right-aligned label in a twelve-cell column
	// inside a sixteen-cell view was written at cell 21, over the widget
	// beside it.
	//
	// The VIEWPORT, through painted_widget(): a view's paintEvent paints
	// into it, so it is what the painter's device answers and what cells_of()
	// already measures against. Using opt.widget instead would clip a
	// combo box's drop-down to the one-row combo, which is the fault
	// recorded beside visible_rect().
	//
	// The elision budget is deliberately NOT clipped with the writes. It is
	// the item's rectangle, which is what Qt elides to on a pixel screen: a
	// partially scrolled item shows a partial label rather than a re-elided
	// one, and shrinking it would move a right-aligned label the moment a
	// scroll bar appeared.
	const QWidget *pw = painted_widget(painter, widget);
	const CellClip bound(dev, pw ? cells_of(visible_rect(pw), painter, dev, pw)
	                             : c);

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
	// Laid out from the LEADING edge, by the leading_edge() the style's own
	// CE_ItemViewItem and both of its rectangle answers now use. `used`
	// counts cells consumed from that edge; the absolute column each element
	// lands in is derived from it, so right-to-left puts the indent, the box
	// and the decoration at the right-hand end as Qt does.
	//
	// Shared rather than copied for the reason this file's header gives: a
	// second copy of a rule arrived at by measurement is the kind that
	// drifts, and the displayAlignment above is a case where these two
	// writers HAD drifted -- a program with this delegate installed was
	// right and the same program without it was wrong.
	int used = indent_cells();

	if (opt.features & QStyleOptionViewItem::HasCheckIndicator) {
		QString box = QStringLiteral("[ ]");
		if (opt.checkState == Qt::Checked)                box = QStringLiteral("[x]");
		else if (opt.checkState == Qt::PartiallyChecked)  box = QStringLiteral("[-]");
		buffer.text(leading_edge(c.left(), c.right(), used, 3, opt.direction),
		            row, box, fg, bg, attrs);
		used += check_cells();
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
		const int deco_at =
		    leading_edge(c.left(), c.right(), used, dw, opt.direction);
		const QRect px(opt.rect.left() + (deco_at - c.left()) * cw,
		               opt.rect.top() + (row - c.top()) * ch, dw * cw, dh * ch);
		painter->drawPixmap(px, opt.icon.pixmap(opt.decorationSize));
		used += dw + 1;
	}

	const int budget = c.width() - used;
	const int col =
	    leading_edge(c.left(), c.right(), used, budget, opt.direction);
	if (budget > 0 && !opt.text.isEmpty()) {
		// The view's own elide mode, for the reason the style records at
		// its own call: the option carries it and both writers discarded it.
		const QString s = elide_to_cells(opt.text, budget, opt.textElideMode);
		const int width = text_cells(s);
		int x = col;
		const Qt::Alignment al =
		    QStyle::visualAlignment(opt.direction, opt.displayAlignment);
		if (al & Qt::AlignRight)        x = col + budget - width;
		else if (al & Qt::AlignHCenter) x = col + (budget - width) / 2;
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
