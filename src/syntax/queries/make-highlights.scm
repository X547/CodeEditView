; Make highlighting. A later pattern overrides an earlier one for the same
; text; nested nodes override their parents.

[":" "&:" "::" "|" ";" "," "(" ")" "{" "}"] @punctuation

["=" ":=" "::=" "?=" "+=" "!="] @operator

; Recipe prefixes.
(recipe_line ["@" "-" "+"] @operator)

[
  "ifeq" "ifneq" "ifdef" "ifndef" "else" "endif"
  "define" "endef" "vpath" "undefine" "export" "unexport" "override" "private"
  "include" "sinclude" "-include"
] @keyword

(function_call function: _ @function)
(shell_function function: _ @function)
(function_call ["$" "$$" "(" ")"] @function)
(shell_function ["$" "$$" "(" ")"] @function)

; Variables named like functions, such as $(dir) or $(file).
(function_call ["$" "$$"] @constant . "(" @constant . function: _ @constant . ")" @constant)

[(variable_reference) (substitution_reference) (automatic_variable)] @constant
(variable_reference ["$" "$$" "(" ")" "{" "}"] @constant)
(substitution_reference ["$" "$$" "(" ")" "{" "}" ":" "="] @constant)
(automatic_variable ["$" "$$" "(" ")"] @constant)

(targets (word) @function)
((targets (word) @keyword)
  (#any-of? @keyword
    ".PHONY" ".SUFFIXES" ".DEFAULT" ".PRECIOUS" ".INTERMEDIATE" ".NOTINTERMEDIATE"
    ".SECONDARY" ".SECONDEXPANSION" ".DELETE_ON_ERROR" ".IGNORE"
    ".LOW_RESOLUTION_TIME" ".SILENT" ".EXPORT_ALL_VARIABLES" ".NOTPARALLEL"
    ".ONESHELL" ".POSIX"))

(string) @string

(comment) @comment
