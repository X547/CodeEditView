project('tree-sitter-css', 'c', version: '0.25.0', license: 'MIT')

treeSitterCssLib = static_library(
	'tree-sitter-css',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_css_dep = declare_dependency(link_with: treeSitterCssLib)
