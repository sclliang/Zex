#include "shellintegration.h"
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <optional>
#include <windows.h>
#include <shlobj.h>

namespace {
const QString owner="sixunited.Zex/1";
const QString classes="Software\\Classes\\";
const QString verb=classes+"*\\shell\\sixunited.Zex";
const QString progId="sixunited.Zex.Text";
const QString prog=classes+progId;
const QString application=classes+"Applications\\Zex.exe";
const QString capabilities="Software\\sixunited\\Zex\\Capabilities";
const QString manifest="Software\\sixunited\\Zex\\Integration";
const QString registered="Software\\RegisteredApplications";
LPCWSTR wide(const QString& value) { return reinterpret_cast<LPCWSTR>(value.utf16()); }
struct Value { bool exists=false;DWORD type=REG_SZ;QByteArray bytes; };
Value readValue(const QString& path,const QString& name) {
    HKEY key=nullptr;Value result;
    auto status=RegOpenKeyExW(HKEY_CURRENT_USER,wide(path),0,KEY_QUERY_VALUE,&key);
    if(status==ERROR_FILE_NOT_FOUND)return result;if(status!=ERROR_SUCCESS)throw status;
    DWORD size=0;status=RegQueryValueExW(key,wide(name),nullptr,&result.type,nullptr,&size);
    if(status==ERROR_SUCCESS && size<=4*1024*1024) {
        result.bytes.resize(size);
        status=RegQueryValueExW(key,wide(name),nullptr,&result.type,reinterpret_cast<BYTE*>(result.bytes.data()),&size);
        result.exists=status==ERROR_SUCCESS;result.bytes.resize(size);
    } else if(status==ERROR_SUCCESS)status=ERROR_MORE_DATA;
    RegCloseKey(key);
    if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND)throw status;
    return result;
}
QString stringValue(const QString& path,const QString& name) {
    const auto value=readValue(path,name);
    if(!value.exists || value.type!=REG_SZ || value.bytes.size()%2)return {};
    auto text=QString::fromUtf16(reinterpret_cast<const char16_t*>(value.bytes.constData()),value.bytes.size()/2);
    if(text.endsWith(QChar(0)))text.chop(1);return text;
}
bool keyExists(const QString& path) {
    HKEY key=nullptr;const auto status=RegOpenKeyExW(HKEY_CURRENT_USER,wide(path),0,KEY_READ,&key);
    if(status==ERROR_SUCCESS){RegCloseKey(key);return true;}
    if(status==ERROR_FILE_NOT_FOUND)return false;throw status;
}
class Transaction {
    struct Change { QString path,name;Value before; };
    QVector<Change> journal;
    QSet<QString> cleanup;
    bool committed=false;
    static LSTATUS restore(const QString& path,const QString& name,const Value& value) {
        HKEY key=nullptr;auto status=value.exists?
            RegCreateKeyExW(HKEY_CURRENT_USER,wide(path),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr):
            RegOpenKeyExW(HKEY_CURRENT_USER,wide(path),0,KEY_SET_VALUE,&key);
        if(!value.exists && status==ERROR_FILE_NOT_FOUND)return ERROR_SUCCESS;
        if(status!=ERROR_SUCCESS)return status;
        status=value.exists?RegSetValueExW(key,wide(name),0,value.type,reinterpret_cast<const BYTE*>(value.bytes.constData()),DWORD(value.bytes.size())):RegDeleteValueW(key,wide(name));
        RegCloseKey(key);return !value.exists && status==ERROR_FILE_NOT_FOUND?ERROR_SUCCESS:status;
    }
public:
    ~Transaction() {
        if(!committed)for(auto i=journal.crbegin();i!=journal.crend();++i)restore(i->path,i->name,i->before);
        auto keys=cleanup.values();std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return a.size()>b.size();});
        for(const auto& path:keys) {
            HKEY key=nullptr;DWORD children=0,values=0;
            if(RegOpenKeyExW(HKEY_CURRENT_USER,wide(path),0,KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS,&key)!=ERROR_SUCCESS)continue;
            const auto status=RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr);
            RegCloseKey(key);
            if(status==ERROR_SUCCESS && children==0 && values==0)RegDeleteKeyW(HKEY_CURRENT_USER,wide(path));
        }
    }
    void set(const QString& path,const QString& name,const std::optional<QString>& text) {
        const auto before=readValue(path,name);Value next;
        if(text) { next.exists=true;next.bytes=QByteArray(reinterpret_cast<const char*>(text->utf16()),(text->size()+1)*2); }
        if(before.exists==next.exists && (!next.exists || (before.type==REG_SZ && before.bytes==next.bytes)))return;
        if(next.exists) {
            auto parent=path;
            while(!parent.isEmpty() && !keyExists(parent)){cleanup.insert(parent);const auto slash=parent.lastIndexOf('\\');if(slash<0)break;parent=parent.left(slash);}
        }
        journal.append({path,name,before});
        const auto status=restore(path,name,next);if(status!=ERROR_SUCCESS)throw status;
    }
    void prune(const QString& path) { cleanup.insert(path); }
    void commit() { committed=true; }
};
}

ShellIntegration::ShellIntegration(QString executable,QString registryPrefix)
    : executable_(QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath())),prefix_(std::move(registryPrefix)) {
    if(!prefix_.isEmpty() && !prefix_.endsWith('\\'))prefix_+='\\';
}
QStringList ShellIntegration::supportedExtensions() {
    return {".txt",".log",".md",".markdown",".csv",".json",".jsonc",".xml",".yaml",".yml",".toml",".ini",".cfg",".conf",".properties",
        ".c",".h",".cpp",".hpp",".cc",".cxx",".cs",".java",".py",".pyw",".js",".jsx",".ts",".tsx",".mjs",".html",".htm",".css",".scss",
        ".rs",".go",".sql",".lua",".sh",".bash",".ps1",".psm1",".bat",".cmd",".cmake",".diff",".patch",".svg",".qrc",".ui"};
}
QString ShellIntegration::defaultAppsUrl() { return "ms-settings:defaultapps?registeredAppUser=Zex"; }
IntegrationState ShellIntegration::state() const {
    IntegrationState result;
    try {
        result.contextMenu=stringValue(prefix_+verb,"ZexOwner")==owner;
        result.executable=stringValue(prefix_+manifest,"Executable");
        const auto items=QJsonDocument::fromJson(stringValue(prefix_+manifest,"Extensions").toUtf8()).array();
        for(const auto& item:items)if(supportedExtensions().contains(item.toString()))result.extensions.append(item.toString());
    } catch(LSTATUS) {} // Reading a locked registry must not prevent the viewer from opening.
    return result;
}
bool ShellIntegration::apply(bool contextMenu,const QStringList& requested,QString* error) const {
    if(error)error->clear();
    auto fail=[error](const QString& message){if(error)*error=message;return false;};
    QStringList extensions;
    for(auto extension:requested) {
        extension=extension.toLower();
        if(!supportedExtensions().contains(extension))return fail(QStringLiteral("不支持关联此文件类型：%1").arg(extension));
        if(!extensions.contains(extension))extensions.append(extension);
    }
    if(!QFileInfo(executable_).isFile())return fail(QStringLiteral("找不到当前程序，请完整解压发布包后重试。"));
    extensions.sort();const auto previous=state();
    try {
        // These roots belong exclusively to Zex; never replace a different application's registration.
        for(const auto& key:{verb,prog,application,capabilities,manifest})
            if(keyExists(prefix_+key) && stringValue(prefix_+key,"ZexOwner")!=owner)
                return fail(QStringLiteral("注册项已被其他配置占用，未修改系统集成：%1").arg(key));
        const auto existing=readValue(prefix_+registered,"Zex");
        if(existing.exists && stringValue(prefix_+registered,"Zex")!=capabilities)return fail(QStringLiteral("已有其他名为 Zex 的应用注册，未修改它的设置。"));
        Transaction transaction;
        auto write=[&](const QString& path,const QString& name,const std::optional<QString>& text){transaction.set(prefix_+path,name,text);};
        auto erase=[&](const QString& path,const QString& name){write(path,name,std::nullopt);transaction.prune(prefix_+path);};
        const auto command='"'+executable_+"\" -- \"%1\"";
        const auto icon='"'+executable_+"\",0";
        auto own=[&](const QString& path){write(path,"ZexOwner",owner);};
        auto removeCommand=[&](const QString& path){erase(path+"\\shell\\open\\command","");erase(path+"\\shell\\open","MultiSelectModel");transaction.prune(prefix_+path+"\\shell");};
        if(contextMenu) {
            own(verb);write(verb,"",QStringLiteral("使用 Zex 打开"));write(verb,"Icon",icon);write(verb,"MultiSelectModel","Player");write(verb+"\\command","",command);
        } else { erase(verb+"\\command","");for(const auto& name:{"","Icon","MultiSelectModel","ZexOwner"})erase(verb,name); }
        // OpenWithProgids is additive. The extension's default value and UserChoice are never changed.
        for(const auto& ext:previous.extensions)if(!extensions.contains(ext))erase(classes+ext+"\\OpenWithProgids",progId);
        if(!extensions.isEmpty()) {
            own(prog);write(prog,"",QStringLiteral("Zex 文本与代码文件"));write(prog+"\\DefaultIcon","",icon);
            write(prog+"\\shell\\open\\command","",command);write(prog+"\\shell\\open","MultiSelectModel","Player");
            own(application);write(application,"FriendlyAppName","Zex");write(application+"\\DefaultIcon","",icon);
            write(application+"\\shell\\open\\command","",command);write(application+"\\shell\\open","MultiSelectModel","Player");
            own(capabilities);write(capabilities,"ApplicationName","Zex");write(capabilities,"ApplicationDescription",QStringLiteral("Zex 文件与代码阅读器"));write(capabilities,"ApplicationIcon",icon);
            write(registered,"Zex",capabilities);
            for(const auto& ext:extensions) {
                write(classes+ext+"\\OpenWithProgids",progId,QString());
                write(capabilities+"\\FileAssociations",ext,progId);write(application+"\\SupportedTypes",ext,QString());
            }
        } else {
            erase(registered,"Zex");
            for(const auto& key:{prog,application}){removeCommand(key);erase(key+"\\DefaultIcon","");erase(key,"ZexOwner");}
            erase(prog,"");erase(application,"FriendlyAppName");
            for(const auto& name:{"ZexOwner","ApplicationName","ApplicationDescription","ApplicationIcon"})erase(capabilities,name);
        }
        for(const auto& ext:previous.extensions)if(!extensions.contains(ext)) {
            erase(capabilities+"\\FileAssociations",ext);erase(application+"\\SupportedTypes",ext);
        }
        // Parent pruning is deferred until all child values have been processed.
        for(const auto& key:{verb,prog,application,capabilities,manifest})transaction.prune(prefix_+key);
        if(contextMenu || !extensions.isEmpty()) {
            own(manifest);write(manifest,"Executable",executable_);
            write(manifest,"Extensions",QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(extensions)).toJson(QJsonDocument::Compact)));
        } else for(const auto& name:{"ZexOwner","Executable","Extensions"})erase(manifest,name);
        transaction.commit();
    } catch(LSTATUS status) {
        return fail(QStringLiteral("无法保存 Windows 集成设置（错误 %1），已回退本次修改。请检查当前用户的注册表访问权限。").arg(status));
    }
    if(prefix_.isEmpty())SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
    return true;
}
