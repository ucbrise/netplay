#ifndef QUERY_PARSER_H_
#define QUERY_PARSER_H_

#include <cwctype>
#include <cctype>
#include <vector>
#include <exception>
#include <string>
#include <sstream>

#include "expression.h"

// Grammar:
// 
// <expression>::=<term>{'||'<term>}
// <term>::=<factor>{'&&'<factor>}
// <factor>::=<predicate>|'!'<factor>|(<expression>)
// <predicate>::=<attribute><op><value>

namespace netplay {

class parse_exception : public std::exception {
 public:
  parse_exception(const std::string msg)
      : msg_(msg) {
  }

  const char* what() const noexcept {
    return msg_.c_str();
  }

 private:
  const std::string msg_;
};

struct lex_token {
  lex_token(const int i, const std::string& val)
      : id(i),
        value(val) {
  }

  const int id;
  const std::string value;
};

class lexer {
 public:
  static const int INVALID = -2;
  static const int END = -1;
  // Boolean
  static const int OR = 0;
  static const int AND = 1;
  static const int NOT = 2;
  // Parentheses
  static const int LEFT = 3;
  static const int RIGHT = 4;
  // Operators
  static const int OPERATOR = 5;
  // Operands
  static const int OPERAND = 6;

  lexer() {
  }

  lexer(const std::string& exp) {
    str(exp);
  }

  void str(const std::string& exp) {
    stream_.str(exp);
  }

  size_t pos() {
    return stream_.tellg();
  }

  std::string str() {
    return stream_.str();
  }

  const lex_token next_token() {
    while (iswspace(stream_.peek()))
      stream_.get();

    char c = stream_.get();
    switch (c) {
      case EOF:
        return lex_token(END, "");
      case '|': {
        if (stream_.get() != '|')
          throw parse_exception(
              "Invalid token starting with |; did you mean ||?");
        return lex_token(OR, "||");
      }
      case '&': {
        if (stream_.get() != '&')
          throw parse_exception(
              "Invalid token starting with &; did you mean &&?");
        return lex_token(AND, "&&");
      }
      case '!': {
        char c1 = stream_.get();
        if (c1 == '=')
          return lex_token(OPERATOR, "!=");
        if (c1 == 'i') {
          if (stream_.get() == 'n' && iswspace(stream_.peek()))
            return lex_token(OPERATOR, "!in");
          stream_.unget();
        }
        stream_.unget();
        return lex_token(NOT, "!");
      }
      case '(':
        return lex_token(LEFT, "(");
      case ')':
        return lex_token(RIGHT, ")");
      case '=': {
        if (stream_.get() != '=')
          throw parse_exception(
              "Invalid token starting with =; did you mean ==?");
        return lex_token(OPERATOR, "==");
      }
      case '<': {
        if (stream_.get() == '=')
          return lex_token(OPERATOR, "<=");
        stream_.unget();
        return lex_token(OPERATOR, "<");
      }
      case '>': {
        if (stream_.get() == '=')
          return lex_token(OPERATOR, ">=");
        stream_.unget();
        return lex_token(OPERATOR, ">");
      }
      default: {
        if (!opvalid(c))
          throw parse_exception("All operands must conform to [a-zA-Z0-9_.]+");

        if (c == 'i') {
          if (stream_.get() == 'n' && iswspace(stream_.peek()))
            return lex_token(OPERATOR, "in");
          stream_.unget();
        }
        stream_.unget();

        std::string operand = "";
        while (opvalid(stream_.peek()))
          operand += (char) stream_.get();

        return lex_token(OPERAND, operand);
      }
    }
    return lex_token(INVALID, "");
  }

  const lex_token peek_token() {
    const lex_token tok = next_token();
    put_back(tok);
    return tok;
  }

  void put_back(const lex_token& tok) {
    for (size_t i = 0; i < tok.value.size(); i++) {
      stream_.unget();
    }
  }

 private:
  bool opvalid(int c) {
    return isalnum(c) || c == '.' || c == '_' || c == '/' || c == '-';
  }

  std::stringstream stream_;
};

class parser {
 public:
  parser(const std::string& exp) {
    lex_.str(exp);
  }

  expression* parse() {
    expression* e = exp();
    if (lex_.next_token().id != EOF)
      throw parse_exception("Parsing ended prematurely");
    return e;
  }

 private:
  expression* exp() {
    expression* t = term();
    if (lex_.peek_token().id != lexer::OR)
      return t;
    disjunction *d = new disjunction;
    d->children.push_back(t);
    while (lex_.peek_token().id == lexer::OR) {
      lex_.next_token();
      d->children.push_back(term());
    }
    return d;
  }

  expression* term() {
    expression* f = factor();
    if (lex_.peek_token().id != lexer::AND)
      return f;
    conjunction *c = new conjunction;
    c->children.push_back(f);
    while (lex_.peek_token().id == lexer::AND) {
      lex_.next_token();
      c->children.push_back(factor());
    }
    return c;
  }

  expression* factor() {
    lex_token tok = lex_.next_token();
    if (tok.id == lexer::NOT) {
      return negate(factor());
    } else if (tok.id == lexer::LEFT) {
      expression *e = exp();
      lex_token right = lex_.next_token();
      if (right.id != lexer::RIGHT)
        throw parse_exception("Could not find matching right parenthesis");
      return e;
    } else if (tok.id == lexer::OPERAND) {
      predicate *p = new predicate;
      p->attr = tok.value;
      lex_token op = lex_.next_token();
      if (op.id != lexer::OPERATOR)
        throw parse_exception(
            "First operand must be followed by operator in all predicates");
      p->op = op.value;
      lex_token operand = lex_.next_token();
      if (operand.id != lexer::OPERAND)
        throw parse_exception(
            "Operand must be followed by operator in all predicates");
      p->value = operand.value;
      return p;
    } else {
      throw parse_exception("Unexpected token " + tok.value);
    }
  }

  expression* negate(expression* exp) {
    switch (exp->type) {
      case expression_type::AND: {
        conjunction* c = (conjunction*) exp;
        disjunction* d = new disjunction;
        for (size_t i = 0; i < c->children.size(); i++) {
          d->children.push_back(negate(c->children[i]));
        }
        delete c;
        return d;
      }
      case expression_type::OR: {
        disjunction* d = (disjunction*) exp;
        conjunction* c = new conjunction;
        for (size_t i = 0; i < d->children.size(); i++) {
          c->children.push_back(negate(d->children[i]));
        }
        delete d;
        return c;
      }
      case expression_type::NOT: {
        negation* n = (negation*) exp;
        expression* e = n->child;
        delete n;
        return e;
      }
      case expression_type::PREDICATE: {
        predicate* p = (predicate *) exp;
        p->op = negate(p->op);
        return p;
      }
      default:
        return NULL;
    }
  }

  std::string negate(std::string& op) {
    if (op == "==") {
      return "!=";
    } else if (op == "!=") {
      return "==";
    } else if (op == "<") {
      return ">=";
    } else if (op == ">") {
      return "<=";
    } else if (op == "<=") {
      return ">";
    } else if (op == ">=") {
      return "<";
    } else if (op == "in") {
      return "!in";
    } else if (op == "!in") {
      return "in";
    } else {
      throw parse_exception("Invalid operator " + op);
    }
  }

  lexer lex_;
};

}

#endif	// QUERY_PARSER_H_
