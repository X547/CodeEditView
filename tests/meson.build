catch2Dep = dependency('catch2-with-main', version: '>= 3.0')

codeEditTests = executable(
	'CodeEditTests',
	files(
		'RopeTest.cpp',
		'TextModelTest.cpp',
		'VersionTest.cpp',
	),
	include_directories: [privateInc],
	dependencies: [codeEditViewDep, treeSitterDep, catch2Dep]
)

test('CodeEditTests', codeEditTests)
