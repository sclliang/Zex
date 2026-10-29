#pragma once
#include <QStringList>

struct IntegrationState {
    bool contextMenu=false;
    QStringList extensions;
    QString executable;
};

class ShellIntegration {
public:
    explicit ShellIntegration(QString executable, QString registryPrefix={});
    IntegrationState state() const;
    bool apply(bool contextMenu,const QStringList& extensions,QString* error=nullptr) const;
    bool remove(QString* error=nullptr) const { return apply(false,{},error); }
    static QStringList supportedExtensions();
    static QString defaultAppsUrl();
private:
    QString executable_, prefix_;
};
