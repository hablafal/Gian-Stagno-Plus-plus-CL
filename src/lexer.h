#ifndef GSPP_LEXER_H
#define GSPP_LEXER_H

#include <string>
#include <vector>
#include <cstdint>

namespace gspp {

enum class TokenKind {
    Eof,
    Invalid,
    IntLit,
    FloatLit,
    Ident,
    StringLit,
    // Keywords — Python-style + C++ power
    Var, Let, Func, Def, Class, Struct, Return,
    If, Else, While, For, In,
    Int, Float, Bool, True, False, And, Or, Not,
    Import, Asm, Unsafe,
    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semicolon, Comma, Colon, Arrow,
    Assign,
    Plus, Minus, Star, Slash, Percent,
    Eq, Ne, Lt, Gt, Le, Ge,
    Dot
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    int line = 1;
    int column = 1;
    int64_t intVal = 0;
    double floatVal = 0.0;
};

class Lexer {
public:
    explicit Lexer(const std::string& source, const std::string& filename = "");
    Token next();
    Token peek();
    const std::string& filename() const { return filename_; }
    std::string lineSnippet(int line) const;

private:
    char cur() const;
    char peekChar() const;
    void advance();
    bool match(char c);
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind k);
    Token lexNumber();
    Token lexString();
    Token lexIdentOrKeyword();
    Token lex();

    std::string source_;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    Token peeked_;
    bool hasPeeked_ = false;
};

} // namespace gspp

#endif
