#include <iostream>
#include <clang/Lex/PPCallbacks.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <fstream>
#include <sstream>

using namespace std;

using namespace clang;

class MyPPCallbacks : public clang::PPCallbacks {
public:
    MyPPCallbacks(clang::Rewriter *R) : TheRewriter(R) {}

    virtual void InclusionDirective(SourceLocation HashLoc,
                                    const Token &IncludeTok,
                                    StringRef FileName,
                                    bool IsAngled,
                                    CharSourceRange FilenameRange,
                                    const FileEntry *File,
                                    StringRef SearchPath,
                                    StringRef RelativePath,
                                    const Module *Imported,
                                    SrcMgr::CharacteristicKind FileType) {
        if (FileName == "temp.h") {
            // Record the range of the #include directive.
            std::ifstream HeaderFile(FileName.str());
            std::stringstream HeaderContents;
            HeaderContents << HeaderFile.rdbuf();
            SourceRange range(HashLoc, FilenameRange.getAsRange().getEnd());
            TheRewriter->ReplaceText(range, HeaderContents.str());
        }
    }

    const std::vector<clang::SourceRange> &getIncludeRanges() const {
        return IncludeRanges;
    }

    const std::vector<std::string> &getIncludedFilesContents() const {
        return IncludedFilesContents;
    }

private:
    clang::Rewriter *TheRewriter;
    std::vector<clang::SourceRange> IncludeRanges;
    std::vector<std::string> IncludedFilesContents;
};

class FindNamedClassVisitor
        : public RecursiveASTVisitor<FindNamedClassVisitor> {
public:
    explicit FindNamedClassVisitor(ASTContext *Context)
            : Context(Context) {}

    bool VisitTypeDecl(TypeDecl *decl) {
//        std::cerr << decl->getNameAsString() << std::endl;
        return true;
    }

    bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
        if (Declaration->getQualifiedNameAsString() == "n::m::C") {
            FullSourceLoc FullLocation = Context->getFullLoc(Declaration->getBeginLoc());
            if (FullLocation.isValid())
                llvm::outs() << "Found declaration at "
                             << FullLocation.getSpellingLineNumber() << ":"
                             << FullLocation.getSpellingColumnNumber() << "\n";
        }
        return true;
    }

private:
    ASTContext *Context;
};

class MyASTConsumer : public clang::ASTConsumer {
public:
    MyASTConsumer(clang::CompilerInstance &Instance, clang::Rewriter *r)
            : R(r), Instance(Instance), Visitor(&Instance.getASTContext()) {}

    virtual void Initialize(clang::ASTContext &Context) {
        Instance.getPreprocessor().addPPCallbacks(
                std::make_unique<MyPPCallbacks>(R));
    }

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {

//        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    clang::Rewriter *R;
    FindNamedClassVisitor Visitor;
    clang::CompilerInstance &Instance;
};

class DeadCodeVisitor : public clang::RecursiveASTVisitor<DeadCodeVisitor> {
public:
    DeadCodeVisitor(clang::Rewriter *R, clang::ASTContext &Context) : Context(Context), TheRewriter(R) {}

    bool isUsed(clang::CXXRecordDecl *Declaration) {
        // Here, you should implement a way to check if the class is used.
        // This can be complex depending on what exactly you consider "usage" of a class.
        // For simplicity, let's consider a class is used if it's ever instantiated.
        for (auto it = Context.getTranslationUnitDecl()->decls_begin(),
                     end = Context.getTranslationUnitDecl()->decls_end();
             it != end; ++it) {
            if (clang::VarDecl *Var = llvm::dyn_cast<clang::VarDecl>(*it)) {
                if (Var->getType().getCanonicalType()->getAsCXXRecordDecl() == Declaration) {
                    return true;
                }
            }
        }

        return false;
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *Declaration) {
        auto name = Declaration->getNameAsString();
        if (!Declaration->isThisDeclarationADefinition()) {
            // This is a declaration, not a definition.
            return true;
        }

        clang::SourceLocation Loc = Declaration->getLocation();

        auto ta = Context.getSourceManager().isInSystemHeader(Loc);
        auto tb = !isFromTargetFile(Declaration->getLocation());

        if (ta || tb) {
            // Don't process system headers to avoid unnecessary work.
            return true;
        }

        if (!isUsed(Declaration)) {
            TheRewriter->RemoveText(Declaration->getSourceRange());
        }

        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl *FD) {
        auto name = FD->getNameAsString();
        if (name != "main" &&
            isFromTargetFile(FD->getLocation()) &&
            FD->isThisDeclarationADefinition() &&
            FD->hasBody() &&
            !FD->isUsed()) {
            TheRewriter->RemoveText(FD->getSourceRange());
        }
        return true;
    }

    bool isFromTargetFile(clang::SourceLocation Loc) {
        clang::SourceManager &SM = Context.getSourceManager();
        llvm::StringRef File = SM.getFilename(Loc);
        SmallVector<llvm::StringRef> vec(15);
        SplitString(File, vec, "/");
        return vec.rbegin()->str() == "test.cpp" || vec.rbegin()->str() == "temp.h";
    }


private:
    clang::ASTContext &Context;
    clang::Rewriter *TheRewriter;
};

class MyASTConsumer2 : public clang::ASTConsumer {
public:
    MyASTConsumer2(clang::CompilerInstance &Instance, clang::Rewriter *r)
            : R(r), Instance(Instance), Visitor(r, Instance.getASTContext()) {}

    void HandleTranslationUnit(clang::ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    clang::Rewriter *R;
    DeadCodeVisitor Visitor;
    clang::CompilerInstance &Instance;
};

class MyFrontendAction : public clang::ASTFrontendAction {
public:

    ~MyFrontendAction() override {
        rewriter->overwriteChangedFiles();
    }

//    virtual void EndSourceFileAction() {
//        cerr << "got one" << endl;
//    }

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &Instance, llvm::StringRef) final {
        if (!rewriter) {
            rewriter = make_shared<clang::Rewriter>();
            rewriter->setSourceMgr(Instance.getSourceManager(), Instance.getLangOpts());
        }
        return std::make_unique<MyASTConsumer>(Instance, rewriter.get());
    }

    std::shared_ptr<clang::Rewriter> rewriter;
};

class MyFrontendAction2 : public clang::ASTFrontendAction {
public:

    ~MyFrontendAction2() override {
        rewriter->overwriteChangedFiles();
    }

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &Instance, llvm::StringRef) final {
        if (!rewriter) {
            rewriter = make_shared<clang::Rewriter>();
            rewriter->setSourceMgr(Instance.getSourceManager(), Instance.getLangOpts());
        }
        return std::make_unique<MyASTConsumer2>(Instance, rewriter.get());
    }

    std::shared_ptr<clang::Rewriter> rewriter;
};

static llvm::cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...\n");

static llvm::cl::OptionCategory MyToolCategory("my-tool options");

#include <string>

using namespace std;

int main(int argc, const char **argv) {

    clang::tooling::CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
    auto ls = OptionsParser.getCompilations().getAllCompileCommands();
    auto pathList = OptionsParser.getSourcePathList();

    clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                   pathList);
    int ec = Tool.run(clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());
    if (ec != 0) {
        return ec;
    }
    clang::tooling::ClangTool Tool2(OptionsParser.getCompilations(),
                                    pathList);
    return Tool2.run(clang::tooling::newFrontendActionFactory<MyFrontendAction2>().get());
}






