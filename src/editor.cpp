#include "editor.h"
#include "theme.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QMimeData>
#include <QUrl>
#include <QToolTip>
#include <algorithm>

namespace {
int rgb(const QColor& c) { return c.red() | (c.green() << 8) | (c.blue() << 16); }
constexpr int searchIndicator = 8;
}

CodeEditor::CodeEditor(QWidget* parent) : ScintillaEditBase(parent), colorTheme_(Theme::id()) {
    setObjectName("codeEditor");
    setAccessibleName(QStringLiteral("代码内容（只读）"));
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_SETUNDOCOLLECTION, 0);
    send(SCI_STYLESETFORE, STYLE_DEFAULT, rgb(Theme::colors().text));
    send(SCI_STYLESETBACK, STYLE_DEFAULT, rgb(Theme::colors().background));
    sends(SCI_STYLESETFONT, STYLE_DEFAULT, appearance_.family.toUtf8().constData());
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, appearance_.points);
    send(SCI_STYLESETCHECKMONOSPACED, STYLE_DEFAULT, 1);
    send(SCI_STYLECLEARALL);
    send(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);
    send(SCI_SETEXTRAASCENT, 3);
    send(SCI_SETEXTRADESCENT, 3);
    send(SCI_SETCARETFORE, rgb(Theme::colors().accent));
    send(SCI_SETCARETWIDTH, 2);
    send(SCI_SETCARETLINEVISIBLE, 1);
    send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    send(SCI_SETCARETLINEBACK, rgb(Theme::colors().elevated));
    send(SCI_SETSELBACK, 1, rgb(Theme::colors().selection));
    send(SCI_SETSELFORE, 0, 0);
    send(SCI_SETSELEOLFILLED, 1);
    send(SCI_SETMARGINLEFT, 0, 12);
    send(SCI_SETMARGINRIGHT, 0, 16);
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, rgb(Theme::colors().subtle));
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, rgb(Theme::colors().background));
    send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINMASKN, 1, SC_MASK_FOLDERS);
    send(SCI_SETMARGINSENSITIVEN, 1, 1);
    send(SCI_SETMARGINWIDTHN, 1, 16);
    send(SCI_SETMARGINTYPEN,2,SC_MARGIN_SYMBOL);send(SCI_SETMARGINMASKN,2,1<<1);send(SCI_SETMARGINSENSITIVEN,2,1);send(SCI_SETMARGINWIDTHN,2,14);
    send(SCI_MARKERDEFINE,1,SC_MARK_BOOKMARK);send(SCI_MARKERSETFORE,1,rgb(Theme::colors().accent));send(SCI_MARKERSETBACK,1,rgb(Theme::colors().accent));
    send(SCI_SETFOLDMARGINCOLOUR, 1, rgb(Theme::colors().background));
    send(SCI_SETFOLDMARGINHICOLOUR, 1, rgb(Theme::colors().background));
    for (int i = SC_MARKNUM_FOLDEREND; i <= SC_MARKNUM_FOLDEROPEN; ++i) {
        send(SCI_MARKERDEFINE, i, SC_MARK_EMPTY);
        send(SCI_MARKERSETFORE, i, rgb(Theme::colors().muted));
        send(SCI_MARKERSETBACK, i, rgb(Theme::colors().background));
    }
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_ARROW);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_ARROWDOWN);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_ARROW);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_ARROWDOWN);
    send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE);
    send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    send(SCI_STYLESETFORE, STYLE_INDENTGUIDE, rgb(Theme::colors().border));
    send(SCI_STYLESETFORE, STYLE_BRACELIGHT, rgb(Theme::colors().accent));
    send(SCI_STYLESETBACK, STYLE_BRACELIGHT, rgb(Theme::colors().selection));
    send(SCI_STYLESETFORE, STYLE_BRACEBAD, rgb(Theme::colors().error));
    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETSCROLLWIDTH, 1);
    send(SCI_SETSCROLLWIDTHTRACKING, 1);
    send(SCI_SETENDATLASTLINE, 0);
    send(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    send(SCI_INDICSETSTYLE, searchIndicator, INDIC_ROUNDBOX);
    send(SCI_INDICSETFORE, searchIndicator, rgb(Theme::colors().number));
    send(SCI_INDICSETALPHA, searchIndicator, 55);
    send(SCI_INDICSETOUTLINEALPHA, searchIndicator, 95);
    send(SCI_INDICSETUNDER, searchIndicator, 1);
    send(SCI_SETREADONLY, 1);
    applyColors();
    updateMargins();
    connect(this, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update updated) {
        updateBraces();
        emit positionChanged();
        if (static_cast<int>(updated) & (SC_UPDATE_SELECTION | SC_UPDATE_V_SCROLL | SC_UPDATE_H_SCROLL)) emit readingPositionChanged();
        if (static_cast<int>(updated) & SC_UPDATE_V_SCROLL)emit verticalPositionChanged();
        if(navigating_ && (static_cast<int>(updated)&SC_UPDATE_SELECTION)
            && (send(SCI_GETCURRENTPOS)!=navigationCaret_ || send(SCI_GETANCHOR)!=navigationAnchor_))navigating_=false;
        if (!query_.isEmpty()) emit searchChanged();
    });
    connect(this, &ScintillaEditBase::marginClicked, this, [this](Scintilla::Position pos, Scintilla::KeyMod, int margin) {
        if (margin == 1) send(SCI_TOGGLEFOLD, send(SCI_LINEFROMPOSITION, pos));
        if(margin==2)toggleBookmark(send(SCI_LINEFROMPOSITION,pos));
    });
    connect(this, &ScintillaEditBase::zoom, this, [this] { updateMargins(); });
    connect(&searchTimer_, &QTimer::timeout, this, &CodeEditor::countMatches);
    searchTimer_.setInterval(1);
}

void CodeEditor::setContent(const FileContent& content, const QString& path) {
    clearSearch();
    send(SCI_SETREADONLY, 0);
    // SCI_ADDTEXT accepts an explicit byte length; the decoder rejects embedded NULs.
    send(SCI_CLEARALL);
    send(SCI_MARKERDELETEALL,1);
    send(SCI_ADDTEXT, content.utf8.size(), reinterpret_cast<sptr_t>(content.utf8.constData()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETREADONLY, 1);
    configureLanguage(path, content.large);
    send(SCI_GOTOPOS, 0);
    updateMargins();
}

void CodeEditor::setPreparedContent(const PreparedFile& file,const QString& path) {
    clearSearch();
    send(SCI_SETDOCPOINTER,0,reinterpret_cast<sptr_t>(file.document.get()));
    documentOwner_=file.document;
    send(SCI_SETCODEPAGE,SC_CP_UTF8);send(SCI_SETUNDOCOLLECTION,0);send(SCI_SETREADONLY,1);
    configureLanguage(path,file.content.large);send(SCI_GOTOPOS,0);updateMargins();
}

bool CodeEditor::appendLogText(const QByteArray& utf8,const QString& path,bool large) {
    if(utf8.isEmpty())return true;
    const auto oldLength=send(SCI_GETLENGTH);
    send(SCI_SETSTATUS,SC_STATUS_OK);send(SCI_SETREADONLY,0);
    send(SCI_APPENDTEXT,utf8.size(),reinterpret_cast<sptr_t>(utf8.constData()));send(SCI_SETREADONLY,1);
    if(send(SCI_GETSTATUS)!=SC_STATUS_OK)return false;
    if(large && lexer_!="null")configureLanguage(path,true);
    updateMargins();
    if(!query_.isEmpty()) {
        if(regex_){cancelSearch();matches_.clear();regexSnapshot_.clear();send(SCI_SETINDICATORCURRENT,searchIndicator);send(SCI_INDICATORCLEARRANGE,0,send(SCI_GETLENGTH));searchError_=QStringLiteral("内容已更新，按 Enter 重新搜索");emit searchChanged();return true;}
        if(searchInSelection_ || stopped_)return true;
        searchEnd_=send(SCI_GETLENGTH);
        // Re-scan only the boundary (including whole-word matches that may now extend).
        auto from=qMax<sptr_t>(0,oldLength-query_.size()*16-4);
        from=send(SCI_POSITIONBEFORE,send(SCI_POSITIONAFTER,from));
        if(capped_ && !matches_.isEmpty() && matches_.last().second<from)return true;
        if(counting_)from=qMin(from,scanAt_);
        while(!matches_.isEmpty() && matches_.last().second>=from){from=qMin(from,matches_.last().first);matches_.removeLast();}
        send(SCI_SETINDICATORCURRENT,searchIndicator);send(SCI_INDICATORCLEARRANGE,from,send(SCI_GETLENGTH)-from);
        scanAt_=from;counting_=true;capped_=false;navigating_=false;searchTimer_.start();
    }
    return true;
}
bool CodeEditor::atDocumentEnd() const {
    const auto last=qMax<sptr_t>(0,send(SCI_GETLINECOUNT)-1);
    const auto end=send(SCI_VISIBLEFROMDOCLINE,last)+send(SCI_WRAPCOUNT,last);
    return send(SCI_GETFIRSTVISIBLELINE)+qMax<sptr_t>(1,send(SCI_LINESONSCREEN))>=end;
}
void CodeEditor::scrollToEnd() {
    const auto last=qMax<sptr_t>(0,send(SCI_GETLINECOUNT)-1);send(SCI_ENSUREVISIBLE,last);
    const auto end=send(SCI_VISIBLEFROMDOCLINE,last)+send(SCI_WRAPCOUNT,last);
    send(SCI_SETFIRSTVISIBLELINE,qMax<sptr_t>(0,end-qMax<sptr_t>(1,send(SCI_LINESONSCREEN))));
}

void CodeEditor::setPointSize(int points) {
    appearance_.points = qBound(8, points, 32);
    applyTypography();
}

void CodeEditor::setAppearance(const EditorAppearance& appearance) {
    appearance_ = appearance.sanitized();
    applyTypography();
}

void CodeEditor::applyTypography() {
    const auto family = appearance_.family.toUtf8();
    const auto top = send(SCI_GETFIRSTVISIBLELINE);
    const auto horizontal = send(SCI_GETXOFFSET);
    for (int style = 0; style < 256; ++style) {
        sends(SCI_STYLESETFONT, style, family.constData());
        send(SCI_STYLESETSIZE, style, appearance_.points);
        send(SCI_STYLESETBOLD, style, appearance_.bold);
        send(SCI_STYLESETITALIC, style, appearance_.italic || (appearance_.commentItalics && commentStyles_.contains(style)));
    }
    send(SCI_SETEXTRAASCENT, (appearance_.linePadding + 1) / 2);
    send(SCI_SETEXTRADESCENT, appearance_.linePadding / 2);
    send(SCI_SETZOOM, 0);
    updateMargins();
    send(SCI_SETFIRSTVISIBLELINE, top);
    send(SCI_SETXOFFSET, horizontal);
}

void CodeEditor::setWordWrap(bool on) { send(SCI_SETWRAPMODE, on ? SC_WRAP_WORD : SC_WRAP_NONE); }

ReadingPosition CodeEditor::readingPosition() const {
    const auto first = send(SCI_GETFIRSTVISIBLELINE);
    const auto top = send(SCI_DOCLINEFROMVISIBLE, first);
    const auto caret = send(SCI_GETCURRENTPOS), anchor = send(SCI_GETANCHOR);
    return {top, int(first - send(SCI_VISIBLEFROMDOCLINE,top)), int(send(SCI_GETXOFFSET)),
        send(SCI_LINEFROMPOSITION,caret), send(SCI_GETCOLUMN,caret),
        send(SCI_LINEFROMPOSITION,anchor), send(SCI_GETCOLUMN,anchor)};
}

void CodeEditor::restoreReadingPosition(const ReadingPosition& position) {
    navigating_=false;
    const auto last = qMax<sptr_t>(0,send(SCI_GETLINECOUNT)-1);
    const auto top = qBound<sptr_t>(0,position.topLine,last);
    const auto caret = send(SCI_FINDCOLUMN,qBound<sptr_t>(0,position.caretLine,last),qMax<qint64>(0,position.caretColumn));
    const auto anchor = send(SCI_FINDCOLUMN,qBound<sptr_t>(0,position.anchorLine,last),qMax<qint64>(0,position.anchorColumn));
    send(SCI_SETSEL,anchor,caret);
    // Resolve wrapped display lines only for the active document, after it has a viewport.
    send(SCI_ENSUREVISIBLE,top);
    const auto subline = std::clamp<sptr_t>(position.wrapOffset,0,qMax<sptr_t>(0,send(SCI_WRAPCOUNT,top)-1));
    send(SCI_SETFIRSTVISIBLELINE,send(SCI_VISIBLEFROMDOCLINE,top)+subline);
    if (send(SCI_GETWRAPMODE) == SC_WRAP_NONE) {
        // Scintilla learns the horizontal extent as lines are painted. Keep the saved viewport reachable.
        const auto offset = qBound(0,position.xOffset,1000000);
        if(offset>0)send(SCI_SETSCROLLWIDTH,qMax<sptr_t>(send(SCI_GETSCROLLWIDTH),offset+viewport()->width()));
        send(SCI_SETXOFFSET,offset);
    }
}

void CodeEditor::updateMargins() {
    QByteArray digits(qMax(3, QByteArray::number(send(SCI_GETLINECOUNT)).size()), '9');
    send(SCI_SETMARGINWIDTHN, 0, sends(SCI_TEXTWIDTH, STYLE_LINENUMBER, digits.constData()) + 22);
}

void CodeEditor::updateBraces() {
    auto pos = send(SCI_GETCURRENTPOS);
    auto brace = [](int c) { return c && QByteArray("()[]{}").contains(char(c)); };
    sptr_t at = -1;
    if (pos > 0 && brace(send(SCI_GETCHARAT, pos - 1))) at = pos - 1;
    else if (brace(send(SCI_GETCHARAT, pos))) at = pos;
    const sptr_t match = at >= 0 ? send(SCI_BRACEMATCH, at, 0) : -1;
    if (match >= 0) send(SCI_BRACEHIGHLIGHT, at, match);
    else send(SCI_BRACEHIGHLIGHT, static_cast<uptr_t>(-1), -1);
}

void CodeEditor::configureLanguage(const QString& path, bool large) {
    QString extension = QFileInfo(path).suffix().toLower();
    QString name = QFileInfo(path).fileName().toLower();
    QByteArray lexer = "null", keywords, extra;
    language_ = "Plain Text";
    if (QStringList{"cpp","cc","cxx","h","hpp","hxx","c","ino"}.contains(extension)) {
        lexer="cpp"; language_=extension=="c" ? "C" : "C++";
        keywords="alignas alignof asm auto bool break case catch char char8_t char16_t char32_t class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false final float for friend goto if inline int long mutable namespace new noexcept nullptr operator override private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while";
        extra="std string string_view vector map unordered_map optional unique_ptr shared_ptr size_t QString QByteArray QWidget QObject";
    } else if (QStringList{"js","jsx","mjs","cjs","ts","tsx"}.contains(extension)) {
        lexer="cpp"; language_=QStringList{"ts","tsx"}.contains(extension) ? "TypeScript" : "JavaScript";
        keywords="abstract as async await break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in instanceof interface let module namespace new null of package private protected public readonly return set static super switch symbol this throw true try type typeof undefined var void while with yield";
        extra="any boolean number string unknown never Promise Array Object Map Set console JSON";
    } else if (QStringList{"java","cs"}.contains(extension)) {
        lexer="cpp"; language_=extension=="java" ? "Java" : "C#";
        keywords="abstract as assert async await base bool boolean break byte case catch char class const continue decimal default delegate do double else enum event explicit extends extern false final finally fixed float for foreach goto if implements implicit import in int interface internal is lock long namespace native new null object operator out override package params private protected public readonly ref return sbyte sealed short sizeof stackalloc static strictfp string struct super switch synchronized this throw throws transient true try typeof uint ulong unchecked unsafe ushort using var virtual void volatile while yield record";
    } else if (QStringList{"py","pyw","pyi"}.contains(extension)) {
        lexer="python"; language_="Python";
        keywords="False None True and as assert async await break case class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield";
        extra="abs all any bool bytes dict enumerate filter float int len list map max min object open print range repr set sorted str sum super tuple type zip self cls";
    } else if (QStringList{"json","jsonc","ipynb"}.contains(extension)) {
        lexer="json"; language_="JSON"; keywords="false true null";
    } else if (extension=="rs") {
        lexer="rust"; language_="Rust";
        keywords="as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while";
        extra="bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option Result Some None Ok Err";
    } else if (extension=="go") {
        lexer="cpp"; language_="Go"; keywords="break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var";
        extra="bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr true false nil";
    } else if (QStringList{"html","htm","vue","svelte"}.contains(extension)) {
        lexer="hypertext"; language_="HTML"; keywords="a article aside body br button canvas code div footer form h1 h2 h3 head header html img input label li link main meta nav ol option p pre script section select span style table td textarea th title tr ul";
    } else if (QStringList{"xml","svg","qrc","ui","xaml"}.contains(extension)) { lexer="xml"; language_="XML"; }
    else if (QStringList{"css","scss","less"}.contains(extension)) { lexer="css"; language_="CSS"; }
    else if (QStringList{"md","markdown"}.contains(extension)) { lexer="markdown"; language_="Markdown"; }
    else if (QStringList{"yaml","yml"}.contains(extension)) { lexer="yaml"; language_="YAML"; keywords="true false null yes no on off"; }
    else if (extension=="toml") { lexer="toml"; language_="TOML"; }
    else if (QStringList{"ini","cfg","conf","properties","editorconfig"}.contains(extension) || name==".gitignore") { lexer="props"; language_="Config"; }
    else if (QStringList{"sh","bash","zsh"}.contains(extension) || name==".bashrc") { lexer="bash"; language_="Shell"; keywords="if then else elif fi case esac for do done while until function in select time coproc echo exit export local read readonly return set shift source test trap unset"; }
    else if (QStringList{"ps1","psm1","psd1"}.contains(extension)) { lexer="powershell"; language_="PowerShell"; keywords="begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function if in param process return switch throw trap try until using var while workflow"; }
    else if (QStringList{"bat","cmd"}.contains(extension)) { lexer="batch"; language_="Batch"; keywords="assoc call cd chdir cls color copy date del dir do echo else endlocal erase exit for ftype goto if in md mkdir mklink move not path pause popd prompt pushd rd rem ren rename rmdir set setlocal shift start time title type ver verify vol"; }
    else if (extension=="sql") { lexer="sql"; language_="SQL"; keywords="add all alter and any as asc begin between by case check column commit constraint create database default delete desc distinct drop else end exists false foreign from full group having in index inner insert into is join key left like limit not null on or order outer primary references right rollback select set table then true union unique update values view when where with"; }
    else if (extension=="lua") { lexer="lua"; language_="Lua"; keywords="and break do else elseif end false for function goto if in local nil not or repeat return then true until while"; }
    else if (name=="cmakelists.txt" || extension=="cmake") { lexer="cmake"; language_="CMake"; keywords="add_executable add_library add_subdirectory cmake_minimum_required find_package foreach function if include message project set target_compile_definitions target_include_directories target_link_libraries endif endforeach endfunction"; }
    else if (extension=="diff" || extension=="patch") { lexer="diff"; language_="Diff"; }
    if (large) { lexer="null"; language_ += QStringLiteral(" · 大文件模式"); }
    lexer_=lexer;
    send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(CreateLexer(lexer.constData())));
    send(SCI_STYLECLEARALL);
    commentStyles_.clear();
    sends(SCI_SETKEYWORDS,0,keywords.constData());
    sends(SCI_SETKEYWORDS,1,extra.constData());
    sends(SCI_SETPROPERTY,reinterpret_cast<uptr_t>("fold"),"1");
    sends(SCI_SETPROPERTY,reinterpret_cast<uptr_t>("lexer.cpp.track.preprocessor"),"0");
    send(SCI_SETMARGINWIDTHN,1,large || lexer=="null" ? 0 : 16);
    applyColors();
    applyTypography();
    send(SCI_COLOURISE,0,-1);
}


void CodeEditor::setColorTheme(const QString& id) {
    colorTheme_=Theme::normalizedTheme(id);
    applyColors();
    viewport()->update();
}

void CodeEditor::applyColors() {
    const auto& p=Theme::colors(colorTheme_);
    for(int style=0;style<256;++style) {
        send(SCI_STYLESETFORE,style,rgb(p.text));send(SCI_STYLESETBACK,style,rgb(p.background));
    }
    send(SCI_SETCARETFORE,rgb(p.accent));send(SCI_SETCARETLINEBACK,rgb(p.elevated));
    send(SCI_SETSELBACK,1,rgb(p.selection));send(SCI_SETSELFORE,1,rgb(p.text));
    // Search and settings move focus away from the editor. Keep that selection
    // readable instead of falling back to Scintilla's fixed gray background.
    for(int element:{SC_ELEMENT_SELECTION_INACTIVE_BACK,SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK})
        send(SCI_SETELEMENTCOLOUR,element,0xff000000U|rgb(p.selection));
    for(int element:{SC_ELEMENT_SELECTION_INACTIVE_TEXT,SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_TEXT})
        send(SCI_SETELEMENTCOLOUR,element,0xff000000U|rgb(p.text));
    send(SCI_SETFOLDMARGINCOLOUR,1,rgb(p.background));send(SCI_SETFOLDMARGINHICOLOUR,1,rgb(p.background));
    for(int marker=SC_MARKNUM_FOLDEREND;marker<=SC_MARKNUM_FOLDEROPEN;++marker) {
        send(SCI_MARKERSETFORE,marker,rgb(p.muted));send(SCI_MARKERSETBACK,marker,rgb(p.background));
    }
    send(SCI_INDICSETFORE,searchIndicator,rgb(p.number));
    send(SCI_INDICSETALPHA,searchIndicator,p.dark?55:45);
    commentStyles_.clear();
    auto color = [this](std::initializer_list<int> styles, const QColor& value, bool italic=false) {
        for (int s : styles) {
            send(SCI_STYLESETFORE,s,rgb(value));
            if (italic) commentStyles_.insert(s);
        }
    };
    const auto& comment=p.comment;const auto& purple=p.keyword;const auto& green=p.string;
    const auto& gold=p.number;const auto& blue=p.type;const auto& red=p.preprocessor;
    if (lexer_=="cpp") {
        color({1,2,3,15,17,18,23,24},comment,true); color({4},gold); color({5},purple); color({6,7,12,13,14,20,21,22},green); color({9},red); color({16,19},blue); color({10},p.text);
    } else if (lexer_=="python") {
        color({1,12},comment,true); color({2},gold); color({3,4,6,7,13,16,17,18,19},green); color({5},purple); color({8,9,14,15},blue);
    } else if (lexer_=="json") {
        color({SCE_JSON_NUMBER},gold); color({SCE_JSON_STRING},green); color({SCE_JSON_PROPERTYNAME},blue); color({SCE_JSON_KEYWORD,SCE_JSON_LDKEYWORD},purple); color({SCE_JSON_LINECOMMENT,SCE_JSON_BLOCKCOMMENT},comment,true);
        sends(SCI_SETPROPERTY,reinterpret_cast<uptr_t>("lexer.json.allow.comments"),"1");
    } else if (lexer_=="rust") {
        color({1,2,3,4},comment,true); color({5},gold); color({6,7,8,9,10,11,12},purple); color({13,14,15,21,22,23},green); color({16},blue);
    } else if (lexer_=="hypertext" || lexer_=="xml") {
        color({1,2,11,12,13},red); color({3,4},gold); color({5},gold); color({6,7,19},green); color({9,20,29,30},comment,true); color({10},purple);
        color({SCE_HJ_COMMENT,SCE_HJ_COMMENTLINE,SCE_HJ_COMMENTDOC},comment,true); color({SCE_HJ_KEYWORD},purple); color({SCE_HJ_DOUBLESTRING,SCE_HJ_SINGLESTRING,SCE_HJ_REGEX},green); color({SCE_HJ_NUMBER},gold);
    } else if (lexer_=="css") { color({1,2,3,10,18},purple); color({6,7,15,19},blue); color({8,13,14},green); color({9},comment,true); }
    else if (lexer_=="markdown") {
        color({SCE_MARKDOWN_HEADER1,SCE_MARKDOWN_HEADER2,SCE_MARKDOWN_HEADER3,SCE_MARKDOWN_HEADER4,SCE_MARKDOWN_HEADER5,SCE_MARKDOWN_HEADER6},blue);
        color({SCE_MARKDOWN_CODE,SCE_MARKDOWN_CODE2,SCE_MARKDOWN_CODEBK},green); color({SCE_MARKDOWN_LINK},purple); color({SCE_MARKDOWN_BLOCKQUOTE},comment,true); color({SCE_MARKDOWN_STRONG1,SCE_MARKDOWN_STRONG2},gold);
    } else if (lexer_=="yaml") { color({1},comment,true); color({2},blue); color({3},purple); color({4},gold); color({5,6,7},green); }
    else if (lexer_=="props") { color({1},comment,true); color({2},purple); color({5},blue); color({4},green); }
    else if (lexer_=="toml") { color({1},comment,true); color({3,5},purple); color({4,14},gold); color({6},blue); color({9,10,11,12},green); }
    else if (lexer_=="bash") { color({2},comment,true); color({3},gold); color({4},purple); color({5,6},green); color({9,10,11},blue); }
    else if (lexer_=="powershell") { color({1,13,16},comment,true); color({2,3,14,15},green); color({4},gold); color({5,9,10},blue); color({8},purple); }
    else if (lexer_=="batch") { color({1},comment,true); color({2},purple); color({3,6},blue); color({4,5},green); }
    else if (lexer_=="sql") { color({1,2,3,13,15,17,18},comment,true); color({4},gold); color({5,16},purple); color({6,7},green); }
    else if (lexer_=="lua") { color({1,2,3},comment,true); color({4},gold); color({5},purple); color({6,7,8,12},green); color({13,14,15},blue); }
    else if (lexer_=="cmake") { color({1},comment,true); color({2,3,4},green); color({5,6,7,11,12},purple); color({8,13},blue); }
    else if (lexer_=="diff") { color({1,2,3},comment); color({5},p.preprocessor); color({6},green); color({4},blue); }

    color({STYLE_LINENUMBER},p.subtle);color({STYLE_INDENTGUIDE},p.border);
    color({STYLE_BRACELIGHT},p.accent);send(SCI_STYLESETBACK,STYLE_BRACELIGHT,rgb(p.selection));
    color({STYLE_BRACEBAD},p.error);send(SCI_STYLESETBACK,STYLE_BRACEBAD,rgb(p.elevated));
    send(SCI_MARKERSETFORE,1,rgb(p.accent));send(SCI_MARKERSETBACK,1,rgb(p.accent));
}

QVector<qint64> CodeEditor::bookmarks() const {
    QVector<qint64> result;auto line=send(SCI_MARKERNEXT,0,1<<1);
    while(line>=0){result.append(line);line=send(SCI_MARKERNEXT,line+1,1<<1);}return result;
}
void CodeEditor::setBookmarks(const QVector<qint64>& lines) {
    auto next=lines;std::sort(next.begin(),next.end());next.erase(std::unique(next.begin(),next.end()),next.end());
    const auto limit=send(SCI_GETLINECOUNT);next.erase(std::remove_if(next.begin(),next.end(),[limit](auto line){return line<0 || line>=limit;}),next.end());if(next.size()>1000)next.resize(1000);
    if(next==bookmarks())return;send(SCI_MARKERDELETEALL,1);for(auto line:next)send(SCI_MARKERADD,line,1);emit bookmarksChanged();
}
void CodeEditor::toggleBookmark(qint64 line) {
    if(line<0)line=send(SCI_LINEFROMPOSITION,send(SCI_GETCURRENTPOS));
    if(line>=send(SCI_GETLINECOUNT))return;
    if(send(SCI_MARKERGET,line)&(1<<1))send(SCI_MARKERDELETE,line,1);
    else {if(bookmarks().size()>=1000){QToolTip::showText(mapToGlobal(QPoint(20,20)),QStringLiteral("每个文件最多保存 1,000 个书签。"),this);return;}send(SCI_MARKERADD,line,1);}
    emit bookmarksChanged();
}
bool CodeEditor::nextBookmark(bool backwards) {
    const auto lines=bookmarks();if(lines.isEmpty())return false;
    const auto current=send(SCI_LINEFROMPOSITION,send(SCI_GETCURRENTPOS));qint64 next=backwards?lines.last():lines.first();
    if(backwards){for(auto it=lines.crbegin();it!=lines.crend();++it)if(*it<current){next=*it;break;}}
    else for(auto line:lines)if(line>current){next=line;break;}
    goToLine(int(next)+1);return true;
}

void CodeEditor::clearSearch() {
    if(regexControl_)regexControl_->canceled.store(true);regexControl_.reset();++searchGeneration_;
    regexBusy_=false;regexSnapshot_.clear();searchError_.clear();stopped_=false;lastRegexMatch_.reset();
    searchTimer_.stop(); query_.clear(); matches_.clear(); counting_=false; capped_=false; navigating_=false;
    send(SCI_SETINDICATORCURRENT,searchIndicator);
    send(SCI_INDICATORCLEARRANGE,0,send(SCI_GETLENGTH));
    emit searchChanged();
}

void CodeEditor::setSearch(const QString& query, bool caseSensitive, bool wholeWord,bool regex,std::optional<SearchMatch> scope,bool navigate) {
    clearSearch();
    if (query.isEmpty()) return;
    query_=query.toUtf8();
    regex_=regex;regexAutoNavigate_=navigate;searchInSelection_=scope.has_value();
    searchStart_=scope?std::clamp<qint64>(scope->first,0,send(SCI_GETLENGTH)):0;
    searchEnd_=scope?std::clamp<qint64>(scope->second,searchStart_,send(SCI_GETLENGTH)):send(SCI_GETLENGTH);
    searchFlags_=(caseSensitive ? SCFIND_MATCHCASE : 0) | (wholeWord ? SCFIND_WHOLEWORD : 0);
    if(regex_) {
        regexCaret_=send(SCI_GETCURRENTPOS);regexAnchor_=send(SCI_GETANCHOR);regexBusy_=true;
        try {regexSnapshot_=QByteArray(searchEnd_-searchStart_+1,Qt::Uninitialized);}catch(...){regexBusy_=false;searchError_=QStringLiteral("内存不足，无法搜索。");emit searchChanged();return;}
        const auto generation=searchGeneration_;QTimer::singleShot(0,this,[this,generation]{copyRegexSnapshot(generation,0);});emit searchChanged();return;
    }
    scanAt_=searchStart_; counting_=true;
    if(navigate)beginNavigation(false,true);
    searchTimer_.start();
}

sptr_t CodeEditor::searchChunk(sptr_t& cursor,sptr_t limit,bool backwards,bool& done) {
    // Overlap bounds UTF-8 case-fold expansion as well as matches spanning a chunk.
    const sptr_t overlap=query_.size()*16+4;
    const sptr_t step=qMax<sptr_t>(512*1024,overlap*2);
    const auto boundary=backwards?qMax(limit,cursor-step):qMin(limit,cursor+step);
    const auto end=backwards?boundary:qMin(limit,boundary+overlap);
    send(SCI_SETSEARCHFLAGS,searchFlags_);
    send(SCI_SETTARGETRANGE,cursor,end);
    const auto pos=send(SCI_SEARCHINTARGET,query_.size(),reinterpret_cast<sptr_t>(query_.constData()));
    done=end==limit;
    if(pos<0 && !done) {
        const auto next=backwards?boundary+overlap:boundary;
        cursor=send(SCI_POSITIONAFTER,send(SCI_POSITIONBEFORE,next));
    }
    return pos;
}

void CodeEditor::countMatches() {
    QElapsedTimer timer; timer.start();
    if(navigating_)advanceNavigation();
    const auto length=searchEnd_;
    send(SCI_SETINDICATORCURRENT,searchIndicator);
    while (counting_ && timer.elapsed()<8) {
        bool done=false;
        const auto pos=searchChunk(scanAt_,length,false,done);
        if(pos<0) { if(done)counting_=false; }
        else {
            const auto end=send(SCI_GETTARGETEND);
            matches_.append({pos,end});send(SCI_INDICATORFILLRANGE,pos,end-pos);
            scanAt_=end>pos?end:send(SCI_POSITIONAFTER,pos);
            if(matches_.size()>=10000){capped_=true;counting_=false;}
            if(scanAt_>=length)counting_=false;
        }
    }
    if(!searchBusy())searchTimer_.stop();
    emit searchChanged();
}

bool CodeEditor::findNext(bool backwards) {
    if(stopped_)return false;
    if(query_.isEmpty() || (!counting_ && matches_.isEmpty()))return false;
    if(regex_){if(regexBusy_)return false;runRegex(backwards?RegexMode::Previous:RegexMode::Next,false);return true;}
    return beginNavigation(backwards,false);
}

bool CodeEditor::beginNavigation(bool backwards,bool includeSelection) {
    navigationBackwards_=backwards;navigationWrapped_=false;navigationFound_=false;
    navigationCaret_=send(SCI_GETCURRENTPOS);navigationAnchor_=send(SCI_GETANCHOR);
    navigationOrigin_=qBound<sptr_t>(searchStart_,send(backwards || includeSelection?SCI_GETSELECTIONSTART:SCI_GETSELECTIONEND),searchEnd_);
    navigationAt_=navigationOrigin_;navigationLimit_=backwards?searchStart_:searchEnd_;navigating_=true;
    advanceNavigation();
    if(navigating_)searchTimer_.start();
    return navigating_ || navigationFound_;
}

void CodeEditor::advanceNavigation() {
    QElapsedTimer timer;timer.start();
    const auto length=searchEnd_;
    do {
        bool done=false;
        const auto pos=searchChunk(navigationAt_,navigationLimit_,navigationBackwards_,done);
        if(pos>=0) {
            const auto end=send(SCI_GETTARGETEND);navigating_=false;navigationFound_=true;
            send(SCI_ENSUREVISIBLEENFORCEPOLICY,send(SCI_LINEFROMPOSITION,pos));
            send(SCI_SETSEL,pos,end);send(SCI_SCROLLCARET);emit searchChanged();return;
        }
        if(done) {
            if(navigationWrapped_ || navigationOrigin_==(navigationBackwards_?length:searchStart_)){navigating_=false;return;}
            navigationWrapped_=true;navigationAt_=navigationBackwards_?length:searchStart_;
            navigationLimit_=navigationBackwards_?navigationOrigin_:length;
        }
    } while(timer.elapsed()<4);
}

QString CodeEditor::matchSummary() const {
    if(!searchError_.isEmpty())return searchError_;
    if(stopped_)return QStringLiteral("已取消 · %1 项").arg(matches_.size());
    if (query_.isEmpty()) return QStringLiteral("输入关键词");
    if (matches_.isEmpty()) return searchBusy() ? QStringLiteral("搜索中…") : QStringLiteral("无结果");
    const auto position=send(SCI_GETSELECTIONSTART);
    auto found=std::lower_bound(matches_.cbegin(),matches_.cend(),position,[](const auto& m, sptr_t p){return m.first<p;});
    const int index=found!=matches_.cend() && found->first==position ? int(found-matches_.cbegin())+1 : 0;
    return QStringLiteral("%1 / %2%3").arg(index ? QString::number(index) : "—").arg(matches_.size()).arg(capped_ || counting_ ? "+" : "");
}

QString CodeEditor::selectedText() const {
    const auto length=send(SCI_GETSELTEXT);
    if (length<=0 || length>4096) return {};
    QByteArray bytes(length+1,'\0');
    send(SCI_GETSELTEXT,0,reinterpret_cast<sptr_t>(bytes.data()));
    return QString::fromUtf8(bytes.constData(),length);
}

QString CodeEditor::positionSummary() const {
    const auto pos=send(SCI_GETCURRENTPOS);
    return QStringLiteral("行 %1，列 %2").arg(send(SCI_LINEFROMPOSITION,pos)+1).arg(send(SCI_GETCOLUMN,pos)+1);
}

void CodeEditor::goToLine(int line) { send(SCI_ENSUREVISIBLEENFORCEPOLICY,qMax(0,line-1)); send(SCI_GOTOLINE,qMax(0,line-1)); }
void CodeEditor::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    else event->ignore();
}
void CodeEditor::dropEvent(QDropEvent* event) {
    for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) emit fileDropped(url.toLocalFile());
    event->acceptProposedAction();
}
