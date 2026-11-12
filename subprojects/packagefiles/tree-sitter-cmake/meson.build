project('tree-sitter-cmake', 'c', version: '0.7.6', license: 'MIT')

treeSitterCMakeLib = static_library(
	'tree-sitter-cmake',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_cmake_dep = declare_dependency(link_with: treeSitterCMakeLib)
