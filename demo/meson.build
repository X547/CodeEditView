trackerDep = cpp.find_library('tracker')

executable(
	'CodeEditDemo',
	files(
		'DemoApp.cpp',
		'DemoWindow.cpp',
		'OutlineView.cpp',
	),
	dependencies: [codeEditViewDep, findKitDep, trackerDep],
	install: true,
	install_dir: appInstallDir
)
