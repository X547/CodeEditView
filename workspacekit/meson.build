# Documents addressed by locators (local files, Git commits, SFTP), their
# providers and the mapping of tool paths, for the CodeEdit app.
workspaceKit = static_library(
	'workspacekit',
	files(
		'Document.cpp',
		'DocumentWatcher.cpp',
		'FileProvider.cpp',
		'GitProvider.cpp',
		'JobQueue.cpp',
		'Locator.cpp',
		'PathMapper.cpp',
		'Provider.cpp',
	),
	dependencies: [diffKitDep]
)
workspaceKitDep = declare_dependency(
	link_with: workspaceKit,
	include_directories: include_directories('.'),
	dependencies: [diffKitDep]
)

if get_option('tests')
	workspaceKitTests = executable(
		'WorkspaceKitTests',
		files(
			'tests/DocumentTest.cpp',
			'tests/LocatorTest.cpp',
			'tests/PathMapperTest.cpp',
			'tests/ProviderTest.cpp',
		),
		dependencies: [workspaceKitDep, catch2Dep]
	)
	test('WorkspaceKitTests', workspaceKitTests)
endif
