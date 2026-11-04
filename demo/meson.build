trackerDep = cpp.find_library('tracker')

executable(
	'CodeEditDemo',
	files(
		'DemoApp.cpp',
		'DemoWindow.cpp',
		'OutlineView.cpp',
	),
	dependencies: [codeEditViewDep, trackerDep],
	install: true
)
