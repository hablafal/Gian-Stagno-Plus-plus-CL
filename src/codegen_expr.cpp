#include "codegen.h"
#include <iostream>

namespace gspp {

void CodeGenerator::emitExpr(Expr* expr, const std::string& destReg, bool wantFloat) {
    if (!expr) return;
    std::string dest = destReg;
    std::string rax = "rax";

    switch (expr->kind) {
        case Expr::Kind::IntLit:
            if (dest != rax) *out_ << "\tmovq\t$" << expr->intVal << ", %" << dest << "\n";
            else *out_ << "\tmovq\t$" << expr->intVal << ", %rax\n";
            break;
        case Expr::Kind::FloatLit: {
            std::string label = ".LC_float_" + std::to_string(labelCounter_++);
            stringPool_[std::to_string(expr->floatVal)] = label;
            *out_ << "\tmovsd\t" << label << "(%rip), %xmm0\n";
            if (dest != "xmm0") *out_ << "\tmovq\t%xmm0, %" << dest << "\n";
            break;
        }
        case Expr::Kind::BoolLit:
            *out_ << "\tmovq\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n";
            break;
        case Expr::Kind::StringLit: {
            if (stringPool_.find(expr->ident) == stringPool_.end()) {
                stringPool_[expr->ident] = ".LC" + std::to_string(labelCounter_++);
            }
            *out_ << "\tleaq\t" << stringPool_[expr->ident] << "(%rip), %" << dest << "\n";
            break;
        }
        case Expr::Kind::ListLit: {
            int n = (int)expr->args.size();
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            *out_ << "\tmovq\t$" << n << ", %" << regs[0] << "\n";
            emitCall("gspp_list_new", 1);
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get()); *out_ << "\tmovq\t%rax, %rsi\n\tpopq\t%rdi\n\tpushq\t%rdi\n\tcall\tgspp_list_append\n\tpopq\t%rax\n";
            }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::DictLit: {
            int n = (int)expr->args.size() / 2;
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tgspp_dict_new\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i*2].get()); *out_ << "\tpushq\t%rax\n";
                emitExprToRax(expr->args[i*2+1].get());
                *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                emitCall("gspp_dict_set", 3);
                *out_ << "\tpopq\t%rax\n";
            }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::SetLit: {
            int n = (int)expr->args.size();
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            emitCall("gspp_set_new", 0);
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get());
                *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                emitCall("gspp_set_add", 2);
                *out_ << "\tpopq\t%rax\n";
            }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::TupleLit: {
            int n = (int)expr->args.size();
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            *out_ << "\tmovq\t$" << n << ", %" << regs[0] << "\n";
            *out_ << "\tmovq\t$" << (expr->boolVal ? 1 : 0) << ", %" << regs[1] << "\n";
            emitCall("gspp_tuple_new", 2);
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get());
                *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tmovq\t$" << i << ", %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                emitCall("gspp_tuple_set", 3);
                *out_ << "\tpopq\t%rax\n";
            }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Comprehension: {
            std::string loopStart = nextLabel(), loopEnd = nextLabel(), skipLabel = nextLabel();
            *out_ << "\tmovq\t$0, %rdi\n\tcall\tgspp_list_new\n\tpushq\t%rax\n";
            emitExprToRax(expr->right.get()); *out_ << "\tpushq\t%rax\n\tmovq\t$0, %rbx\n";
            *out_ << loopStart << ":\n\tmovq\t(%rsp), %rdx\n\tmovq\t8(%rdx), %rcx\n\tcmpq\t%rcx, %rbx\n\tjge\t" << loopEnd << "\n";
            *out_ << "\tmovq\t(%rdx), %rdx\n\tmovq\t(%rdx,%rbx,8), %rax\n";
            std::string loc = getVarLocation(expr->ident);
            if (!loc.empty()) *out_ << "\tmovq\t%rax, " << loc << "\n";
            *out_ << "\tpushq\t%rbx\n";
            if (expr->cond) { emitExprToRax(expr->cond.get()); *out_ << "\ttestq\t%rax, %rax\n\tje\t" << skipLabel << "\n"; }
            emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %rsi\n\tmovq\t16(%rsp), %rdi\n\tcall\tgspp_list_append\n";
            *out_ << skipLabel << ":\n\tpopq\t%rbx\n\tincq\t%rbx\n\tjmp\t" << loopStart << "\n" << loopEnd << ":\n\taddq\t$8, %rsp\n\tpopq\t%rax\n";
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Var: {
            std::string loc = getVarLocation(expr->ident);
            if (loc.empty()) { error("unknown variable " + expr->ident, expr->loc); return; }
            *out_ << "\tmovq\t" << loc << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Binary: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;

            if (expr->left->exprType.kind == Type::Kind::Set || expr->left->exprType.kind == Type::Kind::Dict) {
                emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
                emitExprToRax(expr->right.get()); *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                if (expr->op == "|") {
                    if (expr->left->exprType.kind == Type::Kind::Set) *out_ << "\tcall\tgspp_set_union\n";
                    else *out_ << "\tcall\tgspp_dict_union\n";
                } else if (expr->op == "&") {
                    if (expr->left->exprType.kind == Type::Kind::Set) *out_ << "\tcall\tgspp_set_intersection\n";
                    else *out_ << "\tcall\tgspp_dict_intersection\n";
                }
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                return;
            }
            if (expr->op == "and" || expr->op == "or") {
                std::string endLabel = nextLabel(); emitExprToRax(expr->left.get());
                if (expr->op == "and") { *out_ << "\ttestq\t%rax, %rax\n\tje\t" << endLabel << "\n"; }
                else { *out_ << "\ttestq\t%rax, %rax\n\tjne\t" << endLabel << "\n"; }
                emitExprToRax(expr->right.get()); *out_ << endLabel << ":\n";
                if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                return;
            }
            if (expr->op == "==" || expr->op == "!=" || expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
                emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->right.get()); *out_ << "\tpopq\t%rcx\n\tcmpq\t%rax, %rcx\n";
                if (expr->op == "==") *out_ << "\tsete\t%al\n"; else if (expr->op == "!=") *out_ << "\tsetne\t%al\n"; else if (expr->op == "<") *out_ << "\tsetl\t%al\n"; else if (expr->op == ">") *out_ << "\tsetg\t%al\n"; else if (expr->op == "<=") *out_ << "\tsetle\t%al\n"; else *out_ << "\tsetge\t%al\n";
                *out_ << "\tmovzbq\t%al, %rax\n"; if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                return;
            }
            if (expr->left->exprType.kind == Type::Kind::Float) {
                emitExprToXmm0(expr->left.get()); *out_ << "\tsubq\t$8, %rsp\n\tmovq\t%xmm0, (%rsp)\n"; emitExprToXmm0(expr->right.get());
                *out_ << "\tmovq\t%xmm0, %xmm1\n\tmovq\t(%rsp), %xmm0\n\taddq\t$8, %rsp\n";
                if (expr->op == "+") *out_ << "\taddsd\t%xmm1, %xmm0\n"; else if (expr->op == "-") *out_ << "\tsubsd\t%xmm1, %xmm0\n"; else if (expr->op == "*") *out_ << "\tmulsd\t%xmm1, %xmm0\n"; else if (expr->op == "/") *out_ << "\tdivsd\t%xmm1, %xmm0\n";
                if (dest != "xmm0") *out_ << "\tmovq\t%xmm0, %" << dest << "\n";
                return;
            }
            emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->right.get()); *out_ << "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n";
            if (expr->op == "+") {
                if (expr->left->exprType.kind == Type::Kind::String) { *out_ << "\tmovq\t%rax, %rdi\n\tmovq\t%rcx, %rsi\n\tcall\t_gspp_strcat\n"; }
                else *out_ << "\taddq\t%rcx, %rax\n";
            } else if (expr->op == "-") *out_ << "\tsubq\t%rcx, %rax\n";
            else if (expr->op == "*" ) *out_ << "\timulq\t%rcx, %rax\n";
            else if (expr->op == "/") { *out_ << "\tcqto\n\tidivq\t%rcx\n"; }
            if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Index: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;

            emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->right.get()); *out_ << "\tpopq\t%rdx\n";
            if (expr->left->exprType.kind == Type::Kind::String) *out_ << "\tmovzbl\t(%rdx,%rax), %eax\n";
            else if (expr->left->exprType.kind == Type::Kind::List) { *out_ << "\tmovq\t(%rdx), %rdx\n\tmovq\t(%rdx,%rax,8), %rax\n"; }
            else if (expr->left->exprType.kind == Type::Kind::Dict) {
                *out_ << "\tmovq\t%rdx, %" << regs[0] << "\n\tmovq\t%rax, %" << regs[1] << "\n";
                emitCall("gspp_dict_get", 2);
            }
            else if (expr->left->exprType.kind == Type::Kind::Tuple) {
                *out_ << "\tmovq\t%rdx, %" << regs[0] << "\n\tmovq\t%rax, %" << regs[1] << "\n";
                emitCall("gspp_tuple_get", 2);
            }
            else { int sz = getTypeSize(expr->exprType); *out_ << "\tmovq\t(%rdx,%rax," << sz << "), %rax\n"; }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Slice: {
            emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
            emitExprToRax(expr->args[0].get()); *out_ << "\tpushq\t%rax\n";
            emitExprToRax(expr->args[1].get()); *out_ << "\tmovq\t%rax, %rdx\n\tpopq\t%rsi\n\tpopq\t%rdi\n";
            if (expr->left->exprType.kind == Type::Kind::String) *out_ << "\tcall\tgspp_str_slice\n";
            else *out_ << "\tcall\tgspp_list_slice\n";
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Ternary: {
            std::string elseLabel = nextLabel(), endLabel = nextLabel();
            emitExprToRax(expr->cond.get()); *out_ << "\ttestq\t%rax, %rax\n\tje\t" << elseLabel << "\n";
            emitExpr(expr->left.get(), destReg, wantFloat); *out_ << "\tjmp\t" << endLabel << "\n" << elseLabel << ":\n";
            emitExpr(expr->right.get(), destReg, wantFloat); *out_ << endLabel << ":\n";
            break;
        }
        case Expr::Kind::Unary:
            if (expr->op == "-") { emitExprToRax(expr->right.get()); *out_ << "\tnegq\t%rax\n"; }
            else if (expr->op == "not") { emitExprToRax(expr->right.get()); *out_ << "\ttestq\t%rax, %rax\n\tsete\t%al\n\tmovzbq\t%al, %rax\n"; }
            if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        case Expr::Kind::Spawn: {
            Expr* call = expr->left.get();
            if (!call->args.empty()) {
                emitExprToRax(call->args[0].get());
                *out_ << "\tmovq\t%rax, " << (isLinux_ ? "%rsi" : "%rdx") << "\n";
            } else {
                *out_ << "\tmovq\t$0, " << (isLinux_ ? "%rsi" : "%rdx") << "\n";
            }
            std::string label;
            if (!call->ident.empty()) {
                auto it = semantic_->functions().find(call->ident);
                if (it != semantic_->functions().end()) label = it->second.mangledName;
                else label = call->ident;
            } else if (call->left && call->left->kind == Expr::Kind::Var) {
                auto it = semantic_->functions().find(call->left->ident);
                if (it != semantic_->functions().end()) label = it->second.mangledName;
                else label = call->left->ident;
            }
            *out_ << "\tleaq\t" << label << "(%rip), " << (isLinux_ ? "%rdi" : "%rcx") << "\n";
            emitCall("gspp_spawn", 2);
            break;
        }
        case Expr::Kind::Call: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            int num_regs = isLinux_ ? 6 : 4;

            if (expr->left && expr->left->exprType.kind == Type::Kind::String && expr->ident == "len") {
                emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tstrlen\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                return;
            }
            if (expr->left && (expr->left->exprType.kind == Type::Kind::Set || expr->left->exprType.kind == Type::Kind::Dict)) {
                if (expr->ident == "len") {
                    emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                    if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                    if (expr->left->exprType.kind == Type::Kind::Set) *out_ << "\tcall\tgspp_set_len\n";
                    else *out_ << "\tcall\tgspp_dict_len\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    return;
                }
                if (expr->left->exprType.kind == Type::Kind::Dict) {
                    if (expr->ident == "get") {
                        emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
                        emitExprToRax(expr->args[0].get()); *out_ << "\tpushq\t%rax\n";
                        if (expr->args.size() > 1) emitExprToRax(expr->args[1].get()); else *out_ << "\tmovq\t$0, %rax\n";
                        *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_get_default\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        *out_ << "\tpopq\t%rcx\n";
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                        return;
                    }
                }
            }
            if (expr->ident == "println" || expr->ident == "print" || expr->ident == "log") {
                for (auto& a : expr->args) {
                    if (a->exprType.kind == Type::Kind::Float) {
                        emitExprToXmm0(a.get());
                        emitCall(expr->ident == "log" || expr->ident == "println" ? "println_float" : "print_float", 1);
                    } else if (a->exprType.kind == Type::Kind::String) {
                        emitExprToRax(a.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                        emitCall(expr->ident == "log" || expr->ident == "println" ? "println_string" : "print_string", 1);
                    } else {
                        emitExprToRax(a.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                        emitCall(expr->ident == "log" || expr->ident == "println" ? "println" : "print", 1);
                    }
                }
                return;
            }

            for (int i = (int)expr->args.size() - 1; i >= 0; i--) {
                if (expr->args[i]->exprType.kind == Type::Kind::Float) emitExprToXmm0(expr->args[i].get());
                else emitExprToRax(expr->args[i].get());
                *out_ << "\tpushq\t%rax\n";
            }
            if (expr->left && !expr->ident.empty()) {
                emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
            }

            for (int i = 0; i < (int)expr->args.size() + (expr->left && !expr->ident.empty() ? 1 : 0); i++) {
                if (i < num_regs) *out_ << "\tpopq\t%" << regs[i] << "\n";
            }

            std::string label;
            if (!expr->ns.empty()) {
                if (semantic_->moduleFunctions().count(expr->ns) && semantic_->moduleFunctions().at(expr->ns).count(expr->ident))
                    label = semantic_->moduleFunctions().at(expr->ns).at(expr->ident).mangledName;
                else label = expr->ns + "_" + expr->ident;
            } else if (expr->left && !expr->ident.empty()) {
                Type baseType = expr->left->exprType;
                if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
                StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
                if (sd && sd->methods.count(expr->ident)) label = sd->methods.at(expr->ident).mangledName;
                else label = expr->ident;
            } else {
                auto it = semantic_->functions().find(expr->ident);
                if (it != semantic_->functions().end()) label = it->second.mangledName;
                else label = expr->ident;
            }

            emitCall(label, (int)expr->args.size());
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Member: {
            emitExprToRax(expr->left.get());
            Type baseType = expr->left->exprType;
            if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
            StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
            if (sd) {
                auto it = sd->memberIndex.find(expr->member);
                if (it != sd->memberIndex.end()) *out_ << "\tmovq\t" << (it->second * 8) << "(%rax), %" << dest << "\n";
            }
            break;
        }
        case Expr::Kind::Cast:
            emitExpr(expr->left.get(), dest, wantFloat);
            break;
        case Expr::Kind::Sizeof:
            *out_ << "\tmovq\t$" << getTypeSize(*expr->targetType) << ", %" << dest << "\n";
            break;
        case Expr::Kind::Deref:
            emitExprToRax(expr->right.get());
            *out_ << "\tmovq\t(%rax), %" << dest << "\n";
            break;
        case Expr::Kind::AddressOf:
            if (expr->right->kind == Expr::Kind::Var) {
                std::string loc = getVarLocation(expr->right->ident);
                *out_ << "\tleaq\t" << loc << ", %" << dest << "\n";
            }
            break;
        case Expr::Kind::New: {
            int sz = getTypeSize(*expr->targetType);
            *out_ << "\tmovq\t$" << sz << ", %rdi\n";
            emitCall("malloc", 1);
            *out_ << "\tpushq\t%rax\n"; // Save pointer to object

            // Call init if it exists
            StructDef* sd = resolveStruct(expr->targetType->structName, expr->targetType->ns);
            FuncSymbol* initSym = semantic_->getMethod(sd, "init");
            if (initSym) {
                const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
                const char** regs = isLinux_ ? regs_linux : regs_win;
                int num_regs = isLinux_ ? 6 : 4;

                // Push arguments in reverse
                for (int i = (int)expr->args.size() - 1; i >= 0; i--) {
                    emitExprToRax(expr->args[i].get());
                    *out_ << "\tpushq\t%rax\n";
                }
                // self pointer is already on stack but we need it in first reg
                *out_ << "\tmovq\t" << (expr->args.size() * 8) << "(%rsp), %" << regs[0] << "\n";

                // Pop other args into regs
                for (int i = 0; i < (int)expr->args.size(); i++) {
                    if (i + 1 < num_regs) *out_ << "\tpopq\t%" << regs[i+1] << "\n";
                }
                emitCall(initSym->mangledName, (int)expr->args.size() + 1);
                // Clean up any remaining args on stack
                if (expr->args.size() + 1 > (size_t)num_regs) {
                    // TODO: handle more than 6 args
                }
            }

            *out_ << "\tpopq\t%rax\n";
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Delete:
            emitExprToRax(expr->right.get());
            *out_ << "\tmovq\t%rax, %rdi\n";
            emitCall("free", 1);
            break;
        case Expr::Kind::Receive:
            emitExprToRax(expr->right.get());
            *out_ << "\tmovq\t%rax, %rdi\n";
            emitCall("gspp_chan_recv", 1);
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        case Expr::Kind::ChanInit:
            if (!expr->args.empty()) {
                emitExprToRax(expr->args[0].get());
                *out_ << "\tmovq\t%rax, %rdi\n";
            } else {
                *out_ << "\tmovq\t$0, %rdi\n";
            }
            emitCall("gspp_chan_new", 1);
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        case Expr::Kind::Super: {
            std::string loc = getVarLocation("self");
            if (loc.empty()) { error("super used without self", expr->loc); return; }
            *out_ << "\tmovq\t" << loc << ", %" << dest << "\n";
            break;
        }
    }
}

} // namespace gspp
