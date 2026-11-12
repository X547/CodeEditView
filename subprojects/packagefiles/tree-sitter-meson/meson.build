project('tree-sitter-meson', 'c', version: '1.3.0', license: 'MIT')

treeSitterMesonLib = static_library(
	'tree-sitter-meson',
	'src/parser.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_meson_dep = declare_dependency(link_with: treeSitterMesonLib)
