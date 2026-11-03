; C highlighting, styled after Pe. A later pattern overrides an earlier one
; for the same text.

[
  "." ";" "," ":"
  "(" ")" "[" "]" "{" "}"
] @punctuation

[
  "=" "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>="
  "+" "-" "*" "/" "%" "++" "--"
  "==" "!=" "<" ">" "<=" ">="
  "&&" "||" "!"
  "&" "|" "^" "~" "<<" ">>"
  "->" "?"
] @operator

(type_identifier) @type
(primitive_type) @type.builtin
(sized_type_specifier) @type.builtin

[
  "break" "case" "const" "continue" "default" "do" "else" "enum" "extern"
  "for" "goto" "if" "inline" "register" "restrict" "return" "sizeof"
  "static" "struct" "switch" "typedef" "union" "volatile" "while"
] @keyword

[
  "#define" "#elif" "#else" "#endif" "#if" "#ifdef" "#ifndef" "#include"
  (preproc_directive)
] @preprocessor

(true) @constant.builtin
(false) @constant.builtin
(null) @constant.builtin

(number_literal) @number
(char_literal) @character
(string_literal) @string
(system_lib_string) @string

(comment) @comment
