#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"


#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

static constexpr const char *BindNonVirtualDtor = "nonVirtualDtor";
static constexpr const char *BindMethodDecl = "methodDecl";
static constexpr const char *BindVarDecl = "VarDecl";

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>(BindNonVirtualDtor)) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>(BindMethodDecl);
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>(BindVarDecl)) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Dtor->getLocation()))
        return;

    unsigned LocHash = Dtor->getLocation().getRawEncoding();
    if (virtualDtorLocations.count(LocHash))
        return;
    virtualDtorLocations.insert(LocHash);

    Rewrite.InsertTextBefore(Dtor->getLocation(), "virtual ");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Added 'virtual' to destructor");
    Diag.Report(Dtor->getLocation(), DiagID);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Method->getLocation()))
        return;

    SourceLocation NameLoc = Method->getLocation();
    const char *Buf = SM.getCharacterData(NameLoc);

    int Offset = 0;
    int ParenDepth = 0;
    while (Buf[Offset] != '(')
        Offset++;
    ParenDepth = 1;
    Offset++;
    while (ParenDepth > 0) {
        if (Buf[Offset] == '(')
            ParenDepth++;
        else if (Buf[Offset] == ')')
            ParenDepth--;
        Offset++;
    }

    int InsertOffset = Offset;

    while (true) {
        while (std::isspace(static_cast<unsigned char>(Buf[Offset])))
            Offset++;

        if (Buf[Offset] == '{' || Buf[Offset] == '=' || Buf[Offset] == ';')
            break;

        if (Buf[Offset] == '/' && Buf[Offset + 1] == '*') {
            Offset += 2;
            while (!(Buf[Offset] == '*' && Buf[Offset + 1] == '/'))
                Offset++;
            Offset += 2;
            InsertOffset = Offset;
            continue;
        }

        if (Buf[Offset] == '(') {
            int Depth = 1;
            Offset++;
            while (Depth > 0) {
                if (Buf[Offset] == '(') Depth++;
                else if (Buf[Offset] == ')') Depth--;
                Offset++;
            }
            InsertOffset = Offset;
            continue;
        }

        Offset++;
        InsertOffset = Offset;
    }

    SourceLocation InsertLoc = NameLoc.getLocWithOffset(InsertOffset);
    Rewrite.InsertTextBefore(InsertLoc, " override");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Added 'override' to method");
    Diag.Report(Method->getLocation(), DiagID);
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(LoopVar->getLocation()))
        return;

    QualType Type = LoopVar->getType().getCanonicalType();
    if (Type->isBuiltinType())
        return;

    auto *TSI = LoopVar->getTypeSourceInfo();
    if (!TSI)
        return;

    TypeLoc TL = TSI->getTypeLoc();
    SourceLocation TypeEndLoc = Lexer::getLocForEndOfToken(TL.getEndLoc(), 0, SM, Rewrite.getLangOpts());

    Rewrite.InsertTextBefore(TypeEndLoc, "&");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Added '&' to range-for variable");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

// todo: ниже необходимо реализовать матчеры для поиска узлов AST
// note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto NvDtorMatcher() {
    return cxxRecordDecl(isDerivedFrom(
        cxxRecordDecl(has(cxxDestructorDecl(unless(isVirtual()), unless(isImplicit())).bind(BindNonVirtualDtor)))));
}

auto NoOverrideMatcher() {
    return cxxMethodDecl(isOverride(), unless(hasAttr(attr::Override)), unless(cxxDestructorDecl()))
        .bind(BindMethodDecl);
}

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(
        varDecl(hasType(qualType(isConstQualified())), unless(hasType(referenceType()))).bind(BindVarDecl)));
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}