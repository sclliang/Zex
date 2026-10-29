#pragma once
#include "appearance.h"
#include <QDialog>
class QFontComboBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class CodeEditor;

class AppearanceDialog : public QDialog {
    Q_OBJECT
public:
    AppearanceDialog(const EditorAppearance& appearance, const QString& iconTheme, QWidget* parent = nullptr);
    EditorAppearance editorAppearance() const;
    QString iconTheme() const;
    QString colorTheme() const;
signals:
    void applyRequested();
private:
    void setValues(const EditorAppearance& appearance, const QString& iconTheme, const QString& colorTheme);
    void updatePreview();
    QFontComboBox* family_;
    QSpinBox* size_;
    QComboBox* style_;
    QSpinBox* padding_;
    QCheckBox* comments_;
    QComboBox* icons_;
    QComboBox* themes_;
    CodeEditor* preview_;
    QList<QLabel*> iconSamples_;
};
