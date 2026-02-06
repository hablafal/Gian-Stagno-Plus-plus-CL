#include "codegen.h"
#include <iostream>

namespace gspp {

void CodeGenerator::emitStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block: {
            deferStack_.emplace_back();
            rcVars_.emplace_back();
            for (auto& s : stmt->blockStmts) emitStmt(s.get());

            // Release RC variables
            for (auto it = rcVars_.back().rbegin(); it != rcVars_.back().rend(); ++it) {
                emitRCRelease(*it);
            }

            auto& defers = deferStack_.back();
            for (auto it = defers.rbegin(); it != defers.rend(); ++it) emitStmt(*it);
            deferStack_.pop_back();
            rcVars_.pop_back();
            break;
        }
        case Stmt::Kind::VarDecl: {
            std::string loc = getVarLocation(stmt->varName);
            if (loc.empty()) break;

            // Initialize to 0
            *out_ << "\tmovq\t$0, " << loc << "\n";

            if (isRefCounted(stmt->varType)) {
                rcVars_.back().push_back(stmt->varName);
            }

            if (stmt->varType.kind == Type::Kind::Mutex && !stmt->varInit) {
                emitCall("gspp_mutex_create", 0);
                *out_ << "\tmovq\t%rax, " << loc << "\n";
            } else if (stmt->varInit) {
                emitExprToRax(stmt->varInit.get());
                if (isRefCounted(stmt->varType)) {
                    // If value is not newly produced, we must retain it to own it
                    if (!isRCProducer(stmt->varInit.get())) {
                        *out_ << "\tpushq\t%rax\n";
                        emitRCRetain("rax");
                        *out_ << "\tpopq\t%rax\n";
                    }
                }
                *out_ << "\tmovq\t%rax, " << loc << "\n";
            }
            break;
        }
        case Stmt::Kind::Assign: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;

            if (stmt->assignTarget->kind == Expr::Kind::Var) {
                emitExprToRax(stmt->assignValue.get());
                std::string loc = getVarLocation(stmt->assignTarget->ident);
                if (!loc.empty()) {
                    if (isRefCounted(stmt->assignTarget->exprType)) {
                        *out_ << "\tpushq\t%rax\n";
                        // If value is not newly produced, we must retain it to own it
                        if (!isRCProducer(stmt->assignValue.get())) {
                            emitRCRetain("rax");
                        }
                        // Release old value
                        *out_ << "\tmovq\t" << loc << ", %rdi\n";
                        emitCall("gspp_release", 1);
                        *out_ << "\tpopq\t%rax\n";
                    }
                    *out_ << "\tmovq\t%rax, " << loc << "\n";
                }
            }
            else if (stmt->assignTarget->kind == Expr::Kind::Member) {
                emitExprToRax(stmt->assignTarget->left.get());
                *out_ << "\tpushq\t%rax\n"; // Save object pointer
                emitExprToRax(stmt->assignValue.get());
                *out_ << "\tmovq\t%rax, %rcx\n";
                *out_ << "\tpopq\t%rax\n"; // Restore object pointer

                Type baseType = stmt->assignTarget->left->exprType;
                if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
                StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
                if (sd) {
                    auto it = sd->memberIndex.find(stmt->assignTarget->member);
                    if (it != sd->memberIndex.end()) {
                        int offset = it->second * 8;
                        if (isRefCounted(stmt->assignTarget->exprType)) {
                            *out_ << "\tpushq\t%rax\n";
                            *out_ << "\tpushq\t%rcx\n";
                            if (!isRCProducer(stmt->assignValue.get())) {
                                emitRCRetain("rcx");
                            }
                            *out_ << "\tmovq\t8(%rsp), %rax\n"; // Restore rax for offset access
                            *out_ << "\tmovq\t" << offset << "(%rax), %rdi\n";
                            emitCall("gspp_release", 1);
                            *out_ << "\tpopq\t%rcx\n";
                            *out_ << "\tpopq\t%rax\n";
                        }
                        *out_ << "\tmovq\t%rcx, " << offset << "(%rax)\n";
                    }
                }
            }
            else if (stmt->assignTarget->kind == Expr::Kind::Index) {
                Type baseTy = stmt->assignTarget->left->exprType;
                if (baseTy.kind == Type::Kind::Tuple) {
                    emitExprToRax(stmt->assignTarget->left.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignTarget->right.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignValue.get());
                    *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                    emitCall("gspp_tuple_set", 3);
                } else if (baseTy.kind == Type::Kind::List) {
                    emitExprToRax(stmt->assignTarget->left.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignTarget->right.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignValue.get());
                    *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";

                    if (isRefCounted(stmt->assignTarget->exprType)) {
                        *out_ << "\tpushq\t%" << regs[0] << "\n";
                        *out_ << "\tpushq\t%" << regs[1] << "\n";
                        *out_ << "\tpushq\t%" << regs[2] << "\n";
                        if (!isRCProducer(stmt->assignValue.get())) {
                            *out_ << "\tmovq\t%" << regs[2] << ", %rdi\n";
                            emitCall("gspp_retain", 1);
                        }
                        // Release old
                        *out_ << "\tmovq\t8(%rsp), %rax\n"; // Restore regs[1] (index)
                        *out_ << "\tmovq\t16(%rsp), %rdx\n"; // Restore regs[0] (list)
                        *out_ << "\tmovq\t(%rdx), %rdx\n"; // list->data
                        *out_ << "\tmovq\t(%rdx,%rax,8), %rdi\n";
                        emitCall("gspp_release", 1);
                        *out_ << "\tpopq\t%" << regs[2] << "\n";
                        *out_ << "\tpopq\t%" << regs[1] << "\n";
                        *out_ << "\tpopq\t%" << regs[0] << "\n";
                    }

                    *out_ << "\tmovq\t(%" << regs[0] << "), %rax\n";
                    *out_ << "\tmovq\t%" << regs[2] << ", (%rax,%" << regs[1] << ",8)\n";
                }
            } else if (stmt->assignTarget->kind == Expr::Kind::Deref) {
                emitExprToRax(stmt->assignTarget->right.get()); *out_ << "\tpushq\t%rax\n";
                emitExprToRax(stmt->assignValue.get()); *out_ << "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n";
                *out_ << "\tmovq\t%rcx, (%rax)\n";
            }
            break;
        }
        case Stmt::Kind::If: { std::string elseLabel = nextLabel(), endLabel = nextLabel(); emitExprToRax(stmt->condition.get()); *out_ << "\ttestq\t%rax, %rax\n\tje\t" << elseLabel << "\n"; emitStmt(stmt->thenBranch.get()); *out_ << "\tjmp\t" << endLabel << "\n" << elseLabel << ":\n"; if (stmt->elseBranch) emitStmt(stmt->elseBranch.get()); *out_ << endLabel << ":\n"; break; }
        case Stmt::Kind::While: { std::string condLabel = nextLabel(), bodyLabel = nextLabel(); *out_ << "\tjmp\t" << condLabel << "\n" << bodyLabel << ":\n"; emitStmt(stmt->body.get()); *out_ << condLabel << ":\n"; emitExprToRax(stmt->condition.get()); *out_ << "\ttestq\t%rax, %rax\n\tjne\t" << bodyLabel << "\n"; break; }
        case Stmt::Kind::Repeat: { std::string condLabel = nextLabel(), bodyLabel = nextLabel(), endLabel = nextLabel(); emitExprToRax(stmt->condition.get()); *out_ << "\tpushq\t%rax\n" << condLabel << ":\n\tmovq\t(%rsp), %rax\n\ttestq\t%rax, %rax\n\tjle\t" << endLabel << "\n"; emitStmt(stmt->body.get()); *out_ << "\tdecq\t(%rsp)\n\tjmp\t" << condLabel << "\n" << endLabel << ":\n\taddq\t$8, %rsp\n"; break; }
        case Stmt::Kind::RangeFor: { std::string condLabel = nextLabel(), bodyLabel = nextLabel(), stepLabel = nextLabel(); emitExprToRax(stmt->startExpr.get()); std::string loc = getVarLocation(stmt->varName); if (!loc.empty()) *out_ << "\tmovq\t%rax, " << loc << "\n"; *out_ << "\tjmp\t" << condLabel << "\n" << bodyLabel << ":\n"; emitStmt(stmt->body.get()); *out_ << stepLabel << ":\n"; if (!loc.empty()) *out_ << "\tincq\t" << loc << "\n"; *out_ << condLabel << ":\n"; emitExprToRax(stmt->endExpr.get()); *out_ << "\tpushq\t%rax\n"; if (!loc.empty()) *out_ << "\tmovq\t" << loc << ", %rax\n"; *out_ << "\tpopq\t%rcx\n\tcmpq\t%rax, %rcx\n"; if (stmt->isInclusive) *out_ << "\tjge\t" << bodyLabel << "\n"; else *out_ << "\tjg\t" << bodyLabel << "\n"; break; }
        case Stmt::Kind::ForEach: {
            std::string loopStart = nextLabel(), loopEnd = nextLabel();
            emitExprToRax(stmt->expr.get()); *out_ << "\tpushq\t%rax\n\tmovq\t$0, %rbx\n";
            *out_ << loopStart << ":\n\tmovq\t(%rsp), %rdx\n\tmovq\t8(%rdx), %rcx\n\tcmpq\t%rcx, %rbx\n\tjge\t" << loopEnd << "\n";
            *out_ << "\tmovq\t(%rdx), %rdx\n\tmovq\t(%rdx,%rbx,8), %rax\n";
            std::string loc = getVarLocation(stmt->varName);
            if (!loc.empty()) *out_ << "\tmovq\t%rax, " << loc << "\n";
            *out_ << "\tpushq\t%rbx\n"; emitStmt(stmt->body.get()); *out_ << "\tpopq\t%rbx\n\tincq\t%rbx\n\tjmp\t" << loopStart << "\n" << loopEnd << ":\n\taddq\t$8, %rsp\n";
            break;
        }
        case Stmt::Kind::Switch: { std::string endLabel = nextLabel(); emitExprToRax(stmt->condition.get()); *out_ << "\tpushq\t%rax\n"; emitStmt(stmt->body.get()); *out_ << "\taddq\t$8, %rsp\n" << endLabel << ":\n"; break; }
        case Stmt::Kind::Case: { std::string nextCase = nextLabel(); emitExprToRax(stmt->condition.get()); *out_ << "\tcmpq\t%rax, (%rsp)\n\tjne\t" << nextCase << "\n"; emitStmt(stmt->body.get()); *out_ << nextCase << ":\n"; break; }
        case Stmt::Kind::Join: {
            emitExprToRax(stmt->expr.get());
            *out_ << "\tmovq\t%rax, " << (isLinux_ ? "%rdi" : "%rcx") << "\n";
            emitCall("gspp_join", 1);
            break;
        }
        case Stmt::Kind::Lock: {
            emitExprToRax(stmt->expr.get());
            *out_ << "\tpushq\t%rax\n";
            *out_ << "\tmovq\t%rax, " << (isLinux_ ? "%rdi" : "%rcx") << "\n";
            emitCall("gspp_mutex_lock", 1);
            emitStmt(stmt->body.get());
            *out_ << "\tpopq\t" << (isLinux_ ? "%rdi" : "%rcx") << "\n";
            emitCall("gspp_mutex_unlock", 1);
            break;
        }
        case Stmt::Kind::Defer: if (!deferStack_.empty()) deferStack_.back().push_back(stmt->body.get()); break;
        case Stmt::Kind::Return:
            if (stmt->returnExpr) {
                if (stmt->returnExpr->exprType.kind == Type::Kind::Float) {
                    emitExprToXmm0(stmt->returnExpr.get());
                } else {
                    emitExprToRax(stmt->returnExpr.get());
                    if (isRefCounted(stmt->returnExpr->exprType)) {
                        // If return value is not newly produced, we must retain it so it survives local releases
                        if (!isRCProducer(stmt->returnExpr.get())) {
                            *out_ << "\tpushq\t%rax\n";
                            emitRCRetain("rax");
                            *out_ << "\tpopq\t%rax\n";
                        }
                    }
                }
            } else {
                *out_ << "\tmovq\t$0, %rax\n";
            }
            // ARC releases for all scopes
            if (stmt->returnExpr && stmt->returnExpr->exprType.kind != Type::Kind::Float) *out_ << "\tpushq\t%rax\n";
            for (auto sit = rcVars_.rbegin(); sit != rcVars_.rend(); ++sit) {
                for (auto it = sit->rbegin(); it != sit->rend(); ++it) {
                    emitRCRelease(*it);
                }
            }
            if (stmt->returnExpr && stmt->returnExpr->exprType.kind != Type::Kind::Float) *out_ << "\tpopq\t%rax\n";
            // Defer stmts
            for (auto sit = deferStack_.rbegin(); sit != deferStack_.rend(); ++sit) {
                for (auto it = sit->rbegin(); it != sit->rend(); ++it) {
                    emitStmt(*it);
                }
            }
            *out_ << "\tjmp\t" << currentEndLabel_ << "\n";
            break;
        case Stmt::Kind::For: emitStmt(stmt->initStmt.get()); { std::string condLabel = nextLabel(), endLabel = nextLabel(); *out_ << condLabel << ":\n"; emitExprToRax(stmt->condition.get()); *out_ << "\ttestq\t%rax, %rax\n\tje\t" << endLabel << "\n"; emitStmt(stmt->body.get()); emitStmt(stmt->stepStmt.get()); *out_ << "\tjmp\t" << condLabel << "\n" << endLabel << ":\n"; } break;
        case Stmt::Kind::ExprStmt: emitExprToRax(stmt->expr.get()); break;
        case Stmt::Kind::Unsafe: emitStmt(stmt->body.get()); break;
        case Stmt::Kind::Asm: *out_ << "\t" << stmt->asmCode << "\n"; break;
        case Stmt::Kind::Send: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            emitExprToRax(stmt->assignTarget.get());
            *out_ << "\tpushq\t%rax\n";
            emitExprToRax(stmt->assignValue.get());
            *out_ << "\tmovq\t%rax, %" << regs[1] << "\n";
            *out_ << "\tpopq\t%" << regs[0] << "\n";
            emitCall("gspp_chan_send", 2);
            break;
        }
        case Stmt::Kind::Try: {
            std::string labelExc = nextLabel();
            std::string labelFinally = nextLabel();
            std::string labelEnd = nextLabel();

            *out_ << "\tsubq\t$256, %rsp\n";
            *out_ << "\tmovq\t%rsp, %rdi\n";
            *out_ << "\tcall\t_setjmp\n";
            *out_ << "\ttestq\t%rax, %rax\n";
            *out_ << "\tjnz\t" << labelExc << "\n";

            *out_ << "\tmovq\t%rsp, %rdi\n";
            emitCall("gspp_push_exception_handler", 1);
            emitStmt(stmt->body.get());
            emitCall("gspp_pop_exception_handler", 0);
            *out_ << "\taddq\t$256, %rsp\n";
            *out_ << "\tjmp\t" << labelFinally << "\n";

            *out_ << labelExc << ":\n";
            *out_ << "\taddq\t$256, %rsp\n";
            for (auto& h : stmt->handlers) {
                emitStmt(h.get());
                *out_ << "\tjmp\t" << labelFinally << "\n";
            }
            *out_ << labelFinally << ":\n";
            if (stmt->finallyBlock) emitStmt(stmt->finallyBlock.get());
            *out_ << labelEnd << ":\n";
            break;
        }
        case Stmt::Kind::Except: {
            if (!stmt->excVar.empty()) {
                std::string loc = getVarLocation(stmt->excVar);
                if (!loc.empty()) {
                    emitCall("gspp_get_current_exception", 0);
                    *out_ << "\tmovq\t%rax, " << loc << "\n";
                }
            }
            emitStmt(stmt->body.get());
            break;
        }
        case Stmt::Kind::Raise: {
            if (stmt->expr) {
                emitExprToRax(stmt->expr.get());
                *out_ << "\tmovq\t%rax, %rdi\n";
            } else {
                *out_ << "\tmovq\t$0, %rdi\n";
            }
            emitCall("gspp_raise", 1);
            break;
        }
    }
}

} // namespace gspp
