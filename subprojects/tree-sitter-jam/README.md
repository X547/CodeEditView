# tree-sitter-jam

Tree-sitter grammar for Jam, the build tool (Perforce Jam 2.5 and the Haiku
variant). It follows `jamgram.yy` and `scan.c` of Jam.

`grammar.js` and `src/scanner.c` are the sources; the other files in `src/`
are generated. After changing the grammar, regenerate them with the
tree-sitter CLI (needs Node.js; both are available on Haiku):

```sh
tree-sitter generate
```

and commit the generated files.

The scanner reads the body of `actions` definitions, which Jam takes as raw
text up to the matching closing brace.

The grammar parses all Jamfiles, `Jamrules` and `build/jam` files of the
Haiku source tree and `Jambase` without errors.
