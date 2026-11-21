; JavaScript highlighting, also the base of TypeScript and TSX: only nodes
; of all three grammars. A later pattern overrides an earlier one for the
; same text.

[";" "," "." ":" "(" ")" "[" "]" "{" "}" (optional_chain)] @punctuation

[
  "=" "+=" "-=" "*=" "/=" "%=" "**=" "&=" "|=" "^=" "<<=" ">>=" ">>>="
  "&&=" "||=" "??="
  "+" "-" "*" "/" "%" "**" "++" "--"
  "==" "!=" "===" "!==" "<" ">" "<=" ">="
  "&&" "||" "??" "!"
  "&" "|" "^" "~" "<<" ">>" ">>>"
  "=>" "?" "..."
] @operator

[
  "as" "async" "await" "break" "case" "catch" "class" "const" "continue"
  "debugger" "default" "delete" "do" "else" "export" "extends" "finally"
  "for" "from" "function" "get" "if" "import" "in" "instanceof" "let" "new"
  "of" "return" "set" "static" "switch" "target" "throw" "try" "typeof"
  "using" "var" "void" "while" "with" "yield"
] @keyword
[(this) (super)] @keyword

; Capitalized names are classes; all capitals, constants.
((identifier) @type (#match? @type "^[A-Z]"))
((identifier) @constant (#match? @constant "^[A-Z_][A-Z0-9_]+$"))
(class_declaration name: (_) @type)
(class name: (_) @type)
(new_expression constructor: (identifier) @type)

(function_declaration name: (identifier) @function)
(generator_function_declaration name: (identifier) @function)
(function_expression name: (identifier) @function)
(method_definition name: (property_identifier) @function)
(variable_declarator
  name: (identifier) @function
  value: [(function_expression) (arrow_function)])
(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (property_identifier) @function))
(decorator "@" @function)
(decorator (identifier) @function)
(decorator (call_expression function: (identifier) @function))

[(true) (false) (null) (undefined)] @constant.builtin
(number) @number

(string) @string
(escape_sequence) @character
; A template string is a string apart from its substitutions.
(template_string ["`" (string_fragment)] @string)
(template_substitution ["${" "}"] @punctuation)
(regex) @string
(regex "/" @string)

(hash_bang_line) @preprocessor
(comment) @comment
