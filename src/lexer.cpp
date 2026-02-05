#include "lexer.h"
#include <cctype>
#include <stdexcept>

namespace gspp {

Lexer::Lexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename.empty() ? "<input>" : filename) {}

char Lexer::cur() const {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peekChar() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

void Lexer::advance() {
    if (pos_ < source_.size()) {
        if (source_[pos_] == '\n') { line_++; col_ = 1; }
        else col_++;
        pos_++;
    }
}

bool Lexer::match(char c) {
    if (cur() != c) return false;
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        while (cur() == ' ' || cur() == '\t' || cur() == '\r') advance();
        if (cur() == '/' && peekChar() == '/') {
            while (cur() != '\0' && cur() != '\n') advance();
            continue;
        }
        if (cur() == '#') {
            while (cur() != '\0' && cur() != '\n') advance();
            continue;
        }
        if (cur() == '/' && peekChar() == '*') {
            advance(); advance();
            while (cur() != '\0' && !(cur() == '*' && peekChar() == '/')) advance();
            if (cur() == '*') { advance(); advance(); }
            continue;
        }
        break;
    }
}

Token Lexer::makeToken(TokenKind k) {
    Token t;
    t.kind = k;
    t.loc = { filename_, line_, col_ };
    return t;
}

Token Lexer::lexNumber() {
    SourceLoc loc = { filename_, line_, col_ };
    std::string num;
    bool isFloat = false;
    while (std::isdigit(static_cast<unsigned char>(cur()))) { num += cur(); advance(); }
    if (cur() == '.' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
        isFloat = true;
        num += cur(); advance();
        while (std::isdigit(static_cast<unsigned char>(cur()))) { num += cur(); advance(); }
    }
    Token t;
    t.loc = loc;
    if (isFloat) {
        t.kind = TokenKind::FloatLit;
        t.floatVal = std::stod(num);
    } else {
        t.kind = TokenKind::IntLit;
        t.intVal = std::stoll(num);
    }
    return t;
}

Token Lexer::lexString() {
    SourceLoc loc = { filename_, line_, col_ };
    bool isTriple = false;
    if (cur() == '"' && peekChar() == '"' && source_[pos_+2] == '"') {
        isTriple = true;
        advance(); advance(); advance();
    } else {
        advance();
    }

    std::string s;
    while (cur() != '\0') {
        if (isTriple && cur() == '"' && peekChar() == '"' && source_[pos_+2] == '"') {
            advance(); advance(); advance();
            break;
        }
        if (!isTriple && cur() == '"') {
            advance();
            break;
        }

        if (cur() == '\\') {
            advance();
            if (cur() == 'n') { s += '\n'; advance(); }
            else if (cur() == 't') { s += '\t'; advance(); }
            else if (cur() == '"') { s += '"'; advance(); }
            else { s += cur(); advance(); }
        } else {
            s += cur();
            advance();
        }
    }

    Token t;
    t.kind = TokenKind::StringLit;
    t.text = s;
    t.loc = loc;
    return t;
}

Token Lexer::lexIdentOrKeyword() {
    SourceLoc loc = { filename_, line_, col_ };
    std::string id;
    if (std::isalpha(static_cast<unsigned char>(cur())) || cur() == '_') {
        id += cur(); advance();
        while (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_') { id += cur(); advance(); }
    }
    Token t;
    t.loc = loc;
    t.text = id;
    if (id == "var") t.kind = TokenKind::Var;
    else if (id == "let") t.kind = TokenKind::Let;
    else if (id == "func" || id == "def") t.kind = TokenKind::Func;
    else if (id == "fn") t.kind = TokenKind::Fn;
    else if (id == "class") t.kind = TokenKind::Class;
    else if (id == "struct") t.kind = TokenKind::Struct;
    else if (id == "mut") t.kind = TokenKind::Mut;
    else if (id == "data") t.kind = TokenKind::Data;
    else if (id == "if") t.kind = TokenKind::If;
    else if (id == "else") t.kind = TokenKind::Else;
    else if (id == "elif") t.kind = TokenKind::Elif;
    else if (id == "then") t.kind = TokenKind::Then;
    else if (id == "while") t.kind = TokenKind::While;
    else if (id == "for") t.kind = TokenKind::For;
    else if (id == "from") t.kind = TokenKind::From;
    else if (id == "in") t.kind = TokenKind::In;
    else if (id == "repeat") t.kind = TokenKind::Repeat;
    else if (id == "loop") t.kind = TokenKind::Loop;
    else if (id == "as") t.kind = TokenKind::As;
    else if (id == "check") t.kind = TokenKind::Check;
    else if (id == "case") t.kind = TokenKind::Case;
    else if (id == "defer") t.kind = TokenKind::Defer;
    else if (id == "return") t.kind = TokenKind::Return;
    else if (id == "int") t.kind = TokenKind::Int;
    else if (id == "float") t.kind = TokenKind::Float;
    else if (id == "decimal") t.kind = TokenKind::Decimal;
    else if (id == "bool") t.kind = TokenKind::Bool;
    else if (id == "string") t.kind = TokenKind::String;
    else if (id == "Text") t.kind = TokenKind::Text;
    else if (id == "Arr" || id == "arr") t.kind = TokenKind::Arr;
    else if (id == "tuple") t.kind = TokenKind::Tuple;
    else if (id == "char") t.kind = TokenKind::Char;
    else if (id == "true" || id == "True" || id == "yes" || id == "on") t.kind = TokenKind::True;
    else if (id == "false" || id == "False" || id == "no" || id == "off") t.kind = TokenKind::False;
    else if (id == "and") t.kind = TokenKind::And;
    else if (id == "or") t.kind = TokenKind::Or;
    else if (id == "not") t.kind = TokenKind::Not;
    else if (id == "import") t.kind = TokenKind::Import;
    else if (id == "use") t.kind = TokenKind::Use;
    else if (id == "asm") t.kind = TokenKind::Asm;
    else if (id == "unsafe") t.kind = TokenKind::Unsafe;
    else if (id == "new") t.kind = TokenKind::New;
    else if (id == "delete") t.kind = TokenKind::Delete;
    else if (id == "cast") t.kind = TokenKind::Cast;
    else if (id == "sizeof") t.kind = TokenKind::Sizeof;
    else if (id == "spawn") t.kind = TokenKind::Spawn;
    else if (id == "join") t.kind = TokenKind::Join;
    else if (id == "mutex") t.kind = TokenKind::Mutex;
    else if (id == "lock") t.kind = TokenKind::Lock;
    else if (id == "thread") t.kind = TokenKind::Thread;
    else if (id == "chan") t.kind = TokenKind::Chan;
    else if (id == "ptr") t.kind = TokenKind::Ptr;
    else if (id == "super") t.kind = TokenKind::Super;
    else if (id == "try") t.kind = TokenKind::Try;
    else if (id == "except") t.kind = TokenKind::Except;
    else if (id == "finally") t.kind = TokenKind::Finally;
    else if (id == "raise") t.kind = TokenKind::Raise;
    else if (id == "extern") t.kind = TokenKind::Extern;
    else if (id == "nil" || id == "Nil") t.kind = TokenKind::Nil;
    else t.kind = TokenKind::Ident;
    return t;
}

Token Lexer::lex() {
    if (pendingDedents_ > 0) {
        pendingDedents_--;
        return makeToken(TokenKind::Dedent);
    }

    skipWhitespaceAndComments();

    if (atLineStart_) {
        atLineStart_ = false;
        int currentIndent = 0;
        size_t tempPos = pos_;
        while (tempPos > 0 && source_[tempPos-1] != '\n') tempPos--;
        while (source_[tempPos] == ' ' || source_[tempPos] == '\t') {
            if (source_[tempPos] == ' ') currentIndent++;
            else currentIndent = (currentIndent + 4) & ~3;
            tempPos++;
        }

        if (cur() != '\0' && cur() != '\n' && cur() != '\r' && cur() != '#' && !(cur() == '/' && (peekChar() == '/' || peekChar() == '*'))) {
            if (currentIndent > indentStack_.back()) {
                indentStack_.push_back(currentIndent);
                return makeToken(TokenKind::Indent);
            } else if (currentIndent < indentStack_.back()) {
                while (currentIndent < indentStack_.back()) {
                    indentStack_.pop_back();
                    pendingDedents_++;
                }
                if (pendingDedents_ > 0) {
                    pendingDedents_--;
                    return makeToken(TokenKind::Dedent);
                }
            }
        }
    }

    SourceLoc loc = { filename_, line_, col_ };
    if (cur() == '\0') {
        if (indentStack_.size() > 1) {
            indentStack_.pop_back();
            return makeToken(TokenKind::Dedent);
        }
        return makeToken(TokenKind::Eof);
    }

    if (cur() == '\n' || cur() == '\r') {
        atLineStart_ = true;
        advance();
        if (cur() == '\n') advance();
        if (nestingLevel_ > 0) return lex();
        return makeToken(TokenKind::Newline);
    }

    if (cur() == '"') return lexString();
    if (std::isdigit(static_cast<unsigned char>(cur()))) return lexNumber();
    if (std::isalpha(static_cast<unsigned char>(cur())) || cur() == '_') return lexIdentOrKeyword();

    char c = cur();
    advance();
    Token t;
    t.loc = loc;
    switch (c) {
        case '#':
            while (cur() != '\0' && cur() != '\n' && cur() != '\r') advance();
            return lex();
        case '(': nestingLevel_++; t.kind = TokenKind::LParen; break;
        case ')': nestingLevel_--; t.kind = TokenKind::RParen; break;
        case '{': nestingLevel_++; t.kind = TokenKind::LBrace; break;
        case '}': nestingLevel_--; t.kind = TokenKind::RBrace; break;
        case '[': nestingLevel_++; t.kind = TokenKind::LBracket; break;
        case ']': nestingLevel_--; t.kind = TokenKind::RBracket; break;
        case ';': t.kind = TokenKind::Semicolon; break;
        case ',': t.kind = TokenKind::Comma; break;
        case ':': t.kind = TokenKind::Colon; break;
        case '&': t.kind = TokenKind::Amp; break;
        case '|': t.kind = TokenKind::Pipe; break;
        case '.':
            if (cur() == '.') {
                advance();
                if (cur() == '.') {
                    advance();
                    t.kind = TokenKind::DotDotDot;
                } else {
                    t.kind = TokenKind::DotDot;
                }
            } else {
                t.kind = TokenKind::Dot;
            }
            break;
        case '+': t.kind = TokenKind::Plus; break;
        case '-':
            if (cur() == '>') { advance(); t.kind = TokenKind::Arrow; }
            else t.kind = TokenKind::Minus;
            break;
        case '*': t.kind = TokenKind::Star; break;
        case '/': t.kind = TokenKind::Slash; break;
        case '%': t.kind = TokenKind::Percent; break;
        case '=':
            if (cur() == '=') { advance(); t.kind = TokenKind::Eq; }
            else t.kind = TokenKind::Assign;
            break;
        case '!':
            if (cur() == '=') { advance(); t.kind = TokenKind::Ne; }
            else { t.kind = TokenKind::Invalid; t.text = "!"; }
            break;
        case '<':
            if (cur() == '=') { advance(); t.kind = TokenKind::Le; }
            else if (cur() == '-') { advance(); t.kind = TokenKind::ArrowLeft; }
            else t.kind = TokenKind::Lt;
            break;
        case '>':
            if (cur() == '=') { advance(); t.kind = TokenKind::Ge; }
            else t.kind = TokenKind::Gt;
            break;
        default:
            t.kind = TokenKind::Invalid;
            t.text = std::string(1, c);
            break;
    }
    return t;
}

Token Lexer::next() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }
    return lex();
}

Token Lexer::peek() {
    if (!hasPeeked_) {
        peeked_ = lex();
        hasPeeked_ = true;
    }
    return peeked_;
}

std::string Lexer::lineSnippet(int line) const {
    size_t start = 0;
    int l = 1;
    for (size_t i = 0; i < source_.size(); i++) {
        if (l == line) {
            start = i;
            while (i < source_.size() && source_[i] != '\n') i++;
            return source_.substr(start, i - start);
        }
        if (source_[i] == '\n') l++;
    }
    return "";
}

bool Lexer::peekForGenericEnd() {
    int depth = 0;
    for (size_t i = pos_; i < source_.size(); i++) {
        if (source_[i] == '<') depth++;
        else if (source_[i] == '>') {
            if (depth == 0) {
                size_t j = i + 1;
                while (j < source_.size() && std::isspace(static_cast<unsigned char>(source_[j]))) j++;
                return (j < source_.size() && source_[j] == '(');
            }
            depth--;
        }
        else if (source_[i] == ';' || source_[i] == '{' || source_[i] == '}' || source_[i] == '\n') break;
    }
    return false;
}

} // namespace gspp
