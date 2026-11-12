; Meson highlighting. A later pattern overrides an earlier one for the same
; text; nested nodes override their parents.

[
  "." "," ":"
  "(" ")" "[" "]" "{" "}"
] @punctuation

[
  "=" "+=" "-=" "/="
  "+" "/"
  "==" "!=" "<" ">" ">="
  "?"
] @operator

[
  "if" "elif" "else" "endif" "foreach" "endforeach"
  "and" "or" "not" "in"
  (keyword_break) (keyword_continue)
] @keyword

; The grammar does not parse break and continue in all places.
((identifier) @keyword
  (#any-of? @keyword "break" "continue"))

((identifier) @type.builtin
  (#any-of? @type.builtin "meson" "build_machine" "host_machine" "target_machine"))

(normal_command command: (identifier) @function)

(bool) @constant.builtin
(number) @number
(string) @string
(escape_sequence) @character
; @variable@ substitutions in format strings.
(formatunit) @constant

(comment) @comment
