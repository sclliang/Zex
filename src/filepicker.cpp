#include "filepicker.h"
#include "theme.h"
#include "windowplacement.h"
#include <QAbstractListModel>
#include <QAction>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString key(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded(); }
QString basename(const QString& path) { return path.mid(path.lastIndexOf('/')+1); }
// Contiguous filename matches rank highest; subsequences allow e.g. "mwc" for mainwindow.cpp.
int score(const QString& value, const QString& token) {
    const auto at=value.indexOf(token);
    if(at>=0)return 1000-int(qMin<qsizetype>(at,100))+int(at==0)*100+int(value==token)*200;
    qsizetype cursor=0,first=-1,last=-1;
    for(const auto c:token) {
        const auto found=value.indexOf(c,cursor);if(found<0)return -1;
        if(first<0)first=found;last=found;cursor=found+1;
    }
    return 500-int(qMin<qsizetype>(last-first-token.size()+1,300))-int(qMin<qsizetype>(first,100));
}
}

QVector<FileChoice> mergeFileChoices(const QVector<FileChoice>& opened, const QStringList& recent) {
    QVector<FileChoice> result;QSet<QString> seen;
    auto add=[&](FileChoice choice){const auto normalized=key(choice.path);if(choice.path.isEmpty() || seen.contains(normalized))return;seen.insert(normalized);choice.path=QDir::cleanPath(QDir::fromNativeSeparators(choice.path));result.append(choice);};
    for(const auto& choice:opened)add(choice);
    for(const auto& path:recent)add({path,false,false});
    return result;
}

QVector<int> rankFileChoices(const QVector<FileChoice>& choices, const QString& query) {
    const auto tokens=QDir::fromNativeSeparators(query).toCaseFolded().split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    QVector<QPair<int,int>> matches;
    for(int i=0;i<choices.size();++i) {
        const auto path=key(choices[i].path),name=basename(path);int total=0;bool matched=true;
        for(const auto& token:tokens) {
            const int filenameScore=score(name,token),pathScore=score(path,token);
            if(filenameScore<0 && pathScore<0){matched=false;break;}
            total+=qMax(filenameScore<0?-1:filenameScore+1500,pathScore);
        }
        if(matched)matches.append({total,i});
    }
    std::stable_sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    QVector<int> result;result.reserve(matches.size());for(const auto& item:matches)result.append(item.second);return result;
}

class FileChoiceModel : public QAbstractListModel {
public:
    FileChoiceModel(QVector<FileChoice> choices,QObject* parent):QAbstractListModel(parent),choices_(std::move(choices)){filter({});}
    void filter(const QString& query){beginResetModel();rows_=rankFileChoices(choices_,query);endResetModel();}
    int rowCount(const QModelIndex& parent={}) const override{return parent.isValid()?0:rows_.size();}
    QVariant data(const QModelIndex& index,int role) const override {
        if(!index.isValid() || index.row()<0 || index.row()>=rows_.size())return {};
        const auto& choice=choices_[rows_[index.row()]];
        if(role==Qt::DisplayRole)return basename(choice.path);
        if(role==Qt::ToolTipRole || role==Qt::AccessibleTextRole)return QDir::toNativeSeparators(choice.path);
        if(role==Qt::DecorationRole)return Theme::fileIcon(choice.path);
        if(role==Qt::UserRole)return choice.path;
        if(role==Qt::UserRole+1)return choice.pinned?QStringLiteral("已固定"):choice.opened?QStringLiteral("已打开"):QStringLiteral("最近使用");
        return {};
    }
private:
    QVector<FileChoice> choices_;
    QVector<int> rows_;
};

class FileChoiceDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex&) const override{return {300,qMax(48,option.fontMetrics.height()*2+12)};}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        painter->save();painter->setClipRect(option.rect);
        const auto& c=Theme::colors();const bool selected=option.state&QStyle::State_Selected;
        painter->fillRect(option.rect,selected?c.selection:(option.state&QStyle::State_MouseOver)?c.hover:c.elevated);
        const auto rect=option.rect;const int line=option.fontMetrics.height();
        index.data(Qt::DecorationRole).value<QIcon>().paint(painter,QRect(rect.left()+12,rect.top()+(rect.height()-16)/2,16,16));
        const auto tag=index.data(Qt::UserRole+1).toString();const int tagWidth=option.fontMetrics.horizontalAdvance(tag)+18;
        const QRect nameRect(rect.left()+40,rect.top()+5,rect.width()-56-tagWidth,line);
        painter->setFont(option.font);painter->setPen(c.text);
        painter->drawText(nameRect,Qt::AlignVCenter,option.fontMetrics.elidedText(index.data().toString(),Qt::ElideMiddle,nameRect.width()));
        auto detailFont=option.font;detailFont.setPixelSize(12);painter->setFont(detailFont);const QFontMetrics detailMetrics(detailFont);
        painter->setPen(c.muted);painter->drawText(QRect(rect.right()-tagWidth,rect.top()+5,tagWidth-12,line),Qt::AlignRight|Qt::AlignVCenter,tag);
        const auto path=QDir::toNativeSeparators(index.data(Qt::UserRole).toString());
        const QRect pathRect(rect.left()+40,rect.top()+7+line,rect.width()-54,line);
        painter->drawText(pathRect,Qt::AlignVCenter,detailMetrics.elidedText(path,Qt::ElideMiddle,pathRect.width()));
        painter->restore();
    }
};

FilePickerDialog::FilePickerDialog(QVector<FileChoice> choices,bool tabsOnly,QWidget* parent):QDialog(parent) {
    setObjectName("filePicker");setWindowTitle(tabsOnly?QStringLiteral("全部标签"):QStringLiteral("快速切换文件"));
    setWindowFlags((tabsOnly?Qt::Popup:Qt::Dialog)|Qt::FramelessWindowHint);
    setMinimumSize(340,220);resize(tabsOnly?580:680,440);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(12,10,12,8);layout->setSpacing(9);
    auto* row=new QHBoxLayout;auto* title=new QLabel(windowTitle());row->addWidget(title);row->addStretch();
    auto* close=new QToolButton;close->setIcon(Theme::icon("close",Theme::colors().muted));close->setIconSize({16,16});close->setFixedSize(24,24);close->setToolTip(QStringLiteral("关闭 (Esc)"));close->setAccessibleName(close->toolTip());row->addWidget(close);connect(close,&QToolButton::clicked,this,&QDialog::reject);layout->addLayout(row);
    input_=new QLineEdit;input_->setObjectName("filePickerInput");input_->setPlaceholderText(QStringLiteral("输入文件名或路径，支持模糊匹配"));input_->setAccessibleName(QStringLiteral("筛选文件"));input_->setMaxLength(512);layout->addWidget(input_);
    auto* clear=input_->addAction(Theme::icon("close",Theme::colors().muted),QLineEdit::TrailingPosition);clear->setText(QStringLiteral("清空筛选"));clear->setVisible(false);connect(clear,&QAction::triggered,input_,&QLineEdit::clear);connect(input_,&QLineEdit::textChanged,clear,[clear](const QString& text){clear->setVisible(!text.isEmpty());});
    list_=new QListView;list_->setObjectName("filePickerList");list_->setAccessibleName(windowTitle());list_->setMouseTracking(true);list_->setUniformItemSizes(true);list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    model_=new FileChoiceModel(std::move(choices),this);list_->setModel(model_);list_->setItemDelegate(new FileChoiceDelegate(list_));layout->addWidget(list_,1);
    summary_=new QLabel;summary_->setObjectName("muted");summary_->setStyleSheet("font-size:11px;");layout->addWidget(summary_);
    auto filter=[this](const QString& query){model_->filter(query);if(model_->rowCount())list_->setCurrentIndex(model_->index(0,0));summary_->setText(model_->rowCount()?QStringLiteral("%1 个文件    ↑ ↓ 选择    Enter 打开    Esc 关闭").arg(model_->rowCount()):QStringLiteral("没有匹配的文件"));};
    connect(input_,&QLineEdit::textChanged,this,filter);filter({});
    connect(input_,&QLineEdit::returnPressed,this,&FilePickerDialog::chooseCurrent);connect(list_,&QListView::activated,this,[this]{chooseCurrent();});
    input_->installEventFilter(this);input_->setFocus();WindowPlacement::keepVisible(this);
}
QString FilePickerDialog::selectedPath() const{return list_->currentIndex().data(Qt::UserRole).toString();}
void FilePickerDialog::chooseCurrent(){if(!selectedPath().isEmpty())accept();}
bool FilePickerDialog::eventFilter(QObject* object,QEvent* event) {
    if(object==input_ && event->type()==QEvent::KeyPress) {
        const auto* keyEvent=static_cast<QKeyEvent*>(event);int delta=0;
        if(keyEvent->key()==Qt::Key_Up)delta=-1;else if(keyEvent->key()==Qt::Key_Down)delta=1;
        else if(keyEvent->key()==Qt::Key_PageUp)delta=-qMax(1,list_->height()/qMax(1,list_->sizeHintForRow(0)));else if(keyEvent->key()==Qt::Key_PageDown)delta=qMax(1,list_->height()/qMax(1,list_->sizeHintForRow(0)));
        if(delta){if(model_->rowCount()){const auto next=model_->index(qBound(0,list_->currentIndex().row()+delta,model_->rowCount()-1),0);list_->setCurrentIndex(next);list_->scrollTo(next);}return true;}
    }
    return QDialog::eventFilter(object,event);
}
