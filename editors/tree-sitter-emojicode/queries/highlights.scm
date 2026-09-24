; Neovim gives the pattern that comes later precedence when captures overlap, so general captures come first and the
; more specific ones follow.

; Comments and literals

(comment) @comment @spell
(documentation_comment) @comment.documentation @spell

(string) @string
(escape_sequence) @string.escape
(interpolation "🧲" @punctuation.special)
(number) @number
(boolean) @boolean
(no_value) @constant.builtin

; General captures

(variable) @variable
(operator) @operator
(mood) @punctuation.delimiter

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

["🍺" "🔺" "🔲" "⚖" "🏮" "📣" "⁉" "➡" "⬅" "🍬" "✴" "⬛" "⚫" "🚧" "🍱" "▶"] @operator
["🍇" "🍉" "🤜" "🤛" "🍿" "🍆" "🐚"] @punctuation.bracket
"🔶" @punctuation.delimiter

; Types

(type_identifier name: (identifier) @type)
(type_identifier namespace: (identifier) @module)
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

; Variables

(parameter name: (variable) @variable.parameter)
(instance_variable name: (variable) @variable.member)
(this) @variable.builtin
"⤴" @variable.builtin
(package_import package: (variable) @module namespace: (identifier) @module)
