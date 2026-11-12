project('tree-sitter-make', 'c', version: '1.1.1', license: 'MIT')

treeSitterMakeLib = static_library(
	'tree-sitter-make',
	'src/parser.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_make_dep = declare_dependency(link_with: treeSitterMakeLib)
