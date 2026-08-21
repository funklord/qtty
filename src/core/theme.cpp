#include "qtty/theme.h"

namespace qtty {

CellTheme CellTheme::terminalDefault() { return CellTheme{}; }

CellTheme CellTheme::fromPalette(const QPalette &p) {
    CellTheme t;
    t.windowText       = Color::rgb(p.color(QPalette::WindowText));
    t.text             = Color::rgb(p.color(QPalette::Text));
    t.buttonText       = Color::rgb(p.color(QPalette::ButtonText));
    t.window           = Color::rgb(p.color(QPalette::Window));
    t.base             = Color::rgb(p.color(QPalette::Base));
    t.button           = Color::rgb(p.color(QPalette::Button));
    t.highlight        = Color::rgb(p.color(QPalette::Highlight));
    t.highlightedText  = Color::rgb(p.color(QPalette::HighlightedText));
    return t;
}

Color CellTheme::foreground(QPalette::ColorRole r) const {
    switch (r) {
    case QPalette::Text:            return text;
    case QPalette::ButtonText:      return buttonText;
    case QPalette::HighlightedText: return highlightedText;
    default:                        return windowText;
    }
}

Color CellTheme::background(QPalette::ColorRole r) const {
    switch (r) {
    case QPalette::Base:      return base;
    case QPalette::Button:    return button;
    case QPalette::Highlight: return highlight;
    default:                  return window;
    }
}

static CellTheme s_theme = CellTheme::terminalDefault();
const CellTheme &theme() { return s_theme; }
void setTheme(const CellTheme &t) { s_theme = t; }

} // namespace qtty
