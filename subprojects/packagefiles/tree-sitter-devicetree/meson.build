project('tree-sitter-devicetree', 'c', version: '0.15.0', license: 'MIT')

treeSitterDeviceTreeLib = static_library(
	'tree-sitter-devicetree',
	'src/parser.c',
	include_directories: include_directories('src'),
	c_args: ['-w'],
	pic: true
)

tree_sitter_devicetree_dep = declare_dependency(link_with: treeSitterDeviceTreeLib)
