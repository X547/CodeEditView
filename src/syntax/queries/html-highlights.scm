; HTML highlighting. A later pattern overrides an earlier one for the same
; text. The text of <script> and <style> elements is not parsed.

["<" ">" "</" "/>" "<!"] @punctuation
"=" @operator

(tag_name) @keyword
(erroneous_end_tag_name) @error
(attribute_name) @type
[(attribute_value) (quoted_attribute_value)] @string
(entity) @character
(doctype) @preprocessor

(comment) @comment
