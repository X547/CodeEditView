# Viewer of LLVM source-based coverage (llvm-cov export).

# Everything without UI, shared with the tests.
coverageCore = static_library(
	'coveragecore',
	files(
		'Coverage.cpp',
		'LlvmCov.cpp',
	),
	dependencies: [codeEditViewDep, jsonDep]
)
coverageCoreDep = declare_dependency(
	link_with: coverageCore,
	include_directories: include_directories('.'),
	dependencies: [codeEditViewDep, jsonDep]
)

# The file list and the open window, shared with the CodeEdit app.
# BColumnListView is private API (libcolumnlistview.a), as in the debugger.
coverageUiInc = include_directories('/boot/system/develop/headers/private/interface')
coverageUi = static_library(
	'coverageui',
	files(
		'FileListView.cpp',
		'OpenWindow.cpp',
	),
	include_directories: coverageUiInc,
	dependencies: [coverageCoreDep]
)
coverageUiDep = declare_dependency(
	link_with: coverageUi,
	include_directories: coverageUiInc,
	dependencies: [coverageCoreDep, cpp.find_library('columnlistview'), cpp.find_library('tracker')]
)

if get_option('coverage')
	executable(
		'CodeEditCoverage',
		files(
			'CoverageApp.cpp',
			'CoverageWindow.cpp',
		),
		dependencies: [coverageUiDep, findKitDep],
		install: true
	)
endif

if get_option('tests')
	coverageTests = executable(
		'CoverageTests',
		files('tests/CoverageTest.cpp'),
		dependencies: [coverageCoreDep, catch2Dep]
	)
	test('CoverageTests', coverageTests)
endif
