#include "mainwindow.h"
#include "theme.h"
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

QWidget* MainWindow::buildWelcome() {
    auto label=[](const QString& text,const char* role){auto* item=new QLabel(text);item->setObjectName(role);item->setTextFormat(Qt::PlainText);return item;};
    auto* page=new QWidget;auto* outer=new QVBoxLayout(page);outer->setContentsMargins(36,30,36,24);outer->setSpacing(0);outer->addStretch(2);
    auto* panel=new QWidget;panel->setMaximumWidth(760);panel->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);auto* body=new QVBoxLayout(panel);body->setContentsMargins(0,0,0,0);body->setSpacing(0);
    body->addWidget(label("Zex","brand"));body->addSpacing(6);body->addWidget(label(QStringLiteral("文件与代码阅读器"),"subtitle"));body->addSpacing(42);
    auto* columns=new QHBoxLayout;columns->setSpacing(64);
    auto* start=new QVBoxLayout;start->setSpacing(5);start->addWidget(label(QStringLiteral("开始"),"sectionHeading"));start->addSpacing(8);
    auto action=[&](const QString& text,const QString& icon,const QString& key,auto callback){
        auto* row=new QHBoxLayout;row->setSpacing(8);auto* button=new QToolButton;button->setObjectName("welcomeAction");button->setProperty("glyph",icon);button->setProperty("glyphRole","accent");button->setIcon(Theme::icon(icon,Theme::colors().accent));button->setIconSize({16,16});button->setText(text);button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);button->setFixedHeight(34);button->setCursor(Qt::PointingHandCursor);row->addWidget(button,1);
        row->addWidget(label(key,"keycap"),0,Qt::AlignVCenter);start->addLayout(row);connect(button,&QToolButton::clicked,this,callback);
    };
    action(QStringLiteral("打开文件…"),"folder","Ctrl O",[this]{openFiles();});
    action(QStringLiteral("快速切换…"),"search","Ctrl P",[this]{showFilePicker();});
    action(QStringLiteral("外观设置…"),"settings","Ctrl ,",[this]{showAppearanceSettings();});
    start->addSpacing(20);auto* hint=label(QStringLiteral("也可以将文件拖放到窗口中"),"muted");hint->setStyleSheet("font-size:12px;");start->addWidget(hint);start->addStretch();columns->addLayout(start,1);
    auto* recent=new QVBoxLayout;recent->setSpacing(4);recent->addWidget(label(QStringLiteral("最近打开"),"sectionHeading"));recent->addSpacing(8);
    if(recent_.isEmpty()) {
        auto* empty=label(QStringLiteral("打开过的文件会显示在这里"),"muted");empty->setStyleSheet("font-size:12px;");recent->addWidget(empty);recent->addSpacing(8);
        auto* detail=label(QStringLiteral("再次启动时，自动恢复上次的阅读位置。"),"muted");detail->setWordWrap(true);detail->setStyleSheet("font-size:12px;");recent->addWidget(detail);
    } else for(const auto& path:recent_.mid(0,5)) {
        auto* button=new QToolButton;button->setObjectName("recentWelcome");button->setProperty("filePath",path);button->setText(QFileInfo(path).fileName());button->setToolTip(QDir::toNativeSeparators(path));button->setIcon(Theme::fileIcon(path));button->setIconSize({16,16});button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);button->setCursor(Qt::PointingHandCursor);recent->addWidget(button);
        connect(button,&QToolButton::clicked,this,[this,path]{openPath(path);});
    }
    recent->addStretch();columns->addLayout(recent,1);body->addLayout(columns);
    auto* centered=new QHBoxLayout;centered->addStretch();centered->addWidget(panel,4);centered->addStretch();outer->addLayout(centered);outer->addStretch(3);
    auto* footer=label(QStringLiteral("本地文件  ·  只读查看  ·  即开即读"),"muted");footer->setAlignment(Qt::AlignCenter);footer->setStyleSheet("font-size:11px;");outer->addWidget(footer);
    auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(page);return scroll;
}
