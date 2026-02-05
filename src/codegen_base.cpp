#include "codegen.h"
#include <iostream>
#include <sstream>

namespace gspp {

CodeGenerator::CodeGenerator(Program* program, SemanticAnalyzer* semantic, std::ostream& out, bool use32Bit, bool isLinux)
    : program_(program), semantic_(semantic), out_(&out), use32Bit_(use32Bit), isLinux_(isLinux) {}

std::string CodeGenerator::nextLabel() {
    return ".L" + std::to_string(labelCounter_++);
}

std::string CodeGenerator::getVarLocation(const std::string& name) {
    if (currentVars_.count(name)) {
        int offset = currentVars_.at(name).frameOffset;
        return std::to_string(offset) + "(%rbp)";
    }
    return "";
}

int CodeGenerator::getTypeSize(const Type& t) {
    if (t.kind == Type::Kind::Int || t.kind == Type::Kind::Float || t.kind == Type::Kind::Pointer || t.kind == Type::Kind::String || t.kind == Type::Kind::List || t.kind == Type::Kind::Dict || t.kind == Type::Kind::Set || t.kind == Type::Kind::Tuple) return 8;
    if (t.kind == Type::Kind::Bool || t.kind == Type::Kind::Char) return 1;
    StructDef* sd = resolveStruct(t.structName, t.ns);
    if (sd) return (int)sd->sizeBytes;
    return 8;
}

int CodeGenerator::getFrameSize() {
    int sz = 0;
    for (auto& p : currentVars_) {
        int s = getTypeSize(p.second.type);
        if (s < 8) s = 8;
        sz += s;
    }
    return (sz + 31) & ~15; // Extra padding for safety
}

bool CodeGenerator::isRefCounted(const Type& t) {
    return t.kind == Type::Kind::String ||
           t.kind == Type::Kind::List ||
           t.kind == Type::Kind::Dict ||
           t.kind == Type::Kind::Set ||
           t.kind == Type::Kind::Tuple ||
           t.kind == Type::Kind::Chan ||
           (t.kind == Type::Kind::Pointer && t.ptrTo && t.ptrTo->kind == Type::Kind::StructRef);
}

bool CodeGenerator::isRCProducer(Expr* expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::New:
        case Expr::Kind::Call:
        case Expr::Kind::ListLit:
        case Expr::Kind::DictLit:
        case Expr::Kind::SetLit:
        case Expr::Kind::TupleLit:
        case Expr::Kind::Comprehension:
        case Expr::Kind::ChanInit:
        case Expr::Kind::Receive:
        case Expr::Kind::Slice:
            return true;
        case Expr::Kind::Binary:
            return expr->exprType.kind == Type::Kind::String ||
                   expr->exprType.kind == Type::Kind::List ||
                   expr->exprType.kind == Type::Kind::Dict ||
                   expr->exprType.kind == Type::Kind::Set;
        case Expr::Kind::Cast:
            return isRCProducer(expr->left.get());
        case Expr::Kind::Ternary:
            return isRCProducer(expr->left.get()) || isRCProducer(expr->right.get());
        default:
            return false;
    }
}

StructDef* CodeGenerator::resolveStruct(const std::string& name, const std::string& ns) {
    return semantic_->getStruct(name, ns);
}

void CodeGenerator::emitExprToRax(Expr* expr) { emitExpr(expr, "rax", false); }
void CodeGenerator::emitExprToXmm0(Expr* expr) { emitExpr(expr, "xmm0", true); }

void CodeGenerator::emitProgram() {
    *out_ << "\t.file\t\"gspp\"\n";
    std::ostringstream textOut;
    std::ostream* originalOut = out_;
    out_ = &textOut;
    emitProgramBody();
    out_ = originalOut;
    *out_ << "\t.data\n.LC_fmt_d:\n\t.string \"%d\"\n.LC_fmt_d_nl:\n\t.string \"%d\\n\"\n.LC_fmt_f:\n\t.string \"%f\"\n.LC_fmt_f_nl:\n\t.string \"%f\\n\"\n.LC_fmt_s:\n\t.string \"%s\"\n.LC_fmt_s_nl:\n\t.string \"%s\\n\"\n";
    for (auto& p : stringPool_) {
        if (p.second.find(".LC_float_") != std::string::npos) {
            *out_ << "\t.align 8\n";
            *out_ << p.second << ":\n\t.double " << p.first << "\n";
        } else {
            *out_ << "\t.align 16\n";
            *out_ << p.second << "_header:\n\t.quad -1\n\t.quad 0\n";
            *out_ << p.second << ":\n\t.string \"" << p.first << "\"\n";
        }
    }
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
    *out_ << "\t.extern\tgspp_spawn\n\t.extern\tgspp_join\n\t.extern\tgspp_mutex_create\n\t.extern\tgspp_mutex_lock\n\t.extern\tgspp_mutex_unlock\n";
    *out_ << "\t.extern\tgspp_chan_new\n\t.extern\tgspp_chan_send\n\t.extern\tgspp_chan_recv\n\t.extern\tgspp_chan_destroy\n";
    *out_ << "\t.extern\tgspp_alloc\n\t.extern\tgspp_retain\n\t.extern\tgspp_release\n";
    *out_ << "\t.extern\tgspp_push_exception_handler\n\t.extern\tgspp_pop_exception_handler\n\t.extern\tgspp_raise\n\t.extern\tgspp_get_current_exception\n";
    *out_ << "\t.extern\t_setjmp\n";
    for (const auto& pair : semantic_->functions()) emitFunc(pair.second);
    for (const auto& modPair : semantic_->moduleFunctions()) for (const auto& pair : modPair.second) emitFunc(pair.second);
    for (const auto& pair : semantic_->structs()) for (const auto& mPair : pair.second.methods) emitFunc(mPair.second);
}

bool CodeGenerator::generate() { emitProgram(); return errors_.empty(); }

void CodeGenerator::error(const std::string& msg, SourceLoc loc) {
    errors_.push_back(SourceManager::instance().formatError(loc, msg));
}

void CodeGenerator::emitRCRelease(const std::string& varName) {
    std::string loc = getVarLocation(varName);
    if (loc.empty()) return;
    *out_ << "\tmovq\t" << loc << ", %rdi\n";
    emitCall("gspp_release", 1);
}

void CodeGenerator::emitRCRetain(const std::string& reg) {
    *out_ << "\tmovq\t%" << reg << ", %rdi\n";
    emitCall("gspp_retain", 1);
}

void CodeGenerator::emitCall(const std::string& label, int numArgs) {
    if (!isLinux_) {
        *out_ << "\tsubq\t$32, %rsp\n";
        *out_ << "\tcall\t" << label << "\n";
        *out_ << "\taddq\t$32, %rsp\n";
    } else {
        // Simple 16-byte alignment for Linux
        std::string alignedLabel = label + "_aligned_" + std::to_string(labelCounter_);
        std::string doneLabel = label + "_done_" + std::to_string(labelCounter_);
        labelCounter_++;

        *out_ << "\ttestq\t$15, %rsp\n";
        *out_ << "\tjnz\t" << alignedLabel << "\n";
        // Stack is 16-byte aligned (rsp % 16 == 0). Call will push 8, making it 8-aligned at entry. Correct.
        *out_ << "\tcall\t" << label << "\n";
        *out_ << "\tjmp\t" << doneLabel << "\n";
        *out_ << alignedLabel << ":\n";
        // Stack is 8-byte aligned (rsp % 16 == 8). Subtract 8 to make it 16-aligned before call.
        *out_ << "\tsubq\t$8, %rsp\n";
        *out_ << "\tcall\t" << label << "\n";
        *out_ << "\taddq\t$8, %rsp\n";
        *out_ << doneLabel << ":\n";
    }
}

} // namespace gspp
