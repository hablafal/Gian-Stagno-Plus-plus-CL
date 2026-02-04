#include "optimizer.h"

namespace gspp {

void Optimizer::optimizeExpr(Expr* expr) {
    if (!expr) return;
    switch (expr->kind) {
        case Expr::Kind::Binary:
            optimizeExpr(expr->left.get());
            optimizeExpr(expr->right.get());
            break;
        case Expr::Kind::Unary:
            optimizeExpr(expr->right.get());
            break;
        case Expr::Kind::Call:
            for (auto& a : expr->args) optimizeExpr(a.get());
            break;
        case Expr::Kind::Member:
            optimizeExpr(expr->left.get());
            break;
        default:
            break;
    }
}

void Optimizer::optimizeStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block:
            for (auto& s : stmt->blockStmts) optimizeStmt(s.get());
            break;
        case Stmt::Kind::VarDecl:
            optimizeExpr(stmt->varInit.get());
            break;
        case Stmt::Kind::Assign:
            optimizeExpr(stmt->assignTarget.get());
            optimizeExpr(stmt->assignValue.get());
            break;
        case Stmt::Kind::If:
            optimizeExpr(stmt->condition.get());
            optimizeStmt(stmt->thenBranch.get());
            if (stmt->elseBranch) optimizeStmt(stmt->elseBranch.get());
            break;
        case Stmt::Kind::While:
            optimizeExpr(stmt->condition.get());
            optimizeStmt(stmt->body.get());
            break;
        case Stmt::Kind::For:
            optimizeStmt(stmt->initStmt.get());
            optimizeExpr(stmt->condition.get());
            optimizeStmt(stmt->stepStmt.get());
            optimizeStmt(stmt->body.get());
            break;
        case Stmt::Kind::Return:
            optimizeExpr(stmt->returnExpr.get());
            break;
        case Stmt::Kind::ExprStmt:
            optimizeExpr(stmt->expr.get());
            break;
    }
}

void Optimizer::optimizeFunc(FuncDecl& f) {
    if (f.body) optimizeStmt(f.body.get());
}

void Optimizer::optimize() {
    for (auto& f : program_->functions)
        optimizeFunc(f);
}

} // namespace gspp
