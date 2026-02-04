#ifndef GSPP_LEXER_H
#define GSPP_LEXER_H

#include "common.h"
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
    Var, Let, Func, Def, Fn, Class, Struct, Data, Return,
    If, Else, Elif, Then, While, For, In, Repeat, Loop, As,
    Check, Case, Defer,
    Int, Float, Decimal, Bool, String, Text, Arr, Char, True, False, Yes, No, On, Off, And, Or, Not,
    Import, Use, Asm, Unsafe, New, Delete, Extern, Nil,
    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semicolon, Comma, Colon, Arrow,
    Assign, Amp,
    Plus, Minus, Star, Slash, Percent,
    Eq, Ne, Lt, Gt, Le, Ge,
    Dot, DotDot, DotDotDot,
    Indent, Dedent, Newline
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    SourceLoc loc;
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
    bool peekForGenericEnd();

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
    std::vector<int> indentStack_ = {0};
    int pendingDedents_ = 0;
    bool atLineStart_ = true;
    int nestingLevel_ = 0;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    Token peeked_;
    bool hasPeeked_ = false;
};

} // namespace gspp

#endif
