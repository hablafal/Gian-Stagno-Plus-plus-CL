#ifndef GSPP_CODEGEN_H
#define GSPP_CODEGEN_H

#include "ast.h"
#include "semantic.h"
#include <ostream>
#include <vector>
#include <string>
#include <unordered_map>

namespace gspp {

class CodeGenerator {
public:
    CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit);
    bool generate();
    const std::vector<std::string>& errors() const { return errors_; }

private:
    void emitProgram();
    void emitProgramBody();
    void emitFunc(const FuncSymbol& fs);
    void emitStmt(Stmt* stmt);
    void emitExpr(Expr* expr, const std::string& destReg, bool wantFloat);
    void emitExprToRax(Expr* expr);
    void emitExprToXmm0(Expr* expr);
    int getTypeSize(const Type& t);
    std::string getVarLocation(const std::string& name);
    int getFrameSize();
    std::string nextLabel();
    void error(const std::string& msg, SourceLoc loc);
    StructDef* resolveStruct(const std::string& name, const std::string& ns);
    FuncSymbol* resolveFunc(const std::string& name, const std::string& ns);

    Program* program_;
    SemanticAnalyzer* semantic_;
    std::ostream* out_;
    const FuncDecl* currentFunc_ = nullptr;
    std::vector<std::vector<Stmt*>> deferStack_;
    std::unordered_map<std::string, VarSymbol> currentVars_;
    int frameSize_ = 0;
    std::vector<std::string> errors_;
    int labelCounter_ = 0;
    bool use32Bit_ = false;
    bool isLinux_ = false;
    std::string currentNamespace_;
    std::unordered_map<std::string, std::string> stringPool_;
};

}

#endif
