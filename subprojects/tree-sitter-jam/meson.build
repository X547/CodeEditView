project('tree-sitter-jam', 'c', version: '0.1.0', license: 'MIT')

treeSitterJamLib = static_library(
	'tree-sitter-jam',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_jam_dep = declare_dependency(link_with: treeSitterJamLib)
