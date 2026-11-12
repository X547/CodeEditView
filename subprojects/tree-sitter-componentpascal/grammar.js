/**
 * Tree-sitter grammar for Component Pascal, after the language report
 * (Docu/CP-Lang, appendix B) and the BlackBox compiler (DevCPP, DevCPS),
 * with the system flags of BlackBox interface modules.
 */

const PREC = {
	relation: 1,
	add: 2,
	multiply: 3,
	sign: 4,
	not: 5,
	designator: 6,
};

function commaSep1(rule)
{
	return seq(rule, repeat(seq(',', rule)));
}

// Items separated by a separator, each may be empty, at least one token.
function sepBy1Optional(separator, rule)
{
	return choice(
		seq(rule, repeat(seq(separator, optional(rule)))),
		seq(repeat1(seq(separator, optional(rule)))),
	);
}

module.exports = grammar({
	name: 'componentpascal',

	// Control characters are blanks for the compiler; plain text exports of
	// BlackBox documents keep some.
	extras: $ => [/[\s\x01-\x1F\x7F ]/, $.comment],

	externals: $ => [$.comment, $.number, $.character, $.trailing_text, $._error_sentinel],

	word: $ => $.identifier,

	conflicts: $ => [],

	rules: {
		module: $ => seq(
			'MODULE',
			field('name', $.identifier),
			choice(
				seq(
					';',
					optional($.import_list),
					optional($._declarations),
					optional(seq('BEGIN', optional($.statement_sequence))),
					optional(seq('CLOSE', optional($.statement_sequence))),
				),
				// Interface modules declare procedures of a library without
				// bodies.
				seq(
					field('library', $.module_flag),
					';',
					optional($.import_list),
					repeat(choice(
						$._declaration_section,
						seq(alias($.procedure_heading, $.procedure_declaration), ';'),
					)),
				),
			),
			'END',
			$.identifier,
			'.',
			// Documents go on after the module, for example with commands;
			// the compiler stops reading at the end of the module.
			optional($.trailing_text),
		),

		module_flag: $ => seq('[', optional('='), $.string, ']'),

		import_list: $ => seq('IMPORT', commaSep1($.import), ';'),

		import: $ => seq(
			optional(seq(field('alias', $.identifier), ':=')),
			field('name', $.identifier),
		),

		_declaration_section: $ => choice($.const_section, $.type_section, $.var_section),

		// Sections and procedures; the compiler also takes sections after
		// procedures.
		_declarations: $ => repeat1(choice(
			$._declaration_section,
			seq(choice($.procedure_declaration, $.forward_declaration), ';'),
		)),

		const_section: $ => seq('CONST', repeat(seq($.const_declaration, ';'))),

		type_section: $ => seq('TYPE', repeat(seq($.type_declaration, ';'))),

		var_section: $ => seq('VAR', repeat(seq($.var_declaration, ';'))),

		const_declaration: $ => seq($._ident_def, '=', field('value', $._expression)),

		type_declaration: $ => seq($._ident_def, '=', field('type', $._type)),

		var_declaration: $ => seq(commaSep1($._ident_def), ':', field('type', $._type)),

		// A declared name with its export mark; interface modules give the
		// library entry in brackets.
		// "-" after the name is an export mark, as for the compiler.
		_ident_def: $ => prec.right(seq(
			field('name', $.identifier),
			optional(field('export', $.export_mark)),
			optional($.system_flag),
		)),

		export_mark: _ => choice('*', '-'),

		// Flags of the BlackBox compiler: [untagged], [code], ["entry"] etc.
		system_flag: $ => seq(
			'[',
			repeat(choice($.identifier, $.number, $.character, $.string, ',', '+', '=')),
			']',
		),

		procedure_declaration: $ => seq(
			'PROCEDURE',
			choice(
				// A code procedure: the machine code bytes, maybe none.
				prec.right(seq(
					alias($._code_flag, $.system_flag),
					$._ident_def,
					optional(field('parameters', $.formal_parameters)),
					optional(field('code', $.code)),
				)),
				seq(
					$._procedure_signature,
					choice(
						// With a body.
						seq(
							optional(seq(',', 'NEW')),
							optional(seq(',', 'EXTENSIBLE')),
							';',
							optional($._declarations),
							optional(seq('BEGIN', optional($.statement_sequence))),
							'END',
							$.identifier,
						),
						// An abstract or empty method.
						seq(
							optional(seq(',', 'NEW')),
							',',
							choice('ABSTRACT', 'EMPTY'),
						),
						// A code procedure with the old flag [1].
						field('code', $.code),
					),
				),
			),
		),

		// "code" is a keyword only here.
		_code_flag: _ => seq('[', 'code', ']'),

		procedure_heading: $ => seq(
			'PROCEDURE',
			$._procedure_signature,
			optional(seq(',', 'NEW')),
			optional(seq(',', choice('ABSTRACT', 'EMPTY', 'EXTENSIBLE'))),
		),

		code: $ => commaSep1($._expression),

		forward_declaration: $ => seq(
			'PROCEDURE',
			'^',
			$._procedure_signature,
			optional(seq(',', 'NEW')),
			optional(seq(',', choice('ABSTRACT', 'EMPTY', 'EXTENSIBLE'))),
		),

		// "(" after the name starts the parameters, also of code procedures.
		_procedure_signature: $ => prec.right(seq(
			optional($.system_flag),
			optional(field('receiver', $.receiver)),
			$._ident_def,
			optional(field('parameters', $.formal_parameters)),
		)),

		receiver: $ => seq(
			'(',
			optional(choice('VAR', 'IN')),
			field('name', $.identifier),
			':',
			field('type', $.identifier),
			')',
		),

		formal_parameters: $ => seq(
			'(',
			optional(seq($.parameter_section, repeat(seq(';', $.parameter_section)))),
			')',
			optional(seq(':', field('result', $._type))),
		),

		parameter_section: $ => seq(
			optional(choice('VAR', 'IN', 'OUT')),
			optional($.system_flag),
			commaSep1(field('name', $.identifier)),
			':',
			field('type', $._type),
		),

		_type: $ => choice(
			$.qualident,
			$.array_type,
			$.record_type,
			$.pointer_type,
			$.procedure_type,
		),

		array_type: $ => seq(
			'ARRAY',
			optional($.system_flag),
			optional(commaSep1($._expression)),
			'OF',
			field('element', $._type),
		),

		record_type: $ => seq(
			optional(field('attribute', choice('ABSTRACT', 'EXTENSIBLE', 'LIMITED'))),
			'RECORD',
			optional($.system_flag),
			optional(seq('(', field('base', $.qualident), ')')),
			optional(sepBy1Optional(';', $.field_list)),
			'END',
		),

		field_list: $ => seq(commaSep1($._ident_def), ':', field('type', $._type)),

		pointer_type: $ => seq(
			'POINTER',
			optional($.system_flag),
			'TO',
			field('target', $._type),
		),

		procedure_type: $ => prec.right(seq(
			'PROCEDURE',
			optional($.system_flag),
			optional($.formal_parameters),
		)),

		qualident: $ => seq(
			optional(seq(field('module', $.identifier), '.')),
			field('name', $.identifier),
		),

		statement_sequence: $ => sepBy1Optional(';', $._statement),

		_statement: $ => choice(
			$.assignment,
			$.procedure_call,
			$.if_statement,
			$.case_statement,
			$.while_statement,
			$.repeat_statement,
			$.for_statement,
			$.loop_statement,
			$.with_statement,
			$.exit_statement,
			$.return_statement,
		),

		assignment: $ => seq(field('left', $._designator), ':=', field('right', $._expression)),

		procedure_call: $ => $._designator,

		if_statement: $ => seq(
			'IF',
			field('condition', $._expression),
			'THEN',
			optional($.statement_sequence),
			repeat($.elsif_clause),
			optional($.else_clause),
			'END',
		),

		elsif_clause: $ => seq(
			'ELSIF',
			field('condition', $._expression),
			'THEN',
			optional($.statement_sequence),
		),

		else_clause: $ => seq('ELSE', optional($.statement_sequence)),

		case_statement: $ => seq(
			'CASE',
			field('value', $._expression),
			'OF',
			optional(sepBy1Optional('|', $.case)),
			optional($.else_clause),
			'END',
		),

		case: $ => seq(commaSep1($.case_label), ':', optional($.statement_sequence)),

		case_label: $ => seq($._expression, optional(seq('..', $._expression))),

		while_statement: $ => seq(
			'WHILE',
			field('condition', $._expression),
			'DO',
			optional($.statement_sequence),
			'END',
		),

		repeat_statement: $ => seq(
			'REPEAT',
			optional($.statement_sequence),
			'UNTIL',
			field('condition', $._expression),
		),

		for_statement: $ => seq(
			'FOR',
			field('variable', $.identifier),
			':=',
			field('from', $._expression),
			'TO',
			field('to', $._expression),
			optional(seq('BY', field('step', $._expression))),
			'DO',
			optional($.statement_sequence),
			'END',
		),

		loop_statement: $ => seq('LOOP', optional($.statement_sequence), 'END'),

		with_statement: $ => seq(
			'WITH',
			optional(sepBy1Optional('|', $.guarded_statements)),
			optional($.else_clause),
			'END',
		),

		guarded_statements: $ => seq($.guard, 'DO', optional($.statement_sequence)),

		guard: $ => seq(field('variable', $.qualident), ':', field('type', $.qualident)),

		exit_statement: _ => 'EXIT',

		return_statement: $ => prec.right(seq('RETURN', optional($._expression))),

		_expression: $ => choice(
			$._designator,
			$.binary_expression,
			$.unary_expression,
			$.number,
			$.character,
			$.string,
			$.nil,
			$.set,
			$.parenthesized_expression,
		),

		binary_expression: $ => {
			const table = [
				[PREC.relation, choice('=', '#', '<', '<=', '>', '>=', 'IN', 'IS')],
				[PREC.add, choice('+', '-', 'OR')],
				[PREC.multiply, choice('*', '/', 'DIV', 'MOD', '&')],
			];
			return choice(...table.map(([precedence, operator]) => prec.left(precedence, seq(
				field('left', $._expression),
				field('operator', operator),
				field('right', $._expression),
			))));
		},

		unary_expression: $ => choice(
			prec(PREC.sign, seq(field('operator', choice('+', '-')), field('operand', $._expression))),
			prec(PREC.not, seq(field('operator', '~'), field('operand', $._expression))),
		),

		parenthesized_expression: $ => seq('(', $._expression, ')'),

		nil: _ => 'NIL',

		set: $ => seq('{', optional(commaSep1($.set_element)), '}'),

		set_element: $ => seq($._expression, optional(seq('..', $._expression))),

		_designator: $ => choice(
			$.identifier,
			$.selector_expression,
			$.index_expression,
			$.dereference_expression,
			$.call_expression,
			$.string_conversion,
		),

		// Qualified names, record fields and methods.
		selector_expression: $ => prec(PREC.designator, seq(
			field('operand', $._designator),
			'.',
			field('field', $.identifier),
		)),

		index_expression: $ => prec(PREC.designator, seq(
			field('operand', $._designator),
			'[',
			commaSep1($._expression),
			']',
		)),

		dereference_expression: $ => prec(PREC.designator, seq(field('operand', $._designator), '^')),

		// Also type guards, which have the same syntax.
		call_expression: $ => prec(PREC.designator, seq(
			field('function', $._designator),
			field('arguments', $.arguments),
		)),

		arguments: $ => seq('(', optional(commaSep1($._expression)), ')'),

		string_conversion: $ => prec(PREC.designator, seq(field('operand', $._designator), '$')),

		string: _ => token(choice(/"[^"\n]*"/, /'[^'\n]*'/)),

		identifier: _ => /[A-Za-z_À-ÖØ-öø-￯][A-Za-z0-9_À-ÖØ-öø-￯]*/,
	},
});
