#include "semantic.h"
#include <iostream>

namespace gspp {

void SemanticAnalyzer::addModule(const std::string& name, Program* prog) {
    modules_[name] = prog;
    auto oldStructs = std::move(structs_);
    auto oldFunctions = std::move(functions_);
    auto oldNs = currentNamespace_;
    structs_.clear();
    functions_.clear();
    currentNamespace_ = name;
    for (const auto& s : prog->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else moduleStructTemplates_[name][s.name] = &s;
    }
    for (const auto& f : prog->functions) {
        if (f.typeParams.empty()) analyzeFunc(f);
        else moduleFuncTemplates_[name][f.name] = &f;
    }
    for (const auto& s : prog->structs) {
        for (const auto& m : s.methods) analyzeMethod(s.name, m);
    }
    moduleStructs_[name] = std::move(structs_);
    moduleFunctions_[name] = std::move(functions_);
    structs_ = std::move(oldStructs);
    functions_ = std::move(oldFunctions);
    currentNamespace_ = oldNs;
}

void SemanticAnalyzer::instantiateStruct(const std::string& name, const std::string& ns, const std::vector<Type>& args) {
    if (args.empty()) return;
    std::string mangled = mangleGenericName(name, args);
    if (getStruct(mangled, ns)) return;
    const StructDecl* tmpl = nullptr;
    if (ns.empty()) {
        if (structTemplates_.count(name)) tmpl = structTemplates_[name];
    } else {
        if (moduleStructTemplates_.count(ns) && moduleStructTemplates_[ns].count(name))
            tmpl = moduleStructTemplates_[ns][name];
    }
    if (!tmpl) return;
    std::unordered_map<std::string, Type> subs;
    for (size_t i = 0; i < tmpl->typeParams.size() && i < args.size(); i++)
        subs[tmpl->typeParams[i]] = args[i];
    auto spec = std::make_unique<StructDecl>();
    spec->name = mangled;
    spec->loc = tmpl->loc;
    for (const auto& m : tmpl->members) {
        StructMember sm = m;
        sm.type = substitute(m.type, subs);
        spec->members.push_back(std::move(sm));
    }
    auto oldNs = currentNamespace_;
    currentNamespace_ = ns;
    analyzeStruct(*spec);
    instantiatedStructDecls_.push_back(std::move(spec));
    if (!ns.empty()) {
        moduleStructs_[ns][mangled] = std::move(structs_[mangled]);
        structs_.erase(mangled);
    }
    currentNamespace_ = oldNs;
}

void SemanticAnalyzer::instantiateFunc(const std::string& name, const std::string& ns, const std::vector<Type>& args) {
    if (args.empty()) return;
    std::string mangled = mangleGenericName(name, args);
    if (getFunc(mangled, ns)) return;
    const FuncDecl* tmpl = nullptr;
    if (ns.empty()) {
        if (funcTemplates_.count(name)) tmpl = funcTemplates_[name];
    } else {
        if (moduleFuncTemplates_.count(ns) && moduleFuncTemplates_[ns].count(name))
            tmpl = moduleFuncTemplates_[ns][name];
    }
    if (!tmpl) return;
    std::unordered_map<std::string, Type> subs;
    for (size_t i = 0; i < tmpl->typeParams.size() && i < args.size(); i++)
        subs[tmpl->typeParams[i]] = args[i];
    auto spec = std::make_unique<FuncDecl>();
    spec->name = mangled;
    spec->loc = tmpl->loc;
    spec->returnType = substitute(tmpl->returnType, subs);
    for (const auto& p : tmpl->params) {
        FuncParam fp = p;
        fp.type = substitute(p.type, subs);
        spec->params.push_back(std::move(fp));
    }
    spec->body = substituteStmt(tmpl->body.get(), subs);
    auto oldNs = currentNamespace_;
    currentNamespace_ = ns;
    analyzeFunc(*spec);
    instantiatedFuncDecls_.push_back(std::move(spec));
    if (!ns.empty()) {
        moduleFunctions_[ns][mangled] = std::move(functions_[mangled]);
        functions_.erase(mangled);
    }
    currentNamespace_ = oldNs;
}

void SemanticAnalyzer::analyzeStruct(const StructDecl& s) {
    StructDef def;
    def.name = s.name;
    def.mangledName = currentNamespace_.empty() ? s.name : currentNamespace_ + "_" + s.name;
    def.baseName = s.baseName;
    size_t offset = 0;

    if (!s.baseName.empty()) {
        StructDef* base = getStruct(s.baseName);
        if (base) {
            def.members = base->members;
            def.memberIndex = base->memberIndex;
            offset = base->sizeBytes;
        } else {
            error("base class '" + s.baseName + "' not found", s.loc);
        }
    }

    for (size_t i = 0; i < s.members.size(); i++) {
        const auto& m = s.members[i];
        Type ty = resolveType(m.type);
        if (def.memberIndex.count(m.name)) {
            def.members[def.memberIndex[m.name]] = {m.name, ty};
        } else {
            def.memberIndex[m.name] = def.members.size();
            def.members.push_back({m.name, ty});
            offset += 8;
        }
    }
    def.sizeBytes = (offset + 7) & ~7;
    if (def.sizeBytes < 64) def.sizeBytes = 64;
    for (const auto& m : s.methods) {
        FuncSymbol msym;
        msym.name = m.name;
        msym.ns = currentNamespace_;
        msym.mangledName = def.mangledName + "_" + m.name;
        msym.returnType = resolveType(m.returnType);
        msym.decl = &m;
        msym.isMethod = true;
        Type selfTy{Type::Kind::Pointer};
        selfTy.ptrTo = std::make_unique<Type>(Type::Kind::StructRef);
        selfTy.ptrTo->structName = s.name;
        selfTy.ptrTo->kind = Type::Kind::StructRef;
        selfTy.ptrTo->ns = currentNamespace_;
        msym.paramTypes.push_back(selfTy);
        bool firstIsSelf = !m.params.empty() && m.params[0].name == "self";
        for (size_t i = (firstIsSelf ? 1 : 0); i < m.params.size(); i++)
            msym.paramTypes.push_back(resolveType(m.params[i].type));
        def.methods[m.name] = std::move(msym);
    }
    structs_[s.name] = std::move(def);
}

void SemanticAnalyzer::analyzeMethod(const std::string& structName, const FuncDecl& f) {
    StructDef* sd = getStruct(structName, currentNamespace_);
    if (!sd) return;
    FuncSymbol& sym = sd->methods[f.name];
    auto oldFunc = currentFunc_;
    auto oldFuncSym = currentFuncSymbol_;
    auto oldStruct = currentStruct_;
    auto oldOffset = nextFrameOffset_;
    pushScope();
    currentFunc_ = const_cast<FuncDecl*>(&f);
    currentFuncSymbol_ = &sym;
    currentStruct_ = sd;
    nextFrameOffset_ = 0;
    addVar("self", sym.paramTypes[0], true);
    sym.locals["self"] = *lookupVar("self");
    bool firstIsSelf = !f.params.empty() && f.params[0].name == "self";
    for (size_t i = (firstIsSelf ? 1 : 0); i < f.params.size(); i++) {
        addVar(f.params[i].name, sym.paramTypes[i + (firstIsSelf ? 0 : 1)], true);
        sym.locals[f.params[i].name] = *lookupVar(f.params[i].name);
    }
    if (f.body) analyzeStmt(f.body.get());
    popScope();
    currentFunc_ = oldFunc;
    currentFuncSymbol_ = oldFuncSym;
    currentStruct_ = oldStruct;
    nextFrameOffset_ = oldOffset;
}

void SemanticAnalyzer::analyzeFunc(const FuncDecl& f) {
    FuncSymbol sym;
    sym.name = f.name;
    sym.ns = currentNamespace_;
    sym.isExtern = f.isExtern;
    if (f.isExtern) sym.mangledName = f.name;
    else sym.mangledName = currentNamespace_.empty() ? f.name : currentNamespace_ + "_" + f.name;
    sym.returnType = resolveType(f.returnType);
    sym.decl = &f;
    for (const auto& p : f.params) sym.paramTypes.push_back(resolveType(p.type));
    functions_[f.name] = std::move(sym);
    FuncSymbol& fs = functions_[f.name];
    auto oldFunc = currentFunc_;
    auto oldFuncSym = currentFuncSymbol_;
    auto oldOffset = nextFrameOffset_;
    pushScope();
    currentFunc_ = const_cast<FuncDecl*>(&f);
    currentFuncSymbol_ = &fs;
    nextFrameOffset_ = 0;
    for (size_t i = 0; i < f.params.size(); i++) {
        Type pt = resolveType(f.params[i].type);
        addVar(f.params[i].name, pt, true);
        if (auto vs = lookupVar(f.params[i].name)) fs.locals[f.params[i].name] = *vs;
    }
    if (f.body) analyzeStmt(f.body.get());
    popScope();
    currentFunc_ = oldFunc;
    currentFuncSymbol_ = oldFuncSym;
    nextFrameOffset_ = oldOffset;
}

void SemanticAnalyzer::analyzeProgram() {
    for (const auto& imp : program_->imports) {
        if (!imp.importNames.empty()) {
            std::string ns = imp.alias.empty() ? imp.name : imp.alias;
            for (const auto& name : imp.importNames) {
                if (moduleFunctions_.count(ns) && moduleFunctions_[ns].count(name)) {
                    functions_[name] = moduleFunctions_[ns][name];
                } else if (moduleStructs_.count(ns) && moduleStructs_[ns].count(name)) {
                    structs_[name] = moduleStructs_[ns][name];
                } else {
                    error("name '" + name + "' not found in module '" + ns + "'", imp.loc);
                }
            }
        }
    }
    for (const auto& s : program_->structs) {
        if (s.typeParams.empty()) analyzeStruct(s);
        else structTemplates_[s.name] = &s;
    }
    for (const auto& s : program_->structs) {
        StructDef* sd = getStruct(s.name, currentNamespace_);
        if (!sd) continue;
        for (const auto& m : s.methods) {
            if (m.body) {
                for (const auto& stmt : m.body->blockStmts) {
                    if (stmt->kind == Stmt::Kind::Assign && stmt->assignTarget->kind == Expr::Kind::Member) {
                        if (stmt->assignTarget->left->kind == Expr::Kind::Var && stmt->assignTarget->left->ident == "self") {
                            if (sd->memberIndex.find(stmt->assignTarget->member) == sd->memberIndex.end()) {
                                sd->memberIndex[stmt->assignTarget->member] = sd->members.size();
                                sd->members.push_back({stmt->assignTarget->member, Type{Type::Kind::Int}});
                                sd->sizeBytes += 8;
                            }
                        }
                    }
                }
            }
        }
    }
    FuncSymbol printlnSym; printlnSym.name = "println"; printlnSym.mangledName = "println"; printlnSym.returnType.kind = Type::Kind::Int; printlnSym.paramTypes.push_back(Type{Type::Kind::Int}); printlnSym.isExtern = true; functions_["println"] = std::move(printlnSym);
    FuncSymbol printSym; printSym.name = "print"; printSym.mangledName = "print"; printSym.returnType.kind = Type::Kind::Int; printSym.paramTypes.push_back(Type{Type::Kind::Int}); printSym.isExtern = true; functions_["print"] = std::move(printSym);
    FuncSymbol logSym; logSym.name = "log"; logSym.mangledName = "println"; logSym.returnType.kind = Type::Kind::Int; logSym.paramTypes.push_back(Type{Type::Kind::Int}); logSym.isExtern = true; functions_["log"] = std::move(logSym);
    FuncSymbol pfSym; pfSym.name = "print_float"; pfSym.mangledName = "print_float"; pfSym.returnType.kind = Type::Kind::Int; pfSym.paramTypes.push_back(Type{Type::Kind::Float}); pfSym.isExtern = true; functions_["print_float"] = std::move(pfSym);
    FuncSymbol plfSym; plfSym.name = "println_float"; plfSym.mangledName = "println_float"; plfSym.returnType.kind = Type::Kind::Int; plfSym.paramTypes.push_back(Type{Type::Kind::Float}); plfSym.isExtern = true; functions_["println_float"] = std::move(plfSym);
    FuncSymbol psSym; psSym.name = "print_string"; psSym.mangledName = "print_string"; psSym.returnType.kind = Type::Kind::Int; psSym.paramTypes.push_back(Type{Type::Kind::String}); psSym.isExtern = true; functions_["print_string"] = std::move(psSym);
    FuncSymbol plsSym; plsSym.name = "println_string"; plsSym.mangledName = "println_string"; plsSym.returnType.kind = Type::Kind::Int; plsSym.paramTypes.push_back(Type{Type::Kind::String}); plsSym.isExtern = true; functions_["println_string"] = std::move(plsSym);
    FuncSymbol inputSym; inputSym.name = "input"; inputSym.mangledName = "gspp_input"; inputSym.returnType.kind = Type::Kind::String; functions_["input"] = std::move(inputSym);
    FuncSymbol rfSym; rfSym.name = "read_file"; rfSym.mangledName = "gspp_read_file"; rfSym.returnType.kind = Type::Kind::String; rfSym.paramTypes.push_back(Type{Type::Kind::String}); rfSym.isExtern = true; functions_["read_file"] = std::move(rfSym);
    FuncSymbol wfSym; wfSym.name = "write_file"; wfSym.mangledName = "gspp_write_file"; wfSym.returnType.kind = Type::Kind::Void; wfSym.paramTypes.push_back(Type{Type::Kind::String}); wfSym.paramTypes.push_back(Type{Type::Kind::String}); wfSym.isExtern = true; functions_["write_file"] = std::move(wfSym);
    FuncSymbol execSym; execSym.name = "exec"; execSym.mangledName = "gspp_exec"; execSym.returnType.kind = Type::Kind::Int; execSym.paramTypes.push_back(Type{Type::Kind::String}); execSym.isExtern = true; functions_["exec"] = std::move(execSym);
    FuncSymbol frSym; frSym.name = "read"; frSym.mangledName = "gspp_read_file"; frSym.returnType.kind = Type::Kind::String; frSym.paramTypes.push_back(Type{Type::Kind::String}); frSym.isExtern = true; moduleFunctions_["File"]["read"] = std::move(frSym);
    FuncSymbol fwSym; fwSym.name = "write"; fwSym.mangledName = "gspp_write_file"; fwSym.returnType.kind = Type::Kind::Void; fwSym.paramTypes.push_back(Type{Type::Kind::String}); fwSym.paramTypes.push_back(Type{Type::Kind::String}); fwSym.isExtern = true; moduleFunctions_["File"]["write"] = std::move(fwSym);
    FuncSymbol absSym; absSym.name = "abs"; absSym.mangledName = "abs"; absSym.returnType.kind = Type::Kind::Int; absSym.paramTypes.push_back(Type{Type::Kind::Int}); absSym.isExtern = true; functions_["abs"] = std::move(absSym);
    FuncSymbol sqrtSym; sqrtSym.name = "sqrt"; sqrtSym.mangledName = "sqrt"; sqrtSym.returnType.kind = Type::Kind::Float; sqrtSym.paramTypes.push_back(Type{Type::Kind::Float}); sqrtSym.isExtern = true; functions_["sqrt"] = std::move(sqrtSym);

    // os module
    FuncSymbol exitSym; exitSym.name = "exit"; exitSym.mangledName = "exit"; exitSym.returnType.kind = Type::Kind::Void; exitSym.paramTypes.push_back(Type{Type::Kind::Int}); exitSym.isExtern = true; moduleFunctions_["os"]["exit"] = std::move(exitSym);
    FuncSymbol sleepSym; sleepSym.name = "sleep"; sleepSym.mangledName = "usleep"; sleepSym.returnType.kind = Type::Kind::Void; sleepSym.paramTypes.push_back(Type{Type::Kind::Int}); sleepSym.isExtern = true; moduleFunctions_["os"]["sleep"] = std::move(sleepSym);

    // math module
    FuncSymbol sinSym; sinSym.name = "sin"; sinSym.mangledName = "sin"; sinSym.returnType.kind = Type::Kind::Float; sinSym.paramTypes.push_back(Type{Type::Kind::Float}); sinSym.isExtern = true; moduleFunctions_["math"]["sin"] = std::move(sinSym);
    FuncSymbol cosSym; cosSym.name = "cos"; cosSym.mangledName = "cos"; cosSym.returnType.kind = Type::Kind::Float; cosSym.paramTypes.push_back(Type{Type::Kind::Float}); cosSym.isExtern = true; moduleFunctions_["math"]["cos"] = std::move(cosSym);
    FuncSymbol tanSym; tanSym.name = "tan"; tanSym.mangledName = "tan"; tanSym.returnType.kind = Type::Kind::Float; tanSym.paramTypes.push_back(Type{Type::Kind::Float}); tanSym.isExtern = true; moduleFunctions_["math"]["tan"] = std::move(tanSym);
    FuncSymbol powSym; powSym.name = "pow"; powSym.mangledName = "pow"; powSym.returnType.kind = Type::Kind::Float; powSym.paramTypes.push_back(Type{Type::Kind::Float}); powSym.paramTypes.push_back(Type{Type::Kind::Float}); powSym.isExtern = true; moduleFunctions_["math"]["pow"] = std::move(powSym);

    for (const auto& f : program_->functions) { if (f.typeParams.empty()) analyzeFunc(f); else funcTemplates_[f.name] = &f; }
    if (!program_->topLevelStmts.empty()) {
        auto syntheticMain = std::make_unique<FuncDecl>(); syntheticMain->name = "main"; syntheticMain->returnType.kind = Type::Kind::Int;
        auto body = std::make_unique<Stmt>(); body->kind = Stmt::Kind::Block; for (auto& s : program_->topLevelStmts) body->blockStmts.push_back(std::move(s));
        syntheticMain->body = std::move(body); analyzeFunc(*syntheticMain); instantiatedFuncDecls_.push_back(std::move(syntheticMain));
    }
    for (const auto& s : program_->structs) {
        for (const auto& m : s.methods) if (m.name == "init") analyzeMethod(s.name, m);
        for (const auto& m : s.methods) if (m.name != "init") analyzeMethod(s.name, m);
    }
}

} // namespace gspp
