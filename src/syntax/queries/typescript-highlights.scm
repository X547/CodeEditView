; TypeScript additions to ecma-highlights.scm, for TypeScript and TSX.

[
  "abstract" "accessor" "asserts" "declare" "enum" "global" "implements"
  "infer" "interface" "is" "keyof" "module" "namespace" "override" "private"
  "protected" "public" "readonly" "satisfies" "type"
] @keyword
(this_type) @keyword

(type_identifier) @type
(predefined_type) @type.builtin
; The qualifier of a type, such as JSX in JSX.Element, even in capitals.
(nested_type_identifier module: (identifier) @type)
(enum_declaration name: (identifier) @type)
(type_arguments ["<" ">"] @punctuation)
(type_parameters ["<" ">"] @punctuation)

(function_signature name: (identifier) @function)
(method_signature name: (property_identifier) @function)
(abstract_method_signature name: (property_identifier) @function)
(public_field_definition
  name: (property_identifier) @function
  value: [(function_expression) (arrow_function)])
