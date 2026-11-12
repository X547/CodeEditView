; CMake highlighting. A later pattern overrides an earlier one for the same
; text; nested nodes override their parents.

["(" ")"] @punctuation

[
  (function) (endfunction) (macro) (endmacro)
  (if) (elseif) (else) (endif)
  (foreach) (endforeach) (while) (endwhile)
  (block) (endblock)
] @keyword

(normal_command (identifier) @function)
((normal_command (identifier) @keyword)
  (#any-of? @keyword "break" "continue" "return" "BREAK" "CONTINUE" "RETURN"))

; The name of a function or macro definition.
(function_command (argument_list . (argument) @function))
(macro_command (argument_list . (argument) @function))

; Operators of conditions.
((argument) @keyword
  (#any-of? @keyword
    "NOT" "AND" "OR" "COMMAND" "POLICY" "TARGET" "TEST" "DEFINED" "IN_LIST"
    "EXISTS" "IS_NEWER_THAN" "IS_DIRECTORY" "IS_SYMLINK" "IS_ABSOLUTE"
    "IS_READABLE" "IS_WRITABLE" "IS_EXECUTABLE" "PATH_EQUAL" "MATCHES" "LESS"
    "GREATER" "EQUAL" "LESS_EQUAL" "GREATER_EQUAL" "STRLESS" "STRGREATER"
    "STREQUAL" "STRLESS_EQUAL" "STRGREATER_EQUAL" "VERSION_LESS"
    "VERSION_GREATER" "VERSION_EQUAL" "VERSION_LESS_EQUAL"
    "VERSION_GREATER_EQUAL"))

((unquoted_argument) @constant.builtin
  (#any-of? @constant.builtin "ON" "OFF" "TRUE" "FALSE" "YES" "NO"))

[(quoted_argument) (bracket_argument)] @string
(escape_sequence) @character

; Variables also inside strings.
(variable_ref) @constant

[(line_comment) (bracket_comment)] @comment
