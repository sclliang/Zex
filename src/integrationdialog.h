#pragma once
#include "shellintegration.h"
#include <QDialog>
class QCheckBox;
class QLabel;

class IntegrationDialog : public QDialog {
    Q_OBJECT
public:
    explicit IntegrationDialog(const ShellIntegration& integration,QWidget* parent=nullptr);
private:
    void load();
    bool apply();
    ShellIntegration integration_;
    QCheckBox* contextMenu_;
    QList<QCheckBox*> types_;
    QLabel* status_;
};
