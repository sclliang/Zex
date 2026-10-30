#pragma once
#include <QDialog>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QListView;
class QLabel;
class FileChoiceModel;

struct FileChoice {
    QString path;
    bool opened = false;
    bool pinned = false;
};

// These functions use only strings: no stat, directory enumeration or file reads.
QVector<FileChoice> mergeFileChoices(const QVector<FileChoice>& opened, const QStringList& recent);
QVector<int> rankFileChoices(const QVector<FileChoice>& choices, const QString& query);

class FilePickerDialog : public QDialog {
    Q_OBJECT
public:
    FilePickerDialog(QVector<FileChoice> choices, bool tabsOnly, QWidget* parent = nullptr);
    QString selectedPath() const;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    void chooseCurrent();
    QLineEdit* input_;
    QListView* list_;
    QLabel* summary_;
    FileChoiceModel* model_;
};
