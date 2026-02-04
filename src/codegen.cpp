#include "codegen.h"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace gspp {

CodeGenerator::CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit)
    : program_(program), semantic_(semantic), out_(&out), use32Bit_(use32Bit) {
#ifdef _WIN32
    isLinux_ = false;
#else
    isLinux_ = true;
#endif
}

void CodeGenerator::error(const std::string& msg, SourceLoc loc) {
    errors_.push_back(SourceManager::instance().formatError(loc, msg));
}

std::string CodeGenerator::nextLabel() { return ".L" + std::to_string(labelCounter_++); }

int CodeGenerator::getFrameSize() {
    int slot = 8;
    int n = 0;
    n = (int)currentVars_.size() * slot;
    n = (n + 15) & ~15;
    if (n < 32) n = 32;
    return n;
}

std::string CodeGenerator::getVarLocation(const std::string& name) {
    auto it = currentVars_.find(name);
    if (it == currentVars_.end()) return "";
    return std::to_string(it->second.frameOffset) + "(%rbp)";
}

int CodeGenerator::getTypeSize(const Type& t) {
    if (t.kind == Type::Kind::Int || t.kind == Type::Kind::Float || t.kind == Type::Kind::Pointer || t.kind == Type::Kind::String || t.kind == Type::Kind::List || t.kind == Type::Kind::Dict || t.kind == Type::Kind::Set || t.kind == Type::Kind::Tuple) return 8;
    if (t.kind == Type::Kind::Bool || t.kind == Type::Kind::Char) return 1;
    if (t.kind == Type::Kind::StructRef) { StructDef* sd = resolveStruct(t.structName, t.ns); return sd ? (int)sd->sizeBytes : 8; }
    return 0;
}

StructDef* CodeGenerator::resolveStruct(const std::string& name, const std::string& ns) {
    auto sd = semantic_->getStruct(name, ns);
    if (!sd && ns.empty()) sd = semantic_->getStruct(name, currentNamespace_);
    return sd;
}

FuncSymbol* CodeGenerator::resolveFunc(const std::string& name, const std::string& ns) {
    auto fs = semantic_->getFunc(name, ns);
    if (!fs && ns.empty()) fs = semantic_->getFunc(name, currentNamespace_);
    return fs;
}

void CodeGenerator::emitExprToRax(Expr* expr) { emitExpr(expr, "rax", false); }
void CodeGenerator::emitExprToXmm0(Expr* expr) { emitExpr(expr, "xmm0", true); }

void CodeGenerator::emitExpr(Expr* expr, const std::string& destReg, bool wantFloat) {
    if (!expr) return;
    std::string dest = destReg;
    const char* rax = "rax";
    switch (expr->kind) {
        case Expr::Kind::IntLit: *out_ << "\tmovq\t$" << expr->intVal << ", %" << dest << "\n"; break;
        case Expr::Kind::FloatLit: {
            uint64_t bits; memcpy(&bits, &expr->floatVal, 8);
            if (destReg[0] == 'x') {
                *out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %rax\n";
                *out_ << "\tmovq\t%rax, %" << destReg << "\n";
            } else {
                *out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %" << destReg << "\n";
            }
            break;
        }
        case Expr::Kind::BoolLit: *out_ << "\tmovq\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n"; break;
        case Expr::Kind::StringLit: {
            std::string label; if (stringPool_.count(expr->ident)) label = stringPool_[expr->ident];
            else { label = ".LS" + std::to_string(stringPool_.size()); stringPool_[expr->ident] = label; }
            *out_ << "\tleaq\t" << label << "(%rip), %" << dest << "\n";
            break;
        }
        case Expr::Kind::ListLit: {
            int numArgs = (int)expr->args.size();
            *out_ << "\tmovq\t$" << numArgs << ", %rdi\n\tcall\tgspp_list_new\n";
            for (int i = 0; i < numArgs; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get()); *out_ << "\tmovq\t%rax, %rsi\n\tpopq\t%rdi\n\tpushq\t%rdi\n\tcall\tgspp_list_append\n\tpopq\t%rax\n";
            }
            if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::DictLit: {
            int numPairs = (int)expr->args.size() / 2;
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tgspp_dict_new\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            for (int i = 0; i < numPairs; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i*2].get()); *out_ << "\tpushq\t%rax\n";
                emitExprToRax(expr->args[i*2+1].get());
                *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tgspp_dict_set\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
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
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tgspp_set_new\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get());
                *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tgspp_set_add\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
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
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tgspp_tuple_new\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            for (int i = 0; i < n; i++) {
                *out_ << "\tpushq\t%rax\n"; emitExprToRax(expr->args[i].get());
                *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tmovq\t$" << i << ", %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tgspp_tuple_set\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
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
            else if (expr->op == "*") *out_ << "\timulq\t%rcx, %rax\n";
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
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tgspp_dict_get\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            }
            else if (expr->left->exprType.kind == Type::Kind::Tuple) {
                *out_ << "\tmovq\t%rdx, %" << regs[0] << "\n\tmovq\t%rax, %" << regs[1] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tgspp_tuple_get\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
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
        case Expr::Kind::Call: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            int num_regs = isLinux_ ? 6 : 4;

            if (expr->left && expr->left->exprType.kind == Type::Kind::String && expr->ident == "len") {
                emitExprToRax(expr->left.get());
                *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                *out_ << "\tcall\tstrlen\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                return;
            }
            if (expr->left && (expr->left->exprType.kind == Type::Kind::Set || expr->left->exprType.kind == Type::Kind::Dict)) {
                if (expr->ident == "len") {
                    emitExprToRax(expr->left.get());
                    *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
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
                        *out_ << "\tpopq\t%rcx\n"; // cleanup push %regs[0]
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                        return;
                    }
                    if (expr->ident == "pop") {
                        emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
                        emitExprToRax(expr->args[0].get()); *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n\tpushq\t%" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_pop\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        *out_ << "\tpopq\t%rcx\n";
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                        return;
                    }
                    if (expr->ident == "remove") {
                        emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
                        emitExprToRax(expr->args[0].get()); *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_remove\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        return;
                    }
                    if (expr->ident == "clear") {
                        emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_clear\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        return;
                    }
                    if (expr->ident == "keys") {
                        emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_keys\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                        return;
                    }
                    if (expr->ident == "values") {
                        emitExprToRax(expr->left.get()); *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\tgspp_dict_values\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                        return;
                    }
                }
            }
            if (expr->left && expr->left->exprType.kind == Type::Kind::List) {
                if (expr->ident == "len") { emitExprToRax(expr->left.get()); *out_ << "\tmovq\t8(%rax), %rax\n"; if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n"; return; }
                if (expr->ident == "append") {
                    emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(expr->args[0].get());
                    *out_ << "\tmovq\t%rax, %" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                    if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                    *out_ << "\tcall\tgspp_list_append\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    return;
                }
            }
            if (expr->left && (expr->left->exprType.kind == Type::Kind::StructRef || (expr->left->exprType.kind == Type::Kind::Pointer && expr->left->exprType.ptrTo && expr->left->exprType.ptrTo->kind == Type::Kind::StructRef))) {
                Type& bty = (expr->left->exprType.kind == Type::Kind::Pointer) ? *expr->left->exprType.ptrTo : expr->left->exprType;
                StructDef* sd = resolveStruct(bty.structName, bty.ns);
                if (sd && sd->methods.count(expr->ident)) {
                    FuncSymbol& ms = sd->methods[expr->ident];
                    emitExprToRax(expr->left.get()); *out_ << "\tpushq\t%rax\n\tsubq\t$8, %rsp\n";
                    for (size_t i = 0; i < expr->args.size() && i < (size_t)(num_regs - 1); i++) emitExpr(expr->args[i].get(), regs[i+1], false);
                    *out_ << "\tmovq\t8(%rsp), %" << regs[0] << "\n";
                    if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                    *out_ << "\tcall\t" << ms.mangledName << "\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    *out_ << "\taddq\t$16, %rsp\n";
                    if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    return;
                }
            }
            if (expr->ns.empty()) {
                StructDef* sd = resolveStruct(expr->ident, "");
                if (sd) {
                    *out_ << "\tmovq\t$" << sd->sizeBytes << ", %" << regs[0] << "\n";
                    if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                    *out_ << "\tcall\tmalloc\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    if (sd->methods.count("init")) {
                        *out_ << "\tpushq\t%rax\n\tsubq\t$8, %rsp\n";
                        for (size_t i = 0; i < expr->args.size() && i < (size_t)(num_regs - 1); i++) emitExpr(expr->args[i].get(), regs[i+1], false);
                        *out_ << "\tmovq\t8(%rsp), %" << regs[0] << "\n";
                        if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                        *out_ << "\tcall\t" << sd->mangledName << "_init\n";
                        if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                        *out_ << "\taddq\t$8, %rsp\n\tpopq\t%rax\n";
                    }
                    if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    return;
                }
            }
            std::string funcName = expr->ident;
            if (expr->ns.empty() && (funcName == "print" || funcName == "println" || funcName == "log")) {
                for (size_t i = 0; i < expr->args.size(); i++) {
                    std::string subFunc = (funcName == "log") ? "println" : funcName;
                    if (expr->args[i]->exprType.kind == Type::Kind::String) subFunc += "_string";
                    else if (expr->args[i]->exprType.kind == Type::Kind::Float) subFunc += "_float";
                    if (i < expr->args.size() - 1) { if (subFunc == "println") subFunc = "print"; if (subFunc == "println_string") subFunc = "print_string"; if (subFunc == "println_float") subFunc = "print_float"; }
                    FuncSymbol* fs = resolveFunc(subFunc, "");
                    if (fs) { if (expr->args[i]->exprType.kind == Type::Kind::Float) emitExpr(expr->args[i].get(), "xmm0", true); else emitExpr(expr->args[i].get(), "rdi", false); *out_ << "\tcall\t" << fs->mangledName << "\n"; }
                }
                return;
            }
            FuncSymbol* fs = resolveFunc(funcName, expr->ns);
            if (!fs) { error("unknown function " + expr->ident, expr->loc); return; }
            const char* fregs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
            int ireg = 0, freg = 0;
            for (size_t i = 0; i < expr->args.size(); i++) {
                if (expr->args[i]->exprType.kind == Type::Kind::Float) { if (freg < 8) emitExpr(expr->args[i].get(), fregs[freg++], true); else { emitExprToRax(expr->args[i].get()); *out_ << "\tpushq\t%rax\n"; } }
                else { if (ireg < num_regs) emitExpr(expr->args[i].get(), regs[ireg++], false); else { emitExprToRax(expr->args[i].get()); *out_ << "\tpushq\t%rax\n"; } }
            }
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\t" << fs->mangledName << "\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            int totalPushed = (ireg > num_regs ? ireg - num_regs : 0) + (freg > 8 ? freg - 8 : 0);
            if (totalPushed > 0) *out_ << "\taddq\t$" << (totalPushed * 8) << ", %rsp\n";
            if (fs->returnType.kind == Type::Kind::Float) {
                if (dest != "xmm0") {
                    if (dest[0] == 'x') *out_ << "\tmovsd\t%xmm0, %" << dest << "\n";
                    else *out_ << "\tmovq\t%xmm0, %" << dest << "\n";
                }
            } else { if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n"; }
            break;
        }
        case Expr::Kind::Member: {
            emitExprToRax(expr->left.get()); Type baseType = expr->left->exprType; if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
            StructDef* sd = resolveStruct(baseType.structName, baseType.ns); if (!sd) { error("unknown struct", expr->loc); return; }
            auto it = sd->memberIndex.find(expr->member); if (it == sd->memberIndex.end()) { error("no member " + expr->member, expr->loc); return; }
            int offset = (int)(it->second * 8); *out_ << "\tmovq\t" << offset << "(%rax), %" << dest << "\n";
            break;
        }
        case Expr::Kind::AddressOf: {
            if (expr->right->kind == Expr::Kind::Var) {
                std::string loc = getVarLocation(expr->right->ident);
                if (!loc.empty()) *out_ << "\tleaq\t" << loc << ", %" << dest << "\n";
            } else if (expr->right->kind == Expr::Kind::Member) {
                emitExprToRax(expr->right->left.get());
                Type baseType = expr->right->left->exprType; if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
                StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
                if (sd) {
                    auto it = sd->memberIndex.find(expr->right->member);
                    if (it != sd->memberIndex.end()) {
                        int offset = (int)(it->second * 8);
                        *out_ << "\taddq\t$" << offset << ", %rax\n";
                        if (dest != rax) *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    }
                }
            }
            break;
        }
        case Expr::Kind::Deref: {
            emitExprToRax(expr->right.get());
            *out_ << "\tmovq\t(%rax), %" << dest << "\n";
            break;
        }
        case Expr::Kind::New: {
            int size = getTypeSize(*expr->targetType);
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            if (expr->left) {
                emitExprToRax(expr->left.get());
                *out_ << "\timulq\t$" << size << ", %rax\n";
                *out_ << "\tmovq\t%rax, %" << regs[0] << "\n";
            } else {
                *out_ << "\tmovq\t$" << size << ", %" << regs[0] << "\n";
            }
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tmalloc\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
            break;
        }
        case Expr::Kind::Delete: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;
            emitExpr(expr->right.get(), regs[0], false);
            if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
            *out_ << "\tcall\tfree\n";
            if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            break;
        }
        case Expr::Kind::Cast: {
            emitExpr(expr->right.get(), destReg, wantFloat);
            break;
        }
        case Expr::Kind::Sizeof: {
            int size = 0;
            if (expr->targetType) size = getTypeSize(*expr->targetType);
            else size = getTypeSize(expr->right->exprType);
            *out_ << "\tmovq\t$" << size << ", %" << dest << "\n";
            break;
        }
        default: break;
    }
}

void CodeGenerator::emitStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block: { deferStack_.emplace_back(); for (auto& s : stmt->blockStmts) emitStmt(s.get()); auto& defers = deferStack_.back(); for (auto it = defers.rbegin(); it != defers.rend(); ++it) emitStmt(*it); deferStack_.pop_back(); break; }
        case Stmt::Kind::VarDecl: if (stmt->varInit) { emitExprToRax(stmt->varInit.get()); std::string loc = getVarLocation(stmt->varName); if (!loc.empty()) *out_ << "\tmovq\t%rax, " << loc << "\n"; } break;
        case Stmt::Kind::Assign: {
            const char* regs_linux[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            const char* regs_win[] = {"rcx", "rdx", "r8", "r9"};
            const char** regs = isLinux_ ? regs_linux : regs_win;

            if (stmt->assignTarget->kind == Expr::Kind::Var) { emitExprToRax(stmt->assignValue.get()); std::string loc = getVarLocation(stmt->assignTarget->ident); if (!loc.empty()) *out_ << "\tmovq\t%rax, " << loc << "\n"; }
            else if (stmt->assignTarget->kind == Expr::Kind::Member) { emitExprToRax(stmt->assignTarget->left.get()); *out_ << "\tpushq\t%rax\n"; emitExprToRax(stmt->assignValue.get()); *out_ << "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n"; Type baseType = stmt->assignTarget->left->exprType; if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo; StructDef* sd = resolveStruct(baseType.structName, baseType.ns); if (sd) { auto it = sd->memberIndex.find(stmt->assignTarget->member); if (it != sd->memberIndex.end()) *out_ << "\tmovq\t%rcx, " << (it->second * 8) << "(%rax)\n"; } }
            else if (stmt->assignTarget->kind == Expr::Kind::Index) {
                Type baseTy = stmt->assignTarget->left->exprType;
                if (baseTy.kind == Type::Kind::Tuple) {
                    emitExprToRax(stmt->assignTarget->left.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignTarget->right.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignValue.get());
                    *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                    if (!isLinux_) *out_ << "\tsubq\t$32, %rsp\n";
                    *out_ << "\tcall\tgspp_tuple_set\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                } else if (baseTy.kind == Type::Kind::List) {
                    emitExprToRax(stmt->assignTarget->left.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignTarget->right.get()); *out_ << "\tpushq\t%rax\n";
                    emitExprToRax(stmt->assignValue.get());
                    *out_ << "\tmovq\t%rax, %" << regs[2] << "\n\tpopq\t%" << regs[1] << "\n\tpopq\t%" << regs[0] << "\n";
                    *out_ << "\tmovq\t(%" << regs[0] << "), %rax\n"; // list->data
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
        case Stmt::Kind::Defer: if (!deferStack_.empty()) deferStack_.back().push_back(stmt->body.get()); break;
        case Stmt::Kind::Return: if (stmt->returnExpr) { if (stmt->returnExpr->exprType.kind == Type::Kind::Float) emitExprToXmm0(stmt->returnExpr.get()); else emitExprToRax(stmt->returnExpr.get()); } else *out_ << "\tmovq\t$0, %rax\n"; for (auto sit = deferStack_.rbegin(); sit != deferStack_.rend(); ++sit) for (auto it = sit->rbegin(); it != sit->rend(); ++it) emitStmt(*it); *out_ << "\tleave\n\tret\n"; break;
        case Stmt::Kind::For: emitStmt(stmt->initStmt.get()); { std::string condLabel = nextLabel(), endLabel = nextLabel(); *out_ << condLabel << ":\n"; emitExprToRax(stmt->condition.get()); *out_ << "\ttestq\t%rax, %rax\n\tje\t" << endLabel << "\n"; emitStmt(stmt->body.get()); emitStmt(stmt->stepStmt.get()); *out_ << "\tjmp\t" << condLabel << "\n" << endLabel << ":\n"; } break;
        case Stmt::Kind::ExprStmt: emitExprToRax(stmt->expr.get()); break;
        case Stmt::Kind::Unsafe: emitStmt(stmt->body.get()); break;
        case Stmt::Kind::Asm: *out_ << "\t" << stmt->asmCode << "\n"; break;
    }
}

void CodeGenerator::emitFunc(const FuncSymbol& fs) {
    if (fs.isExtern) return;
    if (fs.mangledName == "println" || fs.mangledName == "print" || fs.mangledName == "print_float" || fs.mangledName == "println_float" || fs.mangledName == "print_string" || fs.mangledName == "println_string" || fs.mangledName == "gspp_input" || fs.mangledName == "gspp_read_file" || fs.mangledName == "gspp_write_file" || fs.mangledName == "abs" || fs.mangledName == "sqrt" || fs.mangledName == "gspp_exec") return;
    currentFunc_ = fs.decl; currentVars_ = fs.locals; currentNamespace_ = fs.ns; frameSize_ = getFrameSize();
    std::string label = fs.mangledName; *out_ << "\t.globl\t" << label << "\n" << label << ":\n\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$" << frameSize_ << ", %rsp\n";
    if (fs.decl) {
        const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"}; const char* fregs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"}; int ireg = 0, freg = 0;
        if (fs.isMethod) { std::string loc = getVarLocation("self"); if (!loc.empty()) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n"; }
        for (size_t i = 0; i < fs.decl->params.size(); i++) { std::string loc = getVarLocation(fs.decl->params[i].name); if (loc.empty()) continue; if (fs.decl->params[i].type.kind == Type::Kind::Float) { if (freg < 8) *out_ << "\tmovq\t%" << fregs[freg++] << ", " << loc << "\n"; } else { if (ireg < 6) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n"; } }
    }
    if (fs.decl && fs.decl->body) emitStmt(fs.decl->body.get());
    if (fs.mangledName == "main") *out_ << "\tmovq\t$0, %rax\n";
    *out_ << "\tleave\n\tret\n\n"; currentFunc_ = nullptr;
}

void CodeGenerator::emitProgram() {
    *out_ << "\t.file\t\"gspp\"\n"; std::ostringstream textOut; std::ostream* originalOut = out_; out_ = &textOut; emitProgramBody(); out_ = originalOut;
    *out_ << "\t.data\n.LC_fmt_d:\n\t.string \"%d\"\n.LC_fmt_d_nl:\n\t.string \"%d\\n\"\n.LC_fmt_f:\n\t.string \"%f\"\n.LC_fmt_f_nl:\n\t.string \"%f\\n\"\n.LC_fmt_s:\n\t.string \"%s\"\n.LC_fmt_s_nl:\n\t.string \"%s\\n\"\n";
    for (auto& p : stringPool_) *out_ << p.second << ":\n\t.string \"" << p.first << "\"\n";
    *out_ << "\t.text\n" << textOut.str();
}

void CodeGenerator::emitProgramBody() {
    *out_ << "\t.extern\tprintf\n\t.extern\tstrlen\n\t.extern\tstrcpy\n\t.extern\tstrcat\n\t.extern\tmalloc\n\t.extern\tfree\n\t.extern\tabs\n\t.extern\tsqrt\n";
    *out_ << "\t.extern\texit\n\t.extern\tusleep\n\t.extern\tsin\n\t.extern\tcos\n\t.extern\ttan\n\t.extern\tpow\n";
    *out_ << "\t.extern\tprintln\n\t.extern\tprint\n\t.extern\tprintln_float\n\t.extern\tprint_float\n\t.extern\tprintln_string\n\t.extern\tprint_string\n";
    *out_ << "\t.extern\t_gspp_strcat\n\t.extern\tgspp_input\n\t.extern\tgspp_read_file\n\t.extern\tgspp_write_file\n\t.extern\tgspp_exec\n";
    *out_ << "\t.extern\tgspp_list_new\n\t.extern\tgspp_list_append\n\t.extern\tgspp_list_slice\n";
    *out_ << "\t.extern\tgspp_str_slice\n\t.extern\tgspp_dict_new\n\t.extern\tgspp_dict_set\n\t.extern\tgspp_dict_get\n";
    *out_ << "\t.extern\tgspp_tuple_new\n\t.extern\tgspp_tuple_set\n\t.extern\tgspp_tuple_get\n";
    *out_ << "\t.extern\tgspp_set_new\n\t.extern\tgspp_set_add\n\t.extern\tgspp_set_union\n\t.extern\tgspp_set_intersection\n";
    *out_ << "\t.extern\tgspp_dict_union\n\t.extern\tgspp_dict_intersection\n\t.extern\tgspp_dict_len\n";
    *out_ << "\t.extern\tgspp_dict_get_default\n\t.extern\tgspp_dict_pop\n\t.extern\tgspp_dict_remove\n\t.extern\tgspp_dict_clear\n\t.extern\tgspp_dict_keys\n\t.extern\tgspp_dict_values\n";
    *out_ << "\t.extern\tgspp_set_len\n";
    for (const auto& pair : semantic_->functions()) emitFunc(pair.second);
    for (const auto& modPair : semantic_->moduleFunctions()) for (const auto& pair : modPair.second) emitFunc(pair.second);
    for (const auto& pair : semantic_->structs()) for (const auto& mPair : pair.second.methods) emitFunc(mPair.second);
}

bool CodeGenerator::generate() { emitProgram(); return errors_.empty(); }

}
