# Viewer of LLVM source-based coverage (llvm-cov export).

# Everything without UI, shared with the tests. The JSON parser is the
# debugger's.
coverageCore = static_library(
	'coveragecore',
	files(
		'Coverage.cpp',
		'LlvmCov.cpp',
		'../debugger/Json.cpp',
	),
	include_directories: include_directories('../debugger'),
	dependencies: [codeEditViewDep]
)
coverageCoreDep = declare_dependency(
	link_with: coverageCore,
	include_directories: include_directories('.', '../debugger'),
	dependencies: [codeEditViewDep]
)

executable(
	'CodeEditCoverage',
	files(
		'CoverageApp.cpp',
		'CoverageWindow.cpp',
		'FileListView.cpp',
		'OpenWindow.cpp',
	),
	# BColumnListView is private API (libcolumnlistview.a), as in the debugger.
	include_directories: include_directories('/boot/system/develop/headers/private/interface'),
	dependencies: [coverageCoreDep, findKitDep, cpp.find_library('columnlistview'), cpp.find_library('tracker')],
	install: true
)

if get_option('tests')
	coverageTests = executable(
		'CoverageTests',
		files('tests/CoverageTest.cpp'),
		dependencies: [coverageCoreDep, catch2Dep]
	)
	test('CoverageTests', coverageTests)
endif
