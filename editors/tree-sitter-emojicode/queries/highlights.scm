; Comments and literals

(comment) @comment @spell
(documentation_comment) @comment.documentation @spell

(string) @string
(escape_sequence) @string.escape
(interpolation "🧲" @punctuation.special)
(number) @number
(boolean) @boolean
(no_value) @constant.builtin

; Types

(type_identifier name: (identifier) @type)
(type_identifier namespace: (identifier) @module)
"🔶" @punctuation.delimiter
(type_variable) @type
[(someobject_type) (no_return_type) (something_type)] @type.builtin

(class_definition name: (type_identifier name: (identifier) @type.definition))
(value_type_definition name: (type_identifier name: (identifier) @type.definition))
(enum_definition name: (type_identifier name: (identifier) @type.definition))
(protocol_definition name: (type_identifier name: (identifier) @type.definition))
(generic_parameter name: (variable) @type.definition)

; Functions

(method name: (identifier) @function.method)
(method name: (operator) @operator)
(method mood: _ @keyword.function)
(initializer name: (identifier) @constructor)
(initializer "🆕" @keyword.function)
(instantiation name: (identifier) @constructor)
(method_call name: (identifier) @function.method.call)
(super_call name: (identifier) @function.method.call)
(mood) @punctuation.delimiter

; Variables

(variable) @variable
(parameter name: (variable) @variable.parameter)
(instance_variable name: (variable) @variable.member)
(this) @variable.builtin
"⤴" @variable.builtin
(package_import package: (variable) @module namespace: (identifier) @module)

; Keywords

["🐇" "🕊" "🔘" "🐊"] @keyword.type
["↪" "🙅" "🙅↪"] @keyword.conditional
["🔁" "🔂"] @keyword.repeat
"↩" @keyword.return
["🚨" "🆗"] @keyword.exception
["📦" "📜"] @keyword.import
["🏁" "🔗" "🆕" "♻" "🖍"] @keyword
["☣" "🔏" "🌍" "🥯" "⚠" "✒" "🔑" "🍼" "📻"] @keyword.modifier
(access_level) @keyword.modifier
(decorator) @attribute
(branch_hint) @attribute

; Operators and punctuation

(operator) @operator
["🍺" "🔺" "🔲" "⚖" "🏮" "📣" "⁉" "➡" "⬅" "🍬" "✴" "⬛" "⚫" "🚧" "🍱" "▶"] @operator
["🍇" "🍉" "🤜" "🤛" "🍿" "🍆" "🐚"] @punctuation.bracket
