package tree_sitter_orgmode_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_orgmode "github.com/zac-garby/tree-sitter-org-mode/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_orgmode.Language())
	if language == nil {
		t.Errorf("Error loading org mode grammar")
	}
}
