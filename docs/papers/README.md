# ClangSOLV papers

Each paper has a self-contained LaTeX source and a PDF edition:

| Paper | Source | PDF |
| --- | --- | --- |
| ClangSOLV implementation and smart-pointer semantics | [clangsolv.tex](clangsolv.tex) | [PDF](clangsolv.pdf) |
| Comprehensive ideation and implementation synthesis | [static_object_labelling.tex](static_object_labelling.tex) | [PDF](static_object_labelling.pdf) |
| Original static-property-tag proposal | [static_property_tags.tex](static_property_tags.tex) | [PDF](static_property_tags.pdf) |
| Original semantic-category proposal | [semantic_roles_beyond_class.tex](semantic_roles_beyond_class.tex) | [PDF](semantic_roles_beyond_class.pdf) |
| Companion JSOLV Java implementation | [jsolv.tex](jsolv.tex) | [PDF](jsolv.pdf) |
| Companion portable CSOLV C++ template library | [csolv.tex](csolv.tex) | [PDF](csolv.pdf) |

ClangSOLV's current public names are `returns`, `needs`, `preserves()` and
`readonly`. The implementation paper and synthesis reflect those names and
pointee labels for supported smart pointers. The earlier proposals discuss
additional features that are not implemented; they remain research background.

The implementation paper's C++ build paths have been adapted to this repository.
Historical maps and the companion papers describe the original jmtools layout;
Java and portable-CSOLV source trees are not part of this repository. The actual
ClangSOLV source lives at the repository root and its consumer in `example-app/`.

With a suitable TeX installation, build from this directory:

```sh
pdflatex -interaction=nonstopmode -halt-on-error clangsolv.tex
pdflatex -interaction=nonstopmode -halt-on-error clangsolv.tex
```

Repeat for another manuscript, and run an additional pass if references or the
table of contents still need to settle. PDFs are retained for readers; auxiliary
TeX output is ignored by Git. Build and semantic details are documented in the
repository's [usage guide](../../USAGE.md) and [design notes](../../DESIGN.md).
