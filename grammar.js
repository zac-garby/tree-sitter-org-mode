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
    $.element,
  ],

  externals: $ => [
    $._block_begin_marker, // #+begin_
    $._block_end_marker, // #+end_
    $.block_begin_name,
    $.block_end_name,
    $.keyword_key,
    $.drawer_name,
    $.drawer_end,
    $.property_name,
    $.stars,
    $._end_section,
    $.bullet,
    $.checkbox,
    $._list_start,
    $._list_end,
    $._bold_start,
    $._bold_end,
    $._italic_start,
    $._italic_end,
    $._underline_start,
    $._underline_end,
    $._verbatim_start,
    $._verbatim_end,
    $._code_inline_start,
    $._code_inline_end,
    $._strikethrough_start,
    $._strikethrough_end,
    $._link_start,
    $._link_end,
    $.word,
    $.pathreg,
    $.comment_line,
    $._nl,
    $.error_sentinel,
  ],

  extras: $ => [
    /[ \t]+/
  ],

  conflicts: $ => [
    [$._minimal_set, $.value],
  ],

  rules: {
    document: $ => seq(
      field("zeroth_section", optional($.body)),
      field("subsection", repeat($.section)),
    ),

    element: $ => prec(1, choice(
      $.keyword,
      $.greater_block,
      $.dynamic_block,
      $.drawer,
      $.node_property,
      $.list,
      $.paragraph,
      $.comment_line,
      $._blank_line,
    )),

    incomplete_element: $ => prec(-1, choice(
      seq(
        $._block_begin_marker,
        $.block_begin_name,
        optional(seq(
          $._space, field("params", $.value)
        )),
        $._blank_line,
        optional(seq(
          field("body", alias(repeat($.element), "body")),
        )),
      )
    )),

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
      optional(field("title", alias(repeat1($._object), "title"))),
      repeat1($._blank_line),
    )),

    keyword: $ => seq(
      $.keyword_key,
      $._space,
      $.value,
    ),

    greater_block: $ => prec.right(0, seq(
      $._block_begin_marker,
      $.block_begin_name,
      field("params", optional(seq(
        $.value,
        $._nl,
      ))),
      field("body", optional(seq(
        alias(repeat($.element), "body")),
      )),
      optional(seq(
        $._block_end_marker,
        $.block_end_name,
      ))
    )),

    // TODO: fix this (#+begin: is captured by keywords now)
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

    drawer: $ => prec.right(0, seq(
      $.drawer_name,
      $._blank_line,
      field("contents", alias(repeat($.element), "contents")),
      optional($.drawer_end),
    )),

    node_property: $ => prec.right(1, seq(
      field("name", $.property_name),
      optional(seq(
        field("value", $.value),
      ))
    )),

    list: $ => prec.left(0, seq(
      $._list_start,
      repeat($.list_item),
      $._list_end,
    )),

    list_item: $ => prec(1, seq(
      $.bullet,
      optional($.checkbox),
      field("content", alias(repeat($.element), "content")),
    )),

    paragraph: $ => prec.right(0, seq(
      repeat1($._object),
      $._nl,
    )),

    _object: $ => choice(
      $._minimal_set,
      $.regular_link,
    ),

    _minimal_set: $ => choice(
      $.word,
      $.markup,
      $._interrupted_start,
    ),

    markup: $ => choice(
      $.bold,
      $.italic,
      $.underline,
      $.verbatim,
      $.code_inline,
      $.strikethrough,
    ),

    regular_link: $ => seq(
      $._link_start,
      $._link_start,
      field("pathreg", $.pathreg),
      $._link_end,
      optional(seq(
        $._link_start,
        field("description", alias(repeat($._minimal_set), "description")),
        $._link_end,
      )),
      $._link_end,
    ),

    // needed because, due to the $.start rules, these would not otherwise
    // constitute valid words.
    _interrupted_start: $ => prec.left(-1, seq(
      choice(
        alias($.stars, $.word),
        alias($._bold_start, $.word),
        alias($._italic_start, $.word),
        alias($._underline_start, $.word),
        alias($._verbatim_start, $.word),
        alias($._code_inline_start, $.word),
        alias($._strikethrough_start, $.word),
        alias($._link_start, $.word),
        alias($._link_end, $.word),
      ),
      repeat($._object)
    )),

    bold: $ => seq($._bold_start, repeat1($._object), $._bold_end),
    italic: $ => seq($._italic_start, repeat1($._object), $._italic_end),
    underline: $ => seq($._underline_start, repeat1($._object), $._underline_end),
    verbatim: $ => seq($._verbatim_start, repeat1($._object), $._verbatim_end),
    code_inline: $ => seq($._code_inline_start, repeat1($._object), $._code_inline_end),
    strikethrough: $ => seq($._strikethrough_start, repeat1($._object), $._strikethrough_end),

    _blank_line: $ => /\r?\n[ \t]*/,
    _space: $ => /[ \t]+/,
    // value: $ => /[^\n]+/,
    value: $ => prec.right(repeat1($.word)),
  }
});
