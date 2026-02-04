#ifndef GSPP_SEMANTIC_H
#define GSPP_SEMANTIC_H

#include "ast.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace gspp {

struct VarSymbol {
    std::string name;
    Type type;
    int frameOffset = 0;  // negative offset from RBP
    bool isParam = false;
};

struct FuncSymbol {
    std::string name;
    std::string mangledName;
    std::string ns;
    Type returnType;
    std::vector<Type> paramTypes;
    const FuncDecl* decl = nullptr;
    bool isMethod = false;
    std::unordered_map<std::string, VarSymbol> locals;  // name -> symbol (frame offset etc.)
};

struct StructDef {
    std::string name;
    std::string mangledName;
    std::vector<std::pair<std::string, Type>> members;
    std::unordered_map<std::string, size_t> memberIndex;
    std::unordered_map<std::string, FuncSymbol> methods;
    size_t sizeBytes = 0;  // for codegen
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(Program* program);
    void addModule(const std::string& name, Program* prog);
    bool analyze();
    const std::vector<std::string>& errors() const { return errors_; }
    StructDef* getStruct(const std::string& name, const std::string& ns = "");
    FuncSymbol* getFunc(const std::string& name, const std::string& ns = "");
    const std::unordered_map<std::string, StructDef>& structs() const { return structs_; }
    const std::unordered_map<std::string, FuncSymbol>& functions() const { return functions_; }
    const std::unordered_map<std::string, std::unordered_map<std::string, FuncSymbol>>& moduleFunctions() const { return moduleFunctions_; }

private:
    void analyzeProgram();
    void analyzeStruct(const StructDecl& s);
    void analyzeMethod(const std::string& structName, const FuncDecl& f);
    void analyzeFunc(const FuncDecl& f);
    void analyzeStmt(Stmt* stmt);
    Type analyzeExpr(Expr* expr);
    Type resolveType(const Type& t);
    void pushScope();
    void popScope();
    void addVar(const std::string& name, const Type& type, bool isParam = false);
    VarSymbol* lookupVar(const std::string& name);
    std::string typeName(const Type& t);
    std::string mangleGenericName(const std::string& name, const std::vector<Type>& args);
    Type substitute(const Type& t, const std::unordered_map<std::string, Type>& subs);
    std::unique_ptr<Expr> substituteExpr(const Expr* e, const std::unordered_map<std::string, Type>& subs);
    std::unique_ptr<Stmt> substituteStmt(const Stmt* s, const std::unordered_map<std::string, Type>& subs);
    void instantiateStruct(const std::string& name, const std::string& ns, const std::vector<Type>& args);
    void instantiateFunc(const std::string& name, const std::string& ns, const std::vector<Type>& args);
    void error(const std::string& msg, SourceLoc loc);

    Program* program_;
    std::unordered_map<std::string, Program*> modules_;
    std::unordered_map<std::string, std::unordered_map<std::string, StructDef>> moduleStructs_;
    std::unordered_map<std::string, std::unordered_map<std::string, FuncSymbol>> moduleFunctions_;

    std::unordered_map<std::string, const StructDecl*> structTemplates_;
    std::unordered_map<std::string, const FuncDecl*> funcTemplates_;
    std::unordered_map<std::string, std::unordered_map<std::string, const StructDecl*>> moduleStructTemplates_;
    std::unordered_map<std::string, std::unordered_map<std::string, const FuncDecl*>> moduleFuncTemplates_;

    std::vector<std::unique_ptr<StructDecl>> instantiatedStructDecls_;
    std::vector<std::unique_ptr<FuncDecl>> instantiatedFuncDecls_;

    std::unordered_map<std::string, StructDef> structs_;
    std::unordered_map<std::string, FuncSymbol> functions_;
    std::vector<std::unordered_map<std::string, VarSymbol>> scopes_;
    std::vector<std::string> errors_;
    FuncDecl* currentFunc_ = nullptr;
    FuncSymbol* currentFuncSymbol_ = nullptr;
    int nextFrameOffset_ = 0;
    std::string currentNamespace_;
};

} // namespace gspp

#endif
