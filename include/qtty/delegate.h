// qtty/delegate.h -- Channel A coverage for the item-view data roles
// (design.md sections 8.4 and 17.2).
#pragma once
#include <QStyledItemDelegate>
#include <QSize>

namespace Qtty {

// An item view paints every cell through a delegate, and the delegate hands
// the drawing to the style as CE_ItemViewItem. GridStyle already answers that
// in Channel A: it fills a selected row with reverse video, suppresses the
// pixel panel primitives, and writes the option's text. What it does NOT do is
// anything with the rest of the option, and the rest of the option is the
// DATA -- the check state, the decoration, the requested alignment all arrive
// in the QStyleOptionViewItem and are dropped on the floor, because a style
// that drew them would have to lay out a row it does not own.
//
// So this class adds exactly that and nothing else. The frame -- the selection
// fill, and whatever GridStyle::drawControl() grows next -- is still drawn by
// the style, from an option this delegate hands back with the data stripped
// out of it. There is deliberately no second copy of the selection rule here:
// duplicating it is how a delegate and a style come to disagree about what a
// selected row looks like.
//
// Install it on the view, not on the application:
//
//     view->setItemDelegate(new Qtty::CellItemDelegate(view));
//
// Outside a cell render -- a GUI build, or the same widget on a screen --
// paint() falls through to QStyledItemDelegate and the view looks exactly as
// it did, per the section 10.1 inertness contract. sizeHint() cannot ask that
// question, because a size is wanted long before any painter exists, so it
// answers in cell multiples always. That is the right answer wherever the
// delegate is installed on purpose, and installing it is an explicit act.
//
// Not a Q_OBJECT: it adds no signal, slot or property of its own, and the
// tree's other QObject subclasses (GridGuard) leave the macro off for the
// same reason.
class CellItemDelegate : public QStyledItemDelegate {
public:
	explicit CellItemDelegate(QObject *parent = nullptr);

	void paint(QPainter *painter, const QStyleOptionViewItem &option,
	           const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option,
	               const QModelIndex &index) const override;

	// The row layout, in cells, so that a caller sizing a column and a test
	// asserting a position can name the numbers rather than count them.
	// "[x] " is four cells wide and matches the checkbox GridStyle draws for
	// PE_IndicatorCheckBox; the leading indent is the one CE_ItemViewItem
	// already puts before an item's text.
	static int indent_cells() { return 1; }
	static int check_cells() { return 4; }
};

} // namespace Qtty
