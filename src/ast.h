#ifndef GSPP_AST_H
#define GSPP_AST_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace gspp {

struct SourceLoc {
    int line = 0;
    int column = 0;
};

// Forward declarations
struct Type;
struct Expr;
struct Stmt;

struct Type {
    enum class Kind { Int, Float, Bool, StructRef };
    Kind kind = Kind::Int;
    std::string structName;  // for StructRef
    SourceLoc loc;
};

struct Expr {
    enum class Kind {
        IntLit, FloatLit, BoolLit,
        Var, Binary, Unary, Call, Member, Cast
    };
    Kind kind = Kind::IntLit;
    Type exprType;
    SourceLoc loc;

    int64_t intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    std::string ident;
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
        Block, VarDecl, Assign, If, While, For, Return, ExprStmt
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
    std::unique_ptr<Expr> returnExpr;
    std::unique_ptr<Expr> expr;
};

struct StructMember {
    std::string name;
    Type type;
    SourceLoc loc;
};

struct StructDecl {
    std::string name;
    std::vector<StructMember> members;
    SourceLoc loc;
};

struct FuncParam {
    std::string name;
    Type type;
    SourceLoc loc;
};

struct FuncDecl {
    std::string name;
    std::vector<FuncParam> params;
    Type returnType;
    std::unique_ptr<Stmt> body;
    SourceLoc loc;
};

struct Program {
    std::vector<StructDecl> structs;
    std::vector<FuncDecl> functions;
    SourceLoc loc;
};

} // namespace gspp

#endif
