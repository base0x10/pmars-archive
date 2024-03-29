/* pMARS -- a portable Memory Array Redcode Simulator
 * Copyright (C) 1993-1996 Albert Ma, Na'ndor Sieben, Stefan Strack and Mintardjo Wangsawidjaja
 * Copyright (C) 2000 Ilmari Karonen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * eval.c: expression evaluator used by assembler and debugger
 * $Id: eval.c,v 1.3 2000/12/25 00:49:08 iltzu Exp $
 */

#include <ctype.h>

#include "global.h"

/*
 * eval_expr - arithmetic expression evaluator
 * by Stefan Strack, version 3 (pMARS v0.4.0+)
 *
 * usage:
 *
 * int eval_expr(char *expr, long *result); returns 0 if expression is ok, -1 if
 * bad, -2 if attempted division by zero expr must be null-terminated and can
 * contain whitespace characters (\t \n)
 *
 * -recursion-based -supports arbitrary levels of precedence and parentheses
 * -supports nested unary minus and plus: 3--2 = 3-(-2) = 5
 * -arithmetic operators: +,-,*,/,%,=
 * -26 registers named "a" thru "z", assignment operator =
 * -C-like comparison and logic operators: ==,!=,<,>,<=,>=,&&,||
 *
 */

#define STANDALONE 0
#define BIGNUM 20                /* largest number taken */

/* two-char operators */
enum {
  EQUAL, NEQU, GTE, LTE, AND, OR, IDENT
};

#ifdef DEBUG
#define X(c) (c==IDENT ? '#' :\
              (c==EQUAL ? '=' :\
               (c==AND ? '&' :\
                (c==OR ? '|' :\
                 (c)))))
#endif

#define TERMINAL(c) (((c)==')' || !(c)) ? 1 : 0)
/* order of precedence (high to low):
   * / %
   + -
   > < == != >= <=
   &&
   ||
   =
 */
#define PRECEDENCE(op) (op=='*' || op=='/' || op=='%' ? 5 :\
                        (op=='+' || op=='-' ? 4 :\
                         (op=='>' || op=='<' || op==EQUAL || op==NEQU\
                                  || op==GTE || op==LTE ? 3 :\
                          (op==AND ? 2 : (op==OR ? 1 : 0)))))
#define SKIP_SPACE(e) while(isspace(*(e))) ++(e)

/* function prototypes */
#ifdef NEW_STYLE
char   *eval(int prevPrec, long val, char operator, char *expr, long *result);
char   *getreg(char *expr, int regId, long *val);
char   *getval(char *expr, long *val);
char   *getop(char *expr, char *op);
long    calc(long x, long y, int op);
#else
char   *eval();
char   *getreg();
char   *getval();
char   *getop();
long    calc();
#endif

/* global error flag */
int     evalerr;

/* registers */

static long regAr[26];

/* kludge to implement several precedence levels - saveOper works like "push back" in token parsers */

char    saveOper = 0;

/*--------------------*/
long
calc(x, y, op)
  long    x;
  long    y;
  int     op;
{
  long    z;

  switch (op) {
  case '+':
    if (evalerr == OK_EXPR && (x > 0 ?
                               y > 0 && x > LONG_MAX - y :
                               y < 0 && x < LONG_MIN - y ))
      evalerr = OVERFLOW;
    z = x + y;
    break;
  case '-':
    if (evalerr == OK_EXPR && (x > 0 ?
                               y < 0 && x > LONG_MAX + y :
                               y > 0 && x < LONG_MIN + y ))
      evalerr = OVERFLOW;
    z = x - y;
    break;
  case '/':
    if (y == 0) {
      evalerr = DIV_ZERO;
      z = 0;
    } else
      z = x / y;
    break;
  case '*':
    if (evalerr == OK_EXPR && x != 0 && y != 0 &&
        x != -1 && y != -1 &&    /* LONG_MIN/(-1) causes FP error! */
	x != 1 && y != 1 &&	/* LONG_MIN/(+1) causes FP error on an Alpha! */
        ((x > 0) == (y > 0) ?   /* (even though it shouldn't.) */
         LONG_MAX / y / x == 0 :
         LONG_MIN / y / x == 0 ))
      evalerr = OVERFLOW;
    z = x * y;
    break;
  case '%':
    if (y == 0) {
      evalerr = DIV_ZERO;
      z = 0;
    } else
      z = x % y;
    break;
  case AND:
    z = x && y;
    break;
  case OR:
    z = x || y;
    break;
  case EQUAL:
    z = x == y;
    break;
  case NEQU:
    z = x != y;
    break;
  case '<':
    z = x < y;
    break;
  case '>':
    z = x > y;
    break;
  case LTE:
    z = x <= y;
    break;
  case GTE:
    z = x >= y;
    break;
  case IDENT:                        /* the right-identity operator, used for
                                 * initial call to eval() */
    z = y;
    break;
  default:
    z = 0;
    evalerr = BAD_EXPR;
  }
  return z;
}

/*--------------------*/
char   *
getop(expr, oper)
  char   *expr;
  char   *oper;
{
  char    ch;
  switch (ch = *(expr++)) {
  case '&':
    if (*(expr++) == '&')
      *oper = AND;
    break;
  case '|':
    if (*(expr++) == '|')
      *oper = OR;
    break;
  case '=':
    if (*(expr++) == '=')
      *oper = EQUAL;
    break;
  case '!':
    if (*(expr++) == '=')
      *oper = NEQU;
    break;
  case '<':
    if (*expr == '=') {
      ++expr;
      *oper = LTE;
    } else
      *oper = '<';
    break;
  case '>':
    if (*expr == '=') {
      ++expr;
      *oper = GTE;
    } else
      *oper = '>';
    break;
  default:
    *oper = ch;
    break;
  }
  return expr;
}
/*--------------------*/
char   *
getreg(expr, regId, val)
  char   *expr;
  int     regId;
  long   *val;
{
  SKIP_SPACE(expr);
  if (*expr == '=' && *(expr + 1) != '=') {        /* assignment, not equality */
    expr = eval(-1, 0L, IDENT, expr + 1, val);
    regAr[regId] = *val;
  } else
    *val = regAr[regId];
  return expr;
}

/*--------------------*/
char   *
getval(expr, val)
  char   *expr;
  long   *val;
{
  char    buffer[BIGNUM];
  int     regId;
  int     bufptr = 0;
  long    accum;

  SKIP_SPACE(expr);
  if (*expr == '(') {                /* parenthetical expression */
    expr = eval(-1, 0L, IDENT, expr + 1, val);
    if (*expr != ')')
      evalerr = BAD_EXPR;
    return expr + 1;
  }
  if (*expr == '-') {                /* unary minus */
    expr = getval(expr + 1, &accum);
    *val = accum * -1;
    return expr;
  } else if (*expr == '!') {        /* logical NOT */
    expr = getval(expr + 1, &accum);
    *val = (accum ? 0 : 1);
    return expr;
  } else if (*expr == '+')        /* unary plus */
    return getval(expr + 1, val);
  else if (((regId = (int) toupper(*expr)) >= 'A') && (regId <= 'Z'))
    return getreg(expr + 1, regId - 'A', val);
  else
    while (isdigit(*expr) && bufptr < BIGNUM) /* FIXED (?): possible buffer overflow without the bufptr < BIGNUM? */
      buffer[bufptr++] = *expr++;
  if (bufptr == 0)
    evalerr = BAD_EXPR;                /* no digits */
  buffer[bufptr] = 0;
  sscanf(buffer, "%ld", val); /* FIXME wouldn't it be simpler and more efficient to calculate val directly, ie val = val*10+digitvalueof(*expr++) in the loop? */
  return expr;
}

/*--------------------*/
#ifdef NEW_STYLE
char   *
eval(int prevPrec, long val1, char oper1, char *expr, long *result)
#else
char   *
eval(prevPrec, val1, oper1, expr, result)
  int     prevPrec;/* prevPrec is the precedence that ends the subevaluation, ie in 1 && 2+3*4-1 || 0 prevPrec would be prec of && while evaluating 2+3*4-1 */
  char    oper1, *expr;
  long    val1, *result;
#endif
{
  long    result2, val2;
  char    oper2;
  int     prec1, prec2;

#ifdef DEBUG
      fprintf(stderr, "eval prev = %ld, val1 = %ld, op1 = %c \n", prevPrec, val1, X(oper1));
#endif

  saveOper = 0;

  expr = getval(expr, &val2);
  SKIP_SPACE(expr);
  if (TERMINAL(*expr)) {        /* trivial: expr is number or () */
    *result = calc(val1, val2, oper1);
    return expr;
  }
  expr = getop(expr, &oper2);

  if ((prec1 = PRECEDENCE(oper1)) >= (prec2 = PRECEDENCE(oper2))) {
    if (prec2 > prevPrec /* don't understand this, so I removed it: || prec1 <= prevPrec */) {
      /* continue to gobble up expressions */

#ifdef DEBUG
      fprintf(stderr, "gobble prev = %ld, val1 = %ld, op1 = %c, val2 = %ld, op2 = %c \n", prevPrec, val1, X(oper1), val2, X(oper2));
#endif

      expr = eval(prevPrec, calc(val1, val2, oper1), oper2, expr, result);/* prec1 -> prevPrec */

#ifdef DEBUG
      fprintf(stderr, "gobbled (prev %ld) %ld = ((%ld %c %ld) %c ..)\n", prevPrec, *result, val1, X(oper1), val2, X(oper2));
#endif

    } else {
      /* return from subevaluation of terms with higher precedence */
      *result = calc(val1, val2, oper1);
      saveOper = oper2;/* akin to "push back" in parsers */

#ifdef DEBUG
      fprintf(stderr, "return (prev %ld, save = %c) %ld = (%ld %c %ld)\n", prevPrec, X(saveOper), *result, val1, X(oper1), val2);
#endif

    }
  } else {

#ifdef DEBUG
      fprintf(stderr, "subevaluate op2, prev = %ld, val1 = %ld, op1 = %c, val2 = %ld, op2 = %c, saveOp = %c (%ld) \n", prevPrec, val1, X(oper1), val2, X(oper2), X(saveOper), saveOper);
#endif

    expr = eval(prec1, val2, oper2, expr, &result2);/* start subevaluation starting from val2 op2 */
    *result = calc(val1, result2, oper1);

#ifdef DEBUG
      fprintf(stderr, "returned from subeval, result2 = %ld, result = %ld, prev = %ld, val1 = %ld, op1 = %c, val2 = %ld, op2 = %c, saveOp = %c (%ld) \n", result2, *result, prevPrec, val1, X(oper1), val2, X(oper2), X(saveOper), saveOper);
#endif

      if (saveOper && PRECEDENCE(saveOper) >= prevPrec) {/* otherwise (if saveOper, but precedence < prevPrec) yet more nested subevalutions to return from */

#ifdef DEBUG
      fprintf(stderr, "evaluate saveOper, prev = %ld, val1 = %ld, op1 = %c \n", prevPrec, *result , X(saveOper));
#endif

      expr = eval(prevPrec, *result, saveOper, expr, result);/* prec2 -> prevPrec */

#ifdef DEBUG
      fprintf(stderr, "return, no more subevals (prev %ld, old Saved: %c (%ld)) %ld = .. ? (%ld %c (%ld %c ..))\n", prevPrec, X(saveOper), saveOper, *result,
              val1, X(oper1), val2, X(oper2));
#endif

      /* saveOper = 0; */ /* FIXED don't erase history, continue if saveOper exists */
    }

#ifdef DEBUG
    else
      fprintf(stderr, "return, no saveOp or more subevals (prev %ld, saveOp = %c (%ld)) %ld = (%ld %c (%ld %c ..))\n", prevPrec, X(saveOper), saveOper, *result, val1, X(oper1), val2, X(oper2));
#endif

  }

  return expr;
}

/*--------------------*/
void
reset_regs()
{
  register int idx;

  for (idx = 0; idx < 26; ++idx)
    regAr[idx] = 0L;
}

/*--------------------*/

void
set_reg(regChr, val)
  char    regChr;
  long    val;
{
  regAr[regChr - 'A'] = val;
}

/*--------------------*/
int
eval_expr(expr, result)                /* wrapper for eval() */
  char   *expr;
  long   *result;
{
#ifdef DEBUG
      fprintf(stderr, "* eval *\n");
#endif
  evalerr = OK_EXPR;
  if (*eval(-1, 0L, IDENT, expr, result) != 0)
    evalerr = BAD_EXPR;                /* still chars left */
  return (evalerr);
}

#if STANDALONE
/*--------------------*/
void
main()
{                                /* test evaluator */
  char    expr[80];
  long    result;
  int     err;

  printf("Enter infix expression: ");
  gets(expr);

  if ((err = eval_expr(expr, &result)) >= OK_EXPR)
    printf("Evaluation result:      %ld\n", result);
  else if (err == DIV_ZERO)
    printf("Division by zero in %s\n", expr);
  else if (err == BAD_EXPR)
    printf("Bad expression %s\n", expr);
}
#endif
