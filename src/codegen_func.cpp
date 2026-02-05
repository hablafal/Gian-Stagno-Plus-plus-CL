#include "codegen.h"

namespace gspp {

void CodeGenerator::emitFunc(const FuncSymbol& fs) {
    if (fs.isExtern) return;
    if (fs.mangledName == "println" || fs.mangledName == "print" || fs.mangledName == "print_float" || fs.mangledName == "println_float" || fs.mangledName == "print_string" || fs.mangledName == "println_string" || fs.mangledName == "gspp_input" || fs.mangledName == "gspp_read_file" || fs.mangledName == "gspp_write_file" || fs.mangledName == "abs" || fs.mangledName == "sqrt" || fs.mangledName == "gspp_exec") return;
    currentFunc_ = fs.decl; currentVars_ = fs.locals; currentNamespace_ = fs.ns; frameSize_ = getFrameSize();
    currentEndLabel_ = fs.mangledName + "_end";
    std::string label = fs.mangledName; *out_ << "\t.globl\t" << label << "\n" << label << ":\n\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n\tsubq\t$" << frameSize_ << ", %rsp\n";
    if (fs.decl) {
        const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        const char* fregs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
        int ireg = 0, freg = 0;
        if (fs.isMethod) {
            std::string loc = getVarLocation("self");
            if (!loc.empty()) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n";
        }
        for (size_t i = 0; i < fs.decl->params.size(); i++) {
            if (fs.isMethod && i == 0 && fs.decl->params[i].name == "self") continue;
            std::string loc = getVarLocation(fs.decl->params[i].name);
            if (loc.empty()) continue;
            if (fs.decl->params[i].type.kind == Type::Kind::Float) {
                if (freg < 8) *out_ << "\tmovq\t%" << fregs[freg++] << ", " << loc << "\n";
            } else {
                if (ireg < 6) *out_ << "\tmovq\t%" << regs[ireg++] << ", " << loc << "\n";
            }
        }
    }
    if (fs.decl && fs.decl->body) emitStmt(fs.decl->body.get());
    if (fs.mangledName == "main") *out_ << "\tmovq\t$0, %rax\n";
    *out_ << currentEndLabel_ << ":\n";
    *out_ << "\tleave\n\tret\n\n"; currentFunc_ = nullptr;
}

} // namespace gspp
