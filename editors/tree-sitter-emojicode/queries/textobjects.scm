(method body: (_) @function.inner) @function.outer
(initializer body: (_) @function.inner) @function.outer
(deinitializer body: (_) @function.inner) @function.outer
(start_flag body: (_) @function.inner) @function.outer
(closure) @function.outer

(class_definition body: (_) @class.inner) @class.outer
(value_type_definition body: (_) @class.inner) @class.outer
(enum_definition body: (_) @class.inner) @class.outer
(protocol_definition body: (_) @class.inner) @class.outer

(if_statement) @conditional.outer
(if_statement consequence: (_) @conditional.inner)
[(for_in) (repeat_while)] @loop.outer
(for_in body: (_) @loop.inner)
(repeat_while body: (_) @loop.inner)

(parameter) @parameter.outer
(parameter) @parameter.inner

[(method_call) (instantiation) (super_call) (callable_call)] @call.outer

[(comment) (documentation_comment)] @comment.outer

(block) @block.outer
