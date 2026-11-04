trackerDep = cpp.find_library('tracker')
columnListViewDep = cpp.find_library('columnlistview')
privateInterfaceInc = include_directories('/boot/system/develop/headers/private/interface')

executable(
	'CodeEditDebugger',
	files(
		'Common.cpp',
		'ConsoleWindow.cpp',
		'ControlWindow.cpp',
		'DapClient.cpp',
		'DebugSession.cpp',
		'DebuggerApp.cpp',
		'Documents.cpp',
		'Json.cpp',
		'SourceWindow.cpp',
		'StackWindow.cpp',
		'ThreadsWindow.cpp',
		'VariablesWindow.cpp',
	),
	include_directories: [privateInterfaceInc],
	dependencies: [codeEditViewDep, trackerDep, columnListViewDep],
	install: true
)

# A program to try the debugger on, built for debugging.
debuggerSample = executable(
	'DebuggerSample',
	files('sample/Sample.cpp'),
	override_options: ['optimization=0', 'debug=true']
)
