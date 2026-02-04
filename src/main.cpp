#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "optimizer.h"
#include "codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static int runCommand(const std::string& cmd) {
    return system(cmd.c_str());
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "GS++ Compiler (gsc) — Gian Stagno Plus Plus\n";
        std::cerr << "Usage: gsc <source.gs> [options]\n";
        std::cerr << "  -o <exe>   Output executable (default: base name of source)\n";
        std::cerr << "  -S         Emit assembly only (do not link)\n";
        std::cerr << "  -g         Debug mode (no optimizations)\n";
        std::cerr << "  -O         Release mode (optimize)\n";
        std::cerr << "  -m64       Generate 64-bit code (default: 32-bit for compatibility)\n";
        return 1;
    }
    std::string sourcePath = argv[1];
    std::string outPath;
    bool emitAsmOnly = false;
    bool use64Bit = true;
    bool debugMode = false;
    bool releaseMode = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { outPath = argv[++i]; continue; }
        if (a == "-S") { emitAsmOnly = true; continue; }
        if (a == "-g") { debugMode = true; continue; }
        if (a == "-O") { releaseMode = true; continue; }
        if (a == "-m64") { use64Bit = true; continue; }
    }
    if (outPath.empty()) {
        size_t dot = sourcePath.find_last_of(".\\/");
        if (dot != std::string::npos && sourcePath[dot] == '.')
            outPath = sourcePath.substr(0, dot);
        else
            outPath = sourcePath;
#ifdef _WIN32
        outPath += ".exe";
#endif
    }

    std::string source = readFile(sourcePath);
    if (source.empty()) {
        std::cerr << "gsc: cannot open '" << sourcePath << "'\n";
        return 1;
    }

    gspp::SourceManager::instance().addSource(sourcePath, source);

    gspp::Lexer lexer(source, sourcePath);
    gspp::Parser parser(lexer);
    std::unique_ptr<gspp::Program> program = parser.parseProgram();
    if (!parser.errors().empty()) {
        for (const auto& e : parser.errors()) std::cerr << e << "\n";
        return 1;
    }

    gspp::SemanticAnalyzer semantic(program.get());

    std::set<std::string> loadedModules;
    std::unordered_map<std::string, gspp::Program*> modulesByPath;
    std::vector<std::unique_ptr<gspp::Program>> modulePrograms;

    auto loadModuleRecursive = [&](auto self, gspp::Program* p) -> void {
        for (const auto& imp : p->imports) {
            std::string ns = imp.alias.empty() ? imp.name : imp.alias;
            if (loadedModules.count(imp.path)) {
                // Already loaded, but we might need to register it under a different namespace/alias
                semantic.addModule(ns, modulesByPath[imp.path]);
                continue;
            }
            loadedModules.insert(imp.path);

            std::string modSource = readFile(imp.path);
            if (modSource.empty()) {
                std::cerr << "error: cannot find module '" << imp.name << "' at '" << imp.path << "'\n";
                continue;
            }
            gspp::SourceManager::instance().addSource(imp.path, modSource);
            gspp::Lexer modLexer(modSource, imp.path);
            gspp::Parser modParser(modLexer);
            auto modProg = modParser.parseProgram();
            gspp::Program* modProgPtr = modProg.get();
            modulesByPath[imp.path] = modProgPtr;
            modulePrograms.push_back(std::move(modProg));

            self(self, modProgPtr);
            semantic.addModule(ns, modProgPtr);
        }
    };

    loadModuleRecursive(loadModuleRecursive, program.get());

    if (!semantic.analyze()) {
        for (const auto& e : semantic.errors()) std::cerr << e << "\n";
        return 1;
    }

    gspp::Optimizer optimizer(program.get());
    if (releaseMode) optimizer.optimize();

    std::string asmPath = outPath;
    if (!emitAsmOnly) {
        size_t dot = asmPath.find_last_of(".\\/");
        asmPath = (dot != std::string::npos && asmPath[dot] == '.') ? asmPath.substr(0, dot) : asmPath;
        asmPath += ".s";
    } else if (outPath.find('.') == std::string::npos)
        asmPath += ".s";

    std::ofstream asmFile(asmPath);
    if (!asmFile) {
        std::cerr << "gsc: cannot write '" << asmPath << "'\n";
        return 1;
    }
    gspp::CodeGenerator codegen(program.get(), &semantic, asmFile, !use64Bit);
    if (!codegen.generate()) {
        for (const auto& e : codegen.errors()) std::cerr << e << "\n";
        return 1;
    }
    asmFile.close();

    if (emitAsmOnly) {
        std::cout << "Assembly written to " << asmPath << "\n";
        return 0;
    }

#ifdef _WIN32
    std::string linkCmd = use64Bit
        ? "gcc -m64 -Wl,-subsystem,console -o \"" + outPath + "\" \"" + asmPath + "\" libgspprun.a -lm"
        : "gcc -m32 -Wl,-subsystem,console -Wl,-e,_main -o \"" + outPath + "\" \"" + asmPath + "\" libgspprun.a -lmsvcrt -lm";
#else
    std::string linkCmd = use64Bit
        ? "g++ -m64 -o \"" + outPath + "\" \"" + asmPath + "\" libgspprun.a -lm"
        : "g++ -m32 -o \"" + outPath + "\" \"" + asmPath + "\" libgspprun.a -lm";
#endif
    if (debugMode) linkCmd += " -g";
    int ret = runCommand(linkCmd);
    if (ret != 0) {
        std::cerr << "gsc: linking failed (is gcc/MinGW in PATH?)\n";
        return 1;
    }
    std::cout << "Built: " << outPath << "\n";
    return 0;
}
