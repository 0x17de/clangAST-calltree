#include <iostream>
#include <stdexcept>
#include <list>

#include <llvm/Support/Host.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/ABI.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/Utils.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Decl.h>

#include <clang/Basic/Version.h>

#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/HeaderSearch.h>

#include <clang/Sema/Scope.h>
#include <clang/Sema/Sema.h>

using namespace std;

class MyASTVisitor : public clang::RecursiveASTVisitor<MyASTVisitor>
{
	public:
		MyASTVisitor()
		{
		}

		bool TraverseDecl(clang::Decl* D)
		{
			if (!D)
				return false;
			declList.push_back(D);
			// cout << "+DECL: " << D->getDeclKindName() << endl;
			bool result = clang::RecursiveASTVisitor<MyASTVisitor>::TraverseDecl(D);
			// cout << "-DECL: " << D->getDeclKindName() << endl;
			declList.pop_back();
			return result;
		}

		bool TraverseStmt(clang::Stmt* S)
		{
			if (!S)
				return false;
			// cout << "+STMT: " << S->getStmtClassName() << endl;
			bool result = clang::RecursiveASTVisitor<MyASTVisitor>::TraverseStmt(S);
			// cout << "-STMT: " << S->getStmtClassName() << endl;
			return result;
		}

		bool VisitStmt(clang::Stmt* s)
		{
			if (clang::isa<clang::CallExpr>(s))
			{
				for (auto it = declList.rbegin(); it != declList.rend(); ++it)
				{
					clang::Decl* d = *it;
					if (d->getKind() == clang::Decl::Function)
					{
						clang::NamedDecl* n = (clang::NamedDecl*)d;
						cout << "Call from: " << n->getNameAsString() << endl;
						break;
					}
				}

				clang::CallExpr* c = (clang::CallExpr*)s;
				cout << c->getDirectCallee()->getNameInfo().getName().getAsString();
				cout << "()";
				cout << endl;
			}
			return true;
		}

		bool VisitFunctionDecl(clang::Decl* d)
		{
			if (clang::isa<clang::NamedDecl>(d))
			{
				clang::NamedDecl* n = (clang::NamedDecl*)d;
				cout << "FunctionDecl: " << n->getNameAsString() << endl;
			}
			return true;
		}
	private:
		list<clang::Decl*> declList;
		list<clang::Stmt*> stmtList;
};

class MyASTConsumer : public clang::ASTConsumer
{
	public:
		MyASTConsumer()
		{
		}

		virtual ~MyASTConsumer()
		{
		}

		virtual bool HandleTopLevelDecl(clang::DeclGroupRef D) override
		{
			for (clang::DeclGroupRef::iterator it = D.begin(); it != D.end(); ++it)
			{
				visitor.TraverseDecl(*it);
			}
			return true;
		}

	private:
		MyASTVisitor visitor;
};

class MyCommentHandler : public clang::CommentHandler
{
	public:
		virtual bool HandleComment(clang::Preprocessor& pp, clang::SourceRange range) override
		{
			auto FID = pp.getSourceManager().getMainFileID();
			int start = range.getBegin().getRawEncoding();
			int end = range.getEnd().getRawEncoding();
			int length = end - start;
			auto bufferStart = pp.getSourceManager().getBuffer(FID)->getBufferStart();
			int fileOffset = pp.getSourceManager().getLocForStartOfFile(FID).getRawEncoding();

			const char* data = bufferStart + start - fileOffset;
			string str(data, length);
			cout << str << endl;

			return false;
		}
};

class MyPPCallbacks : public clang::PPCallbacks
{
    // @TODO: not possible in these callbacks: skip some module imports
    // InclusionDirective (SourceLocation HashLoc, const Token &IncludeTok, StringRef FileName, bool IsAngled, CharSourceRange FilenameRange, const FileEntry *File, StringRef SearchPath, StringRef RelativePath, const Module *Imported)
};

int main()
{
	clang::CompilerInstance ci;

	ci.createDiagnostics();

	llvm::IntrusiveRefCntPtr<clang::TargetOptions> pto(new clang::TargetOptions());
	pto->Triple = llvm::sys::getDefaultTargetTriple();
	clang::TargetInfo *pti = clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), pto.getPtr());
	ci.setTarget(pti);

	ci.createFileManager();
	ci.createSourceManager(ci.getFileManager());

	ci.getHeaderSearchOpts().ResourceDir = "/usr/lib/clang/" CLANG_VERSION_STRING;
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4/x86_64-pc-linux-gnu", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4/backward", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/clang/" CLANG_VERSION_STRING "/include", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/local/include", clang::frontend::System, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/include", clang::frontend::System, false, false);

	ci.createPreprocessor();

	// ci.getPreprocessor().addPPCallbacks(new MyPPCallbacks());
	ci.getPreprocessor().addCommentHandler(new MyCommentHandler());
	// ci.getPreprocessorOpts().UsePredefines = false;

	clang::ASTConsumer* astConsumer = new MyASTConsumer();
	ci.setASTConsumer(astConsumer);

	ci.createASTContext();
	// ci.createSema(clang::TU_Complete, nullptr);

	const clang::FileEntry* entry = ci.getFileManager().getFile("test.cpp");
	ci.getSourceManager().createMainFileID(entry);

	// clang::ParseAST(ci.getSema());
	ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(), &ci.getPreprocessor());
	clang::ParseAST(ci.getPreprocessor(), &ci.getASTConsumer(), ci.getASTContext());
	ci.getDiagnosticClient().EndSourceFile();

	return 0;
}
