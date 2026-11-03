# src/widget/

`CellItemDelegate` (`cell_item_delegate.cpp`), the item-view delegate that
gives Channel A coverage for the common roles. Its header is
`include/qtty/delegate.h`.

## There is no replacement layer here, and the plan for one is closed

This directory was reserved for cell-native substitutes for widgets whose
painting could not be reinterpreted -- a `QTextEdit` interaction layer and a
`QCalendarWidget` -- and **this file described that layer long after it was
measured unnecessary**, which is why the note is here rather than only in
the history.

- **`QTextEdit` and `QPlainTextEdit` needed no code.** Display is free
  wherever the document font's line height equals the cell height
  (design.md §16.1 F8). What was missing was keys reaching the widget, which
  is routing and was fixed as such; both take typing, Return and wide
  clusters now, and what is typed reaches the cells. Mouse selection needed
  no text-widget code either, only motion events that carry the held button.
  Partial-line scrolling moves in whole cells. Measured 2026-09-03.
- **`QCalendarWidget` renders through Qt's own.** Its month arrows are named
  from the widget rather than read from a picture, because Qt draws them as
  pixmaps carrying no text, no tool tip and no action -- they came out as two
  identical `[]` before that. Covered in `test/suite_widgets.cpp`.

So the directory holds a delegate, not substitutes. design.md §8.4 keeps the
**Replaced** tier with nothing in it: whether anything should ever be
replaced is a design question, and this records only that nothing has been.
project.md 8.668 has the measurements and how the stale claim was found.
