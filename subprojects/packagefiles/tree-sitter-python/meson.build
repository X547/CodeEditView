project('tree-sitter-python', 'c', version: '0.25.0', license: 'MIT')

treeSitterPythonLib = static_library(
	'tree-sitter-python',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_python_dep = declare_dependency(link_with: treeSitterPythonLib)
