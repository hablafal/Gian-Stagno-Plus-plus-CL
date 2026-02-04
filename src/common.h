#ifndef GSPP_COMMON_H
#define GSPP_COMMON_H

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace gspp {

struct SourceLoc {
    std::string filename;
    int line = 0;
    int column = 0;
};

class SourceManager {
public:
    static SourceManager& instance() {
        static SourceManager inst;
        return inst;
    }

    void addSource(const std::string& filename, const std::string& source) {
        sources_[filename] = source;
    }

    std::string getLine(const std::string& filename, int line) {
        auto it = sources_.find(filename);
        if (it == sources_.end()) return "";
        const std::string& src = it->second;
        size_t start = 0;
        int l = 1;
        for (size_t i = 0; i < src.size(); i++) {
            if (l == line) {
                start = i;
                while (i < src.size() && src[i] != '\n' && src[i] != '\r') i++;
                return src.substr(start, i - start);
            }
            if (src[i] == '\n') l++;
            else if (src[i] == '\r') {
                if (i+1 < src.size() && src[i+1] == '\n') i++;
                l++;
            }
        }
        return "";
    }

    std::string formatError(const SourceLoc& loc, const std::string& msg) {
        std::ostringstream os;
        os << loc.filename << ":" << loc.line << ":" << loc.column << ": error: " << msg << "\n";
        std::string lineText = getLine(loc.filename, loc.line);
        if (!lineText.empty()) {
            os << "    " << lineText << "\n";
            os << "    ";
            for (int i = 1; i < loc.column; i++) {
                if (i-1 < (int)lineText.size() && lineText[i-1] == '\t') os << "\t";
                else os << " ";
            }
            os << "^";
        }
        return os.str();
    }

private:
    std::unordered_map<std::string, std::string> sources_;
};

}

#endif
