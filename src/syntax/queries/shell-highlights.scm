; Shell (sh and Bash) highlighting. A later pattern overrides an earlier one
; for the same text; nested nodes override their parents.

[
  ";" ";;" ";&" ";;&" "&"
  "(" ")" "((" "))" "[" "]" "[[" "]]" "{" "}"
] @punctuation

[
  "|" "|&" "&&" "||" "!"
  ">" ">>" "<" "<<" "<<-" "<<<" "&>" "&>>" ">&" "<&" ">&-" "<&-" ">|"
  "=" "+=" "-=" "*=" "/=" "==" "!=" "=~" "<=" ">="
  "+" "-" "*" "/" "%" "**" "++" "--"
] @operator

(test_operator) @operator

[
  "case" "do" "done" "elif" "else" "esac" "fi" "for" "function" "if" "in"
  "select" "then" "until" "while"
  "declare" "export" "local" "readonly" "typeset" "unset" "unsetenv"
] @keyword

(command_name) @function
(function_definition name: (word) @function)

; Builtins that change the control flow.
((command_name (word) @keyword)
  (#any-of? @keyword "break" "continue" "return" "exit"))

(number) @number
(file_descriptor) @number

[
  (string)
  (raw_string)
  (ansi_c_string)
  (translated_string)
  (heredoc_body)
  (heredoc_start)
  (heredoc_end)
] @string

; Expansions also inside strings and here-documents.
(simple_expansion) @constant
(expansion) @constant
(expansion ["${" "}"] @constant)
(command_substitution ["$(" ")" "`"] @operator)
(process_substitution ["<(" ">(" ")"] @operator)
(arithmetic_expansion ["$((" "))"] @operator)

(comment) @comment
