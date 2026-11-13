embedText = files('../tools/embed_text.py')

highlightQueries = [
	custom_target(
		'c-highlights',
		input: files('syntax/queries/c-highlights.scm'),
		output: 'CHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kCHighlights', '@INPUT@']
	),
	custom_target(
		'cpp-highlights',
		input: files('syntax/queries/c-highlights.scm', 'syntax/queries/cpp-highlights.scm'),
		output: 'CppHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kCppHighlights', '@INPUT@']
	),
	custom_target(
		'shell-highlights',
		input: files('syntax/queries/shell-highlights.scm'),
		output: 'ShellHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kShellHighlights', '@INPUT@']
	),
	custom_target(
		# Target names starting with "meson-" are reserved.
		'mesonbuild-highlights',
		input: files('syntax/queries/meson-highlights.scm'),
		output: 'MesonHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kMesonHighlights', '@INPUT@']
	),
	custom_target(
		'jam-highlights',
		input: files('syntax/queries/jam-highlights.scm'),
		output: 'JamHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kJamHighlights', '@INPUT@']
	),
	custom_target(
		'componentpascal-highlights',
		input: files('syntax/queries/componentpascal-highlights.scm'),
		output: 'ComponentPascalHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kComponentPascalHighlights', '@INPUT@']
	),
	custom_target(
		'python-highlights',
		input: files('syntax/queries/python-highlights.scm'),
		output: 'PythonHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kPythonHighlights', '@INPUT@']
	),
	custom_target(
		'make-highlights',
		input: files('syntax/queries/make-highlights.scm'),
		output: 'MakeHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kMakeHighlights', '@INPUT@']
	),
	custom_target(
		'cmake-highlights',
		input: files('syntax/queries/cmake-highlights.scm'),
		output: 'CMakeHighlights.inc',
		command: [python, embedText, '@OUTPUT@', 'kCMakeHighlights', '@INPUT@']
	),
]

codeEditViewSources = files(
	'Version.cpp',
	'commands/EditorCommands.cpp',
	'layout/DisplayMap.cpp',
	'layout/LineLayout.cpp',
	'layout/RowIndex.cpp',
	'model/EditRecord.cpp',
	'model/Overlays.cpp',
	'model/Rope.cpp',
	'model/TextLoader.cpp',
	'model/TextModel.cpp',
	'model/UndoStack.cpp',
	'syntax/Highlighter.cpp',
	'syntax/Language.cpp',
	'syntax/Outline.cpp',
	'syntax/SyntaxState.cpp',
	'syntax/SyntaxWorker.cpp',
	'syntax/Theme.cpp',
	'unicode/CharWidth.cpp',
	'unicode/Utf8.cpp',
	'view/CodeEditScrollView.cpp',
	'view/CodeEditView.cpp',
	'view/EditorCore.cpp',
	'view/GutterView.cpp',
	'view/KeyMap.cpp',
	'view/Marks.cpp',
	'view/PositionView.cpp',
	'view/WordMotion.cpp',
)

codeEditViewLib = shared_library(
	'codeeditview',
	codeEditViewSources,
	highlightQueries,
	include_directories: [publicInc, privateInc],
	dependencies: [beDep, treeSitterDep, treeSitterCDep, treeSitterCppDep, treeSitterBashDep, treeSitterMesonDep, treeSitterJamDep,
		treeSitterComponentPascalDep, treeSitterPythonDep, treeSitterMakeDep, treeSitterCMakeDep],
	version: meson.project_version(),
	install: true,
	install_dir: libInstallDir
)

codeEditViewDep = declare_dependency(
	link_with: codeEditViewLib,
	include_directories: publicInc,
	dependencies: [beDep]
)

install_subdir('../headers/CodeEditView', install_dir: get_option('includedir'))
