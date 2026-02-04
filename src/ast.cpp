#include "ast.h"

namespace gspp {

Type::Type(const Type& other) {
    kind = other.kind;
    isMutable = other.isMutable;
    structName = other.structName;
    ns = other.ns;
    typeArgs = other.typeArgs;
    loc = other.loc;
    if (other.ptrTo) ptrTo = std::make_unique<Type>(*other.ptrTo);
}

Type& Type::operator=(const Type& other) {
    if (this == &other) return *this;
    kind = other.kind;
    isMutable = other.isMutable;
    structName = other.structName;
    ns = other.ns;
    typeArgs = other.typeArgs;
    loc = other.loc;
    if (other.ptrTo) ptrTo = std::make_unique<Type>(*other.ptrTo);
    else ptrTo.reset();
    return *this;
}

std::unique_ptr<Expr> Expr::makeIntLit(int64_t v, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::IntLit;
    e->intVal = v;
    e->exprType.kind = Type::Kind::Int;
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeFloatLit(double v, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::FloatLit;
    e->floatVal = v;
    e->exprType.kind = Type::Kind::Float;
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeBoolLit(bool v, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::BoolLit;
    e->boolVal = v;
    e->exprType.kind = Type::Kind::Bool;
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeVar(const std::string& id, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::Var;
    e->ident = id;
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeBinary(std::unique_ptr<Expr> l, const std::string& op,
                                        std::unique_ptr<Expr> r, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::Binary;
    e->left = std::move(l);
    e->right = std::move(r);
    e->op = op;
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeUnary(const std::string& op, std::unique_ptr<Expr> operand, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::Unary;
    e->op = op;
    e->right = std::move(operand);
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeCall(const std::string& id, std::vector<std::unique_ptr<Expr>> args, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::Call;
    e->ident = id;
    e->args = std::move(args);
    e->loc = loc;
    return e;
}

std::unique_ptr<Expr> Expr::makeMember(std::unique_ptr<Expr> base, const std::string& member, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = Kind::Member;
    e->left = std::move(base);
    e->member = member;
    e->loc = loc;
    return e;
}

} // namespace gspp
