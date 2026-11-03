codeEditViewSources = files(
	'Version.cpp',
	'layout/DisplayMap.cpp',
	'layout/LineLayout.cpp',
	'layout/RowIndex.cpp',
	'model/EditRecord.cpp',
	'model/Rope.cpp',
	'model/TextModel.cpp',
	'model/UndoStack.cpp',
	'unicode/CharWidth.cpp',
	'unicode/Utf8.cpp',
	'view/CodeEditScrollView.cpp',
	'view/CodeEditView.cpp',
	'view/EditorCore.cpp',
	'view/GutterView.cpp',
	'view/KeyMap.cpp',
	'view/WordMotion.cpp',
)

codeEditViewLib = shared_library(
	'codeeditview',
	codeEditViewSources,
	include_directories: [publicInc, privateInc],
	dependencies: [beDep, treeSitterDep],
	version: meson.project_version(),
	install: true
)

codeEditViewDep = declare_dependency(
	link_with: codeEditViewLib,
	include_directories: publicInc,
	dependencies: [beDep]
)

install_subdir('../headers/CodeEditView', install_dir: get_option('includedir'))
