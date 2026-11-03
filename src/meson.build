codeEditViewSources = files(
	'Version.cpp',
	'model/Rope.cpp',
	'view/CodeEditView.cpp',
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
