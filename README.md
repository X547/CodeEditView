# CodeEditView

A monospace source code editor view (`BView`) for Haiku.

Planned features:

- monospace grid layout with half-width and CJK full-width characters
- syntax highlighting and navigation with TreeSitter
- multiple views of one document, live-updated across windows
- optional word wrap with continuation indent
- folding
- line marks (bookmarks, breakpoints, debugger execution line)
- Haiku text editing conventions (drag & drop, keyboard navigation, auto-scroll)


## Building

Requirements: `meson`, `ninja`, `tree_sitter_devel`, `catch2_devel` (tests).

```sh
meson setup build
ninja -C build
meson test -C build
```

Options: `-Ddemo=false`, `-Dtests=false`.

The demo application is `build/demo/CodeEditDemo`.


## License

MIT, see [LICENSE](LICENSE).
