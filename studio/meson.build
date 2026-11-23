# The CodeEdit application: editor windows on documents of the workspace kit.

executable(
	'CodeEdit',
	files(
		'CoverageRun.cpp',
		'CoverageSummaryWindow.cpp',
		'DocumentsWindow.cpp',
		'EntryIcons.cpp',
		'FileOptionsWindow.cpp',
		'LazyMenu.cpp',
		'LocatorFolderMenu.cpp',
		'LocatorPanel.cpp',
		'OpenGitWindow.cpp',
		'OpenLocationWindow.cpp',
		'ParentFoldersMenu.cpp',
		'SourceWindow.cpp',
		'StartSessionWindow.cpp',
		'StudioApp.cpp',
		'ToolBar.cpp',
		'ToolBarMenus.cpp',
		# The outline panel is the demo's.
		'../demo/OutlineView.cpp',
		# Signature, single launch, file types and tool bar icons.
		'CodeEdit.rdef',
	),
	# BWindowStack is private API.
	include_directories: include_directories('/boot/system/develop/headers/private/interface', '../demo'),
	dependencies: [debugKitDep, coverageUiDep, findKitDep],
	install: true,
	install_dir: appInstallDir
)

# The executable gets its resources when it is linked and again when it is
# installed. Installing into the system (no DESTDIR) also enters it into the
# MIME database, which registers its file types.
meson.add_install_script(
	find_program('mimeset'),
	'--apps',
	get_option('prefix') / appInstallDir / 'CodeEdit',
	skip_if_destdir: true
)
