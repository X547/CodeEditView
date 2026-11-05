# Everything without UI, shared with the tests.
dirCompareCore = static_library(
	'dircomparecore',
	files(
		'Compare.cpp',
		'FileOps.cpp',
		'FilePair.cpp',
		'Scanner.cpp',
	),
	dependencies: [diffKitDep]
)
dirCompareCoreDep = declare_dependency(
	link_with: dirCompareCore,
	include_directories: include_directories('.'),
	dependencies: [diffKitDep]
)

if get_option('tests')
	dirCompareTests = executable(
		'DirCompareTests',
		files(
			'tests/FileOpsTest.cpp',
			'tests/ScannerTest.cpp',
		),
		dependencies: [dirCompareCoreDep, catch2Dep]
	)
	test('DirCompareTests', dirCompareTests)
endif
