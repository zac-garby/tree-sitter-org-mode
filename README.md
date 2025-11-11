# tree-sitter-org-mode

A tree sitter parser for [Org](https://orgmode.org/worg/org-syntax.html) documents.

Previously, I used [this one](https://github.com/zac-garby/tree-sitter-org), which was in turn a fork of [emiasims/tree-sitter-org](https://github.com/emiasims/tree-sitter-org). That repo has now been archived, and there were a lot of things I wasn't too happy with in the parser and its output anyway, so I made a new one from scratch.

Org is a very weird language, and very context sensitive, so this parser consists of a fairly *huge* custom [scanner.c](src/scanner.c).
