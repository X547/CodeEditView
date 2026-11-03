; C++ additions, appended to the C highlighting.

"::" @operator

(namespace_identifier) @type
(auto) @type.builtin

[
  "catch" "class" "co_await" "co_return" "co_yield" "concept" "consteval"
  "constexpr" "constinit" "decltype" "delete" "explicit" "final" "friend"
  "mutable" "namespace" "new" "noexcept" "operator" "override" "private"
  "protected" "public" "requires" "static_assert" "template" "throw" "try"
  "typename" "using" "virtual"
] @keyword

; Pe colors the C++ casts as keywords; the grammar parses them as names.
((identifier) @keyword
  (#any-of? @keyword "static_cast" "dynamic_cast" "const_cast" "reinterpret_cast"))

(this) @constant.builtin
(null "nullptr" @constant.builtin)

(raw_string_literal) @string

(comment) @comment
