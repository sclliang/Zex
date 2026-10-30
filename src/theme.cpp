#include "theme.h"
#include "fileicons.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSettings>
#include <QWidget>

static void initializeUiResources() { Q_INIT_RESOURCE(ui_resources); }

namespace {
QString activeTheme = "graphite";
const QList<Theme::Colors>& presets() {
    static const QList<Theme::Colors> values{
        {"graphite",QStringLiteral("石墨 · 深色"),true,
            "#1f1f1f","#181818","#262626","#d4d4d4","#a0a0a0","#707070","#4daafc","#ffffff",
            "#2b2b2b","#242424","#303030","#264f78","#494949",
            "#6a9955","#c586c0","#ce9178","#b5cea8","#9cdcfe","#569cd6","#f48771"},
        {"midnight",QStringLiteral("午夜蓝 · 深色"),true,
            "#131b28","#0e1520","#1d293b","#cbd8ec","#8fa4c1","#6d829d","#76b5f5","#0b2038",
            "#2b3a50","#101724","#27384f","#2d4b70","#425b79",
            "#8ca0b8","#bdabf3","#9acba5","#e8bd89","#78c9df","#f29ca7","#ef99a3"},
        {"north",QStringLiteral("北境 · 深色"),true,
            "#2e3440","#272d38","#3b4252","#e5e9f0","#abb5c8","#8794aa","#88c0d0","#1d303c",
            "#4c566a","#292e39","#434e60","#465870","#66748b",
            "#9aa8bd","#c2a2c4","#a3be8c","#ebcb8b","#91b4d6","#e2a084","#e09098"},
        {"violet",QStringLiteral("紫夜 · 深色"),true,
            "#241e30","#1c1726","#30283f","#e3dced","#a89bb8","#8c7e9f","#c7a1f0","#29183d",
            "#493c58","#1f192b","#40324f","#503d69","#665177",
            "#ab99b7","#d4adf6","#b1cea3","#f1c78c","#9ac9eb","#f0a7c2","#efa0ad"},
        {"paper",QStringLiteral("纸白 · 浅色"),false,
            "#fafbfc","#edf0f4","#f0f3f8","#273342","#58677b","#6e7e92","#2d66c3","#ffffff",
            "#d8dfe8","#ffffff","#e3eaf4","#c9ddf8","#acb9ca",
            "#667488","#8145a1","#37724b","#985c22","#245e9d","#b34963","#ba3b4e"},
        {"sand",QStringLiteral("暖砂 · 浅色"),false,
            "#fbf6eb","#f0e7d7","#f5eddd","#493c30","#756450","#8b775d","#9a642c","#fffaf0",
            "#dfd1bb","#fffaf1","#eaddc8","#e6d2b2","#baa587",
            "#7b6a54","#86518e","#507245","#9a591d","#3e6b83","#a24c4c","#ad4545"}
    };
    return values;
}
QPalette palette(const Theme::Colors& c) {
    QPalette result;
    result.setColor(QPalette::Window,c.background);result.setColor(QPalette::WindowText,c.text);
    result.setColor(QPalette::Base,c.input);result.setColor(QPalette::AlternateBase,c.elevated);
    result.setColor(QPalette::Text,c.text);result.setColor(QPalette::Button,c.chrome);
    result.setColor(QPalette::ButtonText,c.text);result.setColor(QPalette::Highlight,c.selection);
    result.setColor(QPalette::HighlightedText,c.text);result.setColor(QPalette::PlaceholderText,c.muted);
    result.setColor(QPalette::ToolTipBase,c.elevated);result.setColor(QPalette::ToolTipText,c.text);
    result.setColor(QPalette::Link,c.accent);result.setColor(QPalette::LinkVisited,c.keyword);
    result.setColor(QPalette::Light,c.elevated);result.setColor(QPalette::Mid,c.border);result.setColor(QPalette::Dark,c.border);
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})result.setColor(QPalette::Disabled,role,c.subtle);
    return result;
}
QString styleSheet(const Theme::Colors& c);
}

QStringList Theme::themes() { QStringList result;for(const auto& c:presets())result.append(c.id);return result; }
QString Theme::normalizedTheme(const QString& id) { return themes().contains(id)?id:"graphite"; }
QString Theme::id() { return activeTheme; }
const Theme::Colors& Theme::colors(const QString& id) {
    const auto selected=normalizedTheme(id.isEmpty()?activeTheme:id);
    for(const auto& c:presets())if(c.id==selected)return c;
    return presets().first();
}
QString Theme::storedTheme(QSettings& settings) { return normalizedTheme(settings.value("colorTheme","graphite").toString()); }
void Theme::saveTheme(QSettings& settings,const QString& id) { settings.setValue("colorTheme",normalizedTheme(id)); }
QIcon Theme::swatch(const QString& id) {
    const auto& c=colors(id);QPixmap pix(52,36);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter painter(&pix);painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(c.border);painter.setBrush(c.background);painter.drawRoundedRect(QRectF(0.5,0.5,25,17),3,3);
    painter.setPen(QPen(c.accent,2));painter.drawLine(5,6,20,6);
    painter.setPen(QPen(c.keyword,2));painter.drawLine(5,11,11,11);
    painter.setPen(QPen(c.string,2));painter.drawLine(14,11,20,11);return QIcon(pix);
}

QString Theme::codeFont() {
    const auto families = QFontDatabase::families();
    for (auto f : {"Cascadia Mono", "Consolas", "Cascadia Code"})
        if (families.contains(QLatin1String(f))) return QLatin1String(f);
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QIcon Theme::fileIcon(const QString& path) { return FileIcons::icon(path); }


namespace {
QString styleSheet(const Theme::Colors& c) {
    QString sheet = R"(
      QMainWindow { background: @chrome; }
      QWidget#shell { background: @background; border: none; }
      QDialog { background: @background; color: @text; }
      QDialog#filePicker { background: @elevated; border: 1px solid @border; }
      QListView#filePickerList { background: @elevated; border: none; outline: none; }
      QWidget#searchResultsPanel { background: @background; border-top: 1px solid @border; }
      QListView#searchResultsList { background: @background; color: @text; border: none; outline: none; }
      QListView#searchResultsList::item { padding: 4px 8px; }
      QListView#searchResultsList::item:hover { background: @hover; }
      QListView#searchResultsList::item:selected { background: @selection; color: @text; }
      QSplitter::handle { background: @border; }
      QSplitter::handle:hover { background: @accent; }
      QWidget#paneHeader { background: @background; border-bottom: 1px solid @border; }
      QWidget#paneHeader QLabel { color: @muted; font-size: 12px; }
      QWidget#paneHeader[active="true"] QLabel { color: @text; }
      QWidget#paneHeader[active="true"] { border-bottom: 1px solid @accent; }
      QWidget#titlebar { background: @chrome; }
      QWidget#tabstrip { background: @chrome; border-bottom: 1px solid @border; }
      QTabBar#fileTabs QToolButton { background: @chrome; border-radius: 0; border-left: 1px solid @border; width: 22px; }
      QTabBar#fileTabs QToolButton:hover { background: @hover; }
      QWidget#breadcrumb { background: @background; }
      QWidget#searchbar { background: @elevated; border: 1px solid @border; border-radius: 3px; }
      QWidget#searchbar QToolButton { padding: 2px; border-radius: 2px; }
      QWidget#searchbar QLineEdit { padding: 3px 6px; border-radius: 2px; }
      QWidget#statusbar { background: @chrome; border-top: 1px solid @border; }
      QLabel { background: transparent; color: @text; border: none; }
      QLabel#muted { color: @muted; }
      QLabel#path { color: @muted; font-size: 12px; }
      QLabel#status { color: @muted; font-size: 11px; padding: 0 5px; }
      QLabel#sessionNotice { color: @error; font-size: 11px; padding: 0 5px; }
      QLabel#brand { color: @text; font-size: 42px; font-weight: 400; }
      QLabel#subtitle { color: @muted; font-size: 14px; }
      QLabel#welcomeTab { background: @background; color: @text; border-top: 1px solid @accent; border-right: 1px solid @border; padding-left: 16px; }
      QLabel#sectionHeading { color: @text; font-size: 13px; font-weight: 600; }
      QLabel#keycap { color: @muted; font-size: 11px; border: 1px solid @border; border-radius: 3px; padding: 1px 5px; }
      QLabel#dialogHeading { color: @text; font-size: 20px; font-weight: 600; }
      QLabel#shortcutKey { color: @muted; font-family: 'Consolas'; font-size: 12px; }
      QToolButton { background: transparent; border: none; border-radius: 3px; padding: 3px; color: @muted; }
      QToolButton:hover { background: @hover; color: @text; }
      QToolButton:pressed { background: @selection; }
      QToolButton:checked { background: @selection; color: @text; }
      QToolButton:disabled { color: @subtle; }
      QToolButton#windowButton { border-radius: 0; }
      QToolButton#windowClose:hover { background: @error; }
      QToolButton::menu-indicator { image: none; width: 0; }
      QLineEdit { background: @input; color: @text; border: 1px solid @border; border-radius: 3px; padding: 5px 8px; selection-background-color: @selection; selection-color: @text; }
      QLineEdit:focus { border-color: @accent; }
      QLineEdit[invalid="true"] { border-color: @error; }
      QComboBox, QSpinBox { background: @input; color: @text; border: 1px solid @border; border-radius: 3px; padding: 4px 8px; min-height: 20px; }
      QComboBox:focus, QSpinBox:focus { border-color: @accent; }
      QComboBox::drop-down { border: none; width: 24px; }
      QComboBox::down-arrow { image: url(:/ui/chevron-down@arrow.svg); width: 12px; height: 12px; }
      QComboBox QAbstractItemView { background: @elevated; color: @text; selection-background-color: @selection; selection-color: @text; border: 1px solid @border; }
      QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 20px; border: none; background: @elevated; }
      QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 4px; }
      QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 4px; }
      QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }
      QSpinBox::up-arrow { image: url(:/ui/chevron-up@arrow.svg); width: 10px; height: 10px; }
      QSpinBox::down-arrow { image: url(:/ui/chevron-down@arrow.svg); width: 10px; height: 10px; }
      QCheckBox { color: @text; spacing: 7px; }
      QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid @subtle; border-radius: 3px; background: @input; }
      QCheckBox::indicator:hover { border-color: @accent; }
      QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(:/ui/check-@check.svg); }
      QPushButton { color: @text; background: @elevated; border: 1px solid @border; border-radius: 3px; padding: 6px 14px; }
      QPushButton:hover { background: @hover; border-color: @accent; }
      QPushButton:default { border-color: @accent; }
      QPushButton#openPrimary { background: @accent; border: 1px solid @accent; color: @accentText; font-weight: 600; }
      QPushButton#openPrimary:hover { background: @accentHover; }
      QPushButton#quickOpenTitle { background: @input; border: 1px solid @border; border-radius: 5px; padding: 0; }
      QPushButton#quickOpenTitle:hover { background: @hover; border-color: @subtle; }
      QPushButton#quickOpenTitle QLabel { color: @muted; font-size: 12px; }
      QToolButton#welcomeAction { text-align: left; color: @accent; padding: 7px 8px; }
      QToolButton#recentWelcome { text-align: left; padding: 7px 8px; }
      QWidget#loadBar { background: @elevated; border-bottom: 1px solid @border; }
      QWidget#followBar { background: @elevated; border-bottom: 1px solid @border; }
      QLabel#followStatus { color: @muted; }
      QProgressBar { background: @border; border: none; border-radius: 2px; }
      QProgressBar::chunk { background: @accent; border-radius: 2px; }
      QMenu { background: @elevated; color: @text; border: 1px solid @border; border-radius: 4px; padding: 4px; }
      QMenu::item { padding: 5px 30px 5px 10px; border-radius: 3px; }
      QMenu::item:selected { background: @selection; }
      QMenu::item:disabled { color: @subtle; }
      QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
      QToolTip { color: @text; background: @elevated; border: 1px solid @border; padding: 5px; }
      QScrollBar:vertical { background: @background; width: 10px; margin: 0; }
      QScrollBar::handle:vertical { background: @scrollbar; min-height: 28px; border: 2px solid @background; border-radius: 3px; }
      QScrollBar::handle:vertical:hover { background: @muted; }
      QScrollBar:horizontal { background: @background; height: 10px; margin: 0; }
      QScrollBar::handle:horizontal { background: @scrollbar; min-width: 28px; border: 2px solid @background; border-radius: 3px; }
      QScrollBar::handle:horizontal:hover { background: @muted; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
      QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )";
    // Replace longer tokens first so @accent does not consume @accentText.
    const QList<QPair<QString,QColor>> tokens{
        {"accentText",c.accentText},{"accentHover",c.dark?c.accent.lighter(110):c.accent.darker(108)},
        {"background",c.background},{"selection",c.selection},{"scrollbar",c.scrollbar},
        {"elevated",c.elevated},{"chrome",c.chrome},{"border",c.border},{"input",c.input},
        {"hover",c.hover},{"subtle",c.subtle},{"muted",c.muted},{"accent",c.accent},{"error",c.error},{"text",c.text}
    };
    for(const auto& token:tokens)sheet.replace('@'+token.first,token.second.name());
    sheet.replace("@arrow",c.dark?"":"-light");
    sheet.replace("@check",c.dark?"dark":"light");
    return sheet;
}
}

void Theme::apply(QApplication& app,const QString& id) {
    static const bool initialized=[&app] {
        initializeUiResources();app.setStyle(QStyleFactory::create("Fusion"));
        QFont font; font.setFamilies({"Segoe UI","Microsoft YaHei UI","Microsoft YaHei"});font.setPixelSize(13);app.setFont(font);
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han,"Microsoft YaHei UI");return true;
    }();
    Q_UNUSED(initialized);
    activeTheme=normalizedTheme(id);const auto& c=colors();
    // Keep Qt's native menus and window frames in sync with our explicit palette.
    app.styleHints()->setColorScheme(c.dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
    app.setPalette(palette(c));app.setStyleSheet(styleSheet(c));
}
void Theme::preview(QWidget& widget,const QString& id) {
    const auto& c=colors(id);widget.setPalette(palette(c));widget.setStyleSheet(styleSheet(c));
}
