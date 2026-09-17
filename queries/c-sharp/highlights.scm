;; C# highlights — hed's enhanced defaults, loaded ahead of the upstream
;; grammar's queries (see plugins/treesitter/ts.h for the lookup order).
;; Written nvim-treesitter style: on the same node a later pattern wins,
;; and an inner node wins over the outer one enclosing it — so the
;; catch-alls come first and the specific rules after.

;; ---------------------------------------------------------------------
;; Catch-alls
;; ---------------------------------------------------------------------

(identifier) @variable
(discard) @variable

;; ---------------------------------------------------------------------
;; Comments, literals
;; ---------------------------------------------------------------------

(comment) @comment

[
  (integer_literal)
  (real_literal)
] @number

[
  (string_literal)
  (raw_string_literal)
  (verbatim_string_literal)
  (character_literal)
  (interpolated_string_expression)
  (interpolation_start)
  (interpolation_quote)
] @string

(escape_sequence) @string.escape

[
  (boolean_literal)
  (null_literal)
] @constant.builtin

;; Interpolation holes: the braces are punctuation and the expression
;; inside gets its normal colours back (inner node wins over the
;; enclosing @string).
(interpolation_brace) @punctuation.bracket
(interpolation (identifier) @variable)

;; ---------------------------------------------------------------------
;; Members: properties, fields, events, enum members
;; ---------------------------------------------------------------------

(property_declaration name: (identifier) @property)
(event_declaration name: (identifier) @property)
(field_declaration
  (variable_declaration
    (variable_declarator name: (identifier) @property)))
(event_field_declaration
  (variable_declaration
    (variable_declarator name: (identifier) @property)))
(enum_member_declaration name: (identifier) @constant)

;; Anything reached through a dot / ?. is a member (property or field);
;; invocations below re-tag the method ones.
(member_access_expression name: (identifier) @property)
(member_binding_expression name: (identifier) @property)

;; Object / with initializers: `new Foo { Bar = 1 }`
(initializer_expression
  (assignment_expression left: (identifier) @property))
(with_initializer (identifier) @property)

;; Property patterns: `is { Length: > 0 }`
(subpattern (identifier) @property)

;; Named arguments: `Foo(name: 1)`
(argument name: (identifier) @variable.parameter)

;; Accessor names — get/set/init/add/remove are keywords.
(accessor_declaration name: _ @keyword)

;; ---------------------------------------------------------------------
;; Functions
;; ---------------------------------------------------------------------

(method_declaration name: (identifier) @function)
(local_function_statement name: (identifier) @function)
(delegate_declaration name: (identifier) @type)
(constructor_declaration name: (identifier) @constructor)
(destructor_declaration name: (identifier) @constructor)
(operator_declaration operator: _ @function)

(invocation_expression
  function: (identifier) @function.call)
(invocation_expression
  function: (generic_name (identifier) @function.call))
(invocation_expression
  function: (member_access_expression name: (identifier) @function.call))
(invocation_expression
  function: (member_access_expression
    name: (generic_name (identifier) @function.call)))
(invocation_expression
  function: (conditional_access_expression
    (member_binding_expression name: (identifier) @function.call)))
(invocation_expression
  function: (conditional_access_expression
    (member_binding_expression
      name: (generic_name (identifier) @function.call))))

;; Lambda parameters
(lambda_expression (implicit_parameter) @variable.parameter)
(lambda_expression
  parameters: (implicit_parameter) @variable.parameter)

;; ---------------------------------------------------------------------
;; Types
;; ---------------------------------------------------------------------

(predefined_type) @type.builtin
(implicit_type) @keyword

(class_declaration name: (identifier) @type)
(struct_declaration name: (identifier) @type)
(interface_declaration name: (identifier) @type)
(enum_declaration name: (identifier) @type)
(record_declaration name: (identifier) @type)
(type_parameter name: (identifier) @type)
(type_parameter_constraints_clause (identifier) @type)
(type_parameter_constraint type: (identifier) @type)

(base_list (identifier) @type)
(primary_constructor_base_type type: (identifier) @type)
(generic_name (identifier) @type)
(type_argument_list (identifier) @type)
(qualified_name (identifier) @type)
(alias_qualified_name (identifier) @type)
(array_type type: (identifier) @type)
(nullable_type type: (identifier) @type)
(pointer_type type: (identifier) @type)
(ref_type type: (identifier) @type)
(tuple_element type: (identifier) @type)
(as_expression right: (identifier) @type)
(is_expression right: (identifier) @type)
(_ type: (identifier) @type)
(_ returns: (identifier) @type)
(explicit_interface_specifier (identifier) @type)

;; Static / namespace access: `Console.WriteLine`, `Math.PI`. A
;; PascalCase identifier at the head of a member chain is a type or
;; namespace far more often than a local.
((member_access_expression
   expression: (identifier) @type)
 (#match? @type "^[A-Z]"))
((member_access_expression
   expression: (member_access_expression
     name: (identifier) @type))
 (#match? @type "^[A-Z]"))

;; ---------------------------------------------------------------------
;; Namespaces / usings
;; ---------------------------------------------------------------------

(namespace_declaration name: (identifier) @module)
(namespace_declaration name: (qualified_name (identifier) @module))
(file_scoped_namespace_declaration name: (identifier) @module)
(file_scoped_namespace_declaration
  name: (qualified_name (identifier) @module))
(using_directive (identifier) @module)
(using_directive (qualified_name (identifier) @module))
(using_directive name: (identifier) @type)
(extern_alias_directive (identifier) @module)

;; ---------------------------------------------------------------------
;; Attributes: `[HttpGet("/x")]`, `[Fact]`, `[return: NotNull]`
;; ---------------------------------------------------------------------

(attribute name: (identifier) @attribute)
(attribute name: (qualified_name (identifier) @attribute))
(attribute name: (generic_name (identifier) @attribute))
(attribute_target_specifier) @keyword
(attribute_argument name: (identifier) @variable.parameter)

;; ---------------------------------------------------------------------
;; Labels
;; ---------------------------------------------------------------------

(labeled_statement (identifier) @label)
(goto_statement (identifier) @label)

;; ---------------------------------------------------------------------
;; Parameters and locals
;; ---------------------------------------------------------------------

(parameter name: (identifier) @variable.parameter)
(catch_declaration name: (identifier) @variable)
(declaration_pattern name: (identifier) @variable)

;; ---------------------------------------------------------------------
;; Keywords
;; ---------------------------------------------------------------------

(modifier) @keyword
[
  "this"
  "base"
] @keyword

[
  "add"
  "alias"
  "and"
  "as"
  "async"
  "await"
  "checked"
  "class"
  "delegate"
  "enum"
  "event"
  "explicit"
  "extern"
  "fixed"
  "get"
  "global"
  "implicit"
  "init"
  "interface"
  "is"
  "lock"
  "managed"
  "namespace"
  "new"
  "not"
  "notnull"
  "operator"
  "or"
  "out"
  "params"
  "record"
  "ref"
  "remove"
  "scoped"
  "set"
  "sizeof"
  "stackalloc"
  "struct"
  "typeof"
  "unchecked"
  "unmanaged"
  "unsafe"
  "using"
  "var"
  "when"
  "with"
  "yield"
] @keyword

[
  "from"
  "where"
  "select"
  "let"
  "join"
  "into"
  "on"
  "equals"
  "orderby"
  "ascending"
  "descending"
  "group"
  "by"
] @keyword

[
  "if"
  "else"
  "switch"
  "case"
  "default"
  "break"
  "continue"
  "return"
  "goto"
] @conditional

[
  "for"
  "foreach"
  "while"
  "do"
  "in"
] @repeat

[
  "try"
  "catch"
  "finally"
  "throw"
] @exception

;; Preprocessor
[
  "#if"
  "#elif"
  "#else"
  "#endif"
  "#define"
  "#undef"
  "#region"
  "#endregion"
  "#error"
  "#warning"
  "#line"
  "#pragma"
  "#nullable"
] @keyword
(preproc_arg) @comment
(shebang_directive) @comment

;; ---------------------------------------------------------------------
;; Operators, punctuation
;; ---------------------------------------------------------------------

[
  "--"
  "-"
  "-="
  "&"
  "&="
  "&&"
  "+"
  "++"
  "+="
  "<"
  "<="
  "<<"
  "<<="
  "="
  "=="
  "!"
  "!="
  "=>"
  ">"
  ">="
  ">>"
  ">>="
  ">>>"
  ">>>="
  "|"
  "|="
  "||"
  "?"
  "??"
  "??="
  "^"
  "^="
  "~"
  "*"
  "*="
  "/"
  "/="
  "%"
  "%="
  ".."
  "->"
] @operator

[
  ";"
  "."
  ","
  ":"
  "::"
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

(type_argument_list ["<" ">"] @punctuation.bracket)
(type_parameter_list ["<" ">"] @punctuation.bracket)
(nullable_type "?" @punctuation.special)

;; ---------------------------------------------------------------------
;; Constants — last so they win over the member/field rules above
;; ---------------------------------------------------------------------

((field_declaration
   (modifier) @_mod
   (variable_declaration
     (variable_declarator name: (identifier) @constant)))
 (#eq? @_mod "const"))
((local_declaration_statement
   (modifier) @_mod
   (variable_declaration
     (variable_declarator name: (identifier) @constant)))
 (#eq? @_mod "const"))
((identifier) @constant
 (#match? @constant "^[A-Z][A-Z0-9_]+$"))
