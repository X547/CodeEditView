; Jam highlighting. A later pattern overrides an earlier one for the same
; text; nested nodes override their parents.

[";" ":" "{" "}" "[" "]" "(" ")"] @punctuation

[
  "=" "+=" "?="
  "!=" "<" "<=" ">" ">="
  "&&" "&" "||" "|" "!"
] @operator

[
  "actions" "bind" "break" "case" "continue" "default" "else" "for" "if"
  "in" "include" "jumptoeof" "local" "on" "return" "rule" "switch" "while"
] @keyword

(action_flag) @keyword

(rule_invocation name: (arg) @function)
(rule_definition name: (arg) @function)
(actions_definition name: (arg) @function)
(call name: (arg) @function)

(string) @string
; Variables also inside strings and rule names.
(variable) @constant
(variable ")" @constant)

(comment) @comment
