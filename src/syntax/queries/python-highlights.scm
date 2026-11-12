; Python highlighting. A later pattern overrides an earlier one for the same
; text; nested nodes override their parents.

["(" ")" "[" "]" "{" "}" "," ":" "." ";"] @punctuation

[
  "-" "-=" "!=" "*" "**" "**=" "*=" "/" "//" "//=" "/=" "&" "&=" "%" "%="
  "^" "^=" "+" "->" "+=" "<" "<<" "<<=" "<=" "<>" "=" ":=" "==" ">" ">="
  ">>" ">>=" "|" "|=" "~" "@="
] @operator

[
  "and" "as" "assert" "async" "await" "break" "case" "class" "continue"
  "def" "del" "elif" "else" "except" "exec" "finally" "for" "from" "global"
  "if" "import" "in" "is" "lambda" "match" "nonlocal" "not" "or" "pass"
  "print" "raise" "return" "try" "while" "with" "yield"
] @keyword

(decorator) @function
(function_definition name: (identifier) @function)
(class_definition name: (identifier) @type)
(call function: (identifier) @function)
(call function: (attribute attribute: (identifier) @function))
(type (identifier) @type)

[(none) (true) (false)] @constant.builtin
[(integer) (float)] @number

; The parts of strings, so that interpolations in f-strings are code.
[(string_start) (string_content) (string_end)] @string
(escape_sequence) @character

(comment) @comment
