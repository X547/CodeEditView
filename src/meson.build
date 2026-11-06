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
]

codeEditViewSources = files(
	'Version.cpp',
	'commands/EditorCommands.cpp',
	'layout/DisplayMap.cpp',
	'layout/LineLayout.cpp',
	'layout/RowIndex.cpp',
	'model/EditRecord.cpp',
	'model/Rope.cpp',
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
	dependencies: [beDep, treeSitterDep, treeSitterCDep, treeSitterCppDep],
	version: meson.project_version(),
	install: true
)

codeEditViewDep = declare_dependency(
	link_with: codeEditViewLib,
	include_directories: publicInc,
	dependencies: [beDep]
)

install_subdir('../headers/CodeEditView', install_dir: get_option('includedir'))
