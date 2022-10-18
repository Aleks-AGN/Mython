#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <cassert>

using namespace std;

namespace parse {

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
        return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}

Lexer::Lexer(std::istream& input)
    : input_(input) {
    LoadNextToken();
}

const Token& Lexer::CurrentToken() const {
    return current_token_;
}

Token Lexer::NextToken() {
    LoadNextToken();
    return current_token_;
}

void Lexer::PassString() {
    std::string s;
    std::getline(input_, s);
    begin_ = true;
    indents_ = 0;
}

void Lexer::PassComment() {
    char c;
    while (input_.get(c)) {
        if (c == '\n') {
            input_.putback(c);
            break;
        }
    }
}

void Lexer::CountSpaces() {
    size_t spaces_amount = 0;
    char c;
    while (input_.get(c)) {
        if (c == ' ') {
            ++spaces_amount;
        } else {
            input_.putback(c);
            break;
        }
    }
    if (begin_) {
        indents_ = spaces_amount / 2;
    }
}

int Lexer::ParseNumber(std::istream &input) {
    std::string number;
    char c;
    while(input.get(c)) {
        if (std::isdigit(c)) {
            number += c;
        } else {
            input.putback(c);
            break;
        }
    }
    return std::stoi(number);
}

std::string Lexer::ParseName(std::istream &input) {
    std::string line;
    char c;
    while (input.get(c)) {
        if (std::isalnum(c) || c == '_') {
            line += c;
        } else {
            input.putback(c);
            break;
        }
    }
    return line;
}

std::string Lexer::ParseString(std::istream& input) {
    std::string line;
    char start = input.get();
    char c;
    while (input.get(c)) {
        if (c == '\\') {
            char next;
            input.get(next);
            if (next == '\"') {
                line += '\"';
            } else if (next == '\'') {
                line += '\'';
            } else if (next == 'n') {
                line += '\n';
            } else if (next == 't') {
                line += '\t';
            }
        } else {
            if (c == start) {
                break;
            }
            line += c;
        }
    }
    assert(c == start);
    return line;
}

void Lexer::LoadNextToken() {
    char ch = input_.peek();

    if (ch == std::ios::traits_type::eof()) {
        if (!begin_) {
            PassString();
            current_token_ = token_type::Newline{};
        } else {
            if (indent_pos_ > 0) {
                --indent_pos_;
                current_token_ = token_type::Dedent{};
            } else {
                current_token_ = token_type::Eof{};
            }
        }
    } else if (ch == '\n') {
        if (begin_) {
            PassString();
            LoadNextToken();
        } else {
            PassString();
            current_token_ = token_type::Newline{};
        }
    } else if (ch == '#') {
        PassComment();
        LoadNextToken();
    } else if (ch == ' ') {
        CountSpaces();
        LoadNextToken();
    } else if (indent_pos_ != indents_ && begin_) {
        if (indent_pos_ < indents_) {
            ++indent_pos_;
            current_token_ = token_type::Indent{};
        } else {
            --indent_pos_;
            current_token_ = token_type::Dedent{};
        }
    } else {
        if (std::isdigit(ch)) {
            int number = ParseNumber(input_);
            current_token_ = token_type::Number{number};
        } else if (std::isalpha(ch) || ch == '_') {
            std::string name = ParseName(input_);
            if (key_words.count(name) != 0) {
                current_token_ = key_words.at(name);
            } else {
                current_token_ = token_type::Id{name};
            }
        } else if (ch == '\"' || ch == '\'') {
            std::string str = ParseString(input_);
            current_token_ = token_type::String{str};
        } else {
            std::string dual;
            dual += input_.get();
            dual += input_.peek();
            if (dual_symbols.count(dual)) {
                input_.get();
                current_token_ = dual_symbols.at(dual);
            } else {
                current_token_ = token_type::Char{dual[0]};
            }
        }
        begin_ = false;
    }
}

}  // namespace parse
