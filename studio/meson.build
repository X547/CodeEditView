# The CodeEdit application: editor windows on documents of the workspace kit.

codeEdit = executable(
	'CodeEdit',
	files(
		'CoverageRun.cpp',
		'CoverageSummaryWindow.cpp',
		'DocumentsWindow.cpp',
		'LazyMenu.cpp',
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
	),
	# BWindowStack is private API.
	include_directories: include_directories('/boot/system/develop/headers/private/interface', '../demo'),
	dependencies: [debugKitDep, coverageUiDep, findKitDep],
	install: true,
	install_dir: appInstallDir
)

# Signature, single launch, file types and tool bar icons; the built and the
# installed executable get them, and installing into the system registers
# the file types.
rc = find_program('rc', required: false)
xres = find_program('xres', required: false)
mimeset = find_program('mimeset', required: false)
if rc.found() and xres.found() and mimeset.found()
	codeEditResources = custom_target(
		'CodeEdit.rsrc',
		input: 'CodeEdit.rdef',
		output: 'CodeEdit.rsrc',
		command: [rc, '-I', meson.current_source_dir(), '-o', '@OUTPUT@', '@INPUT@'],
		depend_files: files(
			'icons/documents.hvif',
			'icons/find-again.hvif',
			'icons/find.hvif',
			'icons/functions.hvif',
			'icons/headers.hvif',
			'icons/lock.hvif',
			'icons/new.hvif',
			'icons/open.hvif',
			'icons/outline.hvif',
			'icons/save.hvif',
		),
		build_by_default: true
	)
	# Linking writes the executable without them.
	custom_target(
		'CodeEdit.resources.stamp',
		input: [codeEdit, codeEditResources],
		output: 'CodeEdit.resources.stamp',
		command: [python, files('../tools/add_resources.py'), xres, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
		build_by_default: true
	)
	meson.add_install_script(
		python,
		files('../tools/install_resources.py'),
		xres.full_path(),
		mimeset.full_path(),
		codeEditResources,
		appInstallDir / 'CodeEdit'
	)
else
	warning('rc, xres or mimeset not found: CodeEdit is built without file types and tool bar icons')
endif
