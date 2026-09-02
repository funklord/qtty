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
#include <QFont>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QStyleOption>
#include <QWidget>
#include <QGuiApplication>
#include <QPalette>
#include "qtty/cell.h"
#include "qtty/grid.h"
#include "qtty/paint.h"
#include "qtty/theme.h"

namespace Qtty {

// Which palette role explains a colour, or NoRole.
//
// This is the kernel of the rule cell_paint.cpp settled and wrote down: the
// application palette is consulted for one thing only -- which ROLE produced
// this colour -- and what that role looks like on a terminal is theme()'s to
// say. A colour no role explains is one the application chose itself, and it
// passes through as true colour.
//
// It lives here because three places need the same answer and two of them had
// no answer at all. CellPaintEngine had it, so a QLabel given a red palette
// came out red; CellItemDelegate and GridStyle's own CE_ItemViewItem wrote
// Color() unconditionally, so the same red on a model's Qt::ForegroundRole
// came out as nothing. Measured, in one program: three answers to one
// question, and only one of them was the rule.
//
// The colour GROUP matters as well as the role, and this asked only the
// palette's current one -- Active, for the application palette. Qt paints a
// disabled widget in the Disabled group's colour, which therefore matched no
// role and fell through as a colour the application chose. Measured on a form
// of thirteen widgets, every one of them disabled in turn: GridStyle said
// "disabled" with Attr::Dim and left the colour alone, while everything
// drawn through QPainter -- a QLabel's text, a field's contents, a check
// box's own label, an item view's rows -- came out as a hard 24-bit #bebebe.
// One state, two answers.
//
// The Channel B answer is the worse of the two on a terminal. #bebebe is
// Fusion's grey for a light desktop: on a light terminal it is nearly
// invisible, on a dark one it is BRIGHTER than ordinary text, so "disabled"
// read as "emphasised". It also spends a true-colour sequence on a terminal
// that may have sixteen colours, which is the thing section 6's rule exists
// to avoid -- and it did so for a colour that has a perfectly good role.
//
// Active is searched first, so a palette whose two groups share a colour
// reads as enabled. That is the safe direction: a missing Dim understates,
// a spurious one greys out a control the user can actually use.
inline QPalette::ColorRole role_of(QRgb c,
                                   std::initializer_list<QPalette::ColorRole> roles,
                                   bool *disabled = nullptr) {
	const QPalette &pal = QGuiApplication::palette();
	if (disabled) *disabled = false;
	for (QPalette::ColorRole r : roles)
		if (pal.color(QPalette::Active, r).rgba() == c) return r;
	for (QPalette::ColorRole r : roles)
		if (pal.color(QPalette::Disabled, r).rgba() == c) {
			if (disabled) *disabled = true;
			return r;
		}
	return QPalette::NoRole;
}

// A text colour, by that rule. The role list is the one CellPaintEngine's pen
// path uses, and is deliberately the same list rather than a similar one.
// A text colour and the emphasis that belongs with it, by that rule. The
// emphasis is the half that had nowhere to go: the role's own colour is what
// the theme says text looks like, and "disabled" is Attr::Dim on top of it,
// which is exactly what GridStyle's with_state() has always written.
struct TextStyle { Color color; Attrs attrs; };
inline TextStyle text_style_for(QRgb c) {
	bool disabled = false;
	const QPalette::ColorRole r = role_of(c, {QPalette::WindowText, QPalette::Text,
		                                      QPalette::ButtonText,
		                                      QPalette::HighlightedText},
		                                  &disabled);
	if (r == QPalette::NoRole) return TextStyle{ Color::rgb(c), Attrs() };
	return TextStyle{ theme().foreground(r),
		              disabled ? Attrs(Attr::Dim) : Attrs() };
}
inline Color fg_for(QRgb c) { return text_style_for(c).color; }

// A background colour, by the same rule. A surface role the theme leaves at
// Color::Default means "the terminal's own background", which is nothing to
// write rather than something to write in black -- so it comes back Default
// and the caller leaves the cell alone.
inline Color bg_for(QRgb c) {
	const QPalette::ColorRole r = role_of(c, {QPalette::Window, QPalette::Base,
		                                      QPalette::Button,
		                                      QPalette::AlternateBase,
		                                      QPalette::Highlight,
		                                      QPalette::ToolTipBase});
	if (r == QPalette::NoRole) return Color::rgb(c);
	const Color themed = theme().background(r);
	return themed;
}

// A disabled control is dim, and until this existed it was not anything.
// Qt reports the state in every option it hands the style, and GridStyle
// tested for it at no site at all -- so a button nobody can press looked
// exactly like one they can, a greyed menu item read as available, and the
// only way to find out was to click and have nothing happen. The same fault
// as the tristate checkbox, at every control rather than one.
//
// Dim rather than a colour: a terminal's dim is one SGR that composes with
// whatever the theme already chose, while a grey would have to be picked
// against a background this style does not know.
//
// It lives here rather than in grid_style.cpp, where it was file-static,
// because CellItemDelegate needs the same answer and getting a different one
// is visible: measured before the move, a disabled item view drew its padding
// dim -- the style's fill -- and then the delegate wrote the label over it
// with no attributes, so one row carried both answers at once.
inline Attrs with_state(const QStyleOption *opt, Attrs base = Attrs()) {
	if (opt && !(opt->state & QStyle::State_Enabled)) base |= Attr::Dim;
	return base;
}

// Whether this item is the one the keys would act on: the view owns the
// router's focus and the index is its current one. DECLARED here and defined
// in grid_style.cpp, which is the compromise the two costs settle on -- the
// definition needs QAbstractItemView, and this header is included by most of
// the tree, so moving the body would buy symmetry with a compile-time bill in
// every translation unit. The declaration is the half that matters: it is what
// stops the style's fill and the delegate's label growing two answers, which
// is exactly how with_state() came to draw one disabled row two ways at once
// -- and that copy was in these same two files.
bool item_view_current(const QStyleOptionViewItem *vi, const QWidget *w);

// The emphasis a font carries, as cell attributes. A terminal has these four
// and no others, which is why this is a translation rather than a rendering:
// weight is bold or it is not, and a font's size, family and stretch have
// nowhere to go on a grid where every cell is the same size.
//
// Text drawn through QPainter already gets this -- CellPaintEngine reads the
// painter's font, which is why a bold QLabel comes out bold. Text written
// straight into the buffer does not, because nothing carries the font that
// far. That is the gap this closes for the one option that delivers a font as
// DATA rather than as a widget's own: QStyleOptionViewItem::font carries
// Qt::FontRole, and an item view is where a model says "this row differs".
inline Attrs attrs_for_font(const QFont &f) {
	Attrs a;
	if (f.bold())      a |= Attr::Bold;
	if (f.italic())    a |= Attr::Italic;
	if (f.underline()) a |= Attr::Underline;
	if (f.strikeOut()) a |= Attr::Strike;
	return a;
}

// What a label written STRAIGHT INTO THE BUFFER must carry: the state the
// option reports, and the emphasis of the font whoever owns the label was
// given.
//
// The font half exists because the two text paths do not agree on their own.
// Text drawn through QPainter arrives at CellPaintEngine, which reads the
// painter's font -- so a bold QLabel comes out bold, and a check box's label
// does too, because GridStyle does not override it and it falls through to
// the base style. Text this style writes itself takes nothing from a font
// unless it is asked to, so a bold QPushButton came out plain: the same
// program, the same font, two answers depending on which route the label
// happened to take.
//
// Take the font from the OPTION where the option carries one -- a menu item
// and a view item both do, and theirs is the one Qt resolved for that item
// rather than the widget's -- and from the widget otherwise. A null widget
// contributes nothing rather than a default-constructed font's answer.
inline Attrs label_attrs(const QStyleOption *opt, const QWidget *w,
                         Attrs base = Attrs()) {
	return with_state(opt, base) | (w ? attrs_for_font(w->font()) : Attrs());
}
// The union of the two, for an option that carries a font of its own. It is a
// union rather than a choice because both are reasons to emphasise and
// neither is the whole answer: measured, QMenuBar leaves
// QStyleOptionMenuItem::font at the application font, so an italic menu bar
// read as plain when the option's font was taken as authoritative -- while a
// default menu action is bold in the option and nowhere on the widget.
inline Attrs label_attrs(const QStyleOption *opt, const QWidget *w, const QFont &f,
                         Attrs base = Attrs()) {
	return label_attrs(opt, w, base) | attrs_for_font(f);
}

// The cell device being painted into, or null when this is an ordinary GUI
// paint. Measured F1: p->device() is the QWidget during a paintEvent, so the
// question has to be asked of the engine.
inline CellPaintDevice *cell_target(QPainter *p) {
	if (auto *e = dynamic_cast<CellPaintEngine *>(p->paintEngine())) return e->device();
	return nullptr;
}

// The arithmetic half of cells_of(), separated from it because two callers
// need the answer without a QPainter to ask about. The cell paint filter has a
// widget and a paint event and no painter at all, and it had its own copy of
// these four lines -- twice, once for ICellPainted and once for a pixel
// surface -- which is the third and fourth copy of a rule this header exists
// to keep to one. They agreed when they were written; nothing was going to
// tell anybody when they stopped.
//
// A widget at least one cell wide and tall whatever its pixels say: a widget
// four pixels across still has to be able to draw something, and the
// alternative to rounding it up is handing it nothing to draw in. That is not
// the half-cell coverage rule fill_rectf() and line() apply, and it is not
// meant to be -- those decide whether a MARK stands for a cell, this decides
// where a WIDGET is.
inline QRect cells_of_rect(const QRect &r, const QWidget *m, const QPoint &origin) {
	const int cw = GridMetrics::cw(), ch = GridMetrics::ch();
	QPoint tl = m ? m->mapTo(m->window(), r.topLeft()) : r.topLeft();
	tl += origin;
	return QRect(qRound(tl.x() / double(cw)), qRound(tl.y() / double(ch)),
	             qMax(1, qRound(r.width() / double(cw))),
	             qMax(1, qRound(r.height() / double(ch))));
}

// A widget-space rectangle in buffer cells. Measured F2: the redirection
// offset QWidget::render() applies is carried by neither transform() nor
// combinedTransform(), so the origin comes from the widget being painted --
// p->device() when there is one, the style's widget otherwise -- plus the
// compositor's origin.
inline QRect cells_of(const QRect &r, QPainter *p, CellPaintDevice *dev,
                      const QWidget *w) {
	const QWidget *paint_widget = dynamic_cast<QWidget *>(p->device());
	return cells_of_rect(r, paint_widget ? paint_widget : w, dev->origin);
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
