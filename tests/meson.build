codeEditTests = executable(
	'CodeEditTests',
	files(
		'EditorCommandsTest.cpp',
		'EditorCoreTest.cpp',
		'FoldTest.cpp',
		'LineLayoutTest.cpp',
		'OutlineTest.cpp',
		'RopeTest.cpp',
		'RowIndexTest.cpp',
		'ScrollTest.cpp',
		'SyntaxTest.cpp',
		'TextCommandsTest.cpp',
		'TextModelTest.cpp',
		'UnicodeTest.cpp',
		'VersionTest.cpp',
		'WrapTest.cpp',
	),
	include_directories: [privateInc],
	dependencies: [codeEditViewDep, treeSitterDep, catch2Dep]
)

test('CodeEditTests', codeEditTests, timeout: 120)
