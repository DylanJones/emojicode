[
  (method)
  (initializer)
  (start_flag)
  (closure)
  (block)
] @local.scope

(parameter name: (variable) @local.definition.parameter)
(variable_declaration name: (variable) @local.definition.var)
(expression_statement declares: (variable) @local.definition.var)
(for_in element: (variable) @local.definition.var)
(error_handler binding: (variable) @local.definition.var)
(error_handler error: (variable) @local.definition.var)
(instance_variable name: (variable) @local.definition.field)

(primary_variable (variable) @local.reference)
