#include "expression.h"
#include "lavender.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

char* lv_expr_getError(ExprError error) {
    #define LEN 12
    static char* msg[LEN] = {
        "Expr does not define a function",
        "Reached end of input while parsing",
        "Expected an argument list",
        "Malformed argument list",
        "Missing function body",
        "Duplicate function definition",
        "Function name not found",
        "Expected operator",
        "Expected operand",
        "Encountered unexpected token",
        "Unbalanced parens or brackets",
        "Wrong number of parameters to function"
    };
    assert(error > 0 && error <= LEN);
    return msg[error - 1];
    #undef LEN
}

//begin section for lv_expr_declareFunction

#define REQUIRE_MORE_TOKENS(x) \
    if(!(x)) { LV_EXPR_ERROR = XPE_UNTERM_EXPR; return RETVAL; } else (void)0
    
static bool specifiesFixing(Token* head);
static int getArity(Token* head);

Operator* lv_expr_declareFunction(Token* head, char* nspace, Token** bodyTok) {
    
    #define RETVAL NULL
    if(LV_EXPR_ERROR)
        return NULL;
    assert(head);
    char* fnameAlias;       //function name alias (not separately allocated!)
    int arity;              //function arity
    Fixing fixing;          //function fixing
    //how many levels of parens nested we are
    //we stop parsing when this becomes less than zero
    int nesting = 0;
    //skip opening paren if one is present
    if(head->value[0] == '(') {
        nesting++;
        head = head->next;
    }
    if(!head || strcmp(head->value, "def") != 0) {
        //this is not a function!
        LV_EXPR_ERROR = XPE_NOT_FUNCT;
        return NULL;
    }
    head = head->next;
    REQUIRE_MORE_TOKENS(head);
    //is this a named function? If so, get fixing as well
    switch(head->type) {
        case TTY_IDENT:
        case TTY_FUNC_SYMBOL:
        case TTY_SYMBOL:
            //save the name
            //check fixing
            if(specifiesFixing(head)) {
                fixing = head->value[0];
                fnameAlias = head->value + 2;
            } else {
                fixing = FIX_PRE;
                fnameAlias = head->value;
            }
            head = head->next;
            REQUIRE_MORE_TOKENS(head);
            break;
        default:
            fixing = FIX_PRE;
            fnameAlias = "";
    }
    if(head->value[0] != '(') {
        //we require a left paren before the arguments
        LV_EXPR_ERROR = XPE_EXPT_ARGS;
        return NULL;
    }
    head = head->next;
    REQUIRE_MORE_TOKENS(head);
    //collect args
    arity = getArity(head);
    if(LV_EXPR_ERROR)
        return NULL;
    //holds the arguments and their names
    Param args[arity];
    if(arity == 0) {
        //incr past close paren
        head = head->next;
    } else {
        //set up the args array
        for(int i = 0; i < arity; i++) {
            args[i].byName = (head->type == TTY_SYMBOL);
            if(args[i].byName) //incr past by name symbol
                head = head->next;
            assert(head->type == TTY_IDENT);
            args[i].name = head->value;
            assert(head->next->type == TTY_LITERAL);
            head = head->next->next; //skip comma or close paren
        }
    }
    REQUIRE_MORE_TOKENS(head);
    if(strcmp(head->value, "=>") != 0) {
        //sorry, a function body is required
        LV_EXPR_ERROR = XPE_MISSING_BODY;
        return NULL;
    }
    //the body starts with the token after the =>
    //it must exist, sorry
    head = head->next;
    REQUIRE_MORE_TOKENS(head);
    //build the function name
    FuncNamespace ns = fixing == FIX_PRE ? FNS_PREFIX : FNS_INFIX;
    int nsOffset = strlen(nspace) + 1;
    char* fqn = lv_alloc(strlen(nspace) + strlen(fnameAlias) + 2);
    strcpy(fqn + nsOffset, fnameAlias);
    //change ':' in symbolic name to '#'
    //because namespaces use ':' as a separator
    char* colon = fqn + nsOffset;
    while((colon = strchr(colon, ':')))
        *colon = '#';
    //add namespace
    strcpy(fqn, nspace);
    fqn[nsOffset - 1] = ':';
    //check to see if this function was defined twice
    Operator* funcObj = lv_op_getOperator(fqn, ns);
    if(funcObj) {
        //let's disallow entirely
        LV_EXPR_ERROR = XPE_DUP_DECL;
        lv_free(fqn);
        return NULL;
    } else {
        //add a new one
        funcObj = lv_alloc(sizeof(Operator));
        funcObj->name = fqn;
        funcObj->next = NULL;
        funcObj->type = FUN_FWD_DECL;
        funcObj->arity = arity;
        funcObj->fixing = fixing;
        funcObj->captureCount = 0; //todo
        funcObj->params = lv_alloc(arity * sizeof(Param));
        memcpy(funcObj->params, args, arity * sizeof(Param));
        //copy param names
        for(int i = 0; i < arity; i++) {
            char* name = lv_alloc(strlen(args[i].name) + 1);
            strcpy(name, args[i].name);
            funcObj->params[i].name = name;
        }
        lv_op_addOperator(funcObj, ns);
        *bodyTok = head;
        return funcObj;
    }
    #undef RETVAL
}

//helpers for lv_expr_declareFunction

static bool specifiesFixing(Token* head) {
    
    switch(head->type) {
        case TTY_FUNC_SYMBOL:
            return true;
        case TTY_IDENT: {
            char c = head->value[0];
            return (c == 'i' || c == 'r' || c == 'u')
                && head->value[1] == '_'
                && head->value[2] != '\0';
        }
        default:
            return false;
    }
}
    
//gets the arity of the function
//also validates the argument list
static int getArity(Token* head) {
    
    #define RETVAL 0
    assert(head);
    int res = 0;
    if(head->value[0] == ')')
        return res;
    while(true) {
        //by name symbol?
        if(strcmp(head->value, "=>") == 0) {
            //by name symbol
            head = head->next;
            REQUIRE_MORE_TOKENS(head);
        }
        //is it a param name
        if(head->type == TTY_IDENT) {
            res++;
            head = head->next;
            REQUIRE_MORE_TOKENS(head);
            //must be a comma or a close paren
            if(head->value[0] == ')')
                break; //we're done
            else if(head->value[0] != ',') {
                //must separate params with commas!
                LV_EXPR_ERROR = XPE_BAD_ARGS;
                return 0;
            } else {
                //it's a comma, increment
                head = head->next;
                REQUIRE_MORE_TOKENS(head);
            }
        } else {
            //malformed argument list
            LV_EXPR_ERROR = XPE_BAD_ARGS;
            return 0;
        }
    }
    return res;
    #undef RETVAL
}

#undef REQUIRE_MORE_TOKENS

//begin section for lv_expr_parseExpr

#define INIT_STACK_LEN 16
typedef struct TextStack {
    size_t len;
    TextBufferObj* top;
    TextBufferObj* stack;
} TextStack;

static void pushStack(TextStack* stack, TextBufferObj* obj);

typedef struct IntStack {
    size_t len;
    int* top;
    int* stack;
} IntStack;

static void pushParam(IntStack* stack, int n);

/** Expression context passed to functions. */
typedef struct ExprContext {
    Token* head;            //the current token
    Operator* decl;         //the current function declaration
    char* startOfName;      //the beginning of the simple name
    bool expectOperand;     //do we expect an operand or an operator
    int nesting;            //how nested in brackets we are
    TextStack ops;          //the temporary operator stack
    TextStack out;          //the output stack
    IntStack params;        //the parameter stack
} ExprContext;

static int compare(TextBufferObj* a, TextBufferObj* b);
static void parseLiteral(TextBufferObj* obj, ExprContext* cxt);
static void parseIdent(TextBufferObj* obj, ExprContext* cxt);
static void parseSymbol(TextBufferObj* obj, ExprContext* cxt);
static void parseQualName(TextBufferObj* obj, ExprContext* cxt);
static void parseNumber(TextBufferObj* obj, ExprContext* cxt);
static void parseString(TextBufferObj* obj, ExprContext* cxt);
static void parseFuncValue(TextBufferObj* obj, ExprContext* cxt);
static void parseTextObj(TextBufferObj* obj, ExprContext* cxt); //calls above functions
//runs one cycle of shunting yard
static void shuntingYard(TextBufferObj* obj, ExprContext* cxt);
static void handleRightBracket(ExprContext* cxt);
static bool isLiteral(TextBufferObj* obj, char c);
static bool shuntOps(ExprContext* cxts);

#define IF_ERROR_CLEANUP \
    if(LV_EXPR_ERROR) { \
        lv_expr_free(cxt.out.stack, cxt.out.top - cxt.out.stack + 1); \
        lv_expr_free(cxt.ops.stack, cxt.ops.top - cxt.ops.stack + 1); \
        lv_free(cxt.params.stack); \
        return NULL; \
    } else (void)0

Token* lv_expr_parseExpr(Token* head, Operator* decl, TextBufferObj** res, size_t* len) {
    
    if(LV_EXPR_ERROR)
        return head;
    //set up required environment
    //this is function local because
    //recursive calls are possible
    ExprContext cxt;
    cxt.head = head;
    cxt.decl = decl;
    cxt.startOfName = strrchr(decl->name, ':') + 1;
    cxt.expectOperand = true;
    cxt.nesting = 0;
    cxt.out.len = INIT_STACK_LEN;
    cxt.out.stack = lv_alloc(INIT_STACK_LEN * sizeof(TextBufferObj));
    cxt.out.top = cxt.out.stack;
    cxt.ops.len = INIT_STACK_LEN;
    cxt.ops.stack = lv_alloc(INIT_STACK_LEN * sizeof(TextBufferObj));
    cxt.ops.top = cxt.ops.stack;
    cxt.params.len = INIT_STACK_LEN;
    cxt.params.stack = lv_alloc(INIT_STACK_LEN * sizeof(int));
    cxt.params.top = cxt.params.stack;
    //loop over each token until we reach the end of the expression
    //(end-of-stream, closing grouper ')', or expression split ';')
    do {
        //get the next text object
        TextBufferObj obj;
        parseTextObj(&obj, &cxt);
        IF_ERROR_CLEANUP;
        //detect end of expr before we parse
        if(cxt.nesting < 0 || cxt.head->value[0] == ';')
            break;
        shuntingYard(&obj, &cxt);
        IF_ERROR_CLEANUP;
        cxt.head = cxt.head->next;
    } while(cxt.head);
    //get leftover ops over
    while(cxt.ops.top != cxt.ops.stack) {
        shuntOps(&cxt);
        IF_ERROR_CLEANUP;
    }
    *res = cxt.out.stack;
    *len = cxt.out.top - cxt.out.stack + 1;
    //calling plain lv_free is ok because ops is empty
    lv_free(cxt.ops.stack);
    lv_free(cxt.params.stack);
    return cxt.head;
}

#undef IF_ERROR_CLEANUP

void lv_expr_free(TextBufferObj* obj, size_t len) {
    
    for(size_t i = 1; i < len; i++) {
        if(obj[i].type == OPT_STRING)
            lv_free(obj[i].str);
    }
    lv_free(obj);
}

//static helpers for lv_expr_parseExpr
    
static int getLexicographicPrecedence(char c) {
    
    switch(c) {
        case '|': return 1;
        case '^': return 2;
        case '&': return 3;
        case '!':
        case '=': return 4;
        case '>':
        case '<': return 5;
        case '#': return 6; //':' was changed to '#' earlier
        case '-':
        case '+': return 7;
        case '%':
        case '/':
        case '*': return 8;
        case '~':
        case '?': return 9;
        default:  return 0;
    }
}

/** Whether this object represents the literal character c. */
static bool isLiteral(TextBufferObj* obj, char c) {
    
    return obj->type == OPT_LITERAL && obj->literal == c;
}

/** Compares a and b by precedence. */
static int compare(TextBufferObj* a, TextBufferObj* b) {
    
    //values have highest precedence
    {
        int ar = (a->type != OPT_LITERAL &&
            (a->type != OPT_FUNCTION || a->func->arity == 0));
        int br = (b->type != OPT_LITERAL &&
            (b->type != OPT_FUNCTION || b->func->arity == 0));
        if(ar || br) {
            assert(false);
            return ar - br;
        }
    }
    //close groupers ']' and ')' have next highest
    {
        int ac = (isLiteral(a, ')') || isLiteral(a, ']'));
        int bc = (isLiteral(b, ')') || isLiteral(b, ']'));
        if(ac || bc)
            return ac - bc;
    }
    //openers '(' and '[' have the lowest
    if(isLiteral(a, '(') || isLiteral(a, '['))
        return -1;
    if(isLiteral(b, '(') || isLiteral(b, '['))
        return 1;
    assert(a->type == OPT_FUNCTION);
    assert(b->type == OPT_FUNCTION);
    //check fixing
    //prefix > infix = postfix
    {
        int afix = (a->func->fixing == FIX_PRE || a->func->arity == 1);
        int bfix = (b->func->fixing == FIX_PRE || b->func->arity == 1);
        if(afix ^ bfix)
            return afix - bfix;
        if(afix) //prefix functions always have equal precedence
            return 0;
    }
    //compare infix operators with modified Scala ordering
    //note that '**' has greater precedence than other combinations
    //get the beginning of the simple names
    {
        char* ac = strrchr(a->func->name, ':') + 1;
        char* bc = strrchr(b->func->name, ':') + 1;
        int ap = getLexicographicPrecedence(*ac);
        int bp = getLexicographicPrecedence(*bc);
        if(ap ^ bp)
            return ap - bp;
        //check special '**' combination
        ap = (strncmp(ac, "**", 2) == 0);
        bp = (strncmp(bc, "**", 2) == 0);
        return ap - bp;
    }
}

/** Returns a new string with concatenation of a and b */
static char* concat(char* a, int alen, char* b, int blen) {
    
    char* res = lv_alloc(alen + blen + 1);
    memcpy(res, a, alen);
    memcpy(res + alen, b, blen);
    res[alen + blen] = '\0';
    return res;
}

static void parseLiteral(TextBufferObj* obj, ExprContext* cxt) {
    
    obj->type = OPT_LITERAL;
    obj->literal = cxt->head->value[0];
    switch(obj->literal) {
        case '(':
        case '[':
            cxt->nesting++;
            //open groupings are "operands"
            if(!cxt->expectOperand) {
                LV_EXPR_ERROR = XPE_EXPECT_PRE;
            }
            break;
        case ')':
        case ']':
            cxt->nesting--;
            //fallthrough
        case ',':
            //close groupings are "operators"
            if(cxt->expectOperand) {
                LV_EXPR_ERROR = XPE_EXPECT_INF;
            }
            break;
        case ';':
            //Separator for the conditional
            //portion of a function can only
            //occur when nesting == 0
            if(cxt->nesting != 0) {
                LV_EXPR_ERROR = XPE_UNEXPECT_TOKEN;
            }
            break;
        default:
            LV_EXPR_ERROR = XPE_UNEXPECT_TOKEN;
    }
    if(obj->literal == ']' || obj->literal == ',')
        cxt->expectOperand = true;
}

static void parseSymbolImpl(TextBufferObj* obj, FuncNamespace ns, char* name, ExprContext* cxt) {
    
    //we find the function with the simple name
    //in the innermost scope possible by going
    //through outer scopes until we can't find
    //a function definition.
    size_t valueLen = strlen(name);
    char* nsbegin = cxt->decl->name;
    Operator* func;
    Operator* test = NULL;
    do {
        func = test;
        if(cxt->startOfName == nsbegin)
            break; //we did all the namespaces
        //get the function with the name in the scope
        char* fname = concat(nsbegin,   //beginning of scope
            cxt->startOfName - nsbegin, //length of scope name
            name,                       //name to check
            valueLen);                  //length of name
        test = lv_op_getOperator(fname, ns);
        lv_free(fname);
        nsbegin = strchr(nsbegin, ':') + 1;
    } while(test);
    //test is null. func should contain the function
    if(!func) {
        //that name does not exist!
        LV_EXPR_ERROR = XPE_NAME_NOT_FOUND;
        return;
    }
    obj->type = OPT_FUNCTION;
    obj->func = func;
    //toggle if RHS is true
    cxt->expectOperand ^=
        ((!cxt->expectOperand && func->arity != 1) || func->arity == 0);
}

static void parseSymbol(TextBufferObj* obj, ExprContext* cxt) {
    
    FuncNamespace ns = cxt->expectOperand ? FNS_PREFIX : FNS_INFIX;
    parseSymbolImpl(obj, ns, cxt->head->value, cxt);
}

static void parseQualNameImpl(TextBufferObj* obj, FuncNamespace ns, char* name, ExprContext* cxt) {
    
    //since it's a qualified name, we don't need to
    //guess what function it could be!
    Operator* func = lv_op_getOperator(name, ns);
    if(!func) {
        //404 func not found
        LV_EXPR_ERROR = XPE_NAME_NOT_FOUND;
        return;
    }
    obj->type = OPT_FUNCTION;
    obj->func = func;
    //toggle if RHS is true
    cxt->expectOperand ^=
        ((!cxt->expectOperand && func->arity != 1) || func->arity == 0);
}

static void parseQualName(TextBufferObj* obj, ExprContext* cxt) {
    
    FuncNamespace ns = cxt->expectOperand ? FNS_PREFIX : FNS_INFIX;
    parseQualNameImpl(obj, ns, cxt->head->value, cxt);
}

static void parseFuncValue(TextBufferObj* obj, ExprContext* cxt) {
    
    if(!cxt->expectOperand) {
        LV_EXPR_ERROR = XPE_EXPECT_PRE;
        return;
    }
    size_t len = strlen(cxt->head->value);
    FuncNamespace ns;
    //values ending in '\' are infix functions
    //subtract 1 from length so we can skip the initial '\'
    if(cxt->head->value[len - 1] == '\\') {
        ns = FNS_INFIX;
        //substringing
        cxt->head->value[len - 1] = '\0';
    } else {
        ns = FNS_PREFIX;
    }
    if(cxt->head->type == TTY_QUAL_FUNC_VAL)
        parseQualNameImpl(obj, ns, cxt->head->value + 1, cxt);
    else
        parseSymbolImpl(obj, ns, cxt->head->value + 1, cxt);
    //undo substringing
    if(ns == FNS_INFIX)
        cxt->head->value[len - 1] = '\\';
    obj->type = OPT_FUNCTION_VAL;
    cxt->expectOperand = false;
}

static void parseIdent(TextBufferObj* obj, ExprContext* cxt) {
    
    if(cxt->expectOperand) {
        //is this a def?
        if(strcmp(cxt->head->value, "def") == 0) {
            //todo
        }
        //try parameter names first
        for(int i = 0; i < cxt->decl->arity; i++) {
            if(strcmp(cxt->head->value, cxt->decl->params[i].name) == 0) {
                //save param name
                obj->type = OPT_PARAM;
                obj->param = i;
                cxt->expectOperand = false;
                return;
            }
        }
    }
    //not a parameter, try a function
    parseSymbol(obj, cxt);
}

static void parseNumber(TextBufferObj* obj, ExprContext* cxt) {
    
    if(!cxt->expectOperand) {
        LV_EXPR_ERROR = XPE_EXPECT_PRE;
        return;
    }
    double num = strtod(cxt->head->value, NULL);
    obj->type = OPT_NUMBER;
    obj->number = num;
    cxt->expectOperand = false;
}

static void parseString(TextBufferObj* obj, ExprContext* cxt) {
    
    if(!cxt->expectOperand) {
        LV_EXPR_ERROR = XPE_EXPECT_PRE;
        return;
    }
    char* c = cxt->head->value + 1; //skip open quote
    LvString* newStr = lv_alloc(sizeof(LvString) + strlen(c) + 1);
    size_t len = 0;
    while(*c != '"') {
        if(*c == '\\') {
            //handle escape sequences
            switch(*++c) {
                case 'n': newStr->value[len] = '\n';
                    break;
                case 't': newStr->value[len] = '\t';
                    break;
                case '"': newStr->value[len] = '"';
                    break;
                case '\'': newStr->value[len] = '\'';
                    break;
                case '\\': newStr->value[len] = '\\';
                    break;
                default:
                    assert(false);
            }
        } else
            newStr->value[len] = *c;
        c++;
        len++;
    }
    newStr = lv_realloc(newStr, sizeof(LvString) + len + 1);
    newStr->value[len] = '\0';
    newStr->len = len;
    obj->type = OPT_STRING;
    obj->str = newStr;
    cxt->expectOperand = false;
}

static void parseTextObj(TextBufferObj* obj, ExprContext* cxt) {
    
    switch(cxt->head->type) {
        case TTY_LITERAL:
            parseLiteral(obj, cxt);
            break;
        case TTY_IDENT:
            parseIdent(obj, cxt);
            break;
        case TTY_SYMBOL:
            parseSymbol(obj, cxt);
            break;
        case TTY_QUAL_IDENT:
        case TTY_QUAL_SYMBOL:
            parseQualName(obj, cxt);
            break;
        case TTY_NUMBER:
            parseNumber(obj, cxt);
            break;
        case TTY_STRING:
            parseString(obj, cxt);
            break;
        case TTY_FUNC_VAL:
        case TTY_QUAL_FUNC_VAL:
            parseFuncValue(obj, cxt);
            break;
        case TTY_FUNC_SYMBOL:
            LV_EXPR_ERROR = XPE_UNEXPECT_TOKEN;
            break;
    }
}

static void pushStack(TextStack* stack, TextBufferObj* obj) {
    
    if(stack->top + 1 == stack->stack + stack->len) {
        stack->len *= 2;
        size_t sz = stack->top - stack->stack;
        stack->stack = lv_realloc(stack->stack,
            stack->len * sizeof(TextBufferObj));
        stack->top = stack->stack + sz;
    }
    *++stack->top = *obj;
}

static void pushParam(IntStack* stack, int num) {
    
    if(stack->top + 1 == stack->stack + stack->len) {
        stack->len *= 2;
        size_t sz = stack->top - stack->stack;
        stack->stack = lv_realloc(stack->stack,
            stack->len * sizeof(TextBufferObj));
        stack->top = stack->stack + sz;
    }
    *++stack->top = num;
}

#define REQUIRE_NONEMPTY(s) \
    if(s.top == s.stack) { LV_EXPR_ERROR = XPE_UNBAL_GROUP; return; } else (void)0

//shunts over one op (or handles right bracket)
//optionally removing the top operator
//and checks function arity if applicable.
//Returns whether an error occurred.
static bool shuntOps(ExprContext* cxt) {
    
    if(isLiteral(cxt->ops.top, ']'))
        handleRightBracket(cxt);
    else {
        TextBufferObj* tmp = cxt->ops.top--;
        pushStack(&cxt->out, tmp);
        if(tmp->type == OPT_FUNCTION && tmp->func->arity > 0) {
            int ar = *cxt->params.top--;
            printf("DEBUG: func=%s, arity=%d, ar=%d\n",
                tmp->func->name, tmp->func->arity, ar);
            if(tmp->func->arity != ar) {
                LV_EXPR_ERROR = XPE_BAD_ARITY;
                return false;
            }
        }
    }
    return LV_EXPR_ERROR == 0;
}

/**
 * Handles Lavender square bracket notation.
 * Lavender requires that expressions in square brackets
 * be moved verbatim to the right of the next sub-expression.
 */
static void handleRightBracket(ExprContext* cxt) {
    
    assert(isLiteral(cxt->ops.top, ']'));
    assert(cxt->params.top != cxt->params.stack);
    int arity = *cxt->params.top--;
    cxt->ops.top--; //pop ']'
    REQUIRE_NONEMPTY(cxt->ops);
    //shunt over operators
    do {
        //no need to check arity, since it was already checked
        if(isLiteral(cxt->ops.top, ']'))
            handleRightBracket(cxt);
        else {
            pushStack(&cxt->out, cxt->ops.top--);
            REQUIRE_NONEMPTY(cxt->ops);
        }
    } while(!isLiteral(cxt->ops.top, '['));
    //todo add call
    printf("Right bracket arity is %d\n", arity);
    cxt->ops.top--; //pop '['
}

/**
 * A modified version of Dijkstra's shunting yard algorithm for
 * converting infix to postfix. The differences from the original
 * algorithm are:
 *  1. Support for Lavender's square bracket notation.
 *      See handleRightBracket() for details.
 *  2. Validation that the number of parameters passed
 *      to functions match arity.
 */
static void shuntingYard(TextBufferObj* obj, ExprContext* cxt) {
    
    if(obj->type == OPT_LITERAL) {
        switch(obj->literal) {
            case '(':
                //push left paren and push new param count
                pushStack(&cxt->ops, obj);
                break;
            case '[':
                //push to op stack and add to out
                pushStack(&cxt->ops, obj);
                pushStack(&cxt->out, obj);
                break;
            case ']':
                //pop out onto op until '['
                //then push ']' onto op
                while(!isLiteral(cxt->out.top, '[')) {
                    REQUIRE_NONEMPTY(cxt->out);
                    pushStack(&cxt->ops, cxt->out.top--);
                }
                REQUIRE_NONEMPTY(cxt->out);
                cxt->out.top--;
                pushParam(&cxt->params, 1);
                pushStack(&cxt->ops, obj);
                break;
            case ')':
                //shunt over all operators until we hit left paren
                //if we underflow, then unbalanced parens
                while(!isLiteral(cxt->ops.top, '(')) {
                    REQUIRE_NONEMPTY(cxt->ops);
                    if(!shuntOps(cxt))
                        return;
                }
                REQUIRE_NONEMPTY(cxt->ops);
                cxt->ops.top--;
                break;
            case ',':
                //shunt ops until a left paren
                while(!isLiteral(cxt->ops.top, '(')) {
                    REQUIRE_NONEMPTY(cxt->ops);
                    if(!shuntOps(cxt))
                        return;
                }
                REQUIRE_NONEMPTY(cxt->params);
                ++*cxt->params.top;
                break;
        }
    } else if(obj->type != OPT_FUNCTION || obj->func->arity == 0) {
        //it's a value, shunt it over
        pushStack(&cxt->out, obj);
    } else if(obj->type == OPT_FUNCTION) {
        //shunt over the ops of greater precedence if right assoc.
        //and greater or equal precedence if left assoc.
        int sub = (obj->func->fixing == FIX_RIGHT_IN ? 0 : 1);
        while(cxt->ops.top != cxt->ops.stack && (compare(obj, cxt->ops.top) - sub) < 0) {
            if(!shuntOps(cxt))
                return;
        }
        //push the actual operator on ops
        pushStack(&cxt->ops, obj);
        if(obj->func->fixing == FIX_PRE || obj->func->arity == 1)
            pushParam(&cxt->params, 1);
        else
            pushParam(&cxt->params, 2);
    } else {
        assert(false);
    }
}