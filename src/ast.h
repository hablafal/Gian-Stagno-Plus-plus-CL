#ifndef GSPP_AST_H
#define GSPP_AST_H

#include "common.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace gspp {

// Forward declarations
struct Type;
struct Expr;
struct Stmt;

struct Type {
    enum class Kind { Int, Float, Bool, StructRef, Pointer, Void, String, Char, TypeParam, List };
    Kind kind = Kind::Int;
    std::string structName;  // for StructRef or TypeParam name
    std::string ns;          // for StructRef
    std::vector<Type> typeArgs; // for generics
    std::unique_ptr<Type> ptrTo; // for Pointer
    SourceLoc loc;

    Type() = default;
    Type(Kind k) : kind(k) {}
    Type(const Type& other);
    Type& operator=(const Type& other);
};

struct Expr {
    enum class Kind {
        IntLit, FloatLit, BoolLit, StringLit, ListLit,
        Var, Binary, Unary, Call, Member, Cast,
        Deref, AddressOf, New, Delete, Index
    };
    Kind kind = Kind::IntLit;
    Type exprType;
    SourceLoc loc;

    int64_t intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    std::string ident;
    std::string ns; // namespace
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::string op;  // binary op or unary op
    std::vector<std::unique_ptr<Expr>> args;
    std::string member;
    std::unique_ptr<Type> targetType;  // for Cast

    static std::unique_ptr<Expr> makeIntLit(int64_t v, SourceLoc loc);
    static std::unique_ptr<Expr> makeFloatLit(double v, SourceLoc loc);
    static std::unique_ptr<Expr> makeBoolLit(bool v, SourceLoc loc);
    static std::unique_ptr<Expr> makeVar(const std::string& id, SourceLoc loc);
    static std::unique_ptr<Expr> makeBinary(std::unique_ptr<Expr> l, const std::string& op,
                                            std::unique_ptr<Expr> r, SourceLoc loc);
    static std::unique_ptr<Expr> makeUnary(const std::string& op, std::unique_ptr<Expr> operand, SourceLoc loc);
    static std::unique_ptr<Expr> makeCall(const std::string& id, std::vector<std::unique_ptr<Expr>> args, SourceLoc loc);
    static std::unique_ptr<Expr> makeMember(std::unique_ptr<Expr> base, const std::string& member, SourceLoc loc);
};

struct Stmt {
    enum class Kind {
        Block, VarDecl, Assign, If, While, For, Return, ExprStmt,
        Unsafe, Asm, Repeat, RangeFor
    };
    Kind kind = Kind::Block;
    SourceLoc loc;

    std::vector<std::unique_ptr<Stmt>> blockStmts;
    std::string varName;
    Type varType;
    std::unique_ptr<Expr> varInit;
    std::unique_ptr<Expr> assignTarget;  // or for expr in For
    std::unique_ptr<Expr> assignValue;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> initStmt;
    std::unique_ptr<Stmt> stepStmt;
    std::unique_ptr<Expr> startExpr; // for RangeFor
    std::unique_ptr<Expr> endExpr;   // for RangeFor
    std::unique_ptr<Expr> returnExpr;
    std::unique_ptr<Expr> expr;
    std::string asmCode; // for Asm
};

struct FuncParam {
    std::string name;
    Type type;
    SourceLoc loc;
};

struct FuncDecl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<FuncParam> params;
    Type returnType;
    std::unique_ptr<Stmt> body;
    SourceLoc loc;
    bool isExtern = false;
    std::string externLib; // e.g. "C"
};

struct StructMember {
    std::string name;
    Type type;
    SourceLoc loc;
};

struct StructDecl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<StructMember> members;
    std::vector<FuncDecl> methods;
    SourceLoc loc;
};

struct Import {
    std::string name; // namespace name
    std::string path; // file path
    SourceLoc loc;
};

struct Program {
    std::vector<Import> imports;
    std::vector<StructDecl> structs;
    std::vector<FuncDecl> functions;
    std::vector<std::unique_ptr<Stmt>> topLevelStmts;
    SourceLoc loc;
};

} // namespace gspp

#endif
