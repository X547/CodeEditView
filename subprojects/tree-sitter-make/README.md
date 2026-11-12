# tree-sitter-make

The Make grammar of tree-sitter-grammars
(https://github.com/tree-sitter-grammars/tree-sitter-make), commit
70613f3d812cbabbd7f38d104d60a409c4008b43 (after release 1.1.1), with one
change: a function call may have no arguments.

Upstream takes `$(file)`, `$(dir)`, `$(sort)` etc. as calls of the builtin
functions and requires arguments, so references to variables with these
names were errors that spread over the following lines (as in Haiku's
makefile-engine). Now they parse as calls without arguments; the
highlighting shows them as variables.

```diff
       optional(WS),
-      $.arguments,
+      optional($.arguments),
       ')',
```

`src/` is generated from `grammar.js` with the tree-sitter CLI (needs
Node.js; both are available on Haiku):

```sh
tree-sitter generate
```
