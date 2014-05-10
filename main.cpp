#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <list>
#include <fstream>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Host.h>

#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/ABI.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/Utils.h>

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

#include <clang/Basic/Version.h>

#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/HeaderSearch.h>

#include <clang/Sema/Scope.h>
#include <clang/Sema/Sema.h>

using namespace std;

class MyASTVisitor : public clang::RecursiveASTVisitor<MyASTVisitor>
{
	public:
		MyASTVisitor(clang::CompilerInstance& ci)
			:
			ci(ci),
			lastId(0),
			graphOut("graph.dot")
		{
			graphOut << "digraph prof {" << endl;
			// graphOut << "\tnode [shape=box];" << endl;
		}

		virtual ~MyASTVisitor()
		{
			for (auto p : idMap)
			{
				graphOut << "_id_" << p.second << "[label=\"" << p.first << "\"]" << endl;
			}
			graphOut << "}" << endl;
			graphOut.close();
		}

		bool TraverseDecl(clang::Decl* d)
		{
			declStack.push_back(d);
			bool res = clang::RecursiveASTVisitor<MyASTVisitor>::TraverseDecl(d);
			declStack.pop_back();

			return res;
		}

		bool VisitStmt(clang::Stmt* stmt)
		{
		    string directCalleeName;
            unsigned int srcStart, srcEnd;

            clang::FileID fid = ci.getSourceManager().getMainFileID();
            srcStart = ci.getSourceManager().getLocForStartOfFile(fid).getRawEncoding();
            srcEnd = ci.getSourceManager().getLocForEndOfFile(fid).getRawEncoding();

		    if (clang::isa<clang::CXXMemberCallExpr>(stmt))
            {
                clang::CXXMemberCallExpr* c = (clang::CXXMemberCallExpr*)stmt;
                clang::CXXRecordDecl* rd = c->getRecordDecl();
                clang::CXXMethodDecl* md = c->getMethodDecl();
                if (!rd || !md)
                    return true;

                if (c->getSourceRange().getBegin().getRawEncoding() < srcStart
                    || c->getSourceRange().getEnd().getRawEncoding() > srcEnd)
                    return true;

                stringstream ss;
                ss << rd->getNameAsString() << "::" << md->getNameAsString();
                directCalleeName = ss.str();
            }
			else if (clang::isa<clang::CallExpr>(stmt))
			{
                clang::CallExpr* c = (clang::CallExpr*)stmt;
                clang::FunctionDecl* fn = c->getDirectCallee();
                if (!fn)
                    return true;

                if (c->getSourceRange().getBegin().getRawEncoding() < srcStart
                    || c->getSourceRange().getEnd().getRawEncoding() > srcEnd)
                    return true;

                directCalleeName = fn->getNameInfo().getAsString();
			}
			else
            {
                return true;
            }

            for (auto it = declStack.rbegin(); it != declStack.rend(); ++it)
            {
                clang::Decl* d = *it;
                if (clang::isa<clang::FunctionDecl>(d)
                        && clang::isa<clang::NamedDecl>(d))
                {
                    clang::NamedDecl* n = (clang::NamedDecl*)d;

                    stringstream ssCxn;

                    ssCxn << "_id_" << getId(n->getNameAsString()) << " -> " << "_id_" << getId(directCalleeName);
                    string sCxn = ssCxn.str();

                    if (cxnSet.find(sCxn) == cxnSet.end())
                    {
                        cxnSet.insert(sCxn);
                        graphOut << "\t" << sCxn << endl;
                        cout << n->getNameAsString() << " -> " << directCalleeName << endl;
                    }

                    break;
                }
            }
			return true;
		}

		bool VisitFunctionDecl(clang::Decl* decl)
		{
			// cout << "decl" << endl;
			return true;
		}

		int getId(const string& str)
		{
		    auto it = idMap.find(str);
		    if (it != idMap.end())
                return it->second;
            idMap.insert({str, ++lastId});
			return lastId;
		}

	private:
		clang::CompilerInstance& ci;
		map<string, int> idMap;
		set<string> cxnSet;
		int lastId;
		list<clang::Decl*> declStack;
		ofstream graphOut;
};

class MyASTConsumer : public clang::ASTConsumer
{
	public:
		MyASTConsumer(clang::CompilerInstance& ci)
			:
			visitor(ci)
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
	ci.getInvocation().setLangDefaults(ci.getLangOpts(), clang::IK_CXX, clang::LangStandard::lang_cxx11);

	ci.createDiagnostics();

	llvm::IntrusiveRefCntPtr<clang::TargetOptions> pto(new clang::TargetOptions());
	pto->Triple = llvm::sys::getDefaultTargetTriple();
	clang::TargetInfo *pti = clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), pto.getPtr());
	ci.setTarget(pti);

	ci.createFileManager();
	ci.createSourceManager(ci.getFileManager());

	ci.getHeaderSearchOpts().ResourceDir = LLVM_PREFIX "/lib/clang/" CLANG_VERSION_STRING;
	cout << "Resorce dir: " << ci.getHeaderSearchOpts().ResourceDir << endl;

	ci.getHeaderSearchOpts().AddPath(LLVM_PREFIX "/lib/clang/" CLANG_VERSION_STRING "/include", clang::frontend::CXXSystem, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4", clang::frontend::CXXSystem, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4/x86_64-pc-linux-gnu", clang::frontend::CXXSystem, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/lib/gcc/x86_64-pc-linux-gnu/4.8.2/include/g++-v4/backward", clang::frontend::CXXSystem, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/local/include", clang::frontend::CXXSystem, false, false);
	ci.getHeaderSearchOpts().AddPath("/usr/include", clang::frontend::CXXSystem, false, false);

	ci.getPreprocessorOpts().addMacroDef("__STDC_LIMIT_MACROS");
	ci.getPreprocessorOpts().addMacroDef("__STDC_CONSTANT_MACROS");

	ci.createPreprocessor();
	ci.getPreprocessor().getBuiltinInfo().InitializeBuiltins(ci.getPreprocessor().getIdentifierTable(), ci.getLangOpts());

	// ci.getPreprocessor().addPPCallbacks(new MyPPCallbacks());
	// ci.getPreprocessor().addCommentHandler(new MyCommentHandler()); // @TODO: not yet, prints unnecessary stuff
	// ci.getPreprocessorOpts().UsePredefines = false;

	clang::ASTConsumer* astConsumer = new MyASTConsumer(ci);
	ci.setASTConsumer(astConsumer);

	// ci.getDiagnosticOpts().Warnings.clear();
	ci.getDiagnosticOpts().Warnings.push_back("fatal-errors");

	ci.createASTContext();
	ci.createSema(clang::TU_Complete, nullptr);

	const clang::FileEntry* entry = ci.getFileManager().getFile("main.cpp");
	ci.getSourceManager().createMainFileID(entry);

	ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(), &ci.getPreprocessor());
	clang::ParseAST(ci.getSema());
	ci.getDiagnosticClient().EndSourceFile();

	return 0;
}
