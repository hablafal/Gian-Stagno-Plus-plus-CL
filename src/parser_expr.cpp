#include "parser.h"

namespace gspp {

std::unique_ptr<Expr> Parser::parsePrimary() {
    SourceLoc l = loc();
    if (check(TokenKind::IntLit)) {
        int64_t v = current_.intVal;
        advance();
        return Expr::makeIntLit(v, l);
    }
    if (check(TokenKind::FloatLit)) {
        double v = current_.floatVal;
        advance();
        return Expr::makeFloatLit(v, l);
    }
    if (match(TokenKind::True)) return Expr::makeBoolLit(true, l);
    if (match(TokenKind::False)) return Expr::makeBoolLit(false, l);
    if (check(TokenKind::StringLit)) {
        auto e = Expr::makeStringLit(current_.text, l);
        advance();
        return e;
    }
    if (match(TokenKind::Nil)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::IntLit;
        e->intVal = 0;
        e->exprType.kind = Type::Kind::Void;
        e->loc = l;
        return e;
    }
    if (match(TokenKind::New)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::New;
        e->loc = l;
        e->targetType = parseType();
        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do { e->args.push_back(parseExpr()); } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after new arguments");
        }
        return e;
    }
    if (match(TokenKind::Cast)) {
        expect(TokenKind::Lt, "expected '<' after cast");
        auto ty = parseType();
        expect(TokenKind::Gt, "expected '>' after cast type");
        expect(TokenKind::LParen, "expected '(' after cast");
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Cast; e->loc = l; e->targetType = std::move(ty);
        e->left = parseExpr();
        expect(TokenKind::RParen, "expected ')'");
        return e;
    }
    if (match(TokenKind::Sizeof)) {
        expect(TokenKind::LParen, "expected '(' after sizeof");
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Sizeof; e->loc = l;
        e->targetType = parseType();
        expect(TokenKind::RParen, "expected ')'");
        return e;
    }
    if (match(TokenKind::Spawn)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Spawn;
        e->loc = l;
        e->left = parseExpr();
        return e;
    }
    if (match(TokenKind::Super)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Super;
        e->loc = l;
        return e;
    }
    if (match(TokenKind::Chan)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::ChanInit;
        e->loc = l;
        if (match(TokenKind::LBracket)) {
            e->targetType = parseType();
            expect(TokenKind::RBracket, "expected ']' after chan type");
        }
        if (match(TokenKind::LParen)) {
            e->args.push_back(parseExpr());
            expect(TokenKind::RParen, "expected ')' after chan capacity");
        }
        return e;
    }
    if (check(TokenKind::Ident)) {
        std::string id = current_.text;
        advance();
        return Expr::makeVar(id, l);
    }
    if (match(TokenKind::LParen)) {
        auto first = parseExpr();
        if (check(TokenKind::Comma)) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::TupleLit;
            e->loc = l;
            e->args.push_back(std::move(first));
            while (match(TokenKind::Comma)) {
                if (check(TokenKind::RParen)) break;
                e->args.push_back(parseExpr());
            }
            expect(TokenKind::RParen, "expected ')' after tuple elements");
            return e;
        } else {
            expect(TokenKind::RParen, "expected ')'");
            return first;
        }
    }
    if (match(TokenKind::LBracket)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::ListLit;
        e->loc = l;
        if (!check(TokenKind::RBracket)) {
            do {
                e->args.push_back(parseExpr());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBracket, "expected ']' after list literal");
        return e;
    }
    if (match(TokenKind::LBrace)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::DictLit;
        e->loc = l;
        if (!check(TokenKind::RBrace)) {
            do {
                e->args.push_back(parseExpr());
                expect(TokenKind::Colon, "expected ':' after key");
                e->args.push_back(parseExpr());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBrace, "expected '}' after dict literal");
        return e;
    }

    error("expected expression");
    advance();
    return Expr::makeIntLit(0, l);
}

std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    while (true) {
        SourceLoc l = loc();
        if (match(TokenKind::Dot)) {
            if (!check(TokenKind::Ident)) { error("expected member name"); break; }
            std::string mem = current_.text;
            advance();
            auto m = std::make_unique<Expr>();
            m->kind = Expr::Kind::Member;
            m->left = std::move(base);
            m->member = mem;
            m->loc = l;
            base = std::move(m);
        } else if (match(TokenKind::LBracket)) {
            auto first = parseExpr();
            if (match(TokenKind::Colon)) {
                auto second = parseExpr();
                expect(TokenKind::RBracket, "expected ']' after slice");
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Slice;
                e->left = std::move(base);
                e->args.push_back(std::move(first));
                e->args.push_back(std::move(second));
                e->loc = l;
                base = std::move(e);
            } else {
                expect(TokenKind::RBracket, "expected ']' after index");
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Index;
                e->left = std::move(base);
                e->right = std::move(first);
                e->loc = l;
                base = std::move(e);
            }
        } else if ((check(TokenKind::Lt) && lexer_.peekForGenericEnd()) || check(TokenKind::LParen)) {
            std::vector<Type> typeArgs;
            if (match(TokenKind::Lt)) {
                do {
                    typeArgs.push_back(*parseType());
                } while (match(TokenKind::Comma));
                expect(TokenKind::Gt, "expected '>' after type arguments");
            }
            if (!match(TokenKind::LParen)) {
                error("expected '(' after type arguments");
                break;
            }
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseExpr());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after arguments");

            if (base->kind == Expr::Kind::Member) {
                std::string func = base->member;
                auto c = Expr::makeCall(func, std::move(args), l);
                c->left = std::move(base->left);
                c->exprType.typeArgs = typeArgs;
                base = std::move(c);
            } else if (base->kind == Expr::Kind::Var) {
                std::string func = base->ident;
                auto c = Expr::makeCall(func, std::move(args), l);
                c->exprType.typeArgs = typeArgs;
                base = std::move(c);
            } else if (base->kind == Expr::Kind::Member) {
                 // method call on non-var base
                std::string func = base->member;
                auto c = Expr::makeCall(func, std::move(args), l);
                c->left = std::move(base->left);
                c->exprType.typeArgs = typeArgs;
                base = std::move(c);
            } else {
                auto c = std::make_unique<Expr>();
                c->kind = Expr::Kind::Call;
                c->left = std::move(base);
                c->exprType.typeArgs = typeArgs;
                c->loc = l;
                base = std::move(c);
            }
        } else {
            break;
        }
    }
    return base;
}

int Parser::binPrec(const std::string& op) {
    if (op == "or" || op == "|") return 1;
    if (op == "and" || op == "&") return 2;
    if (op == "==" || op == "!=") return 3;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    SourceLoc l = loc();
    if (match(TokenKind::ArrowLeft)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Receive;
        e->right = parseUnary();
        e->loc = l;
        return e;
    }
    if (match(TokenKind::Minus)) return Expr::makeUnary("-", parseUnary(), l);
    if (match(TokenKind::Not)) return Expr::makeUnary("not", parseUnary(), l);
    if (match(TokenKind::Star)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Deref; e->right = parseUnary(); e->loc = l;
        return e;
    }
    if (match(TokenKind::Amp)) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::AddressOf; e->right = parseUnary(); e->loc = l;
        return e;
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<Expr> Parser::parseBinary(int minPrec) {
    auto left = parseUnary();
    for (;;) {
        std::string op;
        if (check(TokenKind::Plus)) op = "+";
        else if (check(TokenKind::Minus)) op = "-";
        else if (check(TokenKind::Star)) op = "*";
        else if (check(TokenKind::Slash)) op = "/";
        else if (check(TokenKind::Percent)) op = "%";
        else if (check(TokenKind::Eq)) op = "==";
        else if (check(TokenKind::Ne)) op = "!=";
        else if (check(TokenKind::Lt)) op = "<";
        else if (check(TokenKind::Gt)) op = ">";
        else if (check(TokenKind::Le)) op = "<=";
        else if (check(TokenKind::Ge)) op = ">=";
        else if (check(TokenKind::And)) op = "and";
        else if (check(TokenKind::Or)) op = "or";
        else if (check(TokenKind::Amp)) op = "&";
        else if (check(TokenKind::Pipe)) op = "|";
        else break;

        int prec = binPrec(op);
        if (prec < minPrec) break;
        advance();
        auto right = parseBinary(prec + 1);
        left = Expr::makeBinary(std::move(left), op, std::move(right), left->loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    return parseBinary(0);
}

} // namespace gspp
