#ifndef EXPRESSION_H_
#define EXPRESSION_H_

#include <stdio.h>

#include <vector>
#include <string>

namespace netplay {
  
enum expression_type {
  AND,
  OR,
  NOT,
  PREDICATE
};

struct expression {
  expression(expression_type typ) : type(typ) {}

  expression_type type;
};

struct predicate : public expression {
  predicate() : expression(expression_type::PREDICATE) {}

  std::string attr;
  std::string op;
  std::string value;
};

struct conjunction : public expression {
  conjunction() : expression(expression_type::AND) {}

  std::vector<expression*> children;
};

struct disjunction : public expression {
  disjunction() : expression(expression_type::OR) {}
  std::vector<expression*> children;
};

struct negation : public expression {
  negation() : expression(expression_type::NOT) {
    child = NULL;
  }

  expression* child;
};

void print_expression(expression* exp) {
  switch (exp->type) {
    case expression_type::PREDICATE: {
      predicate *p = (predicate*) exp;
      fprintf(stderr, "[%s %s %s]", p->attr.c_str(), p->op.c_str(),
              p->value.c_str());
      break;
    }
    case expression_type::NOT: {
      negation *n = (negation*) exp;
      fprintf(stderr, "NOT(");
      print_expression(n->child);
      fprintf(stderr, ")");
      break;
    }
    case expression_type::AND: {
      conjunction *c = (conjunction*) exp;
      fprintf(stderr, "AND(");
      for (size_t i = 0; i < c->children.size(); i++) {
        expression* child = c->children[i];
        print_expression(child);
        if (i != c->children.size() - 1)
          fprintf(stderr, ", ");
      }
      fprintf(stderr, ")");
      break;
    }
    case expression_type::OR: {
      disjunction *d = (disjunction*) exp;
      fprintf(stderr, "OR(");
      for (size_t i = 0; i < d->children.size(); i++) {
        expression* child = d->children[i];
        print_expression(child);
        if (i != d->children.size() - 1)
          fprintf(stderr, ", ");
      }
      fprintf(stderr, ")");
      break;
    }
  }
}

}

#endif  // EXPRESSION_H_