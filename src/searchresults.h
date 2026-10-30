#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include "editor.h"

class SearchResultsModel : public QAbstractListModel {
public:
    explicit SearchResultsModel(QObject* parent=nullptr):QAbstractListModel(parent){}
    void setEditor(CodeEditor* editor) {
        if(editor_==editor){refresh();return;}
        if(editor_)disconnect(editor_,nullptr,this,nullptr);
        beginResetModel();editor_=editor;count_=editor?editor->matchCount():0;generation_=editor?editor->searchGeneration():0;endResetModel();
        if(editor){connect(editor,&CodeEditor::searchChanged,this,[this]{refresh();});connect(editor,&QObject::destroyed,this,[this]{beginResetModel();editor_=nullptr;count_=0;endResetModel();});}
    }
    int rowCount(const QModelIndex& parent={}) const override{return parent.isValid()?0:count_;}
    QVariant data(const QModelIndex& index,int role) const override {
        if(!editor_ || !index.isValid() || index.row()<0 || index.row()>=editor_->matchCount())return {};
        if(role==Qt::DisplayRole || role==Qt::ToolTipRole || role==Qt::AccessibleTextRole)return editor_->resultText(index.row());return {};
    }
    void activate(int index){if(editor_)editor_->selectResult(index);}
private:
    void refresh(){if(!editor_)return;if(count_==editor_->matchCount() && generation_==editor_->searchGeneration())return;beginResetModel();count_=editor_->matchCount();generation_=editor_->searchGeneration();endResetModel();}
    QPointer<CodeEditor> editor_;
    int count_=0;quint64 generation_=0;
};
