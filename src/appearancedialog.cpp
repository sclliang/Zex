#include "appearancedialog.h"
#include "editor.h"
#include "fileicons.h"
#include "theme.h"
#include "windowplacement.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <windows.h>
#include <dwmapi.h>

AppearanceDialog::AppearanceDialog(const EditorAppearance& appearance, const QString& iconTheme, QWidget* parent)
    : QDialog(parent) {
    setObjectName("appearanceDialog");
    setWindowTitle(QStringLiteral("外观设置 · Zex"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumSize(610,320); resize(680,670);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(24,20,24,20);outer->setSpacing(14);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);outer->addWidget(scroll,1);
    auto* content=new QWidget;scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(0,0,8,0); layout->setSpacing(14);
    auto* heading = new QLabel(QStringLiteral("编辑器外观"));
    heading->setObjectName("dialogHeading"); layout->addWidget(heading);
    auto* hint = new QLabel(QStringLiteral("在下方预览效果，应用后对所有文件生效。")); hint->setObjectName("muted"); layout->addWidget(hint);
    auto* form = new QFormLayout; form->setHorizontalSpacing(24); form->setVerticalSpacing(10);
    themes_=new QComboBox;themes_->setObjectName("colorTheme");themes_->setIconSize(QSize(26,18));
    for(const auto& id:Theme::themes())themes_->addItem(Theme::swatch(id),Theme::colors(id).name,id);
    form->addRow(QStringLiteral("界面主题"),themes_);
    family_ = new QFontComboBox; family_->setObjectName("fontFamily"); family_->setEditable(false);
    family_->setMinimumWidth(250); form->addRow(QStringLiteral("编辑器字体"), family_);
    auto* fontRow = new QHBoxLayout;
    size_ = new QSpinBox; size_->setObjectName("fontSize"); size_->setRange(8,32); size_->setSuffix(" pt"); size_->setFixedWidth(100);
    style_ = new QComboBox; style_->setObjectName("fontStyle");
    style_->addItems({QStringLiteral("常规"),QStringLiteral("粗体"),QStringLiteral("斜体"),QStringLiteral("粗斜体")});
    fontRow->addWidget(size_); fontRow->addSpacing(10); fontRow->addWidget(style_,1);
    form->addRow(QStringLiteral("字号 / 字形"), fontRow);
    padding_ = new QSpinBox; padding_->setObjectName("linePadding"); padding_->setRange(0,16); padding_->setSuffix(QStringLiteral(" px"));
    padding_->setToolTip(QStringLiteral("每行增加的上下留白总量")); padding_->setFixedWidth(100);
    comments_ = new QCheckBox(QStringLiteral("注释使用斜体")); comments_->setObjectName("commentItalics");
    auto* spacingRow = new QHBoxLayout; spacingRow->addWidget(padding_); spacingRow->addSpacing(10); spacingRow->addWidget(comments_); spacingRow->addStretch();
    form->addRow(QStringLiteral("行间留白"), spacingRow);
    icons_ = new QComboBox; icons_->setObjectName("iconTheme");
    for (const auto& id : FileIcons::themes()) icons_->addItem(FileIcons::icon("example.cpp",id),FileIcons::themeName(id),id);
    form->addRow(QStringLiteral("文件图标"), icons_); layout->addLayout(form);
    auto* samples = new QHBoxLayout; samples->setSpacing(15);
    for (const auto& name : {"main.cpp","app.py","index.ts","data.json","README.md","CMakeLists.txt"}) {
        auto* column = new QVBoxLayout; column->setSpacing(4);
        auto* icon = new QLabel; icon->setAlignment(Qt::AlignCenter); icon->setProperty("sampleFile",name); column->addWidget(icon);
        auto* text = new QLabel(name); text->setAlignment(Qt::AlignCenter); text->setObjectName("muted"); text->setStyleSheet("font-size:11px;"); column->addWidget(text);
        iconSamples_.append(icon); samples->addLayout(column);
    }
    layout->addLayout(samples);
    auto* previewTitle = new QLabel(QStringLiteral("实时预览")); previewTitle->setObjectName("muted"); layout->addWidget(previewTitle);
    preview_ = new CodeEditor; preview_->setObjectName("fontPreview"); preview_->setMinimumHeight(180);
    preview_->setContent(decodeFile(QStringLiteral("// 外观预览 · Read with clarity\n#include <string_view>\n\nint main() {\n    constexpr std::string_view name = \"Hello, Zex\";\n    const int answer = 42;\n    return answer;\n}\n").toUtf8()), "preview.cpp");
    layout->addWidget(preview_,1);
    auto* buttons = new QDialogButtonBox;
    auto* reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    auto* cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    auto* apply = buttons->addButton(QStringLiteral("应用"), QDialogButtonBox::ApplyRole);
    auto* ok = buttons->addButton(QStringLiteral("确定"), QDialogButtonBox::AcceptRole); ok->setDefault(true);
    cancel->setObjectName("appearanceCancel"); apply->setObjectName("appearanceApply"); ok->setObjectName("appearanceOk");
    outer->addWidget(buttons);
    setValues(appearance,iconTheme,Theme::id());
    connect(themes_,&QComboBox::currentIndexChanged,this,&AppearanceDialog::updatePreview);
    connect(family_,&QFontComboBox::currentFontChanged,this,&AppearanceDialog::updatePreview);
    connect(size_,&QSpinBox::valueChanged,this,&AppearanceDialog::updatePreview);
    connect(style_,&QComboBox::currentIndexChanged,this,&AppearanceDialog::updatePreview);
    connect(padding_,&QSpinBox::valueChanged,this,&AppearanceDialog::updatePreview);
    connect(comments_,&QCheckBox::toggled,this,&AppearanceDialog::updatePreview);
    connect(icons_,&QComboBox::currentIndexChanged,this,&AppearanceDialog::updatePreview);
    connect(reset,&QPushButton::clicked,this,[this]{setValues(EditorAppearance::defaults(),"material","graphite");});
    connect(apply,&QPushButton::clicked,this,&AppearanceDialog::applyRequested);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{emit applyRequested();accept();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    const DWORD border = 0xFFFFFFFE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()),34,&border,sizeof(border));
    WindowPlacement::keepVisible(this);
}

EditorAppearance AppearanceDialog::editorAppearance() const {
    EditorAppearance result;
    result.family = family_->currentFont().family(); result.points = size_->value();
    result.bold = style_->currentIndex() & 1; result.italic = style_->currentIndex() & 2;
    result.linePadding = padding_->value(); result.commentItalics = comments_->isChecked();
    return result.sanitized();
}
QString AppearanceDialog::iconTheme() const { return icons_->currentData().toString(); }
QString AppearanceDialog::colorTheme() const { return themes_->currentData().toString(); }
void AppearanceDialog::setValues(const EditorAppearance& appearance, const QString& iconTheme, const QString& colorTheme) {
    const QSignalBlocker a(family_), b(size_), c(style_), d(padding_), e(comments_), f(icons_), g(themes_);
    family_->setCurrentFont(QFont(appearance.family)); size_->setValue(appearance.points);
    style_->setCurrentIndex((appearance.bold ? 1 : 0) | (appearance.italic ? 2 : 0));
    padding_->setValue(appearance.linePadding); comments_->setChecked(appearance.commentItalics);
    icons_->setCurrentIndex(icons_->findData(FileIcons::normalizedTheme(iconTheme)));
    themes_->setCurrentIndex(themes_->findData(Theme::normalizedTheme(colorTheme))); updatePreview();
}
void AppearanceDialog::updatePreview() {
    Theme::preview(*this,colorTheme());
    const BOOL dark=Theme::colors(colorTheme()).dark;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()),20,&dark,sizeof(dark));
    preview_->setAppearance(editorAppearance());
    preview_->setColorTheme(colorTheme());
    for(int i=0;i<icons_->count();++i)icons_->setItemIcon(i,FileIcons::icon("example.cpp",icons_->itemData(i).toString(),colorTheme()));
    for (auto* sample : iconSamples_) sample->setPixmap(FileIcons::icon(sample->property("sampleFile").toString(),iconTheme(),colorTheme()).pixmap(QSize(24,24),devicePixelRatioF()));
}
