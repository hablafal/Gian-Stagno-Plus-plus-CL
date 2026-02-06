#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>

namespace gspp {

void runREPL(bool use64Bit, bool isLinux) {
    std::cout << "GS++ Interactive REPL (Gian Stagno Plus Plus)\n";
    std::cout << "Type 'exit' or 'quit' to exit.\n";

    std::string accumulatedCode;
    int lineNum = 1;

    while (true) {
        std::cout << "[" << lineNum << "] >>> ";
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;

        if (line.empty()) continue;

        // Simple multi-line handling: if line ends with ':', wait for more input until an empty line
        if (line.back() == ':') {
            std::string block = line + "\n";
            while (true) {
                std::cout << "    ... ";
                std::string subline;
                if (!std::getline(std::cin, subline) || subline.empty()) break;
                block += subline + "\n";
            }
            line = block;
        }

        std::string currentAttempt = accumulatedCode + line + "\n";

        // Try to compile it
        std::ofstream tmpGs("repl_tmp.gs");
        tmpGs << currentAttempt;
        tmpGs.close();

        // Run compiler internally or via system
        // For simplicity and to avoid resetting all state in this turn,
        // let's run the gsc binary we just built!
        std::string compileCmd = "./gsc repl_tmp.gs -o repl_tmp.exe";
        if (!use64Bit) compileCmd += " -m32";

        int ret = system((compileCmd + " > repl_error.log 2>&1").c_str());
        if (ret == 0) {
            // Success!
            accumulatedCode = currentAttempt;
            system("./repl_tmp.exe");
            lineNum++;
        } else {
            // Failure. Show error.
            std::ifstream errLog("repl_error.log");
            std::string errLine;
            bool foundRealError = false;
            while (std::getline(errLog, errLine)) {
                if (errLine.find("error:") != std::string::npos) {
                    std::cerr << errLine << "\n";
                    foundRealError = true;
                }
            }
            if (!foundRealError) {
                // Maybe it's an incomplete block?
                // For now just show the whole log if no "error:" found
                // std::cerr << "Incomplete or invalid code.\n";
            }
        }
    }

    // Cleanup
    system("rm -f repl_tmp.gs repl_tmp.s repl_tmp.exe repl_error.log");
}

}

void runREPL(bool use64Bit, bool isLinux) {
    gspp::runREPL(use64Bit, isLinux);
}
