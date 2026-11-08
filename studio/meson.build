# The CodeEdit application: editor windows on documents of the workspace kit.
executable(
	'CodeEdit',
	files(
		'CoverageRun.cpp',
		'CoverageSummaryWindow.cpp',
		'DocumentsWindow.cpp',
		'OpenGitWindow.cpp',
		'OpenLocationWindow.cpp',
		'SourceWindow.cpp',
		'StudioApp.cpp',
		# The outline panel is the demo's.
		'../demo/OutlineView.cpp',
	),
	# BWindowStack is private API.
	include_directories: include_directories('/boot/system/develop/headers/private/interface', '../demo'),
	dependencies: [debugKitDep, coverageUiDep, findKitDep],
	install: true
)
