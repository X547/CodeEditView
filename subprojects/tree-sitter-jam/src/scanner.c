// External scanner of the Jam grammar: the body of an actions definition,
// which Jam reads as raw text up to the matching closing brace.

#include "tree_sitter/parser.h"

#include <stdbool.h>


enum TokenType {
	ACTION_BODY,
	ERROR_SENTINEL,
};


void *tree_sitter_jam_external_scanner_create(void)
{
	return NULL;
}


void tree_sitter_jam_external_scanner_destroy(void *payload)
{
	(void)payload;
}


unsigned tree_sitter_jam_external_scanner_serialize(void *payload, char *buffer)
{
	(void)payload;
	(void)buffer;
	return 0;
}


void tree_sitter_jam_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
	(void)payload;
	(void)buffer;
	(void)length;
}


bool tree_sitter_jam_external_scanner_scan(void *payload, TSLexer *lexer, const bool *validSymbols)
{
	(void)payload;
	// All symbols are valid during error recovery.
	if (validSymbols[ERROR_SENTINEL] || !validSymbols[ACTION_BODY]) {
		return false;
	}

	while (lexer->lookahead == ' ' || lexer->lookahead == '\t'
		|| lexer->lookahead == '\n' || lexer->lookahead == '\r') {
		lexer->advance(lexer, true);
	}

	// Braces nest; the body ends before the unmatched closing brace.
	unsigned nesting = 0;
	bool empty = true;
	for (;;) {
		if (lexer->eof(lexer)) {
			return false;
		}
		const int32_t c = lexer->lookahead;
		if (c == '}') {
			if (nesting == 0) {
				break;
			}
			nesting--;
		} else if (c == '{') {
			nesting++;
		}
		lexer->advance(lexer, false);
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
			// Trailing white space stays out of the body.
			lexer->mark_end(lexer);
			empty = false;
		}
	}
	if (empty) {
		return false;
	}
	lexer->result_symbol = ACTION_BODY;
	return true;
}
