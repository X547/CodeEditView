libgit2Dep = dependency('libgit2', version: '>= 1.5')

# Everything without UI, shared with the tests.
gitDiffCore = static_library(
	'gitdiffcore',
	files(
		'Chunks.cpp',
		'FileDocument.cpp',
		'Git.cpp',
		'Repository.cpp',
	),
	dependencies: [codeEditViewDep, libgit2Dep]
)
gitDiffCoreDep = declare_dependency(
	link_with: gitDiffCore,
	include_directories: include_directories('.'),
	dependencies: [codeEditViewDep, libgit2Dep]
)

executable(
	'CodeEditGitDiff',
	files(
		'DiffView.cpp',
		'DiffWindow.cpp',
		'FileListView.cpp',
		'GitDiffApp.cpp',
		'LinkMapView.cpp',
	),
	dependencies: [gitDiffCoreDep, cpp.find_library('tracker')],
	install: true
)

if get_option('tests')
	gitDiffTests = executable(
		'GitDiffTests',
		files(
			'tests/ChunksTest.cpp',
			'tests/RepositoryTest.cpp',
		),
		dependencies: [gitDiffCoreDep, catch2Dep]
	)
	test('GitDiffTests', gitDiffTests)
endif
