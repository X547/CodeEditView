project('tree-sitter-html', 'c', version: '0.23.2', license: 'MIT')

treeSitterHtmlLib = static_library(
	'tree-sitter-html',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_html_dep = declare_dependency(link_with: treeSitterHtmlLib)
