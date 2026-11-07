# Everything without UI, shared with the tests.
gitDiffCore = static_library(
	'gitdiffcore',
	files(
		'CommitMessage.cpp',
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

# The diff window, shared with the Git tool, which shows it in its own
# process.
gitDiffUi = static_library(
	'gitdiffui',
	files(
		'DiffWindow.cpp',
		'FileListView.cpp',
	),
	dependencies: [gitDiffCoreDep]
)
gitDiffUiDep = declare_dependency(
	link_with: gitDiffUi,
	dependencies: [gitDiffCoreDep, cpp.find_library('tracker')]
)

if get_option('gitdiff')
	executable(
		'CodeEditGitDiff',
		files(
			'GitDiffApp.cpp',
		),
		dependencies: [gitDiffUiDep],
		install: true
	)
endif

if get_option('tests')
	gitDiffTests = executable(
		'GitDiffTests',
		files(
			'tests/CommitMessageTest.cpp',
			'tests/RepositoryTest.cpp',
		),
		dependencies: [gitDiffCoreDep, catch2Dep]
	)
	test('GitDiffTests', gitDiffTests)
endif
