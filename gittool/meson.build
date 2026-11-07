# Git management tool: worktrees, branches, remotes and tags.

# Everything without UI, shared with the tests.
gitToolCore = static_library(
	'gittoolcore',
	files(
		'CommitDiff.cpp',
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

executable(
	'CodeEditGitTool',
	files(
		'CommitWindow.cpp',
		'Dialogs.cpp',
		'GitToolApp.cpp',
		'MainWindow.cpp',
		'NetworkTask.cpp',
	),
	# BColumnListView is private API (libcolumnlistview.a), as in the debugger.
	include_directories: include_directories('/boot/system/develop/headers/private/interface'),
	dependencies: [gitToolCoreDep, gitDiffUiDep, cpp.find_library('columnlistview')],
	install: true
)

if get_option('tests')
	gitToolTests = executable(
		'GitToolTests',
		files('tests/GitToolTest.cpp'),
		dependencies: [gitToolCoreDep, catch2Dep]
	)
	test('GitToolTests', gitToolTests)
endif
