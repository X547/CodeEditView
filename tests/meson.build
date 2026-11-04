catch2Dep = dependency('catch2-with-main', version: '>= 3.0')

codeEditTests = executable(
	'CodeEditTests',
	files(
		'DapTest.cpp',
		'EditorCoreTest.cpp',
		'FoldTest.cpp',
		'LineLayoutTest.cpp',
		'OutlineTest.cpp',
		'RopeTest.cpp',
		'RowIndexTest.cpp',
		'SyntaxTest.cpp',
		'TextModelTest.cpp',
		'UnicodeTest.cpp',
		'VersionTest.cpp',
		'WrapTest.cpp',
		'../debugger/DapClient.cpp',
		'../debugger/Json.cpp',
	),
	include_directories: [privateInc, include_directories('../debugger')],
	dependencies: [codeEditViewDep, treeSitterDep, catch2Dep]
)

test('CodeEditTests', codeEditTests)
