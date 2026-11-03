project('tree-sitter-cpp', 'c', version: '0.23.4', license: 'MIT')

treeSitterCppLib = static_library(
	'tree-sitter-cpp',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_cpp_dep = declare_dependency(link_with: treeSitterCppLib)
