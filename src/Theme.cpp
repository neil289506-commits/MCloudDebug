#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

// 色票：深色底 + 翠綠強調色
const char *kBg = "#0e1014";
const char *kSurface = "#161a21";
const char *kSurface2 = "#1d222b";
const char *kBorder = "#2a303c";
const char *kText = "#e7eaf0";
const char *kMuted = "#8c95a6";
const char *kAccent = "#34d399";
const char *kAccentHover = "#4ade9f";
const char *kDanger = "#f87171";

QString styleSheet()
{
    QString s = QStringLiteral(R"(
* { font-family: "Segoe UI Variable Text", "Segoe UI", "Microsoft JhengHei UI", sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: %BG%; }
QWidget { color: %TEXT%; }
QToolTip { background: %S2%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px 6px; }

QFrame#card { background: %SURFACE%; border: 1px solid %BORDER%; border-radius: 12px; }
QLabel#cardTitle { color: %MUTED%; font-size: 11px; font-weight: 600; letter-spacing: 1px; }
QLabel#muted { color: %MUTED%; }
QLabel#chip { background: %S2%; border: 1px solid %BORDER%; border-radius: 9px; padding: 2px 10px; color: %MUTED%; }
QLabel#chipOk { background: rgba(52,211,153,0.12); border: 1px solid rgba(52,211,153,0.45); border-radius: 9px; padding: 2px 10px; color: %ACCENT%; }
QLabel#chipWarn { background: rgba(248,113,113,0.12); border: 1px solid rgba(248,113,113,0.45); border-radius: 9px; padding: 2px 10px; color: %DANGER%; }
QLabel#appTitle { font-size: 20px; font-weight: 700; }

QFrame#banner { background: rgba(251,191,36,0.10); border: 1px solid rgba(251,191,36,0.5); border-radius: 10px; }

QPushButton { background: %S2%; border: 1px solid %BORDER%; border-radius: 8px; padding: 7px 14px; }
QPushButton:hover { border-color: %ACCENT%; }
QPushButton:pressed { background: %SURFACE%; }
QPushButton:disabled { color: #566070; border-color: #232832; background: #14171d; }
QPushButton[primary="true"] { background: %ACCENT%; color: #04281b; border: none; font-weight: 600; padding: 9px 16px; }
QPushButton[primary="true"]:hover { background: %ACCENT_H%; }
QPushButton[primary="true"]:disabled { background: #1f3a31; color: #5c8a79; }
QPushButton[flat="true"] { background: transparent; border: none; color: %MUTED%; padding: 4px 8px; }
QPushButton[flat="true"]:hover { color: %TEXT%; }

QLineEdit, QSpinBox, QComboBox { min-height: 22px; background: %BG%; border: 1px solid %BORDER%; border-radius: 8px; padding: 6px 10px; selection-background-color: %ACCENT%; selection-color: #04281b; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 24px; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }
QComboBox QAbstractItemView { background: %S2%; border: 1px solid %BORDER%; selection-background-color: rgba(52,211,153,0.25); outline: 0; padding: 4px; }

QPlainTextEdit, QTextBrowser { background: %BG%; border: 1px solid %BORDER%; border-radius: 10px; padding: 8px; selection-background-color: %ACCENT%; selection-color: #04281b; }
QPlainTextEdit { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 12px; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %BORDER%; background: %BG%; }
QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; image: url(:/check.png); }

QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 8px 16px; margin-right: 4px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %TEXT%; border-bottom: 2px solid %ACCENT%; }
QTabBar::tab:hover { color: %TEXT%; }

QProgressBar { background: %BG%; border: 1px solid %BORDER%; border-radius: 7px; height: 14px; text-align: center; color: %TEXT%; font-size: 11px; }
QProgressBar::chunk { background: %ACCENT%; border-radius: 6px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2f3644; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #3b4456; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2f3644; border-radius: 4px; min-width: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QStatusBar { background: %SURFACE%; color: %MUTED%; border-top: 1px solid %BORDER%; }
QStatusBar::item { border: none; }
QMessageBox { background: %SURFACE%; }
)");
    s.replace(QStringLiteral("%BG%"), QLatin1String(kBg));
    s.replace(QStringLiteral("%SURFACE%"), QLatin1String(kSurface));
    s.replace(QStringLiteral("%S2%"), QLatin1String(kSurface2));
    s.replace(QStringLiteral("%BORDER%"), QLatin1String(kBorder));
    s.replace(QStringLiteral("%TEXT%"), QLatin1String(kText));
    s.replace(QStringLiteral("%MUTED%"), QLatin1String(kMuted));
    s.replace(QStringLiteral("%ACCENT_H%"), QLatin1String(kAccentHover));
    s.replace(QStringLiteral("%ACCENT%"), QLatin1String(kAccent));
    s.replace(QStringLiteral("%DANGER%"), QLatin1String(kDanger));
    return s;
}

} // namespace

void Theme::apply(QApplication &app)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark); // 讓 Windows 標題列也變深色
#endif
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, QColor(QLatin1String(kBg)));
    p.setColor(QPalette::WindowText, QColor(QLatin1String(kText)));
    p.setColor(QPalette::Base, QColor(QLatin1String(kBg)));
    p.setColor(QPalette::AlternateBase, QColor(QLatin1String(kSurface)));
    p.setColor(QPalette::Text, QColor(QLatin1String(kText)));
    p.setColor(QPalette::Button, QColor(QLatin1String(kSurface2)));
    p.setColor(QPalette::ButtonText, QColor(QLatin1String(kText)));
    p.setColor(QPalette::Highlight, QColor(QLatin1String(kAccent)));
    p.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#04281b")));
    p.setColor(QPalette::PlaceholderText, QColor(QLatin1String(kMuted)));
    p.setColor(QPalette::Link, QColor(QLatin1String(kAccent)));
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}
