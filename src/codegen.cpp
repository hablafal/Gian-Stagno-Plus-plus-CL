#include "codegen.h"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace gspp {

CodeGenerator::CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit)
    : program_(program), semantic_(semantic), out_(&out), use32Bit_(use32Bit) {
#ifndef _WIN32
    isLinux_ = true;
#endif
}

void CodeGenerator::error(const std::string& msg, SourceLoc loc) {
    errors_.push_back(SourceManager::instance().formatError(loc, msg));
}

std::string CodeGenerator::nextLabel() {
    return ".L" + std::to_string(labelCounter_++);
}

int CodeGenerator::getFrameSize() {
    int slot = use32Bit_ ? 4 : 8;
    int n = 0;
    for (const auto& p : currentVars_)
        if (p.second.frameOffset < 0) n += slot;
    if (use32Bit_) {
        n = (n + 15) & ~15;  // align to 16 for cdecl
    } else {
        n = (n + 15) & ~15;
        if (n < 32) n = 32;  // shadow space for Windows x64
    }
    return n;
}

std::string CodeGenerator::getVarLocation(const std::string& name) {
    auto it = currentVars_.find(name);
    if (it == currentVars_.end()) return "";
    int off = it->second.frameOffset;
    if (use32Bit_) {
        if (off >= 0) off = 8 + (off - 16) / 2;  // 16,24,32,40 -> 8,12,16,20
        else off = off / 2;  // -8,-16 -> -4,-8
        return std::to_string(off) + "(%ebp)";
    }
    return std::to_string(off) + "(%rbp)";
}

int CodeGenerator::getTypeSize(const Type& t) {
    if (t.kind == Type::Kind::Int || t.kind == Type::Kind::Float || t.kind == Type::Kind::Pointer || t.kind == Type::Kind::String)
        return use32Bit_ ? 4 : 8;
    if (t.kind == Type::Kind::Bool || t.kind == Type::Kind::Char)
        return 1;
    if (t.kind == Type::Kind::StructRef) {
        StructDef* sd = resolveStruct(t.structName, t.ns);
        return sd ? (int)sd->sizeBytes : (use32Bit_ ? 4 : 8);
    }
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

void CodeGenerator::emitExprToRax(Expr* expr) {
    emitExpr(expr, "rax", false);
}

void CodeGenerator::emitExprToXmm0(Expr* expr) {
    emitExpr(expr, "xmm0", true);
}

void CodeGenerator::emitExpr(Expr* expr, const std::string& destReg, bool wantFloat) {
    if (!expr) return;
    std::string dest = destReg;
    if (use32Bit_) {
        if (dest == "rax") dest = "eax";
        else if (dest == "rcx") dest = "ecx";
        else if (dest == "rdx") dest = "edx";
        else if (dest == "r8") dest = "eax";
        else if (dest == "r9") dest = "ecx";
    }
    const char* mov = use32Bit_ ? "movl" : "movq";
    const char* rax = use32Bit_ ? "eax" : "rax";
    switch (expr->kind) {
        case Expr::Kind::IntLit:
            if (use32Bit_)
                *out_ << "\tmovl\t$" << (int32_t)expr->intVal << ", %" << dest << "\n";
            else
                *out_ << "\tmovq\t$" << expr->intVal << ", %" << dest << "\n";
            break;
        case Expr::Kind::FloatLit: {
            if (use32Bit_ && (wantFloat || dest == "xmm0")) {
                float f = (float)expr->floatVal;
                uint32_t bits;
                memcpy(&bits, &f, 4);
                *out_ << "\tmovl\t$0x" << std::hex << bits << std::dec << ", %eax\n";
                *out_ << "\tmovd\t%eax, %xmm0\n";
                if (dest != "xmm0") *out_ << "\tmovd\t%xmm0, %" << dest << "\n";
            } else {
                uint64_t bits;
                memcpy(&bits, &expr->floatVal, 8);
                if (wantFloat || destReg == "xmm0") {
                    *out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %rax\n";
                    *out_ << "\tmovq\t%rax, %xmm0\n";
                    if (dest != "xmm0") *out_ << "\tmovq\t%xmm0, %" << dest << "\n";
                } else {
                    *out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %" << dest << "\n";
                }
            }
            break;
        }
        case Expr::Kind::BoolLit:
            if (use32Bit_)
                *out_ << "\tmovl\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n";
            else
                *out_ << "\tmovq\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n";
            break;
        case Expr::Kind::StringLit: {
            std::string label;
            if (stringPool_.count(expr->ident)) label = stringPool_[expr->ident];
            else {
                label = ".LS" + std::to_string(stringPool_.size());
                stringPool_[expr->ident] = label;
            }
            if (use32Bit_) *out_ << "\tmovl\t$" << label << ", %" << dest << "\n";
            else *out_ << "\tleaq\t" << label << "(%rip), %" << dest << "\n";
            break;
        }
        case Expr::Kind::Var: {
            std::string loc = getVarLocation(expr->ident);
            if (loc.empty()) { error("unknown variable " + expr->ident, expr->loc); return; }
            *out_ << "\t" << mov << "\t" << loc << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Binary: {
            if (expr->op == "and" || expr->op == "or") {
                std::string endLabel = nextLabel();
                emitExprToRax(expr->left.get());
                if (use32Bit_) {
                    if (expr->op == "and") { *out_ << "\ttestl\t%eax, %eax\n"; *out_ << "\tje\t" << endLabel << "\n"; }
                    else { *out_ << "\ttestl\t%eax, %eax\n"; *out_ << "\tjne\t" << endLabel << "\n"; }
                } else {
                    if (expr->op == "and") { *out_ << "\ttestq\t%rax, %rax\n"; *out_ << "\tje\t" << endLabel << "\n"; }
                    else { *out_ << "\ttestq\t%rax, %rax\n"; *out_ << "\tjne\t" << endLabel << "\n"; }
                }
                emitExprToRax(expr->right.get());
                *out_ << endLabel << ":\n";
                if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                return;
            }
            if (expr->op == "==" || expr->op == "!=" || expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
                emitExprToRax(expr->left.get());
                *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
                emitExprToRax(expr->right.get());
                *out_ << (use32Bit_ ? "\tpopl\t%ecx\n" : "\tpopq\t%rcx\n");
                *out_ << (use32Bit_ ? "\tcmpl\t%eax, %ecx\n" : "\tcmpq\t%rax, %rcx\n");
                if (expr->op == "==") *out_ << "\tsete\t%al\n";
                else if (expr->op == "!=") *out_ << "\tsetne\t%al\n";
                else if (expr->op == "<") *out_ << "\tsetl\t%al\n";
                else if (expr->op == ">") *out_ << "\tsetg\t%al\n";
                else if (expr->op == "<=") *out_ << "\tsetle\t%al\n";
                else *out_ << "\tsetge\t%al\n";
                *out_ << (use32Bit_ ? "\tmovzbl\t%al, %eax\n" : "\tmovzbq\t%al, %rax\n");
                if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                return;
            }
            if (expr->left->exprType.kind == Type::Kind::Float) {
                emitExprToXmm0(expr->left.get());
                *out_ << "\tsubq\t$8, %rsp\n\tmovq\t%xmm0, (%rsp)\n";
                emitExprToXmm0(expr->right.get());
                *out_ << "\tmovq\t%xmm0, %xmm1\n\tmovq\t(%rsp), %xmm0\n\taddq\t$8, %rsp\n";
                if (expr->op == "+") *out_ << "\taddsd\t%xmm1, %xmm0\n";
                else if (expr->op == "-") *out_ << "\tsubsd\t%xmm1, %xmm0\n";
                else if (expr->op == "*") *out_ << "\tmulsd\t%xmm1, %xmm0\n";
                else if (expr->op == "/") *out_ << "\tdivsd\t%xmm1, %xmm0\n";
                if (dest != "xmm0") *out_ << "\tmovq\t%xmm0, %" << dest << "\n";
                return;
            }
            emitExprToRax(expr->left.get());
            *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
            emitExprToRax(expr->right.get());
            *out_ << (use32Bit_ ? "\tmovl\t%eax, %ecx\n\tpopl\t%eax\n" : "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n");
            if (expr->op == "+") {
                if (expr->left->exprType.kind == Type::Kind::Pointer) {
                    int size = getTypeSize(*expr->left->exprType.ptrTo);
                    if (use32Bit_) *out_ << "\timull\t$" << size << ", %ecx\n\taddl\t%ecx, %eax\n";
                    else *out_ << "\timulq\t$" << size << ", %rcx\n\taddq\t%rcx, %rax\n";
                    if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                    return;
                }
                if (expr->left->exprType.kind == Type::Kind::String) {
                    if (use32Bit_) {
                        *out_ << "\tpushl\t%ecx\n\tpushl\t%eax\n\tcall\t_gspp_strcat\n\taddl\t$8, %esp\n";
                    } else {
                        if (isLinux_) {
                            *out_ << "\tmovq\t%rax, %rdi\n\tmovq\t%rcx, %rsi\n\tcall\t_gspp_strcat\n";
                        } else {
                            *out_ << "\tmovq\t%rax, %rcx\n\tmovq\t%rcx, %rdx\n\tcall\t_gspp_strcat\n";
                        }
                    }
                } else {
                    *out_ << (use32Bit_ ? "\taddl\t%ecx, %eax\n" : "\taddq\t%rcx, %rax\n");
                }
            } else if (expr->op == "-") {
                if (expr->left->exprType.kind == Type::Kind::Pointer) {
                    int size = getTypeSize(*expr->left->exprType.ptrTo);
                    if (use32Bit_) *out_ << "\timull\t$" << size << ", %ecx\n\tsubl\t%ecx, %eax\n";
                    else *out_ << "\timulq\t$" << size << ", %rcx\n\tsubq\t%rcx, %rax\n";
                    if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                    return;
                }
                *out_ << (use32Bit_ ? "\tsubl\t%ecx, %eax\n" : "\tsubq\t%rcx, %rax\n");
            }
            else if (expr->op == "*") *out_ << (use32Bit_ ? "\timull\t%ecx, %eax\n" : "\timulq\t%rcx, %rax\n");
            else if (expr->op == "/") {
                if (use32Bit_) { *out_ << "\tcdq\n\tidivl\t%ecx\n"; }
                else { *out_ << "\tcqto\n\tidivq\t%rcx\n"; }
            } else if (expr->op == "%") {
                if (use32Bit_) { *out_ << "\tcdq\n\tidivl\t%ecx\n\tmovl\t%edx, %eax\n"; }
                else { *out_ << "\tcqto\n\tidivq\t%rcx\n\tmovq\t%rdx, %rax\n"; }
            }
            if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Unary:
            if (expr->op == "-") {
                emitExprToRax(expr->right.get());
                *out_ << (use32Bit_ ? "\tnegl\t%eax\n" : "\tnegq\t%rax\n");
            } else if (expr->op == "not") {
                emitExprToRax(expr->right.get());
                *out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
                *out_ << "\tsete\t%al\n";
                *out_ << (use32Bit_ ? "\tmovzbl\t%al, %eax\n" : "\tmovzbq\t%al, %rax\n");
            }
            if (dest != "rax" && dest != "eax") *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
            break;
        case Expr::Kind::Call: {
            std::string funcName = expr->ident;
            if (expr->ns.empty()) {
                if (funcName == "print" && !expr->args.empty()) {
                    if (expr->args[0]->exprType.kind == Type::Kind::String) funcName = "print_string";
                }
                if (funcName == "println" && !expr->args.empty()) {
                    if (expr->args[0]->exprType.kind == Type::Kind::String) funcName = "println_string";
                }
            }
            FuncSymbol* fs = resolveFunc(funcName, expr->ns);
            if (!fs) { error("unknown function " + expr->ident, expr->loc); return; }
            if (use32Bit_) {
                // cdecl: push args right to left
                for (int i = (int)expr->args.size() - 1; i >= 0; i--)
                    emitExprToRax(expr->args[i].get()), *out_ << "\tpushl\t%eax\n";
                *out_ << "\tcall\t" << fs->mangledName << "\n";
                *out_ << "\taddl\t$" << (4 * (int)expr->args.size()) << ", %esp\n";
                if (dest != "rax" && dest != "eax") *out_ << "\tmovl\t%eax, %" << dest << "\n";
            } else {
                if (isLinux_) {
                    const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                    const char* fregs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
                    int ireg = 0, freg = 0;
                    for (size_t i = 0; i < expr->args.size(); i++) {
                        if (expr->args[i]->exprType.kind == Type::Kind::Float) {
                            if (freg < 8) emitExpr(expr->args[i].get(), fregs[freg++], true);
                            else { emitExprToRax(expr->args[i].get()); *out_ << "\tpushq\t%rax\n"; }
                        } else {
                            if (ireg < 6) emitExpr(expr->args[i].get(), regs[ireg++], false);
                            else { emitExprToRax(expr->args[i].get()); *out_ << "\tpushq\t%rax\n"; }
                        }
                    }
                    *out_ << "\tmovl\t$" << freg << ", %eax\n"; // for varargs
                    *out_ << "\tcall\t" << fs->mangledName << "\n";
                    int totalPushed = (ireg > 6 ? ireg - 6 : 0) + (freg > 8 ? freg - 8 : 0);
                    if (totalPushed > 0) *out_ << "\taddq\t$" << (totalPushed * 8) << ", %rsp\n";
                } else {
                    bool floatFirst = (expr->ident == "print_float" || expr->ident == "println_float") && !expr->args.empty();
                    for (size_t i = 0; i < expr->args.size(); i++) {
                        if (i < 4) {
                            if (i == 0 && floatFirst) emitExpr(expr->args[i].get(), "xmm0", true);
                            else emitExpr(expr->args[i].get(), i == 0 ? "rcx" : i == 1 ? "rdx" : i == 2 ? "r8" : "r9", false);
                        } else {
                            *out_ << "\tsubq\t$8, %rsp\n";
                            emitExprToRax(expr->args[i].get());
                            *out_ << "\tpushq\t%rax\n";
                        }
                    }
                    *out_ << "\tsubq\t$32, %rsp\n";
                    *out_ << "\tcall\t" << fs->mangledName << "\n";
                    *out_ << "\taddq\t$32, %rsp\n";
                    for (size_t i = 4; i < expr->args.size(); i++) *out_ << "\taddq\t$8, %rsp\n";
                }
                if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
            }
            break;
        }
        case Expr::Kind::Member: {
            emitExprToRax(expr->left.get());
            Type baseType = expr->left->exprType;
            if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
            StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
            if (!sd) { error("unknown struct", expr->loc); return; }
            auto it = sd->memberIndex.find(expr->member);
            if (it == sd->memberIndex.end()) { error("no member " + expr->member, expr->loc); return; }
            int offset = (int)(it->second * (use32Bit_ ? 4 : 8));
            *out_ << "\t" << mov << "\t" << offset << "(%" << rax << "), %" << dest << "\n";
            break;
        }
        case Expr::Kind::Deref: {
            emitExprToRax(expr->right.get());
            *out_ << "\t" << mov << "\t(%" << rax << "), %" << dest << "\n";
            break;
        }
        case Expr::Kind::AddressOf: {
            if (expr->right->kind == Expr::Kind::Var) {
                std::string loc = getVarLocation(expr->right->ident);
                *out_ << "\t" << (use32Bit_ ? "leal" : "leaq") << "\t" << loc << ", %" << dest << "\n";
            } else if (expr->right->kind == Expr::Kind::Member) {
                emitExprToRax(expr->right->left.get());
                Type baseType = expr->right->left->exprType;
                if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
                StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
                if (sd) {
                    auto it = sd->memberIndex.find(expr->right->member);
                    if (it != sd->memberIndex.end()) {
                        int off = (int)(it->second * (use32Bit_ ? 4 : 8));
                        *out_ << "\t" << (use32Bit_ ? "addl" : "addq") << "\t$" << off << ", %" << rax << "\n";
                        if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                    }
                }
            }
            break;
        }
        case Expr::Kind::New: {
            int size = getTypeSize(*expr->targetType);
            if (expr->left) {
                emitExprToRax(expr->left.get());
                if (use32Bit_) {
                    *out_ << "\timull\t$" << size << ", %eax\n";
                    *out_ << "\tpushl\t%eax\n";
                    *out_ << "\tcall\tmalloc\n\taddl\t$4, %esp\n";
                    if (dest != "eax") *out_ << "\tmovl\t%eax, %" << dest << "\n";
                } else {
                    *out_ << "\timulq\t$" << size << ", %rax\n";
                    if (isLinux_) *out_ << "\tmovq\t%rax, %rdi\n";
                    else { *out_ << "\tmovq\t%rax, %rcx\n\tsubq\t$32, %rsp\n"; }
                    *out_ << "\tcall\tmalloc\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                }
            } else {
                if (use32Bit_) {
                    *out_ << "\tpushl\t$" << size << "\n";
                    *out_ << "\tcall\tmalloc\n\taddl\t$4, %esp\n";
                    if (dest != "eax") *out_ << "\tmovl\t%eax, %" << dest << "\n";
                } else {
                    if (isLinux_) *out_ << "\tmovq\t$" << size << ", %rdi\n";
                    else { *out_ << "\tmovq\t$" << size << ", %rcx\n\tsubq\t$32, %rsp\n"; }
                    *out_ << "\tcall\tmalloc\n";
                    if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
                    if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                }
            }
            break;
        }
        case Expr::Kind::Delete: {
            emitExprToRax(expr->right.get());
            if (use32Bit_) {
                *out_ << "\tpushl\t%eax\n";
                *out_ << "\tcall\tfree\n";
                *out_ << "\taddl\t$4, %esp\n";
            } else {
                if (isLinux_) *out_ << "\tmovq\t%rax, %rdi\n";
                else {
                    *out_ << "\tmovq\t%rax, %rcx\n";
                    *out_ << "\tsubq\t$32, %rsp\n";
                }
                *out_ << "\tcall\tfree\n";
                if (!isLinux_) *out_ << "\taddq\t$32, %rsp\n";
            }
            break;
        }
        default:
            *out_ << "\t" << mov << "\t$0, %" << dest << "\n";
            break;
    }
}

void CodeGenerator::emitStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block:
            for (auto& s : stmt->blockStmts) emitStmt(s.get());
            break;
        case Stmt::Kind::VarDecl: {
            if (stmt->varInit) {
                emitExprToRax(stmt->varInit.get());
                std::string loc = getVarLocation(stmt->varName);
                if (!loc.empty()) *out_ << "\t" << (use32Bit_ ? "movl\t%eax, " : "movq\t%rax, ") << loc << "\n";
            }
            break;
        }
        case Stmt::Kind::Assign: {
            if (stmt->assignTarget->kind == Expr::Kind::Var) {
                emitExprToRax(stmt->assignValue.get());
                std::string loc = getVarLocation(stmt->assignTarget->ident);
                if (!loc.empty()) *out_ << "\t" << (use32Bit_ ? "movl\t%eax, " : "movq\t%rax, ") << loc << "\n";
            } else if (stmt->assignTarget->kind == Expr::Kind::Member) {
                emitExprToRax(stmt->assignTarget->left.get());
                *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
                emitExprToRax(stmt->assignValue.get());
                *out_ << (use32Bit_ ? "\tmovl\t%eax, %ecx\n\tpopl\t%eax\n" : "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n");
                Type baseType = stmt->assignTarget->left->exprType;
                if (baseType.kind == Type::Kind::Pointer) baseType = *baseType.ptrTo;
                StructDef* sd = resolveStruct(baseType.structName, baseType.ns);
                if (sd) {
                    auto it = sd->memberIndex.find(stmt->assignTarget->member);
                    if (it != sd->memberIndex.end()) {
                        int off = (int)(it->second * (use32Bit_ ? 4 : 8));
                        *out_ << "\t" << (use32Bit_ ? "movl\t%ecx, " : "movq\t%rcx, ") << off << "(%" << (use32Bit_ ? "eax" : "rax") << ")\n";
                    }
                }
            } else if (stmt->assignTarget->kind == Expr::Kind::Deref) {
                emitExprToRax(stmt->assignTarget->right.get());
                *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
                emitExprToRax(stmt->assignValue.get());
                *out_ << (use32Bit_ ? "\tmovl\t%eax, %ecx\n\tpopl\t%eax\n" : "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n");
                *out_ << "\t" << (use32Bit_ ? "movl\t%ecx, (%eax)" : "movq\t%rcx, (%rax)") << "\n";
            }
            break;
        }
        case Stmt::Kind::If: {
            std::string elseLabel = nextLabel();
            std::string endLabel = nextLabel();
            emitExprToRax(stmt->condition.get());
            *out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            *out_ << "\tje\t" << elseLabel << "\n";
            emitStmt(stmt->thenBranch.get());
            *out_ << "\tjmp\t" << endLabel << "\n";
            *out_ << elseLabel << ":\n";
            if (stmt->elseBranch) emitStmt(stmt->elseBranch.get());
            *out_ << endLabel << ":\n";
            break;
        }
        case Stmt::Kind::While: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            *out_ << "\tjmp\t" << condLabel << "\n";
            *out_ << bodyLabel << ":\n";
            emitStmt(stmt->body.get());
            *out_ << condLabel << ":\n";
            emitExprToRax(stmt->condition.get());
            *out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            *out_ << "\tjne\t" << bodyLabel << "\n";
            break;
        }
        case Stmt::Kind::For: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            std::string stepLabel = nextLabel();
            emitStmt(stmt->initStmt.get());
            *out_ << "\tjmp\t" << condLabel << "\n";
            *out_ << bodyLabel << ":\n";
            emitStmt(stmt->body.get());
            *out_ << stepLabel << ":\n";
            emitStmt(stmt->stepStmt.get());
            *out_ << condLabel << ":\n";
            emitExprToRax(stmt->condition.get());
            *out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            *out_ << "\tjne\t" << bodyLabel << "\n";
            break;
        }
        case Stmt::Kind::Return:
            if (stmt->returnExpr) {
                if (stmt->returnExpr->exprType.kind == Type::Kind::Float)
                    emitExprToXmm0(stmt->returnExpr.get());
                else
                    emitExprToRax(stmt->returnExpr.get());
            } else {
                *out_ << (use32Bit_ ? "\tmovl\t$0, %eax\n" : "\tmovq\t$0, %rax\n");
            }
            *out_ << "\tleave\n\tret\n";
            break;
        case Stmt::Kind::ExprStmt:
            emitExprToRax(stmt->expr.get());
            break;
        case Stmt::Kind::Unsafe:
            emitStmt(stmt->body.get());
            break;
        case Stmt::Kind::Asm:
            *out_ << "\t" << stmt->asmCode << "\n";
            break;
    }
}

void CodeGenerator::emitFunc(const FuncSymbol& fs) {
    if (fs.decl && fs.decl->isExtern) return;
    if (fs.mangledName == "println" || fs.mangledName == "print" || fs.mangledName == "print_float" ||
        fs.mangledName == "println_float" || fs.mangledName == "print_string" || fs.mangledName == "println_string") return;

    currentFunc_ = fs.decl;
    currentVars_ = fs.locals;
    currentNamespace_ = fs.ns;
    frameSize_ = getFrameSize();

    std::string label = fs.mangledName;
    if (use32Bit_ && fs.name == "main") label = "_main";
    *out_ << "\t.globl\t" << label << "\n";
    *out_ << label << ":\n";
    if (use32Bit_) {
        *out_ << "\tpushl\t%ebp\n";
        *out_ << "\tmovl\t%esp, %ebp\n";
        *out_ << "\tsubl\t$" << frameSize_ << ", %esp\n";
    } else {
        *out_ << "\tpushq\t%rbp\n";
        *out_ << "\tmovq\t%rsp, %rbp\n";
        *out_ << "\tsubq\t$" << frameSize_ << ", %rsp\n";
        if (fs.decl) {
            if (isLinux_) {
                const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                const char* fregs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
                int ireg = 0, freg = 0;
                for (size_t i = 0; i < fs.decl->params.size(); i++) {
                    if (fs.decl->params[i].type.kind == Type::Kind::Float) {
                        if (freg < 8) *out_ << "\tmovq\t%" << fregs[freg++] << ", " << (16 + i * 8) << "(%rbp)\n";
                    } else {
                        if (ireg < 6) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << (16 + i * 8) << "(%rbp)\n";
                    }
                }
            } else {
                if (fs.decl->params.size() > 0) *out_ << "\tmovq\t%rcx, 16(%rbp)\n";
                if (fs.decl->params.size() > 1) *out_ << "\tmovq\t%rdx, 24(%rbp)\n";
                if (fs.decl->params.size() > 2) *out_ << "\tmovq\t%r8, 32(%rbp)\n";
                if (fs.decl->params.size() > 3) *out_ << "\tmovq\t%r9, 40(%rbp)\n";
            }
        }
    }
    if (fs.decl && fs.decl->body) emitStmt(fs.decl->body.get());
    *out_ << "\tleave\n";
    *out_ << "\tret\n\n";
    currentFunc_ = nullptr;
}

void CodeGenerator::emitProgram() {
    *out_ << "\t.file\t\"gspp\"\n";

    std::ostringstream textOut;
    std::ostream* originalOut = out_;
    out_ = &textOut;

    emitProgramBody();

    out_ = originalOut;
    *out_ << "\t.data\n";
    *out_ << ".LC_fmt_d:\n\t.string \"%d\"\n";
    *out_ << ".LC_fmt_d_nl:\n\t.string \"%d\\n\"\n";
    *out_ << ".LC_fmt_f:\n\t.string \"%f\"\n";
    *out_ << ".LC_fmt_f_nl:\n\t.string \"%f\\n\"\n";
    *out_ << ".LC_fmt_s:\n\t.string \"%s\"\n";
    *out_ << ".LC_fmt_s_nl:\n\t.string \"%s\\n\"\n";
    for (auto& p : stringPool_) {
        *out_ << p.second << ":\n\t.string \"" << p.first << "\"\n";
    }
    *out_ << "\t.text\n";
    *out_ << textOut.str();
}

void CodeGenerator::emitProgramBody() {
    if (use32Bit_) {
        *out_ << "\t.extern\t_printf\n";
        *out_ << "\t.extern\t_strlen\n";
        *out_ << "\t.extern\t_strcpy\n";
        *out_ << "\t.extern\t_strcat\n";
        *out_ << "\t.extern\tmalloc\n";
        *out_ << "\t.extern\tfree\n";
        *out_ << "\t.globl\tprintln\nprintln:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        *out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_d_nl\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";
        *out_ << "\t.globl\tprint\nprint:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        *out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_d\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";
        *out_ << "\t.globl\tprintln_float\nprintln_float:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n\tmovd\t8(%ebp), %xmm0\n\tmovd\t%xmm0, (%esp)\n\tpushl\t$.LC_fmt_f_nl\n\tcall\t_printf\n\taddl\t$12, %esp\n\tleave\n\tret\n";
        *out_ << "\t.globl\tprint_float\nprint_float:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n\tmovd\t8(%ebp), %xmm0\n\tmovd\t%xmm0, (%esp)\n\tpushl\t$.LC_fmt_f\n\tcall\t_printf\n\taddl\t$12, %esp\n\tleave\n\tret\n\n";
    } else {
        *out_ << "\t.extern\tprintf\n";
        *out_ << "\t.extern\tstrlen\n";
        *out_ << "\t.extern\tstrcpy\n";
        *out_ << "\t.extern\tstrcat\n";
        *out_ << "\t.extern\tmalloc\n";
        *out_ << "\t.extern\tfree\n";
        if (isLinux_) {
            *out_ << "\t.globl\tprintln\nprintln:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tmovq\t%rdi, %rsi\n\tleaq\t.LC_fmt_d_nl(%rip), %rdi\n\tmovl\t$0, %eax\n\tcall\tprintf\n";
            *out_ << "\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint\nprint:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tmovq\t%rdi, %rsi\n\tleaq\t.LC_fmt_d(%rip), %rdi\n\tmovl\t$0, %eax\n\tcall\tprintf\n";
            *out_ << "\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprintln_float\nprintln_float:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tleaq\t.LC_fmt_f_nl(%rip), %rdi\n\tmovl\t$1, %eax\n\tcall\tprintf\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint_float\nprint_float:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tleaq\t.LC_fmt_f(%rip), %rdi\n\tmovl\t$1, %eax\n\tcall\tprintf\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprintln_string\nprintln_string:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tmovq\t%rdi, %rsi\n\tleaq\t.LC_fmt_s_nl(%rip), %rdi\n\tmovl\t$0, %eax\n\tcall\tprintf\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint_string\nprint_string:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
            *out_ << "\tmovq\t%rdi, %rsi\n\tleaq\t.LC_fmt_s(%rip), %rdi\n\tmovl\t$0, %eax\n\tcall\tprintf\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\t_gspp_strcat\n_gspp_strcat:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rdi, 16(%rbp)\n\tmovq\t%rsi, 24(%rbp)\n";
            *out_ << "\tcall\tstrlen\n\tmovq\t%rax, %rbx\n";
            *out_ << "\tmovq\t24(%rbp), %rdi\n\tcall\tstrlen\n\taddq\t%rbx, %rax\n\tincq\t%rax\n";
            *out_ << "\tmovq\t%rax, %rdi\n\tcall\tmalloc\n\tmovq\t%rax, %r10\n";
            *out_ << "\tmovq\t%r10, %rdi\n\tmovq\t16(%rbp), %rsi\n\tcall\tstrcpy\n";
            *out_ << "\tmovq\t%r10, %rdi\n\tmovq\t24(%rbp), %rsi\n\tcall\tstrcat\n";
            *out_ << "\tmovq\t%r10, %rax\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";
        } else {
            *out_ << "\t.globl\tprintln\nprintln:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_d_nl(%rip), %rcx\n\tcall\tprintf\n";
            *out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint\nprint:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_d(%rip), %rcx\n\tcall\tprintf\n";
            *out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprintln_float\nprintln_float:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n\tmovaps\t%xmm0, %xmm1\n\tleaq\t.LC_fmt_f_nl(%rip), %rcx\n\tcall\tprintf\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint_float\nprint_float:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n\tmovaps\t%xmm0, %xmm1\n\tleaq\t.LC_fmt_f(%rip), %rcx\n\tcall\tprintf\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprintln_string\nprintln_string:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_s_nl(%rip), %rcx\n\tcall\tprintf\n";
            *out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
            *out_ << "\t.globl\tprint_string\nprint_string:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_s(%rip), %rcx\n\tcall\tprintf\n";
            *out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";
        }
    }
    for (const auto& pair : semantic_->functions())
        emitFunc(pair.second);
    for (const auto& modPair : semantic_->moduleFunctions()) {
        for (const auto& pair : modPair.second) {
            emitFunc(pair.second);
        }
    }
}

bool CodeGenerator::generate() {
    emitProgram();
    return errors_.empty();
}

} // namespace gspp
