" Vim syntax file
" Language: Property

if exists("b:current_syntax")
	finish
endif

syn region PropertyComment start=";" end=";"
syn match PropertyTodo "TODO:" containedin=PropertyComment

hi link PropertyComment Comment
hi link PropertyTodo Todo

syn keyword PropertyKeywords for if else in from to local const return this trigger break while

hi link PropertyKeywords Statement

syn match PropertyLabel "\h\w*\s*\ze:"

hi link PropertyLabel Label

syn match PropertyVariable "\.\s*\h\w*"

syn match PropertyChar "\v'.'"
syn match PropertySpecialChar "\v'\\([abefnrvt]|x[0-9a-fA-F][0-9a-fA-F])'"

hi link PropertyChar Number
hi link PropertySpecialChar SpecialChar

syn region PropertyString start=/"/ skip=/\\"/ end=/"/
syn match PropertyStringChar "\v\\([abefnrvt]|x[0-9a-fA-F][0-9a-fA-F])" contained containedin=PropertyString

hi link PropertyString String
hi link PropertyStringChar SpecialChar

syn match PropertyValue "\v(<array>)?\[.*\]"
syn match PropertyValue "\v<bool\s+(true|false)>"
syn match PropertyValue "\v<color>\s*(\h+|0x[a-fA-F0-9]+|0b[01]+|0c[0-7]+|[0-9]+)"
syn match PropertyValue "\v<font\s+default"
syn match PropertyValue "\v<function>\s*\{"
syn match PropertyValue "\v(<int>\s*)?(0x[a-fA-F0-9]+|0b[01]+|0c[0-7]+|[0-9]+)"
syn match PropertyValue "\v<float>\s*\.[0-9]+(e[+-]?[0-9]+)?"
syn match PropertyValue "\v<float>\s*[0-9]+\.(e[+-]?[0-9]+)?"
syn match PropertyValue "\v<float>\s*[0-9]+\.[0-9]+(e[+-]?[0-9]+)?"
syn match PropertyValue "\v<float>\s*[0-9]+(e[+-]?[0-9]+)?"

syn keyword PropertyValueTypes array bool color font function int float string containedin=PropertyValue
syn keyword PropertyValueSpecialTypes event point rect view containedin=PropertyValue
syn keyword PropertyValueKeywords default black white red green blue yellow cyan magenta gray orange purple brown pink olive teal navy false true containedin=PropertyValue
syn match PropertyValueInt "\v0x[a-fA-F0-9]+|0b[01]+|0c[0-7]+|[0-9]+" contained containedin=PropertyValue

hi link PropertyValueTypes Type
hi link PropertyValueSpecialTypes Macro
hi link PropertyValueKeywords Constant
hi link PropertyValueInt Number

hi link PropertyVariable cMember

syn match PropertyInvoke "\v<\h\k*(\()@="

hi link PropertyInvoke cFunction

syn match PropertyOperators "[{}[\],()=]"

hi link PropertyOperators Operator
