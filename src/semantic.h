#ifndef GSPP_SEMANTIC_H
#define GSPP_SEMANTIC_H

#include "ast.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace gspp {

struct StructDef {
    std::string name;
    std::vector<std::pair<std::string, Type>> members;
    std::unordered_map<std::string, size_t> memberIndex;
    size_t sizeBytes = 0;  // for codegen
};

struct VarSymbol {
    std::string name;
    Type type;
    int frameOffset = 0;  // negative offset from RBP
    bool isParam = false;
};

struct FuncSymbol {
    std::string name;
    Type returnType;
    std::vector<Type> paramTypes;
    const FuncDecl* decl = nullptr;
    std::unordered_map<std::string, VarSymbol> locals;  // name -> symbol (frame offset etc.)
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(Program* program);
    bool analyze();
    const std::vector<std::string>& errors() const { return errors_; }
    StructDef* getStruct(const std::string& name);
    FuncSymbol* getFunc(const std::string& name);
    const std::unordered_map<std::string, StructDef>& structs() const { return structs_; }
    const std::unordered_map<std::string, FuncSymbol>& functions() const { return functions_; }

private:
    void analyzeProgram();
    void analyzeStruct(const StructDecl& s);
    void analyzeFunc(const FuncDecl& f);
    void analyzeStmt(Stmt* stmt);
    Type analyzeExpr(Expr* expr);
    Type resolveType(const Type& t);
    void pushScope();
    void popScope();
    void addVar(const std::string& name, const Type& type, bool isParam = false);
    VarSymbol* lookupVar(const std::string& name);
    void error(const std::string& msg, SourceLoc loc);

    Program* program_;
    std::unordered_map<std::string, StructDef> structs_;
    std::unordered_map<std::string, FuncSymbol> functions_;
    std::vector<std::unordered_map<std::string, VarSymbol>> scopes_;
    std::vector<std::string> errors_;
    FuncDecl* currentFunc_ = nullptr;
    FuncSymbol* currentFuncSymbol_ = nullptr;
    int nextFrameOffset_ = 0;
};

} // namespace gspp

#endif
