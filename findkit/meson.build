# Find and replace for code editor views: search engine (PCRE2), find
# commands and the find panel.
pcre2Dep = dependency('libpcre2-8', version: '>= 10.34')

findKit = static_library(
	'findkit',
	files(
		'FindCommands.cpp',
		'FindPanel.cpp',
		'FindState.cpp',
		'GoToLinePanel.cpp',
		'ModelSearch.cpp',
		'Search.cpp',
	),
	dependencies: [codeEditViewDep, pcre2Dep]
)
findKitDep = declare_dependency(
	link_with: findKit,
	include_directories: include_directories('.'),
	dependencies: [codeEditViewDep]
)

if get_option('tests')
	findKitTests = executable(
		'FindKitTests',
		files(
			'tests/GoToLineTest.cpp',
			'tests/ModelSearchTest.cpp',
			'tests/SearchTest.cpp',
		),
		dependencies: [findKitDep, catch2Dep]
	)
	test('FindKitTests', findKitTests)
endif
