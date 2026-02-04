#ifndef GSPP_CODEGEN_H
#define GSPP_CODEGEN_H

#include "ast.h"
#include "semantic.h"
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gspp {

class CodeGenerator {
public:
    CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit = true);
    bool generate();
    const std::vector<std::string>& errors() const { return errors_; }

private:
    void emitProgram();
    void emitFunc(const FuncSymbol& fs);
    void emitStmt(Stmt* stmt);
    void emitExpr(Expr* expr, const std::string& destReg, bool wantFloat = false);
    void emitExprToRax(Expr* expr);
    void emitExprToXmm0(Expr* expr);
    int getFrameSize();
    std::string getVarLocation(const std::string& name);
    int getTypeSize(const Type& t);
    StructDef* resolveStruct(const std::string& name, const std::string& ns);
    FuncSymbol* resolveFunc(const std::string& name, const std::string& ns);
    void emitProgramBody();
    void error(const std::string& msg, SourceLoc loc);

    Program* program_;
    SemanticAnalyzer* semantic_;
    std::ostream* out_;
    const FuncDecl* currentFunc_ = nullptr;
    std::unordered_map<std::string, VarSymbol> currentVars_;
    int frameSize_ = 0;
    std::vector<std::string> errors_;
    int labelCounter_ = 0;
    std::string nextLabel();
    bool use32Bit_ = true;  // if true, emit 32-bit x86 (cdecl); else x86-64 Windows
    bool isLinux_ = false;
    std::string currentNamespace_;
    std::unordered_map<std::string, std::string> stringPool_;
};

} // namespace gspp

#endif
