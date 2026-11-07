# Git management tool: worktrees, branches, remotes and tags.

# Everything without UI, shared with the tests.
gitToolCore = static_library(
	'gittoolcore',
	files(
		'Network.cpp',
		'Operations.cpp',
		'Plan.cpp',
		'Replay.cpp',
		'Snapshot.cpp',
	),
	dependencies: [diffKitDep]
)
gitToolCoreDep = declare_dependency(
	link_with: gitToolCore,
	include_directories: include_directories('.'),
	dependencies: [diffKitDep]
)

if get_option('tests')
	gitToolTests = executable(
		'GitToolTests',
		files('tests/GitToolTest.cpp'),
		dependencies: [gitToolCoreDep, catch2Dep]
	)
	test('GitToolTests', gitToolTests)
endif
