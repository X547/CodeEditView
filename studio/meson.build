# The CodeEdit application: editor windows on documents of the workspace kit.
executable(
	'CodeEdit',
	files(
		'DocumentsWindow.cpp',
		'OpenGitWindow.cpp',
		'OpenLocationWindow.cpp',
		'SourceWindow.cpp',
		'StudioApp.cpp',
	),
	# BWindowStack is private API.
	include_directories: include_directories('/boot/system/develop/headers/private/interface'),
	dependencies: [workspaceKitDep, findKitDep, cpp.find_library('tracker')],
	install: true
)
