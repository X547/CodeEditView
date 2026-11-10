trackerDep = cpp.find_library('tracker')
columnListViewDep = cpp.find_library('columnlistview')
# BColumnListView is private API (libcolumnlistview.a).
privateInterfaceInc = include_directories('/boot/system/develop/headers/private/interface')

# The debugger: sessions with GDB over DAP and their windows, shared by the
# CodeEditDebugger demo and the CodeEdit app.
debugKit = static_library(
	'debugkit',
	files(
		'BreakpointsWindow.cpp',
		'Common.cpp',
		'ConsoleWindow.cpp',
		'ControlWindow.cpp',
		'DapClient.cpp',
		'DebugSession.cpp',
		'StackWindow.cpp',
		'ThreadsWindow.cpp',
		'VariablesWindow.cpp',
	),
	include_directories: [privateInterfaceInc],
	dependencies: [workspaceKitDep, jsonDep, trackerDep, columnListViewDep]
)
debugKitDep = declare_dependency(
	link_with: debugKit,
	include_directories: [include_directories('.'), privateInterfaceInc],
	dependencies: [workspaceKitDep, jsonDep, trackerDep, columnListViewDep]
)

if get_option('debugger')
	executable(
		'CodeEditDebugger',
		files(
			'DebuggerApp.cpp',
			'SourceWindow.cpp',
		),
		dependencies: [debugKitDep],
		install: true,
		install_dir: appInstallDir
	)
endif

# A program to try the debugger on, built for debugging.
debuggerSample = executable(
	'DebuggerSample',
	files('sample/Sample.cpp'),
	override_options: ['optimization=0', 'debug=true']
)

if get_option('tests')
	debuggerTests = executable(
		'DebuggerTests',
		files(
			'tests/DapTest.cpp',
			'tests/DebugSessionTest.cpp',
		),
		dependencies: [debugKitDep, catch2Dep]
	)
	# The debug session test runs the sample program in GDB.
	test(
		'DebuggerTests',
		debuggerTests,
		env: [
			'DEBUGGER_SAMPLE=' + debuggerSample.full_path(),
			'DEBUGGER_SAMPLE_SOURCE=' + meson.current_source_dir() / 'sample/Sample.cpp',
		],
		depends: [debuggerSample],
		timeout: 120
	)
endif
