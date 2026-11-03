project(
	'CodeEditView',
	'cpp',
	version: '0.1.0',
	license: 'MIT',
	meson_version: '>= 1.0',
	default_options: [
		'cpp_std=c++20',
		'warning_level=2',
		'buildtype=debugoptimized',
	]
)

cpp = meson.get_compiler('cpp')

beDep = cpp.find_library('be')
treeSitterDep = dependency('tree-sitter', version: '>= 0.25')

publicInc = include_directories('headers')
privateInc = include_directories('src')

subdir('src')

if get_option('demo')
	subdir('demo')
endif

if get_option('tests')
	subdir('tests')
endif
