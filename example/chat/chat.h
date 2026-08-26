// chat.h -- the application. 100% vanilla Qt Widgets, ZERO qtty types.
//
// This is the point of the example: everything in this file compiles and
// behaves identically in the GUI build and the TUI build. Stickers are drawn
// with plain QPainter::drawPixmap in a QStyledItemDelegate; in the TUI build
// the qtty paint engine turns them into cell-anchored placements (section 5.7)
// without this code knowing.
//
// The only TUI-mindful choices here are ordinary good practice plus three
// grid disciplines, each marked with [grid]:
//   1. sizes derived from font metrics, not hardcoded pixels
//   2. per-pixel scrolling with a line-height single step
//   3. sticker assets sized in cell multiples (done at asset creation)

#pragma once
#include <QtWidgets>

struct Msg { QString author, text; QPixmap sticker; };

// --- sticker assets ---------------------------------------------------------
// Real apps load PNGs; the import step is where [grid] discipline 3 lives:
// normalise sticker dimensions to multiples of the cell size. Here we just
// draw them at the right size directly. cw/ch come from the app font.
inline QPixmap makeSticker(int cw, int ch, int wCells, int hCells, int kind) {
	QPixmap pm(wCells * cw, hCells * ch);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = pm.rect().adjusted(4, 4, -4, -4);
	if (kind == 0) {                                    // smiley
		p.setBrush(QColor(255, 205, 60)); p.setPen(QPen(QColor(120, 90, 0), 3));
		p.drawEllipse(r);
		p.setBrush(QColor(60, 40, 0)); p.setPen(Qt::NoPen);
		p.drawEllipse(QPointF(r.center().x()-r.width()*0.18, r.center().y()-r.height()*0.15), 5, 7);
		p.drawEllipse(QPointF(r.center().x()+r.width()*0.18, r.center().y()-r.height()*0.15), 5, 7);
		p.setPen(QPen(QColor(60, 40, 0), 4)); p.setBrush(Qt::NoBrush);
		p.drawArc(r.adjusted(r.width()*0.22, r.height()*0.25, -r.width()*0.22, -r.height()*0.12),
		          200*16, 140*16);
	} else {                                            // heart
		p.setPen(QPen(QColor(140, 0, 30), 3)); p.setBrush(QColor(230, 40, 70));
		QPainterPath path;
		qreal w = r.width(), h = r.height();
		path.moveTo(r.center().x(), r.bottom());
		path.cubicTo(r.left()-w*0.15, r.top()+h*0.45, r.left()+w*0.22, r.top()-h*0.25,
		             r.center().x(), r.top()+h*0.28);
		path.cubicTo(r.right()-w*0.22, r.top()-h*0.25, r.right()+w*0.15, r.top()+h*0.45,
		             r.center().x(), r.bottom());
		p.drawPath(path);
	}
	p.end();
	return pm;
}

// --- model ------------------------------------------------------------------
class ChatModel : public QAbstractListModel {
	Q_OBJECT
public:
	QVector<Msg> msgs;
	int rowCount(const QModelIndex & = {}) const override { return msgs.size(); }
	QVariant data(const QModelIndex &, int) const override { return {}; }
	void append(const Msg &m) {
		beginInsertRows({}, msgs.size(), msgs.size());
		msgs.push_back(m);
		endInsertRows();
	}
};

// --- delegate ---------------------------------------------------------------
class ChatDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	ChatModel *m;
	int cw, ch;                                  // [grid] font-derived, not constants
	ChatDelegate(ChatModel *mm, const QFont &f, QObject *parent = nullptr)
	    : QStyledItemDelegate(parent), m(mm) {
		QFontMetrics fm(f);
		cw = fm.horizontalAdvance(u'M');
		ch = fm.height();
	}
	QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &ix) const override {
		const Msg &msg = m->msgs[ix.row()];
		int rows = msg.sticker.isNull() ? 3
		         : 2 + msg.sticker.height() / ch;        // sticker is cell-multiple
		return QSize(o.rect.width(), rows * ch);         // [grid] whole lines
	}
	void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &ix) const override {
		const Msg &msg = m->msgs[ix.row()];
		QFont bold = o.font; bold.setBold(true);
		p->setFont(bold);
		p->drawText(o.rect.x() + cw, o.rect.y() + QFontMetrics(bold).ascent(),
		            msg.author + ":");
		p->setFont(o.font);
		if (msg.sticker.isNull())
			p->drawText(o.rect.x() + 3*cw, o.rect.y() + ch + QFontMetrics(o.font).ascent(),
			            msg.text);
		else
			p->drawPixmap(o.rect.x() + 3*cw, o.rect.y() + ch, msg.sticker);
	}
};

// --- window -----------------------------------------------------------------
class ChatWindow : public QWidget {
	Q_OBJECT
public:
	ChatModel model;
	QListView *view;
	QLineEdit *input;
	QPixmap smiley, heart;

	ChatWindow() {
		const QFont f = font();
		QFontMetrics fm(f);
		const int cw = fm.horizontalAdvance(u'M'), ch = fm.height();

		smiley = makeSticker(cw, ch, 10, 4, 0);
		heart  = makeSticker(cw, ch, 10, 4, 1);

		auto *lay = new QVBoxLayout(this);
		lay->setContentsMargins(0, 0, 0, 0);             // window edge = cell edge
		lay->setSpacing(0);

		view = new QListView(this);
		view->setModel(&model);
		view->setItemDelegate(new ChatDelegate(&model, f, this));
		view->setFrameShape(QFrame::NoFrame);            // [grid] frame would offset rows
		view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		view->verticalScrollBar()->setSingleStep(ch);    // [grid] discipline 2
		lay->addWidget(view, 1);

		input = new QLineEdit(this);
		input->setPlaceholderText("type a message — :) and <3 become stickers");
		lay->addWidget(input);
		connect(input, &QLineEdit::returnPressed, this, &ChatWindow::send);

		for (const Msg &m : QVector<Msg>{
			{"dave", "hey, did the release build pass?", {}},
			{"mika", "yes! all four products green ☺", {}},
			{"dave", "", smiley},
			{"mika", "shipping the TUI beta tonight ★", {}},
			{"dave", "one codebase, GUI and terminal", {}},
			{"mika", "", heart},
		}) model.append(m);

		setWindowTitle("qtty chat");
		resize(52 * cw, 18 * ch);
		input->setFocus();
	}

public slots:
	void send() {
		QString t = input->text().trimmed();
		if (t.isEmpty()) return;
		if      (t == ":)") model.append({"you", "", smiley});
		else if (t == "<3") model.append({"you", "", heart});
		else                model.append({"you", t, {}});
		input->clear();
		view->scrollToBottom();
	}
};
