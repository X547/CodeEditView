project('tree-sitter-javascript', 'c', version: '0.25.0', license: 'MIT')

treeSitterJavaScriptLib = static_library(
	'tree-sitter-javascript',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_javascript_dep = declare_dependency(link_with: treeSitterJavaScriptLib)
