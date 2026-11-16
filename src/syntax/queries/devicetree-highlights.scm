; Devicetree source highlighting. A later pattern overrides an earlier one
; for the same text; nested nodes override their parents.

; "&" and "&{" stay part of the reference they start.
[
  ";" "," ":" "@" "="
  "(" ")" "[" "]" "{" "}" "<" ">"
] @punctuation

; "<" and ">" also enclose cells; only those of expressions are operators.
(unary_expression operator: _ @operator)
(binary_expression operator: _ @operator)
(conditional_expression ["?" ":"] @operator)

[
  "/dts-v1/" "/plugin/" "/include/" "/memreserve/" "/bits/" "/incbin/"
  "/delete-node/" "/delete-property/" "/omit-if-no-ref/"
] @keyword

[
  "#define" "#undef" "#include" "#if" "#ifdef" "#ifndef" "#elif" "#elifdef"
  "#elifndef" "#else" "#endif" "defined"
] @preprocessor

(node name: (identifier) @type)
(delete_node name: (identifier) @type)
(path_node name: (identifier) @type)
(unit_address) @number

; Labels and references to them.
(node label: (identifier) @constant)
(property label: (identifier) @constant)
(memory_reservation label: (identifier) @constant)
(reference) @constant

(call_expression function: (identifier) @function)
(preproc_def name: (identifier) @constant)
(preproc_function_def name: (identifier) @function)

(integer_literal) @number
(byte_string_literal) @number
[(string_literal) (system_lib_string)] @string
(escape_sequence) @character

(comment) @comment
