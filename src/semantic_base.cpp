#include "semantic.h"
#include <iostream>

namespace gspp {

SemanticAnalyzer::SemanticAnalyzer(Program* program) : program_(program) {}

void SemanticAnalyzer::pushScope() {
    scopes_.emplace_back();
}

void SemanticAnalyzer::popScope() {
    scopes_.pop_back();
}

void SemanticAnalyzer::addVar(const std::string& name, const Type& type, bool isParam) {
    VarSymbol sym;
    sym.name = name;
    sym.type = type;
    sym.isParam = isParam;
    if (!scopes_.empty()) {
        nextFrameOffset_ += 8;
        sym.frameOffset = -nextFrameOffset_;
        scopes_.back()[name] = sym;
        if (currentFuncSymbol_)
            currentFuncSymbol_->locals[name] = sym;
    }
}

VarSymbol* SemanticAnalyzer::lookupVar(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->find(name);
        if (i != it->end()) return &i->second;
    }
    return nullptr;
}

void SemanticAnalyzer::error(const std::string& msg, SourceLoc loc) {
    errors_.push_back(SourceManager::instance().formatError(loc, msg));
}

StructDef* SemanticAnalyzer::getStruct(const std::string& name, const std::string& ns) {
    if (ns.empty() || ns == currentNamespace_) {
        auto i = structs_.find(name);
        if (i != structs_.end()) return &i->second;
    }
    if (ns.empty()) return nullptr;
    auto mi = moduleStructs_.find(ns);
    if (mi == moduleStructs_.end()) return nullptr;
    auto i = mi->second.find(name);
    return i == mi->second.end() ? nullptr : &i->second;
}

FuncSymbol* SemanticAnalyzer::getMethod(StructDef* sd, const std::string& name) {
    while (sd) {
        auto i = sd->methods.find(name);
        if (i != sd->methods.end()) return &i->second;
        if (sd->baseName.empty()) break;
        sd = getStruct(sd->baseName);
    }
    return nullptr;
}

FuncSymbol* SemanticAnalyzer::getFunc(const std::string& name, const std::string& ns) {
    if (ns.empty() || ns == currentNamespace_) {
        auto i = functions_.find(name);
        if (i != functions_.end()) return &i->second;
    }
    if (ns.empty()) return nullptr;
    auto mi = moduleFunctions_.find(ns);
    if (mi == moduleFunctions_.end()) return nullptr;
    auto i = mi->second.find(name);
    return i == mi->second.end() ? nullptr : &i->second;
}

bool SemanticAnalyzer::analyze() { analyzeProgram(); return errors_.empty(); }

} // namespace gspp
