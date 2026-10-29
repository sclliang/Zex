#include "integrationdialog.h"
#include "theme.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

IntegrationDialog::IntegrationDialog(const ShellIntegration& integration,QWidget* parent)
    : QDialog(parent),integration_(integration) {
    setObjectName("integrationDialog");setWindowTitle(QStringLiteral("Windows 集成 · Zex"));
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);resize(660,650);setMinimumSize(570,500);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(24,20,24,20);layout->setSpacing(14);
    auto* heading=new QLabel(QStringLiteral("从 Windows 打开文件"));heading->setObjectName("dialogHeading");layout->addWidget(heading);
    auto* hint=new QLabel(QStringLiteral("再次打开的文件会进入已有 Zex 窗口。以下设置只对当前用户生效。"));
    hint->setObjectName("muted");hint->setWordWrap(true);layout->addWidget(hint);
    contextMenu_=new QCheckBox(QStringLiteral("在文件右键菜单中添加“使用 Zex 打开”"));contextMenu_->setObjectName("contextMenuEnabled");layout->addWidget(contextMenu_);
    auto* windowsHint=new QLabel(QStringLiteral("Windows 11 中可在“显示更多选项”中找到此菜单。"));windowsHint->setObjectName("muted");layout->addWidget(windowsHint);
    auto* caption=new QLabel(QStringLiteral("可用 Zex 打开的文件类型"));layout->addWidget(caption);
    auto* row=new QHBoxLayout;
    auto* common=new QPushButton(QStringLiteral("选择常用"));auto* all=new QPushButton(QStringLiteral("全选"));auto* none=new QPushButton(QStringLiteral("清空"));
    row->addWidget(common);row->addWidget(all);row->addWidget(none);row->addStretch();layout->addLayout(row);
    auto* area=new QScrollArea;area->setWidgetResizable(true);area->setFrameShape(QFrame::NoFrame);
    auto* container=new QWidget;auto* grid=new QGridLayout(container);grid->setContentsMargins(4,8,4,8);grid->setSpacing(12);
    int index=0;for(const auto& ext:ShellIntegration::supportedExtensions()) {
        auto* box=new QCheckBox(ext);box->setObjectName("extension"+ext);box->setProperty("extension",ext);
        grid->addWidget(box,index/5,index%5);types_.append(box);++index;
    }
    area->setWidget(container);layout->addWidget(area,1);
    connect(all,&QPushButton::clicked,this,[this]{for(auto* box:types_)box->setChecked(true);});
    connect(none,&QPushButton::clicked,this,[this]{for(auto* box:types_)box->setChecked(false);});
    connect(common,&QPushButton::clicked,this,[this]{const QStringList common{".txt",".log",".md",".json",".xml",".yaml",".yml",".toml",".ini",".cpp",".h",".py",".js",".ts",".css",".html"};for(auto* box:types_)box->setChecked(common.contains(box->property("extension").toString()));});
    auto* note=new QLabel(QStringLiteral("保存后，这些类型会将 Zex 列为打开方式。若要双击默认用 Zex 打开，请到 Windows 默认应用中选择。"));note->setObjectName("muted");note->setWordWrap(true);layout->addWidget(note);
    status_=new QLabel;status_->setObjectName("integrationStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    auto* defaults=new QPushButton(QStringLiteral("保存并设置默认打开方式…"));defaults->setObjectName("integrationDefaults");layout->addWidget(defaults);
    connect(defaults,&QPushButton::clicked,this,[this]{
        bool selected=false;for(auto* box:types_)selected|=box->isChecked();
        if(!selected){status_->setText(QStringLiteral("请先选择至少一种文件类型。"));return;}
        if(apply() && !QDesktopServices::openUrl(QUrl(ShellIntegration::defaultAppsUrl())))status_->setText(QStringLiteral("设置已保存。请手动打开 Windows 设置 → 应用 → 默认应用，搜索 Zex。"));
    });
    auto* buttons=new QDialogButtonBox;
    auto* remove=buttons->addButton(QStringLiteral("撤销全部集成"),QDialogButtonBox::ResetRole);remove->setObjectName("integrationRemove");
    auto* close=buttons->addButton(QStringLiteral("关闭"),QDialogButtonBox::RejectRole);
    auto* save=buttons->addButton(QStringLiteral("保存"),QDialogButtonBox::ApplyRole);save->setDefault(true);save->setObjectName("integrationSave");
    layout->addWidget(buttons);
    connect(save,&QPushButton::clicked,this,[this]{apply();});connect(close,&QPushButton::clicked,this,&QDialog::reject);
    connect(remove,&QPushButton::clicked,this,[this]{QString error;if(integration_.remove(&error)){load();status_->setText(QStringLiteral("已移除 Zex 的右键菜单和打开方式注册。若曾设为默认，可在 Windows 默认应用中选择其他程序。"));}else status_->setText(error);});
    load();
}
void IntegrationDialog::load() {
    const auto state=integration_.state();contextMenu_->setChecked(state.contextMenu);
    for(auto* box:types_)box->setChecked(state.extensions.contains(box->property("extension").toString()));
    status_->setText(state.executable.isEmpty()?QStringLiteral("尚未启用。移动发布包后，可在这里重新保存以更新路径。"):QStringLiteral("当前注册：%1").arg(state.executable));
}
bool IntegrationDialog::apply() {
    QStringList extensions;for(auto* box:types_)if(box->isChecked())extensions.append(box->property("extension").toString());
    QString error;
    if(!integration_.apply(contextMenu_->isChecked(),extensions,&error)){status_->setText(error);return false;}
    status_->setText(QStringLiteral("已保存。右键菜单：%1；已注册 %2 种文件类型。").arg(contextMenu_->isChecked()?QStringLiteral("开启"):QStringLiteral("关闭")).arg(extensions.size()));return true;
}
