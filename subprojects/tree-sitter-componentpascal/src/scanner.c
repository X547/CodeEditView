// External scanner of the Component Pascal grammar: nested comments, and
// numbers, which end before ".." (0..9 is a range, not a real number).

#include "tree_sitter/parser.h"

#include <stdbool.h>


enum TokenType {
	COMMENT,
	NUMBER,
	CHARACTER,
	TRAILING_TEXT,
	ERROR_SENTINEL,
};


void *tree_sitter_componentpascal_external_scanner_create(void)
{
	return NULL;
}


void tree_sitter_componentpascal_external_scanner_destroy(void *payload)
{
	(void)payload;
}


unsigned tree_sitter_componentpascal_external_scanner_serialize(void *payload, char *buffer)
{
	(void)payload;
	(void)buffer;
	return 0;
}


void tree_sitter_componentpascal_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
	(void)payload;
	(void)buffer;
	(void)length;
}


// Blanks of the compiler: control characters and the no-break space.
static bool IsBlank(int32_t c)
{
	return (c > 0 && c <= ' ') || c == 0x7F || c == 0xA0;
}


static bool IsDigit(int32_t c)
{
	return c >= '0' && c <= '9';
}


static bool IsHexDigit(int32_t c)
{
	return IsDigit(c) || (c >= 'A' && c <= 'F');
}


// After "(*": up to the matching "*)". Unterminated comments end at the
// end of the text.
static bool ScanComment(TSLexer *lexer)
{
	unsigned depth = 1;
	while (depth > 0 && !lexer->eof(lexer)) {
		if (lexer->lookahead == '(') {
			lexer->advance(lexer, false);
			if (lexer->lookahead == '*') {
				lexer->advance(lexer, false);
				depth++;
			}
		} else if (lexer->lookahead == '*') {
			lexer->advance(lexer, false);
			if (lexer->lookahead == ')') {
				lexer->advance(lexer, false);
				depth--;
			}
		} else {
			lexer->advance(lexer, false);
		}
	}
	lexer->mark_end(lexer);
	lexer->result_symbol = COMMENT;
	return true;
}


// Integers (decimal, or hexadecimal with H, L, S, R), characters (X) and
// real numbers, as the compiler scans them.
static bool ScanNumber(TSLexer *lexer, const bool *validSymbols)
{
	bool decimal = true;
	while (IsHexDigit(lexer->lookahead)) {
		if (!IsDigit(lexer->lookahead)) {
			decimal = false;
		}
		lexer->advance(lexer, false);
		if (decimal) {
			lexer->mark_end(lexer);
		}
	}
	const int32_t suffix = lexer->lookahead;
	if (suffix == 'X' && validSymbols[CHARACTER]) {
		lexer->advance(lexer, false);
		lexer->mark_end(lexer);
		lexer->result_symbol = CHARACTER;
		return true;
	}
	if (suffix == 'H' || suffix == 'L' || suffix == 'S' || suffix == 'R') {
		lexer->advance(lexer, false);
		lexer->mark_end(lexer);
		lexer->result_symbol = NUMBER;
		return validSymbols[NUMBER];
	}
	// Without a suffix, the decimal digits only.
	lexer->result_symbol = NUMBER;
	if (!decimal || suffix != '.') {
		return validSymbols[NUMBER];
	}
	lexer->advance(lexer, false);
	if (lexer->lookahead == '.') {
		return validSymbols[NUMBER];
	}
	while (IsDigit(lexer->lookahead)) {
		lexer->advance(lexer, false);
	}
	lexer->mark_end(lexer);
	if (lexer->lookahead == 'E' || lexer->lookahead == 'D') {
		lexer->advance(lexer, false);
		if (lexer->lookahead == '+' || lexer->lookahead == '-') {
			lexer->advance(lexer, false);
		}
		if (IsDigit(lexer->lookahead)) {
			while (IsDigit(lexer->lookahead)) {
				lexer->advance(lexer, false);
			}
			lexer->mark_end(lexer);
		}
	}
	return validSymbols[NUMBER];
}


bool tree_sitter_componentpascal_external_scanner_scan(void *payload, TSLexer *lexer, const bool *validSymbols)
{
	(void)payload;
	// All symbols are valid during error recovery; comments are still
	// scanned then, so that they do not become errors.
	const bool recovering = validSymbols[ERROR_SENTINEL];

	while (IsBlank(lexer->lookahead)) {
		lexer->advance(lexer, true);
	}

	// Everything after the end of the module.
	if (validSymbols[TRAILING_TEXT] && !recovering) {
		if (lexer->eof(lexer)) {
			return false;
		}
		while (!lexer->eof(lexer)) {
			lexer->advance(lexer, false);
		}
		lexer->mark_end(lexer);
		lexer->result_symbol = TRAILING_TEXT;
		return true;
	}

	if (lexer->lookahead == '(' && validSymbols[COMMENT]) {
		lexer->advance(lexer, false);
		if (lexer->lookahead != '*') {
			return false;
		}
		lexer->advance(lexer, false);
		return ScanComment(lexer);
	}
	if (IsDigit(lexer->lookahead) && !recovering && (validSymbols[NUMBER] || validSymbols[CHARACTER])) {
		return ScanNumber(lexer, validSymbols);
	}
	return false;
}
