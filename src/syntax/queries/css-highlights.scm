; CSS highlighting. A later pattern overrides an earlier one for the same
; text. Selectors are styled like the HTML they select: tags as keywords,
; classes, IDs and attributes as types.

["," ";" ":" "::" "." "#" "(" ")" "[" "]" "{" "}"] @punctuation
[">" "+" "~" "-" "*" "/" "=" "^=" "|=" "~=" "$=" "*=" "|"] @operator
["and" "or" "not" "only" "of" "selector"] @keyword

[
  "@media" "@import" "@charset" "@namespace" "@supports" "@keyframes" "@scope"
  (at_keyword)
] @preprocessor

[(tag_name) (nesting_selector) (universal_selector)] @keyword
[(class_name) (id_name) (namespace_name) (attribute_name)] @type
(pseudo_class_selector (class_name) @constant)
(pseudo_element_selector (tag_name) @constant)
(attribute_selector (plain_value) @string)
(keyframes_name) @type
[(from) (to)] @keyword

(property_name) @function
(feature_name) @function
(function_name) @function
(important) @keyword

[(integer_value) (float_value)] @number
(color_value) @number
(string_value) @string
(escape_sequence) @character

[(comment) (js_comment)] @comment
