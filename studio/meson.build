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

# Signature, single launch and file types; the installed executable gets
# them, and installing into the system registers the file types.
rc = find_program('rc', required: false)
xres = find_program('xres', required: false)
mimeset = find_program('mimeset', required: false)
if rc.found() and xres.found() and mimeset.found()
	codeEditResources = custom_target(
		'CodeEdit.rsrc',
		input: 'CodeEdit.rdef',
		output: 'CodeEdit.rsrc',
		command: [rc, '-o', '@OUTPUT@', '@INPUT@'],
		build_by_default: true
	)
	meson.add_install_script(
		python,
		files('../tools/install_resources.py'),
		xres.full_path(),
		mimeset.full_path(),
		codeEditResources,
		get_option('bindir') / 'CodeEdit'
	)
else
	warning('rc, xres or mimeset not found: CodeEdit is installed without file types')
endif
