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
    $.block_begin_marker, // #+begin_
    $.block_end_marker, // #+end_
    $.block_begin_name,
    $.block_end_name,
    $.keyword_key,
    $.drawer_name,
    $.drawer_end,
    $.property_name,
    $.stars,
    $.end_section,
    $.bullet,
    $.checkbox,
    $.list_start,
    $.list_end,
    $.bold_start,
    $.bold_end,
    $.italic_start,
    $.italic_end,
    $.underline_start,
    $.underline_end,
    $.verbatim_start,
    $.verbatim_end,
    $.code_inline_start,
    $.code_inline_end,
    $.strikethrough_start,
    $.strikethrough_end,
    $.link_start,
    $.link_end,
    $.word,
    $.pathreg,
    $.comment_line,
    $.nl,
    $.error_sentinel,
  ],

  extras: $ => [
    /[ \t]+/
  ],

  rules: {
    document: $ => seq(
      field("zeroth_section", optional($.body)),
      repeat($.section),
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
      $.blank_line,
      // $.incomplete_element,
    )),

    incomplete_element: $ => prec(-1, choice(
      seq(
        $.block_begin_marker,
        $.block_begin_name,
        optional(seq(
          $.space, field("params", $.value)
        )),
        $.blank_line,
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
      $.end_section,
    ),

    heading: $ => prec.left(0, seq(
      $.stars,
      $.space,
      alias(optional(choice("TODO", "DONE")), "keyword"),
      alias(optional(/\[#[a-zA-Z0-9]\]/), "cookie"),
      optional("COMMENT"),
      optional(field("title", alias(repeat1($.object), "title"))),
      repeat1($.blank_line),
    )),

    keyword: $ => seq(
      $.keyword_key,
      $.space,
      $.value,
    ),

    greater_block: $ => prec.right(0, seq(
      $.block_begin_marker,
      $.block_begin_name,
      optional(seq(
        $.space, field("params", $.value)
      )),
      $.blank_line,
      optional(seq(
        field("body", alias(repeat($.element), "body")),
      )),
      optional(seq(
        $.block_end_marker,
        $.block_end_name,
      ))
    )),

    // TODO: fix this (#+begin: is captured by keywords now)
    dynamic_block: $ => seq(
      "#+begin:",
      $.block_begin_name,
      optional(seq(
        $.space, field("params", $.value)
      )),
      $.blank_line,
      field("contents", alias(repeat($.element), "contents")),
      $.blank_line,
      "#+end:",
      $.block_end_name,
    ),

    drawer: $ => prec.right(0, seq(
      $.drawer_name,
      $.blank_line,
      field("contents", alias(repeat($.element), "contents")),
      optional($.drawer_end),
    )),

    node_property: $ => prec(1, seq(
      field("name", $.property_name),
      optional(seq(
        $.space, field("value", $.value),
      ))
    )),

    list: $ => prec.left(0, seq(
      $.list_start,
      repeat($.list_item),
      $.list_end,
    )),

    list_item: $ => prec(1, seq(
      $.bullet,
      optional($.checkbox),
      field("content", alias(repeat($.element), "content")),
    )),

    paragraph: $ => prec.right(0, seq(
      repeat1($.object),
      $.nl,
    )),

    object: $ => choice(
      $.minimal_set,
      $.regular_link,
    ),

    minimal_set: $ => choice(
      $.word,
      $.markup,
      $.interrupted_start,
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
      $.link_start,
      $.link_start,
      field("pathreg", $.pathreg),
      $.link_end,
      optional(seq(
        $.link_start,
        field("description", alias(repeat($.minimal_set), "description")),
        $.link_end,
      )),
      $.link_end,
    ),

    // needed because, due to the $.start rules, these would not otherwise
    // constitute valid words.
    interrupted_start: $ => prec.left(-1, seq(
      choice(
        alias($.stars, $.word),
        alias($.bold_start, $.word),
        alias($.italic_start, $.word),
        alias($.underline_start, $.word),
        alias($.verbatim_start, $.word),
        alias($.code_inline_start, $.word),
        alias($.strikethrough_start, $.word),
        alias($.link_start, $.word),
        alias($.link_end, $.word),
      ),
      repeat($.object)
    )),

    bold: $ => seq($.bold_start, repeat1($.object), $.bold_end),
    italic: $ => seq($.italic_start, repeat1($.object), $.italic_end),
    underline: $ => seq($.underline_start, repeat1($.object), $.underline_end),
    verbatim: $ => seq($.verbatim_start, repeat1($.object), $.verbatim_end),
    code_inline: $ => seq($.code_inline_start, repeat1($.object), $.code_inline_end),
    strikethrough: $ => seq($.strikethrough_start, repeat1($.object), $.strikethrough_end),

    blank_line: $ => /\r?\n[ \t]*/,
    space: $ => /[ \t]+/,
    value: $ => /[^\n]+/,
  }
});
