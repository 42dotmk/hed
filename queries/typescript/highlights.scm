;; TypeScript highlights — hed's enhanced defaults, loaded ahead of the
;; upstream grammar's queries (see plugins/treesitter/ts.h for the lookup
;; order). The upstream typescript/highlights.scm is a seven-pattern stub
;; that expects the JavaScript query to be appended; hed loads one file
;; per grammar, so this one covers the whole language.
;; Written nvim-treesitter style: on the same node a later pattern wins,
;; and an inner node wins over the outer one enclosing it — so the
;; catch-alls come first and the specific rules after.

;; ---------------------------------------------------------------------
;; Catch-alls
;; ---------------------------------------------------------------------

(identifier) @variable
(private_property_identifier) @property
(property_identifier) @property
(shorthand_property_identifier) @property
(shorthand_property_identifier_pattern) @variable
(statement_identifier) @label

;; ---------------------------------------------------------------------
;; Comments, literals
;; ---------------------------------------------------------------------

(comment) @comment

((comment) @comment.documentation
  (#match? @comment.documentation "^/\\*\\*[^*].*\\*/$"))

(hash_bang_line) @comment

(number) @number

[
  (string)
  (template_string)
  (template_literal_type)
] @string

(escape_sequence) @string.escape

(regex) @string.special

[
  (true)
  (false)
  (null)
  (undefined)
] @constant.builtin

(this) @variable.builtin
(super) @variable.builtin

((identifier) @variable.builtin
  (#any-of? @variable.builtin
    "arguments" "module" "console" "window" "document" "globalThis"
    "self" "process"))

((identifier) @constant.builtin
  (#any-of? @constant.builtin "NaN" "Infinity"))

((identifier) @function.builtin
  (#eq? @function.builtin "require"))

;; ---------------------------------------------------------------------
;; Naming conventions
;; ---------------------------------------------------------------------

((identifier) @type
  (#match? @type "^[A-Z]"))

((identifier) @constant
  (#match? @constant "^_*[A-Z][A-Z0-9_]*$"))

((shorthand_property_identifier) @constant
  (#match? @constant "^_*[A-Z][A-Z0-9_]*$"))

((identifier) @type.builtin
  (#any-of? @type.builtin
    "Object" "Function" "Boolean" "Symbol" "Number" "Math" "Date" "String"
    "RegExp" "Map" "Set" "WeakMap" "WeakSet" "WeakRef" "Promise" "Array"
    "ArrayBuffer" "DataView" "JSON" "Reflect" "Proxy" "Intl" "Error"
    "TypeError" "RangeError" "SyntaxError" "ReferenceError" "Int8Array"
    "Uint8Array" "Uint8ClampedArray" "Int16Array" "Uint16Array" "Int32Array"
    "Uint32Array" "Float32Array" "Float64Array" "BigInt64Array"
    "BigUint64Array" "BigInt"))

;; ---------------------------------------------------------------------
;; Types
;; ---------------------------------------------------------------------

(type_identifier) @type
(predefined_type) @type.builtin

(type_parameter
  name: (type_identifier) @type)

(infer_type
  (type_identifier) @type)

(index_signature
  name: (identifier) @variable.parameter)

(mapped_type_clause
  name: (type_identifier) @variable.parameter)

(enum_declaration
  name: (identifier) @type)

(enum_body
  name: (property_identifier) @constant)

(enum_assignment
  name: (property_identifier) @constant)

(internal_module
  name: (identifier) @module)

(module
  name: (identifier) @module)

(nested_identifier
  object: (identifier) @module)

(nested_type_identifier
  module: (identifier) @module)

(type_arguments
  "<" @punctuation.bracket
  ">" @punctuation.bracket)

(type_parameters
  "<" @punctuation.bracket
  ">" @punctuation.bracket)

;; ---------------------------------------------------------------------
;; Functions, methods, constructors
;; ---------------------------------------------------------------------

(function_declaration
  name: (identifier) @function)

(function_expression
  name: (identifier) @function)

(generator_function_declaration
  name: (identifier) @function)

(generator_function
  name: (identifier) @function)

(function_signature
  name: (identifier) @function)

(method_definition
  name: [(property_identifier) (private_property_identifier)] @function.method)

(method_signature
  name: (property_identifier) @function.method)

(abstract_method_signature
  name: (property_identifier) @function.method)

(method_definition
  name: (property_identifier) @constructor
  (#eq? @constructor "constructor"))

(pair
  key: (property_identifier) @function.method
  value: [(function_expression) (arrow_function)])

(assignment_expression
  left: (member_expression
    property: (property_identifier) @function.method)
  right: [(function_expression) (arrow_function)])

(variable_declarator
  name: (identifier) @function
  value: [(function_expression) (arrow_function)])

(assignment_expression
  left: (identifier) @function
  right: [(function_expression) (arrow_function)])

(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (member_expression
    property: [(property_identifier) (private_property_identifier)]
      @function.call))

(new_expression
  constructor: (identifier) @constructor)

(new_expression
  constructor: (member_expression
    property: (property_identifier) @constructor))

;; ---------------------------------------------------------------------
;; Parameters
;; ---------------------------------------------------------------------

(required_parameter
  pattern: (identifier) @variable.parameter)

(optional_parameter
  pattern: (identifier) @variable.parameter)

(required_parameter
  pattern: (rest_pattern (identifier) @variable.parameter))

(optional_parameter
  pattern: (rest_pattern (identifier) @variable.parameter))

(arrow_function
  parameter: (identifier) @variable.parameter)

(catch_clause
  parameter: (identifier) @variable.parameter)

;; ---------------------------------------------------------------------
;; Decorators
;; ---------------------------------------------------------------------

(decorator
  "@" @attribute
  (identifier) @attribute)

(decorator
  "@" @attribute
  (call_expression
    function: (identifier) @attribute))

(decorator
  "@" @attribute
  (member_expression
    (property_identifier) @attribute))

(decorator
  "@" @attribute
  (call_expression
    function: (member_expression
      (property_identifier) @attribute)))

;; ---------------------------------------------------------------------
;; Keywords
;; ---------------------------------------------------------------------

[
  "abstract"
  "accessor"
  "as"
  "asserts"
  "async"
  "await"
  "class"
  "const"
  "debugger"
  "declare"
  "default"
  "delete"
  "enum"
  "extends"
  "function"
  "get"
  "global"
  "implements"
  "in"
  "infer"
  "instanceof"
  "interface"
  "is"
  "keyof"
  "let"
  "module"
  "namespace"
  "new"
  "of"
  "override"
  "private"
  "protected"
  "public"
  "readonly"
  "satisfies"
  "set"
  "static"
  "target"
  "type"
  "typeof"
  "var"
  "void"
  "with"
  "yield"
] @keyword

(accessibility_modifier) @keyword
(override_modifier) @keyword

[
  "import"
  "export"
  "from"
  "require"
] @include

[
  "if"
  "else"
  "switch"
  "case"
] @conditional

(ternary_expression
  ["?" ":"] @conditional)

(conditional_type
  ["?" ":"] @conditional)

[
  "for"
  "while"
  "do"
  "break"
  "continue"
] @repeat

"return" @keyword.return

[
  "try"
  "catch"
  "finally"
  "throw"
] @exception

;; ---------------------------------------------------------------------
;; Operators, punctuation
;; ---------------------------------------------------------------------

[
  "-" "--" "-=" "+" "++" "+=" "*" "*=" "**" "**=" "/" "/=" "%" "%="
  "<" "<=" "<<" "<<=" "=" "==" "===" "!" "!=" "!==" "=>" ">" ">=" ">>"
  ">>=" ">>>" ">>>=" "~" "^" "&" "|" "^=" "&=" "|=" "&&" "||" "??"
  "&&=" "||=" "??=" "..."
] @operator

(non_null_expression "!" @operator)
(optional_parameter "?" @operator)
(property_signature "?" @operator)
(public_field_definition "?" @operator)
(method_signature "?" @operator)
(optional_type "?" @operator)
(rest_type "..." @operator)
(union_type "|" @operator)
(intersection_type "&" @operator)
(function_type "=>" @operator)
(type_predicate "is" @keyword)

[
  ";"
  "."
  ","
  ":"
  (optional_chain)
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

(template_substitution
  "${" @punctuation.special
  "}" @punctuation.special)

(template_type
  "${" @punctuation.special
  "}" @punctuation.special)
