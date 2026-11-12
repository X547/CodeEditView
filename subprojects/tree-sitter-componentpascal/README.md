# tree-sitter-componentpascal

Tree-sitter grammar for Component Pascal, after the language report
(BlackBox `Docu/CP-Lang`, appendix B) and the BlackBox compiler (`DevCPP`,
`DevCPS`). It also takes what the compiler takes beyond the report:

- system flags: `MODULE WinApi ["user32.dll"]`, `RECORD [untagged]`,
  `PROCEDURE [ccall]`, `VAR [nil]`, entry names after declared names
- interface modules, whose procedures have no bodies
- code procedures: `PROCEDURE [code] Name 0FFH, 0E8H;` (also `[1]`)
- declaration sections after procedures
- text after the end of the module, which BlackBox documents often have

`grammar.js` and `src/scanner.c` are the sources; the other files in `src/`
are generated. After changing the grammar, regenerate them with the
tree-sitter CLI (needs Node.js; both are available on Haiku):

```sh
tree-sitter generate
```

and commit the generated files.

The scanner reads nested comments, and numbers, which end before `..` so
that `0..9` is a range.

The grammar parses the 273 modules of BlackBox 1.7.3 without errors, except
the template `Sql/Mod/ObxDriv`, which is not a valid module.
