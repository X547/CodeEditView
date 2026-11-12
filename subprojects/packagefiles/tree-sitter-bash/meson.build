project('tree-sitter-bash', 'c', version: '0.25.1', license: 'MIT')

treeSitterBashLib = static_library(
	'tree-sitter-bash',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_bash_dep = declare_dependency(link_with: treeSitterBashLib)
