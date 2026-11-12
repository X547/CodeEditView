/**
 * Tree-sitter grammar for Jam, the build tool (Perforce Jam 2.5 and the
 * Haiku variant), after jamgram.yy and scan.c.
 *
 * Jam splits tokens at white space only; a token is a keyword when all of
 * it is one. Keyword extraction with the "word" token does the same, and a
 * keyword is taken only where the parser expects it, like the punctuation
 * scan mode of Jam in lists.
 */

const PREC = {
	or: 1,
	and: 2,
	equal: 3,
	compare: 4,
	not: 5,
};

// Word characters outside strings and variables: \ escapes the next
// character, $ not followed by ( is literal. A lone $ is a token of its own;
// "$(" is longer, so it wins over it.
const TEXT = /([^\s"\\$]|\\[^\n]|\$[^\s"\\$(])+|\$/;
// The same inside a variable, without parentheses.
const VARIABLE_TEXT = /([^\s"\\$()]|\\[^\n]|\$[^\s"\\$(])+|\$/;
// String characters, white space included.
const STRING_CONTENT = /([^"\\$]|\\(.|\n)|\$[^"\\$(])+|\$/;

// A list of lists: lists separated by colons, each may be empty.
function lol($)
{
	return seq(optional($.list), repeat(seq(':', optional($.list))));
}

module.exports = grammar({
	name: 'jam',

	extras: $ => [/\s/, $.comment],

	externals: $ => [$.action_body, $._error_sentinel],

	word: $ => $.text,

	rules: {
		source_file: $ => repeat($._statement),

		_statement: $ => choice(
			$.block,
			$.local_statement,
			$.include_statement,
			$.rule_invocation,
			$.assignment,
			$.on_assignment,
			$.jump_statement,
			$.return_statement,
			$.for_statement,
			$.switch_statement,
			$.if_statement,
			$.while_statement,
			$.rule_definition,
			$.on_statement,
			$.actions_definition,
		),

		block: $ => seq('{', repeat($._statement), '}'),

		local_statement: $ => seq(
			'local',
			optional($.list),
			optional(seq('=', optional($.list))),
			';',
		),

		include_statement: $ => seq('include', optional($.list), ';'),

		rule_invocation: $ => seq(field('name', $.arg), lol($), ';'),

		assignment: $ => seq(
			field('name', $.arg),
			$._assign,
			optional($.list),
			';',
		),

		on_assignment: $ => seq(
			field('name', $.arg),
			'on',
			optional($.list),
			$._assign,
			optional($.list),
			';',
		),

		_assign: $ => choice('=', '+=', '?=', seq('default', '=')),

		jump_statement: $ => seq(
			choice('break', 'continue', 'jumptoeof'),
			optional($.list),
			';',
		),

		return_statement: $ => seq('return', optional($.list), ';'),

		for_statement: $ => seq(
			'for',
			field('variable', $.arg),
			'in',
			optional($.list),
			field('body', $.block),
		),

		switch_statement: $ => seq(
			'switch',
			optional($.list),
			'{',
			repeat($.case_clause),
			'}',
		),

		case_clause: $ => prec.right(seq(
			'case',
			field('pattern', $.arg),
			':',
			repeat($._statement),
		)),

		if_statement: $ => prec.right(seq(
			'if',
			field('condition', $._expression),
			field('consequence', $.block),
			optional(seq('else', field('alternative', $._statement))),
		)),

		while_statement: $ => seq(
			'while',
			field('condition', $._expression),
			field('body', $.block),
		),

		rule_definition: $ => seq(
			'rule',
			field('name', $.arg),
			optional(field('parameters', $.parameters)),
			field('body', $.block),
		),

		parameters: $ => seq($.arg, repeat(seq(':', $.arg))),

		on_statement: $ => seq('on', field('target', $.arg), $._statement),

		actions_definition: $ => seq(
			'actions',
			repeat($.action_flag),
			field('name', $.arg),
			optional(seq('bind', optional($.list))),
			'{',
			optional($.action_body),
			'}',
		),

		action_flag: $ => choice(
			'updated',
			'together',
			'ignore',
			'quietly',
			'piecemeal',
			'existing',
			seq('maxline', $.arg),
		),

		_expression: $ => choice(
			$.arg,
			$.binary_expression,
			$.in_expression,
			$.unary_expression,
			$.parenthesized_expression,
		),

		binary_expression: $ => {
			const table = [
				[PREC.or, choice('||', '|')],
				[PREC.and, choice('&&', '&')],
				[PREC.equal, choice('=', '!=')],
				[PREC.compare, choice('<', '<=', '>', '>=')],
			];
			return choice(...table.map(([precedence, operator]) => prec.left(precedence, seq(
				field('left', $._expression),
				field('operator', operator),
				field('right', $._expression),
			))));
		},

		in_expression: $ => prec.left(PREC.equal, seq(
			field('left', $.arg),
			'in',
			field('right', optional($.list)),
		)),

		unary_expression: $ => prec(PREC.not, seq('!', $._expression)),

		parenthesized_expression: $ => seq('(', $._expression, ')'),

		list: $ => repeat1($.arg),

		// One token of Jam: parts without white space between them, or a
		// rule call in brackets.
		arg: $ => choice(
			$.call,
			seq($._part, repeat($._immediate_part)),
		),

		_part: $ => choice($.text, $.string, $.variable),

		_immediate_part: $ => choice(
			alias($._immediate_text, $.text),
			alias($._immediate_string, $.string),
			alias($._immediate_variable, $.variable),
		),

		text: _ => TEXT,

		_immediate_text: _ => token.immediate(prec(2, TEXT)),

		string: $ => seq('"', optional($._string_body), token.immediate('"')),

		_immediate_string: $ => seq(token.immediate('"'), optional($._string_body), token.immediate('"')),

		_string_body: $ => repeat1(choice(
			alias(token.immediate(prec(2, STRING_CONTENT)), $.string_content),
			alias($._immediate_variable, $.variable),
		)),

		variable: $ => seq('$(', optional($._variable_body), token.immediate(')')),

		_immediate_variable: $ => seq(token.immediate(prec(2, '$(')), optional($._variable_body), token.immediate(')')),

		_variable_body: $ => repeat1(choice(
			token.immediate(prec(2, VARIABLE_TEXT)),
			alias($._immediate_variable, $.variable),
			alias($._immediate_string, $.string),
		)),

		call: $ => seq(
			'[',
			choice(
				seq(field('name', $.arg), lol($)),
				seq('on', field('target', $.arg), field('name', $.arg), lol($)),
				seq('on', field('target', $.arg), 'return', optional($.list)),
			),
			']',
		),

		// Only at the start of a token, as in Jam.
		comment: _ => token(prec(1, /#[^\n]*/)),
	},
});
