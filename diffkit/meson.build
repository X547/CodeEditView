# Diff UI shared by the diff apps: chunks (libgit2's diff of two buffers),
# text sides, the two-pane DiffView and its link map.
libgit2Dep = dependency('libgit2', version: '>= 1.5')

diffKit = static_library(
	'diffkit',
	files(
		'Ask.cpp',
		'Chunks.cpp',
		'CodeEditLink.cpp',
		'DiffView.cpp',
		'FileIO.cpp',
		'FreezeWatchdog.cpp',
		'Git.cpp',
		'LinkMapView.cpp',
		'NameOrder.cpp',
		'Progress.cpp',
		'TextSide.cpp',
		'TruncatingLabel.cpp',
		'WindowHelpers.cpp',
	),
	dependencies: [codeEditViewDep, libgit2Dep]
)
diffKitDep = declare_dependency(
	link_with: diffKit,
	include_directories: include_directories('.'),
	dependencies: [codeEditViewDep, libgit2Dep]
)

if get_option('tests')
	diffKitTests = executable(
		'DiffKitTests',
		files(
			'tests/ChunksTest.cpp',
			'tests/TextSideTest.cpp',
		),
		dependencies: [diffKitDep, catch2Dep]
	)
	test('DiffKitTests', diffKitTests)
endif
