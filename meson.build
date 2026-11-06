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
python = find_program('python3')

# The generated grammar parsers are large; build them without debug info.
grammarOptions = ['debug=false', 'optimization=2', 'warning_level=0']
treeSitterCDep = subproject('tree-sitter-c', default_options: grammarOptions).get_variable('tree_sitter_c_dep')
treeSitterCppDep = subproject('tree-sitter-cpp', default_options: grammarOptions).get_variable('tree_sitter_cpp_dep')

publicInc = include_directories('headers')
privateInc = include_directories('src')

subdir('src')

if get_option('tests')
	catch2Dep = dependency('catch2-with-main', version: '>= 3.0')
endif

subdir('findkit')

if get_option('demo')
	subdir('demo')
endif

if get_option('debugger')
	subdir('debugger')
endif

if get_option('tests')
	subdir('tests')
endif

# The diff apps and their shared library need libgit2; they use the test
# setup above.
if get_option('gitdiff') or get_option('dircompare')
	subdir('diffkit')
endif

if get_option('gitdiff')
	subdir('gitdiff')
endif

if get_option('dircompare')
	subdir('dircompare')
endif

if get_option('coverage')
	subdir('coverage')
endif
