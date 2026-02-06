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
    enum class Kind { Int, Float, Bool, StructRef, Pointer, Void, String, Char, TypeParam, List, Dict, Tuple, Set, Mutex, Thread, Chan };
    Kind kind = Kind::Int;
    bool isMutable = false; // for Tuples
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
        IntLit, FloatLit, BoolLit, StringLit, ListLit, DictLit, SetLit, TupleLit,
        Var, Binary, Unary, Call, Member, Cast, Sizeof,
        Deref, AddressOf, New, Delete, Index, Slice, Ternary,
        Comprehension, Spawn, Receive, ChanInit, Super
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
    std::vector<Type> typeArgs; // for generic function calls
    std::string member;
    std::unique_ptr<Type> targetType;  // for Cast
    std::unique_ptr<Expr> cond;      // for Ternary

    static std::unique_ptr<Expr> makeIntLit(int64_t v, SourceLoc loc);
    static std::unique_ptr<Expr> makeFloatLit(double v, SourceLoc loc);
    static std::unique_ptr<Expr> makeBoolLit(bool v, SourceLoc loc);
    static std::unique_ptr<Expr> makeStringLit(const std::string& v, SourceLoc loc);
    static std::unique_ptr<Expr> makeVar(const std::string& id, SourceLoc loc);
    static std::unique_ptr<Expr> makeBinary(std::unique_ptr<Expr> l, const std::string& op,
                                            std::unique_ptr<Expr> r, SourceLoc loc);
    static std::unique_ptr<Expr> makeUnary(const std::string& op, std::unique_ptr<Expr> operand, SourceLoc loc);
    static std::unique_ptr<Expr> makeCall(const std::string& id, std::vector<std::unique_ptr<Expr>> args, SourceLoc loc);
    static std::unique_ptr<Expr> makeMember(std::unique_ptr<Expr> base, const std::string& member, SourceLoc loc);
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

struct Stmt {
    enum class Kind {
        Block, VarDecl, Assign, If, While, For, Return, ExprStmt,
        Unsafe, Asm, Repeat, RangeFor, ForEach, Switch, Case, Defer,
        Lock, Join, Send, Try, Except, Raise
    };
    Kind kind = Kind::Block;
    SourceLoc loc;

    std::vector<std::unique_ptr<Stmt>> blockStmts;
    std::string varName;
    Type varType;
    std::unique_ptr<Expr> varInit;
    std::unique_ptr<Expr> assignTarget;
    std::unique_ptr<Expr> assignValue;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> initStmt;
    std::unique_ptr<Stmt> stepStmt;
    std::unique_ptr<Expr> startExpr; // for RangeFor
    std::unique_ptr<Expr> endExpr;   // for RangeFor
    bool isInclusive = false;        // for RangeFor
    std::unique_ptr<Expr> returnExpr;
    std::unique_ptr<Expr> expr;
    std::string asmCode; // for Asm

    // for Try/Except
    std::vector<std::unique_ptr<Stmt>> handlers; // List of ExceptStmt
    std::unique_ptr<Stmt> finallyBlock;
    std::string excType; // for ExceptStmt
    std::string excVar;  // for ExceptStmt (e.g. except Exception as e)
};

struct StructMember {
    std::string name;
    Type type;
    SourceLoc loc;
};

struct StructDecl {
    std::string name;
    std::string baseName;
    std::vector<std::string> typeParams;
    std::vector<StructMember> members;
    std::vector<FuncDecl> methods;
    SourceLoc loc;
};

struct Import {
    std::string name; // original module name or alias if 'as' is used
    std::string path; // file path
    std::string alias; // if 'as' is used
    std::vector<std::string> importNames; // for 'from mod import a, b'
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
