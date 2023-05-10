#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY,
} Precedence;

// ParseFn is a function pointer that returns nothing and takes nothing
typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;

  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;

static Chunk* currentChunk() {
  return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
  // Suppress errors while in panic mode
  if (parser.panicMode) return;
  // Enter panic mode
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

// Steps through the token stream
static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type ) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if(!check(type)) return false;
  advance();
  return true;
}

// Writes a byte to the chunk
static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

// Writes two bytes to the chunk, useful for opcode + param
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  // + 2 takes into account the size of the OP_LOOP instructions operands that much be jumped over
  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body is too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
  // Emits the bytecode instruction
  emitByte(instruction);
  // 16 bit offset operand
  emitByte(0xff);
  emitByte(0xff);
  // returns offset of emitted instruction
  return currentChunk()->count -2;
}

// Goes backwards in the bytecode and replaced operand
// at the given location with the calculated jump offset.
static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself
  int jump = currentChunk()->count - offset - 2;
  
  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitReturn() {
  emitByte(OP_NIL);
  emitByte(OP_RETURN);
}

// Ensures we do not add too many constants to the chunk
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  // Grab the function name while its the previous token
  if (type != TYPE_SCRIPT) {
    current->function->name = copyString(parser.previous.start,
        parser.previous.length);
  }

  // Claims stack slot 0 for internal use.
  // We give it empty name so the user cant refer to it
  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.start = "";
  local->name.length = 0;
}

static ObjFunction* endCompiler() {
  emitReturn();
  // When the compiler creates a function object, 
  // we return the function
  ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars :  "<script>");
  }
#endif

  current= current->enclosing;
  return function;
}


// Forward Declaration
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// Stores the identifier in the constant table, and the bytecode instruction refers to it by table index
static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  // If identifier lexeme is equal, its the same
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  // iterate over all locals in the compiler,
  // if a local is found with matching name we return its index
  // We walk backward to find the most recent declaration,
  // ensures inner locals shadow outer scope declared locals
  for (int i = compiler->localCount -1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
          error("Can't read local variable in its own initializer.");
      }   
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index,
    bool isLocal) {
  int upvalCount = compiler->function->upvalueCount;
  
  // use existing upvalues instead of making new one for each usage
  for(int i = 0; i < upvalCount; i++) {
    Upvalue* upval = &compiler->upvalues[i];
    if (upval->index == index && upval->isLocal == isLocal) {
      return i;
    }
  }

  if(upvalCount >= UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalCount].isLocal = isLocal;
  compiler->upvalues[upvalCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
  if(compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  // recursively check enclosing compilers to find upvalues to values from further out stacks
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

// Add local variable name to the compilers list along with its scope depth
static void addLocal(Token name) {
  // If we reach the max amount of vars per scope, error
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  // -1 depth is 'uninitialized' state
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable() {
  if (current->scopeDepth == 0) return;
  // Cancel this if we are in top level scope

  Token* name = &parser.previous;

  // Prevent multiple variables with the same name in the same local scope
  for (int i = current->localCount -1; i >= 0; i--) {
    Local* local = &current->locals[i];
    // If we reach the beginning or a variable opened by another scope, we know we have checked the current scope
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  
  declareVariable();
  // Return early as locals are on the stack, not looked up by name unlike globals
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  // If this is called in a top level function decl, return early
  // There is nothing to set as initialized
  if (current->scopeDepth == 0) return;
  // Marks the current local variable as ready for use, by setting its depth
  current->locals[current->localCount -1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  // If we are in a scope, do not define a global
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

// Get the arguments, compile them to bytecode, and then return how many were compiled.
static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments in a function call.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
  return argCount;
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default: return; // Unreachable
  }
}

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  // Pop all of the locals off of the stack, the scope has ended.
  while (current->localCount > 0
      && current->locals[current->localCount -1].depth > current->scopeDepth) {
    if(current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
    emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    default: return;
  }
}

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expected '}' at end of block.");
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
  // Define local variables for each parameter inside the function body
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters in a function.");
      }
      uint8_t constant = parseVariable("Expected parameter name.");
      defineVariable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after function parameters.");
  consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
  block();

  ObjFunction* function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for(int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void funDeclaration() {
  // Functions are first class, declaring them is the same as declaring a var
  uint8_t global = parseVariable("Expected function name.");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}


static void varDeclaration() {
  uint8_t global = parseVariable("Expected variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    // If no initializer is provided, assign nil implicitly
    // var a; is equal to var a = nil;
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON,
      "Expected ';' after variable declaration.");

  defineVariable(global);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
  emitByte(OP_POP);
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after if condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  // When the condition is truthy, we pop it right before
  // the code inside the then branch

  // Below two sets of bytecode run if above expr is true
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  // Otherwise, we pop it at the beginning of the else branch

  // Below two statements run if statement is falsey, (pop runs even if there is no else branch)
  emitByte(OP_POP);
  if (match(TOKEN_ELSE)) statement();

  patchJump(elseJump);
}

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void returnStatement() {
  // Prevent using return in top level script
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    // Evaluate the return expression
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after return value.");
    // Emit return opcode, so next stack value is the return value
    emitByte(OP_RETURN);
  }
}

// While statements are the same as if statements,
// except for the looping nature
static void whileStatement() {
  // record the start of the loop as the beginning,
  // that way we reevaluate the condition each loop
  int loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after while condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void forStatement() {
  // For loop vars should be inside its own scope only
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'.");

  // initializer clause
  if (match(TOKEN_SEMICOLON)) {
    // No initializer
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;

  // Condition clause
  int exitJump = -1;
  // Check if there is a condition (not an infinite loop)
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after loop condition.");

    // Jump out of the loop if condition is false.
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // condition.
  }


  // Increment clause
  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' at end of for condition.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();
  emitLoop(loopStart);

  // Patch the loop if there was a condition
  if (exitJump != 1) {
    patchJump(exitJump);
    emitByte(OP_POP); // condition
  }

  endScope();
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    // Return when we find a statement ender or a keyword that would begin the next statement
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:
        ; // Do nothing
    }

    advance();
  }
}

static void declaration() {
  if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  // Synchronize if we had a compile error in previous statement
  if (parser.panicMode) synchronize();
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

// +1 and -2 trim the quotation marks
// It then takes the created string object, wraps it in a value, and stuffs it into the constant table
static void string(bool canAssign) {
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
          parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  // if we find a local variable with given name
  // use the local
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    // if there is no local, use the instruction for globals
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, arg);
  } else {
    emitBytes(getOp, arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // Compile the operand
  parsePrecedence(PREC_UNARY);

  // Emit the operator instruction.
  switch(operatorType) {
    case TOKEN_BANG: emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default: return; // Unreachable
  }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

// Starts at the cur token and parses any expression 
// at that precedence or higher.
//
// First token is always a prefix expression
// it may be nested, but left to right will always be a prefix expr
// After parsing that, we look for an infix parser for the next token.
// If one is found, it means the prefix expr may be an operand, but only if the call to parsePrece has a low enough 
// precedence to allow that infix operator.
// If the next token is too low precedence, or not an infix we are done, cannot parse further
// Otherwise, we consume the operator and hand off control to the infix parser we found.
// It consumes tokens (usually right operand) and then returns back to parsePrecedence.
// We keep looping to see if next is a valid infix that can take the preceding expr as an operand.
// Loops until we find non infix operator/ low enough precedence. Ends the recursion
static void parsePrecedence(Precedence precedence) {
  advance();
  // Get the prefix rule of the just consumed token
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  // Call the function associated with that token
  bool canAssign = precedence <=PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <=getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  // If The = doesnt get consumed as part of the expression, it is an error
  if(canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

ObjFunction* compile(const char *source) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while(!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}
