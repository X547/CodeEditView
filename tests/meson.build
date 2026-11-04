catch2Dep = dependency('catch2-with-main', version: '>= 3.0')

codeEditTests = executable(
	'CodeEditTests',
	files(
		'DapTest.cpp',
		'DebugSessionTest.cpp',
		'EditorCoreTest.cpp',
		'FoldTest.cpp',
		'LineLayoutTest.cpp',
		'OutlineTest.cpp',
		'RopeTest.cpp',
		'RowIndexTest.cpp',
		'SyntaxTest.cpp',
		'TextModelTest.cpp',
		'UnicodeTest.cpp',
		'VersionTest.cpp',
		'WrapTest.cpp',
		'../debugger/DapClient.cpp',
		'../debugger/DebugSession.cpp',
		'../debugger/Documents.cpp',
		'../debugger/Json.cpp',
	),
	include_directories: [privateInc, include_directories('../debugger')],
	dependencies: [codeEditViewDep, treeSitterDep, catch2Dep]
)

# The debug session test runs the sample program in GDB.
testEnv = []
testDepends = []
if get_option('debugger')
	testEnv += [
		'DEBUGGER_SAMPLE=' + debuggerSample.full_path(),
		'DEBUGGER_SAMPLE_SOURCE=' + meson.project_source_root() / 'debugger/sample/Sample.cpp',
	]
	testDepends += [debuggerSample]
endif

test('CodeEditTests', codeEditTests, env: testEnv, depends: testDepends, timeout: 120)
