#include "optimizer.h"

namespace gspp {

void Optimizer::optimizeExpr(Expr* expr) {
    if (!expr) return;
    switch (expr->kind) {
        case Expr::Kind::Binary: {
            optimizeExpr(expr->left.get());
            optimizeExpr(expr->right.get());
            if (expr->left->kind == Expr::Kind::IntLit && expr->right->kind == Expr::Kind::IntLit) {
                int64_t l = expr->left->intVal; int64_t r = expr->right->intVal;
                if (expr->op == "+") { expr->kind = Expr::Kind::IntLit; expr->intVal = l + r; expr->left.reset(); expr->right.reset(); }
                else if (expr->op == "-") { expr->kind = Expr::Kind::IntLit; expr->intVal = l - r; expr->left.reset(); expr->right.reset(); }
                else if (expr->op == "*") { expr->kind = Expr::Kind::IntLit; expr->intVal = l * r; expr->left.reset(); expr->right.reset(); }
                else if (expr->op == "/" && r != 0) { expr->kind = Expr::Kind::IntLit; expr->intVal = l / r; expr->left.reset(); expr->right.reset(); }
            }
            break;
        }
        case Expr::Kind::Unary: optimizeExpr(expr->right.get()); break;
        case Expr::Kind::Spawn: optimizeExpr(expr->left.get()); break;
        case Expr::Kind::Call: for (auto& a : expr->args) optimizeExpr(a.get()); if (expr->left) optimizeExpr(expr->left.get()); break;
        case Expr::Kind::Slice:
            optimizeExpr(expr->left.get());
            for (auto& a : expr->args) optimizeExpr(a.get());
            break;
        case Expr::Kind::Comprehension: optimizeExpr(expr->left.get()); optimizeExpr(expr->right.get()); if (expr->cond) optimizeExpr(expr->cond.get()); break;
        case Expr::Kind::Member: optimizeExpr(expr->left.get()); break;
        case Expr::Kind::Index: optimizeExpr(expr->left.get()); optimizeExpr(expr->right.get()); break;
        case Expr::Kind::ListLit:
        case Expr::Kind::DictLit:
        case Expr::Kind::SetLit:
        case Expr::Kind::TupleLit:
            for (auto& a : expr->args) optimizeExpr(a.get());
            break;
        case Expr::Kind::Ternary: optimizeExpr(expr->cond.get()); optimizeExpr(expr->left.get()); optimizeExpr(expr->right.get()); break;
        case Expr::Kind::Receive:
        case Expr::Kind::ChanInit:
            optimizeExpr(expr->right.get());
            for (auto& a : expr->args) optimizeExpr(a.get());
            break;
        default: break;
    }
}

void Optimizer::optimizeStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block: {
            std::vector<std::unique_ptr<Stmt>> optimized; bool returned = false;
            for (auto& s : stmt->blockStmts) {
                if (returned) continue;
                optimizeStmt(s.get());
                if (s->kind == Stmt::Kind::If && s->condition->kind == Expr::Kind::BoolLit) {
                    if (s->condition->boolVal) { if (s->thenBranch->kind == Stmt::Kind::Block) for (auto& bs : s->thenBranch->blockStmts) optimized.push_back(std::move(bs)); else optimized.push_back(std::move(s->thenBranch)); }
                    else if (s->elseBranch) { if (s->elseBranch->kind == Stmt::Kind::Block) for (auto& bs : s->elseBranch->blockStmts) optimized.push_back(std::move(bs)); else optimized.push_back(std::move(s->elseBranch)); }
                    continue;
                }
                if (s->kind == Stmt::Kind::Return) returned = true;
                optimized.push_back(std::move(s));
            }
            stmt->blockStmts = std::move(optimized); break;
        }
        case Stmt::Kind::VarDecl: optimizeExpr(stmt->varInit.get()); break;
        case Stmt::Kind::Assign: optimizeExpr(stmt->assignTarget.get()); optimizeExpr(stmt->assignValue.get()); break;
        case Stmt::Kind::If: optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->thenBranch.get()); if (stmt->elseBranch) optimizeStmt(stmt->elseBranch.get()); break;
        case Stmt::Kind::While: optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Join: optimizeExpr(stmt->expr.get()); break;
        case Stmt::Kind::Lock: optimizeExpr(stmt->expr.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::For: optimizeStmt(stmt->initStmt.get()); optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->stepStmt.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Repeat: optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::RangeFor: optimizeExpr(stmt->startExpr.get()); optimizeExpr(stmt->endExpr.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::ForEach: optimizeExpr(stmt->expr.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Switch: optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Case: optimizeExpr(stmt->condition.get()); optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Defer: optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Return: optimizeExpr(stmt->returnExpr.get()); break;
        case Stmt::Kind::ExprStmt: optimizeExpr(stmt->expr.get()); break;
        case Stmt::Kind::Unsafe: optimizeStmt(stmt->body.get()); break;
        case Stmt::Kind::Asm: break;
        case Stmt::Kind::Send:
            optimizeExpr(stmt->assignTarget.get());
            optimizeExpr(stmt->assignValue.get());
            break;
        case Stmt::Kind::Try:
            optimizeStmt(stmt->body.get());
            for (auto& h : stmt->handlers) optimizeStmt(h.get());
            if (stmt->finallyBlock) optimizeStmt(stmt->finallyBlock.get());
            break;
        case Stmt::Kind::Except:
            optimizeStmt(stmt->body.get());
            break;
        case Stmt::Kind::Raise:
            optimizeExpr(stmt->expr.get());
            break;
    }
}

void Optimizer::optimizeFunc(FuncDecl& f) { if (f.body) optimizeStmt(f.body.get()); }
void Optimizer::optimize() { for (auto& f : program_->functions) optimizeFunc(f); }

}
