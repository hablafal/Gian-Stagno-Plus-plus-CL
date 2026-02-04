#include "codegen.h"
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace gspp {

CodeGenerator::CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit)
    : program_(program), semantic_(semantic), out_(out), use32Bit_(use32Bit) {}

void CodeGenerator::error(const std::string& msg, SourceLoc loc) {
    std::ostringstream os;
    os << ":" << loc.line << ":" << loc.column << ": error: " << msg;
    errors_.push_back(os.str());
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
                out_ << "\tmovl\t$" << (int32_t)expr->intVal << ", %" << dest << "\n";
            else
                out_ << "\tmovq\t$" << expr->intVal << ", %" << dest << "\n";
            break;
        case Expr::Kind::FloatLit: {
            if (use32Bit_ && (wantFloat || dest == "xmm0")) {
                float f = (float)expr->floatVal;
                uint32_t bits;
                memcpy(&bits, &f, 4);
                out_ << "\tmovl\t$0x" << std::hex << bits << std::dec << ", %eax\n";
                out_ << "\tmovd\t%eax, %xmm0\n";
                if (dest != "xmm0") out_ << "\tmovd\t%xmm0, %" << dest << "\n";
            } else {
                uint64_t bits;
                memcpy(&bits, &expr->floatVal, 8);
                if (wantFloat || destReg == "xmm0") {
                    out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %rax\n";
                    out_ << "\tmovq\t%rax, %xmm0\n";
                    if (dest != "xmm0") out_ << "\tmovq\t%xmm0, %" << dest << "\n";
                } else {
                    out_ << "\tmovabsq\t$0x" << std::hex << bits << std::dec << ", %" << dest << "\n";
                }
            }
            break;
        }
        case Expr::Kind::BoolLit:
            if (use32Bit_)
                out_ << "\tmovl\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n";
            else
                out_ << "\tmovq\t$" << (expr->boolVal ? 1 : 0) << ", %" << dest << "\n";
            break;
        case Expr::Kind::Var: {
            std::string loc = getVarLocation(expr->ident);
            if (loc.empty()) { error("unknown variable " + expr->ident, expr->loc); return; }
            out_ << "\t" << mov << "\t" << loc << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Binary: {
            if (expr->op == "and" || expr->op == "or") {
                std::string endLabel = nextLabel();
                emitExprToRax(expr->left.get());
                if (use32Bit_) {
                    if (expr->op == "and") { out_ << "\ttestl\t%eax, %eax\n"; out_ << "\tje\t" << endLabel << "\n"; }
                    else { out_ << "\ttestl\t%eax, %eax\n"; out_ << "\tjne\t" << endLabel << "\n"; }
                } else {
                    if (expr->op == "and") { out_ << "\ttestq\t%rax, %rax\n"; out_ << "\tje\t" << endLabel << "\n"; }
                    else { out_ << "\ttestq\t%rax, %rax\n"; out_ << "\tjne\t" << endLabel << "\n"; }
                }
                emitExprToRax(expr->right.get());
                out_ << endLabel << ":\n";
                if (dest != "rax" && dest != "eax") out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                return;
            }
            if (expr->op == "==" || expr->op == "!=" || expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
                emitExprToRax(expr->left.get());
                out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
                emitExprToRax(expr->right.get());
                out_ << (use32Bit_ ? "\tpopl\t%ecx\n" : "\tpopq\t%rcx\n");
                out_ << (use32Bit_ ? "\tcmpl\t%eax, %ecx\n" : "\tcmpq\t%rax, %rcx\n");
                if (expr->op == "==") out_ << "\tsete\t%al\n";
                else if (expr->op == "!=") out_ << "\tsetne\t%al\n";
                else if (expr->op == "<") out_ << "\tsetl\t%al\n";
                else if (expr->op == ">") out_ << "\tsetg\t%al\n";
                else if (expr->op == "<=") out_ << "\tsetle\t%al\n";
                else out_ << "\tsetge\t%al\n";
                out_ << (use32Bit_ ? "\tmovzbl\t%al, %eax\n" : "\tmovzbq\t%al, %rax\n");
                if (dest != "rax" && dest != "eax") out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
                return;
            }
            emitExprToRax(expr->left.get());
            out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
            emitExprToRax(expr->right.get());
            out_ << (use32Bit_ ? "\tmovl\t%eax, %ecx\n\tpopl\t%eax\n" : "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n");
            if (expr->op == "+") out_ << (use32Bit_ ? "\taddl\t%ecx, %eax\n" : "\taddq\t%rcx, %rax\n");
            else if (expr->op == "-") out_ << (use32Bit_ ? "\tsubl\t%ecx, %eax\n" : "\tsubq\t%rcx, %rax\n");
            else if (expr->op == "*") out_ << (use32Bit_ ? "\timull\t%ecx, %eax\n" : "\timulq\t%rcx, %rax\n");
            else if (expr->op == "/") {
                if (use32Bit_) { out_ << "\tcdq\n\tidivl\t%ecx\n"; }
                else { out_ << "\tcqto\n\tidivq\t%rcx\n"; }
            } else if (expr->op == "%") {
                if (use32Bit_) { out_ << "\tcdq\n\tidivl\t%ecx\n\tmovl\t%edx, %eax\n"; }
                else { out_ << "\tcqto\n\tidivq\t%rcx\n\tmovq\t%rdx, %rax\n"; }
            }
            if (dest != "rax" && dest != "eax") out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
            break;
        }
        case Expr::Kind::Unary:
            if (expr->op == "-") {
                emitExprToRax(expr->right.get());
                out_ << (use32Bit_ ? "\tnegl\t%eax\n" : "\tnegq\t%rax\n");
            } else if (expr->op == "not") {
                emitExprToRax(expr->right.get());
                out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
                out_ << "\tsete\t%al\n";
                out_ << (use32Bit_ ? "\tmovzbl\t%al, %eax\n" : "\tmovzbq\t%al, %rax\n");
            }
            if (dest != "rax" && dest != "eax") out_ << "\t" << mov << "\t%" << rax << ", %" << dest << "\n";
            break;
        case Expr::Kind::Call: {
            FuncSymbol* fs = semantic_->getFunc(expr->ident);
            if (!fs) { error("unknown function " + expr->ident, expr->loc); return; }
            if (use32Bit_) {
                // cdecl: push args right to left
                for (int i = (int)expr->args.size() - 1; i >= 0; i--)
                    emitExprToRax(expr->args[i].get()), out_ << "\tpushl\t%eax\n";
                out_ << "\tcall\t" << expr->ident << "\n";
                out_ << "\taddl\t$" << (4 * (int)expr->args.size()) << ", %esp\n";
                if (dest != "rax" && dest != "eax") out_ << "\tmovl\t%eax, %" << dest << "\n";
            } else {
                bool floatFirst = (expr->ident == "print_float" || expr->ident == "println_float") && !expr->args.empty();
                for (size_t i = 0; i < expr->args.size(); i++) {
                    if (i < 4) {
                        if (i == 0 && floatFirst) emitExpr(expr->args[i].get(), "xmm0", true);
                        else emitExpr(expr->args[i].get(), i == 0 ? "rcx" : i == 1 ? "rdx" : i == 2 ? "r8" : "r9", false);
                    } else {
                        out_ << "\tsubq\t$8, %rsp\n";
                        emitExprToRax(expr->args[i].get());
                        out_ << "\tpushq\t%rax\n";
                    }
                }
                out_ << "\tsubq\t$32, %rsp\n";
                out_ << "\tcall\t" << expr->ident << "\n";
                out_ << "\taddq\t$32, %rsp\n";
                for (size_t i = 4; i < expr->args.size(); i++) out_ << "\taddq\t$8, %rsp\n";
                if (dest != "rax") out_ << "\tmovq\t%rax, %" << dest << "\n";
            }
            break;
        }
        case Expr::Kind::Member: {
            emitExprToRax(expr->left.get());
            StructDef* sd = semantic_->getStruct(expr->left->exprType.structName);
            if (!sd) { error("unknown struct", expr->loc); return; }
            auto it = sd->memberIndex.find(expr->member);
            if (it == sd->memberIndex.end()) { error("no member " + expr->member, expr->loc); return; }
            int offset = (int)(it->second * (use32Bit_ ? 4 : 8));
            out_ << "\t" << mov << "\t" << offset << "(%" << rax << "), %" << dest << "\n";
            break;
        }
        default:
            out_ << "\t" << mov << "\t$0, %" << dest << "\n";
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
                if (!loc.empty()) out_ << "\t" << (use32Bit_ ? "movl\t%eax, " : "movq\t%rax, ") << loc << "\n";
            }
            break;
        }
        case Stmt::Kind::Assign: {
            if (stmt->assignTarget->kind == Expr::Kind::Var) {
                emitExprToRax(stmt->assignValue.get());
                std::string loc = getVarLocation(stmt->assignTarget->ident);
                if (!loc.empty()) out_ << "\t" << (use32Bit_ ? "movl\t%eax, " : "movq\t%rax, ") << loc << "\n";
            } else if (stmt->assignTarget->kind == Expr::Kind::Member) {
                emitExprToRax(stmt->assignTarget->left.get());
                out_ << (use32Bit_ ? "\tpushl\t%eax\n" : "\tpushq\t%rax\n");
                emitExprToRax(stmt->assignValue.get());
                out_ << (use32Bit_ ? "\tmovl\t%eax, %ecx\n\tpopl\t%eax\n" : "\tmovq\t%rax, %rcx\n\tpopq\t%rax\n");
                StructDef* sd = semantic_->getStruct(stmt->assignTarget->left->exprType.structName);
                if (sd) {
                    auto it = sd->memberIndex.find(stmt->assignTarget->member);
                    if (it != sd->memberIndex.end()) {
                        int off = (int)(it->second * (use32Bit_ ? 4 : 8));
                        out_ << "\t" << (use32Bit_ ? "movl\t%ecx, " : "movq\t%rcx, ") << off << "(%" << (use32Bit_ ? "eax" : "rax") << ")\n";
                    }
                }
            }
            break;
        }
        case Stmt::Kind::If: {
            std::string elseLabel = nextLabel();
            std::string endLabel = nextLabel();
            emitExprToRax(stmt->condition.get());
            out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            out_ << "\tje\t" << elseLabel << "\n";
            emitStmt(stmt->thenBranch.get());
            out_ << "\tjmp\t" << endLabel << "\n";
            out_ << elseLabel << ":\n";
            if (stmt->elseBranch) emitStmt(stmt->elseBranch.get());
            out_ << endLabel << ":\n";
            break;
        }
        case Stmt::Kind::While: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            out_ << "\tjmp\t" << condLabel << "\n";
            out_ << bodyLabel << ":\n";
            emitStmt(stmt->body.get());
            out_ << condLabel << ":\n";
            emitExprToRax(stmt->condition.get());
            out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            out_ << "\tjne\t" << bodyLabel << "\n";
            break;
        }
        case Stmt::Kind::For: {
            std::string condLabel = nextLabel();
            std::string bodyLabel = nextLabel();
            std::string stepLabel = nextLabel();
            emitStmt(stmt->initStmt.get());
            out_ << "\tjmp\t" << condLabel << "\n";
            out_ << bodyLabel << ":\n";
            emitStmt(stmt->body.get());
            out_ << stepLabel << ":\n";
            emitStmt(stmt->stepStmt.get());
            out_ << condLabel << ":\n";
            emitExprToRax(stmt->condition.get());
            out_ << (use32Bit_ ? "\ttestl\t%eax, %eax\n" : "\ttestq\t%rax, %rax\n");
            out_ << "\tjne\t" << bodyLabel << "\n";
            break;
        }
        case Stmt::Kind::Return:
            if (stmt->returnExpr) {
                if (stmt->returnExpr->exprType.kind == Type::Kind::Float)
                    emitExprToXmm0(stmt->returnExpr.get());
                else
                    emitExprToRax(stmt->returnExpr.get());
            } else {
                out_ << (use32Bit_ ? "\tmovl\t$0, %eax\n" : "\tmovq\t$0, %rax\n");
            }
            out_ << "\tleave\n\tret\n";
            break;
        case Stmt::Kind::ExprStmt:
            emitExprToRax(stmt->expr.get());
            break;
    }
}

void CodeGenerator::emitFunc(const FuncDecl& f) {
    FuncSymbol* fs = semantic_->getFunc(f.name);
    if (!fs) return;
    currentFunc_ = &f;
    currentVars_ = fs->locals;
    frameSize_ = getFrameSize();

    std::string label = f.name;
    if (use32Bit_ && f.name == "main") label = "_main";
    out_ << "\t.globl\t" << label << "\n";
    out_ << label << ":\n";
    if (use32Bit_) {
        out_ << "\tpushl\t%ebp\n";
        out_ << "\tmovl\t%esp, %ebp\n";
        out_ << "\tsubl\t$" << frameSize_ << ", %esp\n";
    } else {
        out_ << "\tpushq\t%rbp\n";
        out_ << "\tmovq\t%rsp, %rbp\n";
        out_ << "\tsubq\t$" << frameSize_ << ", %rsp\n";
        if (f.params.size() > 0) out_ << "\tmovq\t%rcx, 16(%rbp)\n";
        if (f.params.size() > 1) out_ << "\tmovq\t%rdx, 24(%rbp)\n";
        if (f.params.size() > 2) out_ << "\tmovq\t%r8, 32(%rbp)\n";
        if (f.params.size() > 3) out_ << "\tmovq\t%r9, 40(%rbp)\n";
    }
    if (f.body) emitStmt(f.body.get());
    out_ << "\tleave\n";
    out_ << "\tret\n\n";
    currentFunc_ = nullptr;
}

void CodeGenerator::emitProgram() {
    out_ << "\t.file\t\"gspp\"\n";
    out_ << "\t.data\n";
    out_ << ".LC_fmt_d:\n\t.string \"%d\"\n";
    out_ << ".LC_fmt_d_nl:\n\t.string \"%d\\n\"\n";
    out_ << ".LC_fmt_f:\n\t.string \"%f\"\n";
    out_ << ".LC_fmt_f_nl:\n\t.string \"%f\\n\"\n";
    out_ << "\t.text\n";
    if (use32Bit_) {
        out_ << "\t.extern\t_printf\n";
        out_ << "\t.globl\tprintln\nprintln:\n";
        out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_d_nl\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";
        out_ << "\t.globl\tprint\nprint:\n";
        out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n";
        out_ << "\tpushl\t8(%ebp)\n\tpushl\t$.LC_fmt_d\n\tcall\t_printf\n\taddl\t$8, %esp\n\tleave\n\tret\n";
        out_ << "\t.globl\tprintln_float\nprintln_float:\n";
        out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n\tmovd\t8(%ebp), %xmm0\n\tmovd\t%xmm0, (%esp)\n\tpushl\t$.LC_fmt_f_nl\n\tcall\t_printf\n\taddl\t$12, %esp\n\tleave\n\tret\n";
        out_ << "\t.globl\tprint_float\nprint_float:\n";
        out_ << "\tpushl\t%ebp\n\tmovl\t%esp, %ebp\n\tsubl\t$8, %esp\n\tmovd\t8(%ebp), %xmm0\n\tmovd\t%xmm0, (%esp)\n\tpushl\t$.LC_fmt_f\n\tcall\t_printf\n\taddl\t$12, %esp\n\tleave\n\tret\n\n";
    } else {
        out_ << "\t.extern\tprintf\n";
        out_ << "\t.globl\tprintln\nprintln:\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
        out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_d_nl(%rip), %rcx\n\tcall\tprintf\n";
        out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
        out_ << "\t.globl\tprint\nprint:\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n";
        out_ << "\tmovq\t%rcx, %rdx\n\tleaq\t.LC_fmt_d(%rip), %rcx\n\tcall\tprintf\n";
        out_ << "\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
        out_ << "\t.globl\tprintln_float\nprintln_float:\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n\tmovaps\t%xmm0, %xmm1\n\tleaq\t.LC_fmt_f_nl(%rip), %rcx\n\tcall\tprintf\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n";
        out_ << "\t.globl\tprint_float\nprint_float:\n";
        out_ << "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$32, %rsp\n\tmovaps\t%xmm0, %xmm1\n\tleaq\t.LC_fmt_f(%rip), %rcx\n\tcall\tprintf\n\taddq\t$32, %rsp\n\tpopq\t%rbp\n\tret\n\n";
    }
    for (const auto& f : program_->functions)
        emitFunc(f);
}

bool CodeGenerator::generate() {
    emitProgram();
    return errors_.empty();
}

} // namespace gspp
