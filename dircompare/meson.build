# Everything without UI, shared with the tests.
dirCompareCore = static_library(
	'dircomparecore',
	files(
		'Compare.cpp',
		'Comparison.cpp',
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

executable(
	'CodeEditDirCompare',
	files(
		'CompareWindow.cpp',
		'DirCompareApp.cpp',
		'DirTreeView.cpp',
		'OpenWindow.cpp',
	),
	# BColumnListView is private API (libcolumnlistview.a), as in the debugger.
	include_directories: include_directories('/boot/system/develop/headers/private/interface'),
	dependencies: [dirCompareCoreDep, findKitDep, cpp.find_library('columnlistview'), cpp.find_library('tracker')],
	install: true,
	install_dir: appInstallDir
)

if get_option('tests')
	dirCompareTests = executable(
		'DirCompareTests',
		files(
			'tests/ComparisonTest.cpp',
			'tests/FileOpsTest.cpp',
			'tests/ScannerTest.cpp',
		),
		dependencies: [dirCompareCoreDep, catch2Dep]
	)
	test('DirCompareTests', dirCompareTests)
endif
