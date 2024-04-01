" Vim syntax file
" Language: Property

if exists("b:current_syntax")
	finish
endif

syn region PropertyComment start=";" end=";"
syn match PropertyTodo "TODO:" containedin=PropertyComment

hi link PropertyComment Comment
hi link PropertyTodo Todo

syn match PropertyIdentifier "\h\k*"

syn keyword PropertyKeywords for if else in from to local const return this trigger break while

hi link PropertyKeywords Statement

syn match PropertyLabel "\h\k*\s*\ze:"

hi link PropertyLabel Label

syn match PropertyVariable "\.\s*\h\k*"

syn match PropertyChar "'.'"
syn match PropertySpecialChar "\v'\\([abefnrvt]|x[0-9a-fA-F][0-9a-fA-F])'"

hi link PropertyChar Number
hi link PropertySpecialChar SpecialChar

syn region PropertyString start=/"/ skip=/\\"/ end=/"/
syn match PropertyStringChar "\v\\([abefnrvt]|x[0-9a-fA-F][0-9a-fA-F])" contained containedin=PropertyString

hi link PropertyString String
hi link PropertyStringChar SpecialChar

syn keyword PropertyValueTypes array bool color function int float string
syn keyword PropertyValueSpecialTypes event point rect view
syn keyword PropertyValueKeywords default black white red green blue yellow cyan magenta gray orange purple brown pink olive teal navy false true

syn match PropertyValueFloat "\v\.[0-9]+([Ee][+-]?[0-9]+)?"
syn match PropertyValueFloat "\v[0-9]+\.([Ee][+-]?[0-9]+)?"
syn match PropertyValueFloat "\v[0-9]+\.[0-9]+([Ee][+-]?[0-9]+)?"
syn match PropertyValueFloat "\v[0-9]+([Ee][+-]?[0-9]+)?"

syn match PropertyValueInt "\v(0x[a-fA-F0-9]+)|((0b[01]+)|(0c[0-7]+)|([0-9]+)([Ee][+-]?[0-9]+)?)"

hi link PropertyValueTypes Type
hi link PropertyValueSpecialTypes Macro
hi link PropertyValueKeywords Constant
hi link PropertyValueInt Number
hi link PropertyValueFloat Number

hi link PropertyVariable cMember

syn match PropertyInvoke "\<\h\k*\ze("

hi link PropertyInvoke cFunction

syn match PropertyOperators "[{}[\],()=:<>]"

hi link PropertyOperators Operator

