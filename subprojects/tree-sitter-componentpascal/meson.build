project('tree-sitter-componentpascal', 'c', version: '0.1.0', license: 'MIT')

treeSitterComponentPascalLib = static_library(
	'tree-sitter-componentpascal',
	'src/parser.c',
	'src/scanner.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_componentpascal_dep = declare_dependency(link_with: treeSitterComponentPascalLib)
