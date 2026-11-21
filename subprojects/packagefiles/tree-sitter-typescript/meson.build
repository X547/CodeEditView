project('tree-sitter-typescript', 'c', version: '0.23.2', license: 'MIT')

# Two grammars, TypeScript and TSX; their scanners include common/scanner.h.
treeSitterTypeScriptLib = static_library(
	'tree-sitter-typescript',
	'typescript/src/parser.c',
	'typescript/src/scanner.c',
	include_directories: include_directories('typescript/src'),
	c_args: ['-w'],
	pic: true
)

treeSitterTsxLib = static_library(
	'tree-sitter-tsx',
	'tsx/src/parser.c',
	'tsx/src/scanner.c',
	include_directories: include_directories('tsx/src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_typescript_dep = declare_dependency(link_with: [treeSitterTypeScriptLib, treeSitterTsxLib])
