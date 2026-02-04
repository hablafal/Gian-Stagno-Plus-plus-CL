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
        // x64: Initial RSP is 16-aligned.
        // call pushes 8 bytes. push rbp pushes 8 bytes.
        // So RSP is 16-aligned again.
        // We must subtract a multiple of 16 to keep it 16-aligned.
        n = (n + 15) & ~15;
        if (n < 32) n = 32; // shadow space for Windows x64
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
        case Expr::Kind::ListLit: {
            // New dynamic list
            int numArgs = (int)expr->args.size();
            if (use32Bit_) {
                *out_ << "\tpushl\t$" << numArgs << "\n\tcall\t_gspp_list_new\n\taddl\t$4, %esp\n";
                for (int i = 0; i < numArgs; i++) {
                    *out_ << "\tpushl\t%eax\n"; // save list
                    emitExprToRax(expr->args[i].get());
                    *out_ << "\tmovl\t%eax, %edx\n\tpopl\t%eax\n"; // restore list, elem in edx
                    *out_ << "\tpushl\t%edx\n\tpushl\t%eax\n\tcall\t_gspp_list_append\n\taddl\t$8, %esp\n";
                }
            } else {
                if (isLinux_) {
                    *out_ << "\tmovq\t$" << numArgs << ", %rdi\n\tcall\tgspp_list_new\n";
                    for (int i = 0; i < numArgs; i++) {
                        *out_ << "\tpushq\t%rax\n";
                        emitExprToRax(expr->args[i].get());
                        *out_ << "\tmovq\t%rax, %rsi\n\tpopq\t%rdi\n";
                        *out_ << "\tpushq\t%rdi\n\tcall\tgspp_list_append\n\tpopq\t%rax\n";
                    }
                } else {
                    *out_ << "\tmovq\t$" << numArgs << ", %rcx\n\tsubq\t$32, %rsp\n\tcall\tgspp_list_new\n\taddq\t$32, %rsp\n";
                    for (int i = 0; i < numArgs; i++) {
                        *out_ << "\tpushq\t%rax\n";
                        emitExprToRax(expr->args[i].get());
                        *out_ << "\tmovq\t%rax, %rdx\n\tpopq\t%rcx\n";
                        *out_ << "\tpushq\t%rcx\n\tsubq\t$32, %rsp\n\tcall\tgspp_list_append\n\taddq\t$32, %rsp\n\tpopq\t%rax\n";
                    }
                }
            }
            if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
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
        case Expr::Kind::Index: {
            emitExprToRax(expr->left.get()); // base
            *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
            emitExprToRax(expr->right.get()); // index
            *out_ << (use32Bit_ ? "\tpopl\t%edx\n" : "\tpopq\t%rdx\n"); // edx = base, eax = index
            if (expr->left->exprType.kind == Type::Kind::String) {
                *out_ << "\tmovzbl\t(%rdx,%rax), %eax\n";
            } else if (expr->left->exprType.kind == Type::Kind::List) {
                if (use32Bit_) {
                    *out_ << "\tmovl\t(%edx), %edx\n"; // load list->data
                    *out_ << "\tmovl\t(%edx,%eax,4), %eax\n"; // load data[index]
                } else {
                    *out_ << "\tmovq\t(%rdx), %rdx\n"; // load list->data
                    *out_ << "\tmovq\t(%rdx,%rax,8), %rax\n"; // load data[index]
                }
            } else { // Pointer
                int sz = getTypeSize(expr->exprType);
                if (use32Bit_) *out_ << "\tmovl\t(%edx,%eax," << sz << "), %eax\n";
                else *out_ << "\tmovq\t(%rdx,%rax," << sz << "), %rax\n";
            }
            if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Call: {
            if (expr->left && expr->left->exprType.kind == Type::Kind::String) {
                if (expr->ident == "len") {
                    emitExprToRax(expr->left.get());
                    if (use32Bit_) {
                        *out_ << "\tpushl\t%eax\n\tcall\t_strlen\n\taddl\t$4, %esp\n";
                    } else {
                        if (isLinux_) {
                            *out_ << "\tmovq\t%rax, %rdi\n\tcall\tstrlen\n";
                        } else {
                            *out_ << "\tmovq\t%rax, %rcx\n\tsubq\t$32, %rsp\n\tcall\tstrlen\n\taddq\t$32, %rsp\n";
                        }
                    }
                    if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                    return;
                }
            }
            if (expr->left && expr->left->exprType.kind == Type::Kind::List) {
                if (expr->ident == "len") {
                    emitExprToRax(expr->left.get());
                    *out_ << "\t" << mov << "\t" << (use32Bit_ ? "4" : "8") << "(%" << rax << "), %" << rax << "\n";
                    if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                    return;
                }
                if (expr->ident == "append") {
                    emitExprToRax(expr->left.get());
                    if (use32Bit_) {
                        *out_ << "\tpushl\t%eax\n";
                        emitExprToRax(expr->args[0].get());
                        *out_ << "\tmovl\t%eax, %edx\n\tpopl\t%eax\n\tpushl\t%edx\n\tpushl\t%eax\n\tcall\t_gspp_list_append\n\taddl\t$8, %esp\n";
                    } else {
                        if (isLinux_) {
                            *out_ << "\tpushq\t%rax\n";
                            emitExprToRax(expr->args[0].get());
                            *out_ << "\tmovq\t%rax, %rsi\n\tpopq\t%rdi\n\tcall\tgspp_list_append\n";
                        } else {
                            *out_ << "\tpushq\t%rax\n";
                            emitExprToRax(expr->args[0].get());
                            *out_ << "\tmovq\t%rax, %rdx\n\tpopq\t%rcx\n\tsubq\t$32, %rsp\n\tcall\tgspp_list_append\n\taddq\t$32, %rsp\n";
                        }
                    }
                    return;
                }
            }
            if (expr->left && (expr->left->exprType.kind == Type::Kind::StructRef || (expr->left->exprType.kind == Type::Kind::Pointer && expr->left->exprType.ptrTo && expr->left->exprType.ptrTo->kind == Type::Kind::StructRef))) {
                Type& bty = (expr->left->exprType.kind == Type::Kind::Pointer) ? *expr->left->exprType.ptrTo : expr->left->exprType;
                StructDef* sd = resolveStruct(bty.structName, bty.ns);
                if (sd && sd->methods.count(expr->ident)) {
                    FuncSymbol& ms = sd->methods[expr->ident];
                    if (use32Bit_) {
                        for (int i = (int)expr->args.size() - 1; i >= 0; i--) {
                            emitExprToRax(expr->args[i].get()); *out_ << "\tpushl\t%eax\n";
                        }
                        emitExprToRax(expr->left.get()); *out_ << "\tpushl\t%eax\n";
                        *out_ << "\tcall\t" << ms.mangledName << "\n";
                        *out_ << "\taddl\t$" << (4 * (int)expr->args.size() + 4) << ", %esp\n";
                        if (dest != "eax") *out_ << "\tmovl\t%eax, %" << dest << "\n";
                    } else {
                        if (isLinux_) {
                            const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                            emitExprToRax(expr->left.get());
                            *out_ << "\tpushq\t%rax\n";
                            *out_ << "\tsubq\t$8, %rsp\n";
                            for (size_t i = 0; i < expr->args.size() && i < 5; i++) {
                                emitExpr(expr->args[i].get(), regs[i+1], false);
                            }
                            *out_ << "\tmovq\t8(%rsp), %rdi\n";
                            *out_ << "\tcall\t" << ms.mangledName << "\n";
                            *out_ << "\taddq\t$16, %rsp\n";
                        } else {
                            emitExpr(expr->left.get(), "rcx", false);
                            for (size_t i = 0; i < expr->args.size() && i < 3; i++) {
                                emitExpr(expr->args[i].get(), i == 0 ? "rdx" : i == 1 ? "r8" : "r9", false);
                            }
                            *out_ << "\tsubq\t$32, %rsp\n\tcall\t" << ms.mangledName << "\n\taddq\t$32, %rsp\n";
                        }
                        if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    }
                    return;
                }
            }

            if (expr->ns.empty()) {
                StructDef* sd = resolveStruct(expr->ident, "");
                if (sd) {
                    // Constructor
                    if (use32Bit_) {
                        *out_ << "\tpushl\t$" << sd->sizeBytes << "\n\tcall\tmalloc\n\taddl\t$4, %esp\n";
                        if (sd->methods.count("init")) {
                            *out_ << "\tpushl\t%eax\n"; // save self
                            for (int i = (int)expr->args.size() - 1; i >= 0; i--) {
                                emitExprToRax(expr->args[i].get()); *out_ << "\tpushl\t%eax\n";
                            }
                            *out_ << "\tmovl\t" << (4 * expr->args.size()) << "(%esp), %eax\n\tpushl\t%eax\n";
                            *out_ << "\tcall\t" << sd->mangledName << "_init\n";
                            *out_ << "\taddl\t$" << (4 * expr->args.size() + 4) << ", %esp\n";
                            *out_ << "\tpopl\t%eax\n";
                        }
                        if (dest != "eax") *out_ << "\tmovl\t%eax, %" << dest << "\n";
                    } else {
                        if (isLinux_) {
                            *out_ << "\tmovq\t$" << sd->sizeBytes << ", %rdi\n\tcall\tmalloc\n";
                            if (sd->methods.count("init")) {
                                *out_ << "\tpushq\t%rax\n";
                                *out_ << "\tsubq\t$8, %rsp\n"; // Align to 16
                                const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                                for (size_t i = 0; i < expr->args.size() && i < 5; i++) {
                                    emitExpr(expr->args[i].get(), regs[i+1], false);
                                }
                                *out_ << "\tmovq\t8(%rsp), %rdi\n"; // self
                                *out_ << "\tcall\t" << sd->mangledName << "_init\n";
                                *out_ << "\taddq\t$8, %rsp\n";
                                *out_ << "\tpopq\t%rax\n";
                            }
                        } else {
                            *out_ << "\tmovq\t$" << sd->sizeBytes << ", %rcx\n\tsubq\t$32, %rsp\n\tcall\tmalloc\n\taddq\t$32, %rsp\n";
                            if (sd->methods.count("init")) {
                                *out_ << "\tpushq\t%rax\n";
                                for (size_t i = 0; i < expr->args.size() && i < 3; i++) {
                                    emitExpr(expr->args[i].get(), i == 0 ? "rdx" : i == 1 ? "r8" : "r9", false);
                                }
                                *out_ << "\tpopq\t%rcx\n\tpushq\t%rcx\n\tsubq\t$32, %rsp\n\tcall\t" << sd->mangledName << "_init\n\taddq\t$32, %rsp\n\tpopq\t%rax\n";
                            }
                        }
                        if (dest != "rax") *out_ << "\tmovq\t%rax, %" << dest << "\n";
                    }
                    return;
                }
            }

            std::string funcName = expr->ident;
            if (expr->ns.empty() && (funcName == "print" || funcName == "println")) {
                for (size_t i = 0; i < expr->args.size(); i++) {
                    std::string subFunc = funcName;
                    if (expr->args[i]->exprType.kind == Type::Kind::String) subFunc += "_string";
                    else if (expr->args[i]->exprType.kind == Type::Kind::Float) subFunc += "_float";

                    if (i < expr->args.size() - 1 && subFunc == "println") subFunc = "print";
                    if (i < expr->args.size() - 1 && subFunc == "println_string") subFunc = "print_string";
                    if (i < expr->args.size() - 1 && subFunc == "println_float") subFunc = "print_float";

                    FuncSymbol* fs = resolveFunc(subFunc, "");
                    if (fs) {
                        if (use32Bit_) {
                            emitExprToRax(expr->args[i].get());
                            *out_ << "\tpushl\t%eax\n\tcall\t" << fs->mangledName << "\n\taddl\t$4, %esp\n";
                        } else {
                            if (isLinux_) {
                                if (expr->args[i]->exprType.kind == Type::Kind::Float) emitExpr(expr->args[i].get(), "xmm0", true);
                                else emitExpr(expr->args[i].get(), "rdi", false);
                                *out_ << "\tcall\t" << fs->mangledName << "\n";
                            } else {
                                if (expr->args[i]->exprType.kind == Type::Kind::Float) emitExpr(expr->args[i].get(), "xmm0", true);
                                else emitExpr(expr->args[i].get(), "rcx", false);
                                *out_ << "\tsubq\t$32, %rsp\n\tcall\t" << fs->mangledName << "\n\taddq\t$32, %rsp\n";
                            }
                        }
                    }
                }
                return;
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
                    if (fs->returnType.kind == Type::Kind::Float) {
                        if (dest != "xmm0") *out_ << "\tmovsd\t%xmm0, %" << dest << "\n";
                        return;
                    }
                } else {
                    bool floatFirst = (expr->ident == "print_float" || expr->ident == "println_float" || expr->ident == "sqrt") && !expr->args.empty();
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
                if (fs->returnType.kind == Type::Kind::Float) {
                    if (dest != "xmm0") *out_ << "\tmovsd\t%xmm0, %" << dest << "\n";
                } else {
                    if (dest != rax) *out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                }
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
        case Stmt::Kind::Repeat: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            std::string endLabel = nextLabel();

            // We need a counter. For simplicity, let's use a hidden local or just push/pop
            emitExprToRax(stmt->condition.get());
            *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");

            *out_ << condLabel << ":\n";
            *out_ << (use32Bit_ ? "\tmovl\t(%esp), %eax\n" : "\tmovq\t(%rsp), %rax\n");
            *out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            *out_ << "\tjle\t" << endLabel << "\n";

            emitStmt(stmt->body.get());

            *out_ << (use32Bit_ ? "\tdecl\t(%esp)\n" : "\tdecq\t(%rsp)\n");
            *out_ << "\tjmp\t" << condLabel << "\n";
            *out_ << endLabel << ":\n";
            *out_ << (use32Bit_ ? "\taddl\t$4, %esp\n" : "\taddq\t$8, %rsp\n");
            break;
        }
        case Stmt::Kind::RangeFor: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            std::string stepLabel = nextLabel();

            // Initialize var with startExpr
            emitExprToRax(stmt->startExpr.get());
            std::string loc = getVarLocation(stmt->varName);
            if (!loc.empty()) *out_ << "\t" << (use32Bit_ ? "movl\t%eax, " : "movq\t%rax, ") << loc << "\n";

            *out_ << "\tjmp\t" << condLabel << "\n";
            *out_ << bodyLabel << ":\n";
            emitStmt(stmt->body.get());

            *out_ << stepLabel << ":\n";
            // Increment var
            if (!loc.empty()) *out_ << "\t" << (use32Bit_ ? "incl\t" : "incq\t") << loc << "\n";

            *out_ << condLabel << ":\n";
            // Check var < endExpr
            emitExprToRax(stmt->endExpr.get());
            *out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
            if (!loc.empty()) *out_ << "\t" << (use32Bit_ ? "movl\t" : "movq\t") << loc << ", %rax\n";
            *out_ << (use32Bit_ ? "\tpopl\t%ecx\n" : "\tpopq\t%rcx\n");
            *out_ << (use32Bit_ ? "\tcmpl\t%ecx, %eax\n" : "\tcmpq\t%rcx, %rax\n");
            *out_ << "\tjl\t" << bodyLabel << "\n";
            break;
        }
        case Stmt::Kind::Asm:
            *out_ << "\t" << stmt->asmCode << "\n";
            break;
    }
}

void CodeGenerator::emitFunc(const FuncSymbol& fs) {
    if (fs.decl && fs.decl->isExtern) return;
    if (fs.mangledName == "println" || fs.mangledName == "print" || fs.mangledName == "print_float" ||
        fs.mangledName == "println_float" || fs.mangledName == "print_string" || fs.mangledName == "println_string" ||
        fs.mangledName == "gspp_input" || fs.mangledName == "gspp_read_file" || fs.mangledName == "gspp_write_file" ||
        fs.mangledName == "abs" || fs.mangledName == "sqrt") return;

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
                if (fs.isMethod) {
                    std::string loc = getVarLocation("self");
                    if (!loc.empty()) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n";
                }
                for (size_t i = 0; i < fs.decl->params.size(); i++) {
                    std::string loc = getVarLocation(fs.decl->params[i].name);
                    if (loc.empty()) continue;
                    if (fs.decl->params[i].type.kind == Type::Kind::Float) {
                        if (freg < 8) *out_ << "\tmovq\t%" << fregs[freg++] << ", " << loc << "\n";
                    } else {
                        if (ireg < 6) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n";
                    }
                }
            } else {
                if (fs.isMethod) {
                    std::string loc = getVarLocation("self");
                    if (!loc.empty()) *out_ << "\tmovq\t%rcx, " << loc << "\n";
                }
                for (size_t i = 0; i < fs.decl->params.size(); i++) {
                    std::string loc = getVarLocation(fs.decl->params[i].name);
                    if (loc.empty()) continue;
                    int idx = i + (fs.isMethod ? 1 : 0);
                    if (idx == 0) *out_ << "\tmovq\t%rcx, " << loc << "\n";
                    else if (idx == 1) *out_ << "\tmovq\t%rdx, " << loc << "\n";
                    else if (idx == 2) *out_ << "\tmovq\t%r8, " << loc << "\n";
                    else if (idx == 3) *out_ << "\tmovq\t%r9, " << loc << "\n";
                }
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
        *out_ << "\t.extern\tabs\n";
        *out_ << "\t.extern\tsqrt\n";
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

        *out_ << "\t.globl\tprint_string\nprint_string:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        *out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_s\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";
        *out_ << "\t.globl\tprintln_string\nprintln_string:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        *out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_s_nl\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";

        *out_ << "\t.globl\t_gspp_strcat\n_gspp_strcat:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n\tpushl\t%ebx\n\tpushl\t%edi\n";
        *out_ << "\tpushl\t12(%ebp)\n\tcall\t_strlen\n\taddl\t$4, %esp\n\tmovl\t%eax, %ebx\n";
        *out_ << "\tpushl\t8(%ebp)\n\tcall\t_strlen\n\taddl\t$4, %esp\n\taddl\t%ebx, %eax\n\tincl\t%eax\n";
        *out_ << "\tpushl\t%eax\n\tcall\tmalloc\n\taddl\t$4, %esp\n\tmovl\t%eax, %edi\n";
        *out_ << "\tpushl\t8(%ebp)\n\tpushl\t%edi\n\tcall\t_strcpy\n\taddl\t$8, %esp\n";
        *out_ << "\tpushl\t12(%ebp)\n\tpushl\t%edi\n\tcall\t_strcat\n\taddl\t$8, %esp\n";
        *out_ << "\tmovl\t%edi, %eax\n\tpopl\t%edi\n\tpopl\t%ebx\n\tleave\n\tret\n\n";

        *out_ << "\t.extern\t_scanf\n";
        *out_ << "\t.globl\tgspp_input\ngspp_input:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        *out_ << "\tpushl\t$256\n\tcall\tmalloc\n\taddl\t$4, %esp\n";
        *out_ << "\tpushl\t%eax\n\tpushl\t$.LC_fmt_s\n\tcall\t_scanf\n\taddl\t$4, %esp\n\tpopl\t%eax\n\tleave\n\tret\n\n";

        *out_ << "\t.extern\t_fopen\n\t.extern\t_fseek\n\t.extern\t_ftell\n\t.extern\t_rewind\n\t.extern\t_fread\n\t.extern\t_fclose\n";
        *out_ << "\t.globl\tgspp_read_file\ngspp_read_file:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$16, %esp\n";
        *out_ << "\tpushl\t$.LC_mode_r\n\tpushl\t8(%ebp)\n\tcall\t_fopen\n\taddl\t$8, %esp\n";
        *out_ << "\ttestl\t%eax, %eax\n\tje\t.LRF_err32\n";
        *out_ << "\tmovl\t%eax, -4(%ebp)\n";
        *out_ << "\tpushl\t$2\n\tpushl\t$0\n\tpushl\t-4(%ebp)\n\tcall\t_fseek\n\taddl\t$12, %esp\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tcall\t_ftell\n\taddl\t$4, %esp\n\tmovl\t%eax, -8(%ebp)\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tcall\t_rewind\n\taddl\t$4, %esp\n";
        *out_ << "\tmovl\t-8(%ebp), %eax\n\tincl\t%eax\n\tpushl\t%eax\n\tcall\tmalloc\n\taddl\t$4, %esp\n\tmovl\t%eax, -12(%ebp)\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tpushl\t-8(%ebp)\n\tpushl\t$1\n\tpushl\t-12(%ebp)\n\tcall\t_fread\n\taddl\t$16, %esp\n";
        *out_ << "\tmovl\t-12(%ebp), %edx\n\taddl\t-8(%ebp), %edx\n\tmovb\t$0, (%edx)\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tcall\t_fclose\n\taddl\t$4, %esp\n";
        *out_ << "\tmovl\t-12(%ebp), %eax\n\tjmp\t.LRF_end32\n.LRF_err32:\n\tmovl\t$0, %eax\n.LRF_end32:\n\tleave\n\tret\n\n";

        *out_ << "\t.extern\t_fputs\n";
        *out_ << "\t.globl\tgspp_write_file\ngspp_write_file:\n";
        *out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n";
        *out_ << "\tpushl\t$.LC_mode_w\n\tpushl\t8(%ebp)\n\tcall\t_fopen\n\taddl\t$8, %esp\n";
        *out_ << "\ttestl\t%eax, %eax\n\tje\t.LWF_end32\n";
        *out_ << "\tmovl\t%eax, -4(%ebp)\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tpushl\t12(%ebp)\n\tcall\t_fputs\n\taddl\t$8, %esp\n";
        *out_ << "\tpushl\t-4(%ebp)\n\tcall\t_fclose\n\taddl\t$4, %esp\n.LWF_end32:\n\tleave\n\tret\n\n";
    } else {
        *out_ << "\t.extern\tprintf\n";
        *out_ << "\t.extern\tstrlen\n";
        *out_ << "\t.extern\tstrcpy\n";
        *out_ << "\t.extern\tstrcat\n";
        *out_ << "\t.extern\tmalloc\n";
        *out_ << "\t.extern\tfree\n";
        *out_ << "\t.extern\tabs\n";
        *out_ << "\t.extern\tsqrt\n";
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
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tmovq\t%rdi, -8(%rbp)\n\tmovq\t%rsi, -16(%rbp)\n";
            *out_ << "\tcall\tstrlen\n\tmovq\t%rax, -24(%rbp)\n";
            *out_ << "\tmovq\t-16(%rbp), %rdi\n\tcall\tstrlen\n\taddq\t-24(%rbp), %rax\n\tincq\t%rax\n";
            *out_ << "\tmovq\t%rax, %rdi\n\tcall\tmalloc\n\tmovq\t%rax, -32(%rbp)\n";
            *out_ << "\tmovq\t-32(%rbp), %rdi\n\tmovq\t-8(%rbp), %rsi\n\tcall\tstrcpy\n";
            *out_ << "\tmovq\t-32(%rbp), %rdi\n\tmovq\t-16(%rbp), %rsi\n\tcall\tstrcat\n";
            *out_ << "\tmovq\t-32(%rbp), %rax\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\tscanf\n";
            *out_ << "\t.globl\tgspp_input\ngspp_input:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t$256, %rdi\n\tcall\tmalloc\n\tmovq\t%rax, -8(%rbp)\n";
            *out_ << "\tleaq\t.LC_fmt_s(%rip), %rdi\n\tmovq\t-8(%rbp), %rsi\n\tmovl\t$0, %eax\n\tcall\tscanf\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\tfopen\n\t.extern\tfseek\n\t.extern\tftell\n\t.extern\trewind\n\t.extern\tfread\n\t.extern\tfclose\n";
            *out_ << ".LC_mode_r:\n\t.string \"r\"\n";
            *out_ << "\t.globl\tgspp_read_file\ngspp_read_file:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tleaq\t.LC_mode_r(%rip), %rsi\n\tcall\tfopen\n\ttestq\t%rax, %rax\n\tje\t.LRF_err\n";
            *out_ << "\tmovq\t%rax, -8(%rbp)\n\tmovq\t%rax, %rdi\n\tmovl\t$0, %esi\n\tmovl\t$2, %edx\n\tcall\tfseek\n";
            *out_ << "\tmovq\t-8(%rbp), %rdi\n\tcall\tftell\n\tmovq\t%rax, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rdi\n\tcall\trewind\n";
            *out_ << "\tmovq\t-16(%rbp), %rdi\n\tincq\t%rdi\n\tcall\tmalloc\n\tmovq\t%rax, -24(%rbp)\n";
            *out_ << "\tmovq\t-24(%rbp), %rdi\n\tmovl\t$1, %esi\n\tmovq\t-16(%rbp), %rdx\n\tmovq\t-8(%rbp), %rcx\n\tcall\tfread\n";
            *out_ << "\tmovq\t-24(%rbp), %rdx\n\taddq\t-16(%rbp), %rdx\n\tmovb\t$0, (%rdx)\n";
            *out_ << "\tmovq\t-8(%rbp), %rdi\n\tcall\tfclose\n";
            *out_ << "\tmovq\t-24(%rbp), %rax\n\tjmp\t.LRF_end\n.LRF_err:\n\tmovq\t$0, %rax\n.LRF_end:\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << ".LC_mode_w:\n\t.string \"w\"\n";
            *out_ << "\t.extern\tfputs\n";
            *out_ << "\t.globl\tgspp_write_file\ngspp_write_file:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tmovq\t%rdi, -8(%rbp)\n\tmovq\t%rsi, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rdi\n\tleaq\t.LC_mode_w(%rip), %rsi\n\tcall\tfopen\n\ttestq\t%rax, %rax\n\tje\t.LWF_end\n";
            *out_ << "\tmovq\t%rax, -24(%rbp)\n\tmovq\t-16(%rbp), %rdi\n\tmovq\t-24(%rbp), %rsi\n\tcall\tfputs\n";
            *out_ << "\tmovq\t-24(%rbp), %rdi\n\tcall\tfclose\n.LWF_end:\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.globl\tgspp_list_new\ngspp_list_new:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t$24, %rdi\n\tcall\tmalloc\n\tmovq\t%rax, -8(%rbp)\n";
            *out_ << "\tmovq\t$80, %rdi\n\tcall\tmalloc\n\tmovq\t-8(%rbp), %rdx\n";
            *out_ << "\tmovq\t%rax, (%rdx)\n\tmovq\t$0, 8(%rdx)\n\tmovq\t$10, 16(%rdx)\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\trealloc\n";
            *out_ << "\t.globl\tgspp_list_append\ngspp_list_append:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rdi, -8(%rbp)\n\tmovq\t%rsi, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\tmovq\t8(%rax), %rcx\n\tcmpq\t16(%rax), %rcx\n\tjl\t.Lappend_now_lin\n";
            *out_ << "\tmovq\t16(%rax), %rdx\n\tshlq\t$1, %rdx\n\tmovq\t%rdx, 16(%rax)\n";
            *out_ << "\tmovq\t(%rax), %rdi\n\tshlq\t$3, %rdx\n\tmovq\t%rdx, %rsi\n\tcall\trealloc\n";
            *out_ << "\tmovq\t-8(%rbp), %rdx\n\tmovq\t%rax, (%rdx)\n";
            *out_ << ".Lappend_now_lin:\n\tmovq\t-8(%rbp), %rax\n\tmovq\t(%rax), %rdx\n\tmovq\t8(%rax), %rcx\n";
            *out_ << "\tmovq\t-16(%rbp), %rdi\n\tmovq\t%rdi, (%rdx,%rcx,8)\n\tincq\t8(%rax)\n";
            *out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";
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

            *out_ << "\t.globl\t_gspp_strcat\n_gspp_strcat:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tmovq\t%rcx, -8(%rbp)\n\tmovq\t%rdx, -16(%rbp)\n";
            *out_ << "\tcall\tstrlen\n\tmovq\t%rax, -24(%rbp)\n";
            *out_ << "\tmovq\t-16(%rbp), %rcx\n\tcall\tstrlen\n\taddq\t-24(%rbp), %rax\n\tincq\t%rax\n";
            *out_ << "\tmovq\t%rax, %rcx\n\tcall\tmalloc\n\tmovq\t%rax, -32(%rbp)\n";
            *out_ << "\tmovq\t-32(%rbp), %rcx\n\tmovq\t-8(%rbp), %rdx\n\tcall\tstrcpy\n";
            *out_ << "\tmovq\t-32(%rbp), %rcx\n\tmovq\t-16(%rbp), %rdx\n\tcall\tstrcat\n";
            *out_ << "\tmovq\t-32(%rbp), %rax\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\tscanf\n";
            *out_ << "\t.globl\tgspp_input\ngspp_input:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tmovq\t$256, %rcx\n\tcall\tmalloc\n\tmovq\t%rax, -8(%rbp)\n";
            *out_ << "\tleaq\t.LC_fmt_s(%rip), %rcx\n\tmovq\t-8(%rbp), %rdx\n\tcall\tscanf\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\tfopen\n\t.extern\tfseek\n\t.extern\tftell\n\t.extern\trewind\n\t.extern\tfread\n\t.extern\tfclose\n";
            *out_ << ".LC_mode_r:\n\t.string \"r\"\n";
            *out_ << "\t.globl\tgspp_read_file\ngspp_read_file:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$64, %rsp\n";
            *out_ << "\tleaq\t.LC_mode_r(%rip), %rdx\n\tcall\tfopen\n\ttestq\t%rax, %rax\n\tje\t.LRF_err_win\n";
            *out_ << "\tmovq\t%rax, -8(%rbp)\n\tmovq\t%rax, %rcx\n\tmovl\t$0, %edx\n\tmovl\t$2, %r8d\n\tcall\tfseek\n";
            *out_ << "\tmovq\t-8(%rbp), %rcx\n\tcall\tftell\n\tmovq\t%rax, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rcx\n\tcall\trewind\n";
            *out_ << "\tmovq\t-16(%rbp), %rcx\n\tincq\t%rcx\n\tcall\tmalloc\n\tmovq\t%rax, -24(%rbp)\n";
            *out_ << "\tmovq\t-24(%rbp), %rcx\n\tmovl\t$1, %edx\n\tmovq\t-16(%rbp), %r8\n\tmovq\t-8(%rbp), %r9\n\tcall\tfread\n";
            *out_ << "\tmovq\t-24(%rbp), %rdx\n\taddq\t-16(%rbp), %rdx\n\tmovb\t$0, (%rdx)\n";
            *out_ << "\tmovq\t-8(%rbp), %rcx\n\tcall\tfclose\n";
            *out_ << "\tmovq\t-24(%rbp), %rax\n\tjmp\t.LRF_end_win\n.LRF_err_win:\n\tmovq\t$0, %rax\n.LRF_end_win:\n\taddq\t$64, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << ".LC_mode_w:\n\t.string \"w\"\n";
            *out_ << "\t.extern\tfputs\n";
            *out_ << "\t.globl\tgspp_write_file\ngspp_write_file:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$48, %rsp\n";
            *out_ << "\tmovq\t%rcx, -8(%rbp)\n\tmovq\t%rdx, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rcx\n\tleaq\t.LC_mode_w(%rip), %rdx\n\tcall\tfopen\n\ttestq\t%rax, %rax\n\tje\t.LWF_end_win\n";
            *out_ << "\tmovq\t%rax, -24(%rbp)\n\tmovq\t-16(%rbp), %rcx\n\tmovq\t-24(%rbp), %rdx\n\tcall\tfputs\n";
            *out_ << "\tmovq\t-24(%rbp), %rcx\n\tcall\tfclose\n.LWF_end_win:\n\taddq\t$48, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.globl\tgspp_list_new\ngspp_list_new:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t$24, %rdi\n\tcall\tmalloc\n\tmovq\t%rax, -8(%rbp)\n";
            *out_ << "\tmovq\t$80, %rdi\n\tcall\tmalloc\n\tmovq\t-8(%rbp), %rdx\n";
            *out_ << "\tmovq\t%rax, (%rdx)\n\tmovq\t$0, 8(%rdx)\n\tmovq\t$10, 16(%rdx)\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";

            *out_ << "\t.extern\trealloc\n";
            *out_ << "\t.globl\tgspp_list_append\ngspp_list_append:\n";
            *out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
            *out_ << "\tmovq\t%rdi, -8(%rbp)\n\tmovq\t%rsi, -16(%rbp)\n";
            *out_ << "\tmovq\t-8(%rbp), %rax\n\tmovq\t8(%rax), %rcx\n\tcmpq\t16(%rax), %rcx\n\tjl\t.Lappend_now\n";
            *out_ << "\tmovq\t16(%rax), %rdx\n\tshlq\t$1, %rdx\n\tmovq\t%rdx, 16(%rax)\n";
            *out_ << "\tmovq\t(%rax), %rdi\n\tshlq\t$3, %rdx\n\tmovq\t%rdx, %rsi\n\tcall\trealloc\n";
            *out_ << "\tmovq\t-8(%rbp), %rdx\n\tmovq\t%rax, (%rdx)\n";
            *out_ << ".Lappend_now:\n\tmovq\t-8(%rbp), %rax\n\tmovq\t(%rax), %rdx\n\tmovq\t8(%rax), %rcx\n";
            *out_ << "\tmovq\t-16(%rbp), %rdi\n\tmovq\t%rdi, (%rdx,%rcx,8)\n\tincq\t8(%rax)\n";
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
    for (const auto& pair : semantic_->structs()) {
        for (const auto& mPair : pair.second.methods) {
            emitFunc(mPair.second);
        }
    }
}

bool CodeGenerator::generate() {
    emitProgram();
    return errors_.empty();
}

} // namespace gspp
