; JSX additions to ecma-highlights.scm, for JavaScript and TSX. Elements are
; styled like HTML; capitalized names are components, styled as types.

(jsx_opening_element ["<" ">"] @punctuation)
(jsx_closing_element ["</" ">"] @punctuation)
(jsx_self_closing_element ["<" "/>"] @punctuation)

(jsx_opening_element name: (identifier) @keyword (#match? @keyword "^[a-z]"))
(jsx_closing_element name: (identifier) @keyword (#match? @keyword "^[a-z]"))
(jsx_self_closing_element name: (identifier) @keyword (#match? @keyword "^[a-z]"))
(jsx_attribute (property_identifier) @type)
(html_character_reference) @character
