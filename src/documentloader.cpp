#include "documentloader.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "ScintillaTypes.h"
#include "Debugging.h"
#include "ILexer.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"

PreparedFile prepareFile(const QString& path,const QString& encoding,const LoadControlPtr& control) {
    try { return prepareDocument(readFile(path,encoding,control),control); }
    catch(...) { PreparedFile result;result.content.error=QStringLiteral("无法读取文件，可能没有足够的内存。");return result; }
}

PreparedFile prepareDocument(FileContent content,const LoadControlPtr& control) {
    PreparedFile result;
    try {
        result.content=std::move(content);
        if(result.content.canceled || !result.content.error.isEmpty())return result;
        control->update(LoadStage::Preparing,70);
        const auto& bytes=result.content.utf8;
        // This small factory is tied to our statically linked Scintilla 5.6.7.
        // Constructing the unattached Document here reserves its full capacity
        // off the GUI thread and avoids CREATELOADER's issuing-view side effect.
        auto* raw=new Scintilla::Internal::Document(Scintilla::DocumentOption::Default);
        raw->AddRef();
        auto release=[](Scintilla::ILoader* value){value->Release();};
        std::unique_ptr<Scintilla::ILoader,decltype(release)> loader(raw,release);
        raw->SetUndoCollection(false);raw->Allocate(bytes.size());
        if(control->canceled.load()){result.content.canceled=true;return result;}
        for(qsizetype offset=0;offset<bytes.size();) {
            if(control->canceled.load()){result.content.canceled=true;return result;}
            const auto count=qMin<qsizetype>(2*1024*1024,bytes.size()-offset);
            if(loader->AddData(bytes.constData()+offset,count)!=SC_STATUS_OK) {
                result.content.utf8.clear();result.content.error=QStringLiteral("内存不足，无法准备文档。请关闭部分标签后重试。");return result;
            }
            offset+=count;control->percent.store(70+int(29*offset/qMax<qsizetype>(1,bytes.size())));
        }
        if(control->canceled.load()){result.content.canceled=true;return result;}
        auto* document=static_cast<Scintilla::IDocumentEditable*>(loader->ConvertToDocument());
        loader.release();
        std::unique_ptr<Scintilla::IDocumentEditable,DocumentRelease> owned(document);
        result.document=DocumentPtr(std::move(owned));
        result.content.utf8.clear();result.content.utf8.squeeze();
        control->percent.store(100);
    } catch(...) { result.content.utf8.clear();result.content.error=QStringLiteral("载入失败，可能没有足够的内存。请关闭部分标签后重试。"); }
    return result;
}
