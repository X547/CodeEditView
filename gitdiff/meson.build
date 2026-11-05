# Everything without UI, shared with the tests.
gitDiffCore = static_library(
	'gitdiffcore',
	files(
		'FileDocument.cpp',
		'Repository.cpp',
	),
	dependencies: [diffKitDep]
)
gitDiffCoreDep = declare_dependency(
	link_with: gitDiffCore,
	include_directories: include_directories('.'),
	dependencies: [diffKitDep]
)

executable(
	'CodeEditGitDiff',
	files(
		'DiffWindow.cpp',
		'FileListView.cpp',
		'GitDiffApp.cpp',
	),
	dependencies: [gitDiffCoreDep, cpp.find_library('tracker')],
	install: true
)

if get_option('tests')
	gitDiffTests = executable(
		'GitDiffTests',
		files(
			'tests/RepositoryTest.cpp',
		),
		dependencies: [gitDiffCoreDep, catch2Dep]
	)
	test('GitDiffTests', gitDiffTests)
endif
