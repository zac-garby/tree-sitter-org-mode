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
    $.property_name,
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
      $.node_property,
      $.list,
      $.object,
      $._blank_line,
    ),

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

    value: $ => /[^\n]+/,

    greater_block: $ => seq(
      $._block_begin_marker,
      $.block_begin_name,
      optional(seq(
        $._space, field("params", $.value)
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
        $._space, field("params", $.value)
      )),
      $._blank_line,
      field("contents", alias(repeat($.element), "contents")),
      $._blank_line,
      "#+end:",
      $.block_end_name,
    ),

    drawer: $ => seq(
      $.drawer_name,
      $._blank_line,
      field("contents", alias(repeat($.element), "contents")),
      $.drawer_end,
    ),

    node_property: $ => prec(1, seq(
      field("name", $.property_name),
      optional(seq(
        $._space, field("value", $.value),
      ))
    )),

    list: $ => prec.left(0, seq(
      $._list_start,
      repeat($.list_item),
      $._list_end,
    )),

    list_item: $ => prec(1, seq(
      $.bullet,
      optional(field("checkbox", $.checkbox)),
      field("content", alias(repeat($.element), "content")),
    )),

    checkbox: $ => choice("[ ]", "[-]", "[X]"),

    object: $ => prec(0, choice(
      $._word // placeholder for now, just single words
    )),

    alphanum: $ => /[a-zA-Z0-9_]/,
    _word: $ => /[^\s]+/,
    _blank_line: $ => /\r?\n[ \t]*/,
    _nl: $ => /\r?\n/,
    _space: $ => /[ \t]+/
  }
});
