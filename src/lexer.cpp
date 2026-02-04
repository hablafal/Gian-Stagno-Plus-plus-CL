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
        while (cur() == ' ' || cur() == '\t' || cur() == '\r' || cur() == '\n') advance();
        if (cur() == '/' && peekChar() == '/') {
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
    t.line = line_;
    t.column = col_;
    return t;
}

Token Lexer::lexNumber() {
    int startLine = line_, startCol = col_;
    std::string num;
    bool isFloat = false;
    while (std::isdigit(static_cast<unsigned char>(cur()))) { num += cur(); advance(); }
    if (cur() == '.' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
        isFloat = true;
        num += cur(); advance();
        while (std::isdigit(static_cast<unsigned char>(cur()))) { num += cur(); advance(); }
    }
    Token t;
    t.line = startLine;
    t.column = startCol;
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
    int startLine = line_, startCol = col_;
    advance(); // skip opening "
    std::string s;
    while (cur() != '"' && cur() != '\0') {
        if (cur() == '\\') {
            advance();
            if (cur() == 'n') { s += '\n'; advance(); }
            else if (cur() == 't') { s += '\t'; advance(); }
            else if (cur() == '"') { s += '"'; advance(); }
            else { s += cur(); advance(); }
        } else { s += cur(); advance(); }
    }
    if (cur() == '"') advance();
    Token t;
    t.kind = TokenKind::StringLit;
    t.text = s;
    t.line = startLine;
    t.column = startCol;
    return t;
}

Token Lexer::lexIdentOrKeyword() {
    int startLine = line_, startCol = col_;
    std::string id;
    if (std::isalpha(static_cast<unsigned char>(cur())) || cur() == '_') {
        id += cur(); advance();
        while (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_') { id += cur(); advance(); }
    }
    Token t;
    t.line = startLine;
    t.column = startCol;
    t.text = id;
    if (id == "var") t.kind = TokenKind::Var;
    else if (id == "let") t.kind = TokenKind::Let;
    else if (id == "func" || id == "def") t.kind = TokenKind::Func;
    else if (id == "class") t.kind = TokenKind::Class;
    else if (id == "struct") t.kind = TokenKind::Struct;
    else if (id == "if") t.kind = TokenKind::If;
    else if (id == "else") t.kind = TokenKind::Else;
    else if (id == "while") t.kind = TokenKind::While;
    else if (id == "for") t.kind = TokenKind::For;
    else if (id == "in") t.kind = TokenKind::In;
    else if (id == "return") t.kind = TokenKind::Return;
    else if (id == "int") t.kind = TokenKind::Int;
    else if (id == "float") t.kind = TokenKind::Float;
    else if (id == "bool") t.kind = TokenKind::Bool;
    else if (id == "true") t.kind = TokenKind::True;
    else if (id == "false") t.kind = TokenKind::False;
    else if (id == "and") t.kind = TokenKind::And;
    else if (id == "or") t.kind = TokenKind::Or;
    else if (id == "not") t.kind = TokenKind::Not;
    else if (id == "import") t.kind = TokenKind::Import;
    else if (id == "asm") t.kind = TokenKind::Asm;
    else if (id == "unsafe") t.kind = TokenKind::Unsafe;
    else t.kind = TokenKind::Ident;
    return t;
}

Token Lexer::lex() {
    skipWhitespaceAndComments();
    int startLine = line_, startCol = col_;
    if (cur() == '\0') return makeToken(TokenKind::Eof);

    if (cur() == '"') return lexString();
    if (std::isdigit(static_cast<unsigned char>(cur()))) return lexNumber();
    if (std::isalpha(static_cast<unsigned char>(cur())) || cur() == '_') return lexIdentOrKeyword();

    char c = cur();
    advance();
    Token t;
    t.line = startLine;
    t.column = startCol;
    switch (c) {
        case '(': t.kind = TokenKind::LParen; break;
        case ')': t.kind = TokenKind::RParen; break;
        case '{': t.kind = TokenKind::LBrace; break;
        case '}': t.kind = TokenKind::RBrace; break;
        case '[': t.kind = TokenKind::LBracket; break;
        case ']': t.kind = TokenKind::RBracket; break;
        case ';': t.kind = TokenKind::Semicolon; break;
        case ',': t.kind = TokenKind::Comma; break;
        case ':': t.kind = TokenKind::Colon; break;
        case '.': t.kind = TokenKind::Dot; break;
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

} // namespace gspp
