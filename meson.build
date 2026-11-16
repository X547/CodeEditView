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

# Installed layout: the applications in $prefix/apps/<project>, the shared
# library in its lib directory, where Haiku looks for the libraries of the
# applications beside it (%A/lib).
appInstallDir = 'apps' / meson.project_name()
libInstallDir = appInstallDir / 'lib'

# The generated grammar parsers are large; build them without debug info.
grammarOptions = ['debug=false', 'optimization=2', 'warning_level=0']
treeSitterCDep = subproject('tree-sitter-c', default_options: grammarOptions).get_variable('tree_sitter_c_dep')
treeSitterCppDep = subproject('tree-sitter-cpp', default_options: grammarOptions).get_variable('tree_sitter_cpp_dep')
treeSitterBashDep = subproject('tree-sitter-bash', default_options: grammarOptions).get_variable('tree_sitter_bash_dep')
treeSitterMesonDep = subproject('tree-sitter-meson', default_options: grammarOptions).get_variable('tree_sitter_meson_dep')
treeSitterPythonDep = subproject('tree-sitter-python', default_options: grammarOptions).get_variable('tree_sitter_python_dep')
treeSitterCMakeDep = subproject('tree-sitter-cmake', default_options: grammarOptions).get_variable('tree_sitter_cmake_dep')
treeSitterDeviceTreeDep = subproject('tree-sitter-devicetree', default_options: grammarOptions).get_variable('tree_sitter_devicetree_dep')
# Kept in this repository: subprojects/tree-sitter-jam,
# subprojects/tree-sitter-componentpascal and subprojects/tree-sitter-make
# (patched).
treeSitterJamDep = subproject('tree-sitter-jam', default_options: grammarOptions).get_variable('tree_sitter_jam_dep')
treeSitterComponentPascalDep = subproject('tree-sitter-componentpascal', default_options: grammarOptions).get_variable('tree_sitter_componentpascal_dep')
treeSitterMakeDep = subproject('tree-sitter-make', default_options: grammarOptions).get_variable('tree_sitter_make_dep')

publicInc = include_directories('headers')
privateInc = include_directories('src')

subdir('src')

if get_option('tests')
	catch2Dep = dependency('catch2-with-main', version: '>= 3.0')
endif

subdir('findkit')

# The JSON parser of the debugger, also used by the coverage viewer.
jsonLib = static_library('json', files('debugger/Json.cpp'))
jsonDep = declare_dependency(link_with: jsonLib, include_directories: include_directories('debugger'))

if get_option('demo')
	subdir('demo')
endif

if get_option('tests')
	subdir('tests')
endif

# The diff apps and their shared library need libgit2; they use the test
# setup above.
if get_option('gitdiff') or get_option('dircompare') or get_option('gittool') or get_option('studio') or get_option('debugger')
	subdir('diffkit')
endif

# Documents by locators, used by the debugger and the CodeEdit app.
if get_option('studio') or get_option('debugger')
	subdir('workspacekit')
	subdir('debugger')
endif

# The Git tool shows the diff window of gitdiff.
if get_option('gitdiff') or get_option('gittool')
	subdir('gitdiff')
endif

if get_option('dircompare')
	subdir('dircompare')
endif

if get_option('gittool')
	subdir('gittool')
endif

if get_option('coverage') or get_option('studio')
	subdir('coverage')
endif

if get_option('studio')
	subdir('studio')
endif

if get_option('tests')
	# Window threads must not wait for dialogs (DiffKit::Ask() and the Open()
	# methods instead).
	test('NoBlockingDialogs', python,
		args: [files('tools/check_blocking_dialogs.py'), meson.project_source_root()])
endif
