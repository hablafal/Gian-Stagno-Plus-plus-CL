#include "semantic.h"

namespace gspp {

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block: {
            pushScope();
            for (auto& s : stmt->blockStmts) analyzeStmt(s.get());
            popScope();
            break;
        }
        case Stmt::Kind::VarDecl: {
            Type t = resolveType(stmt->varType);
            if (stmt->varInit) {
                Type initTy = analyzeExpr(stmt->varInit.get());
                if (t.kind == Type::Kind::Int && initTy.kind != Type::Kind::Int) t = initTy;
            }
            addVar(stmt->varName, t);
            stmt->varType = t;
            break;
        }
        case Stmt::Kind::Assign: {
            analyzeExpr(stmt->assignTarget.get());
            analyzeExpr(stmt->assignValue.get());
            break;
        }
        case Stmt::Kind::Try: {
            analyzeStmt(stmt->body.get());
            for (auto& h : stmt->handlers) {
                analyzeStmt(h.get());
            }
            if (stmt->finallyBlock) analyzeStmt(stmt->finallyBlock.get());
            break;
        }
        case Stmt::Kind::Except: {
            pushScope();
            if (!stmt->excVar.empty()) {
                // For now, exception type is just a string or generic object
                addVar(stmt->excVar, Type{Type::Kind::String});
            }
            analyzeStmt(stmt->body.get());
            popScope();
            break;
        }
        case Stmt::Kind::Raise: {
            if (stmt->expr) analyzeExpr(stmt->expr.get());
            break;
        }
        case Stmt::Kind::If: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->thenBranch.get()); if (stmt->elseBranch) analyzeStmt(stmt->elseBranch.get()); break;
        case Stmt::Kind::While: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Join: {
            Type t = analyzeExpr(stmt->expr.get());
            if (t.kind != Type::Kind::Thread) {
                error("join requires a thread handle", stmt->loc);
            }
            break;
        }
        case Stmt::Kind::Lock: {
            Type t = analyzeExpr(stmt->expr.get());
            if (t.kind != Type::Kind::Mutex && (t.kind != Type::Kind::Pointer || t.ptrTo->kind != Type::Kind::Mutex)) {
                error("lock requires a mutex", stmt->loc);
            }
            analyzeStmt(stmt->body.get());
            break;
        }
        case Stmt::Kind::For: pushScope(); analyzeStmt(stmt->initStmt.get()); analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->stepStmt.get()); analyzeStmt(stmt->body.get()); popScope(); break;
        case Stmt::Kind::Repeat: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::RangeFor: pushScope(); analyzeExpr(stmt->startExpr.get()); analyzeExpr(stmt->endExpr.get()); addVar(stmt->varName, Type{Type::Kind::Int}); analyzeStmt(stmt->body.get()); popScope(); break;
        case Stmt::Kind::ForEach: {
            pushScope();
            Type listTy = analyzeExpr(stmt->expr.get());
            Type elemTy{Type::Kind::Int};
            if (listTy.kind == Type::Kind::List && listTy.ptrTo) elemTy = *listTy.ptrTo;
            addVar(stmt->varName, elemTy);
            analyzeStmt(stmt->body.get());
            popScope();
            break;
        }
        case Stmt::Kind::Switch: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Case: analyzeExpr(stmt->condition.get()); analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Defer: analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Return: if (stmt->returnExpr) analyzeExpr(stmt->returnExpr.get()); break;
        case Stmt::Kind::ExprStmt: analyzeExpr(stmt->expr.get()); break;
        case Stmt::Kind::Unsafe: analyzeStmt(stmt->body.get()); break;
        case Stmt::Kind::Asm: break;
        case Stmt::Kind::Send: {
            Type t = analyzeExpr(stmt->assignTarget.get());
            if (t.kind != Type::Kind::Chan) {
                error("send to non-channel type", stmt->loc);
            }
            analyzeExpr(stmt->assignValue.get());
            break;
        }
    }
}

std::unique_ptr<Stmt> SemanticAnalyzer::substituteStmt(const Stmt* s, const std::unordered_map<std::string, Type>& subs) {
    if (!s) return nullptr;
    auto res = std::make_unique<Stmt>();
    res->kind = s->kind;
    res->loc = s->loc;
    res->varName = s->varName;
    res->varType = substitute(s->varType, subs);
    res->asmCode = s->asmCode;
    res->isInclusive = s->isInclusive;
    if (s->varInit) res->varInit = substituteExpr(s->varInit.get(), subs);
    if (s->assignTarget) res->assignTarget = substituteExpr(s->assignTarget.get(), subs);
    if (s->assignValue) res->assignValue = substituteExpr(s->assignValue.get(), subs);
    if (s->condition) res->condition = substituteExpr(s->condition.get(), subs);
    if (s->thenBranch) res->thenBranch = substituteStmt(s->thenBranch.get(), subs);
    if (s->elseBranch) res->elseBranch = substituteStmt(s->elseBranch.get(), subs);
    if (s->body) res->body = substituteStmt(s->body.get(), subs);
    if (s->initStmt) res->initStmt = substituteStmt(s->initStmt.get(), subs);
    if (s->stepStmt) res->stepStmt = substituteStmt(s->stepStmt.get(), subs);
    if (s->startExpr) res->startExpr = substituteExpr(s->startExpr.get(), subs);
    if (s->endExpr) res->endExpr = substituteExpr(s->endExpr.get(), subs);
    if (s->returnExpr) res->returnExpr = substituteExpr(s->returnExpr.get(), subs);
    if (s->expr) res->expr = substituteExpr(s->expr.get(), subs);
    if (s->finallyBlock) res->finallyBlock = substituteStmt(s->finallyBlock.get(), subs);
    for (const auto& h : s->handlers) res->handlers.push_back(substituteStmt(h.get(), subs));
    res->excType = s->excType;
    res->excVar = s->excVar;
    for (const auto& stmt : s->blockStmts) res->blockStmts.push_back(substituteStmt(stmt.get(), subs));
    return res;
}

} // namespace gspp
