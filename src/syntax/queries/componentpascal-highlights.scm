; Component Pascal highlighting. A later pattern overrides an earlier one for
; the same text; nested nodes override their parents.

[";" "," "." ":" ".." "|" "(" ")" "[" "]" "{" "}"] @punctuation

[
  ":=" "=" "#" "<" "<=" ">" ">="
  "+" "-" "*" "/" "&" "~" "^" "$"
] @operator

[
  "ARRAY" "BEGIN" "BY" "CASE" "CLOSE" "CONST" "DIV" "DO" "ELSE" "ELSIF" "END"
  "FOR" "IF" "IMPORT" "IN" "IS" "LOOP" "MOD" "MODULE" "OF" "OR" "OUT"
  "POINTER" "PROCEDURE" "RECORD" "REPEAT" "RETURN" "THEN" "TO" "TYPE" "UNTIL"
  "VAR" "WHILE" "WITH"
  "ABSTRACT" "EMPTY" "EXTENSIBLE" "LIMITED" "NEW"
] @keyword

(exit_statement) @keyword

; Types where a type is expected.
(type_declaration type: (qualident) @type)
(var_declaration type: (qualident) @type)
(field_list type: (qualident) @type)
(parameter_section type: (qualident) @type)
(formal_parameters result: (qualident) @type)
(array_type element: (qualident) @type)
(pointer_type target: (qualident) @type)
(record_type base: (qualident) @type)
(guard type: (qualident) @type)
(receiver type: (identifier) @type)

((identifier) @type.builtin
  (#any-of? @type.builtin
    "BOOLEAN" "CHAR" "SHORTCHAR" "BYTE" "SHORTINT" "INTEGER" "LONGINT"
    "SHORTREAL" "REAL" "SET" "ANYREC" "ANYPTR"))

; Procedure names at the declaration and its end, and calls.
(procedure_declaration name: (identifier) @function)
(procedure_declaration "END" . (identifier) @function)
(forward_declaration name: (identifier) @function)
(call_expression function: (identifier) @function)
(call_expression function: (selector_expression field: (identifier) @function))
(procedure_call (identifier) @function)
(procedure_call (selector_expression field: (identifier) @function))

((identifier) @constant.builtin
  (#any-of? @constant.builtin "TRUE" "FALSE" "INF"))
(nil) @constant.builtin

(number) @number
(character) @character
(string) @string

; Compiler flags such as [untagged] and the library of interface modules.
(system_flag) @preprocessor
(system_flag ["[" "]" "code"] @preprocessor)
(module_flag) @preprocessor
(module_flag ["[" "]" "="] @preprocessor)

(comment) @comment
; Text after the module, which the compiler does not read.
(trailing_text) @comment
