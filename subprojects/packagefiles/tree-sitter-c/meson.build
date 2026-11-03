project('tree-sitter-c', 'c', version: '0.24.2', license: 'MIT')

treeSitterCLib = static_library(
	'tree-sitter-c',
	'src/parser.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_c_dep = declare_dependency(link_with: treeSitterCLib)
