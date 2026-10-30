#pragma once
#include "fileloader.h"
#include "Scintilla.h"
#include "ILoader.h"

struct DocumentRelease { void operator()(Scintilla::IDocumentEditable* document) const { if(document)document->Release(); } };
using DocumentPtr=std::shared_ptr<Scintilla::IDocumentEditable>;
struct PreparedFile {
    FileContent content;
    DocumentPtr document;
};
// The worker owns the unattached document until its completed result is handed off.
PreparedFile prepareFile(const QString& path,const QString& encoding,const LoadControlPtr& control);
PreparedFile prepareDocument(FileContent content,const LoadControlPtr& control);
