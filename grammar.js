/**
 * @file Emacs org-mode files
 * @author Zac Garby <me@zacgarby.co.uk>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
  name: "orgmode",

  supertypes: $ => [
    $.element
  ],

  externals: $ => [
    $._block_begin_marker, // #+begin_
    $._block_end_marker, // #+end_
    $.block_begin_name,
    $.block_end_name,
    $.drawer_name,
    $.drawer_end,
    $.stars,
    $._end_section,
    $.bullet,
    $._list_start,
    $._list_end,
    $.error_sentinel,
  ],

  extras: $ => [
    /[ \t]+/
  ],

  // word: $ => $._word,

  rules: {
    document: $ => seq(
      optional($.body),
      repeat($.section),
    ),

    element: $ => choice(
      $.greater_block,
      $.dynamic_block,
      $.drawer,
      $.list,
      $.object,
      $._blank_line,
    ),

    paragraph: $ => /[^\n]+/,

    body: $ => prec.left(0, seq(
      repeat1($.element)
    )),

    greater_element: $ => choice(
      $.greater_block
    ),

    section: $ => seq(
      $.heading,
      optional($.body),
      repeat(field("subsection", $.section)),
      $._end_section,
    ),

    heading: $ => prec.left(0, seq(
      $.stars,
      $._space,
      alias(optional(choice("TODO", "DONE")), "keyword"),
      alias(optional(/\[#[a-zA-Z0-9]\]/), "cookie"),
      optional("COMMENT"),
      optional(field("title", alias(repeat1($.object), "title"))),
      repeat1($._blank_line),
    )),

    // property_drawer: $ => prec(1, seq(
    //   $.property_drawer_name,
    //   alias($._blank_line, "nl"),
    //   alias(repeat(choice(
    //     $.property_node,
    //     alias($._blank_line, "nl 1"),
    //   )), "nodes"),
    //   // optional(alias($.property_node, "last one")),
    //   // alias($._nl, "blank 2"),
    //   // alias(repeat(seq($.property_node, $._blank_line)), "nodes"),
    //   // alias(optional($.property_node), "last"),
    //   // $._blank_line,
    //   // alias($._nl, "nl 2"),
    //   $.drawer_end,
    // )),

    // property_node: $ => prec.right(0, seq(
    //   $.drawer_name,
    //   optional(seq($._space, $.value)),
    // )),

    value: $ => /[^\n]+/,

    greater_block: $ => seq(
      $._block_begin_marker,
      $.block_begin_name,
      optional(seq(
        $._space, alias(/[^\n]+/, "params")
      )),
      $._blank_line,
      optional(seq(
        field("body", alias(repeat($.element), "body")),
        $._blank_line
      )),
      $._block_end_marker,
      $.block_end_name,
    ),

    dynamic_block: $ => seq(
      "#+begin:",
      $.block_begin_name,
      optional(seq(
        $._space, alias(/[^\n]+/, "params")
      )),
      $._blank_line,
      repeat($.element),
      $._blank_line,
      "#+end:",
      $.block_end_name,
    ),

    drawer: $ => seq(
      $.drawer_name,
      $._blank_line,
      repeat($.element),
      $.drawer_end,
    ),

    list: $ => prec.left(0, seq(
      $._list_start,
      repeat($.list_item),
      $._list_end,
    )),

    list_item: $ => prec.left(1, seq(
      $.bullet,
      optional(choice("[ ]", "[-]", "[X]")),
      repeat($.element),
    )),

    object: $ => prec(-1, choice(
      $._word // placeholder for now, just single words
    )),

    alphanum: $ => /[a-zA-Z0-9_]/,
    _word: $ => /[^\s]+/,
    _blank_line: $ => /\r?\n[ \t]*/,
    _nl: $ => /\r?\n/,
    _space: $ => /[ \t]+/
  }
});
