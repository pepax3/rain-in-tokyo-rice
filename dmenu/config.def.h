/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

#include "themes/tokyonight.h"

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
static int centered = 0;                    /* -c option; centers dmenu on screen */
static int min_width = 500;                    /* minimum width when centered */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
  "Source Code Pro:Medium:pixelsize=15:antialias=true"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
  /*                   fg         bg       */
  [SchemeNorm] = { col_foreground , col_background},
  [SchemeSel] = { col_background , col_magenta },
  [SchemeOut] = { col_background, col_bright_yellow },
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines          = 0;
/* -h option; minimum height of a menu line */
static unsigned int lineheight     = 0;
static unsigned int min_lineheight = 8;

static unsigned int outer_vert_padding   = 10;
static unsigned int outer_horz_padding   = 10;
static unsigned int inner_horz_padding   = 5;


/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
