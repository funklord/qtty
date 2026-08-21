# Replaced widgets (§8.4, Phase 2+)

Cell-native substitutes for widgets whose painting cannot be reinterpreted:
QTextEdit *interaction layer* (display already works — §16.1 F8),
QCalendarWidget. Each implements ICellPainted and mirrors the API subset the
four products actually use.
