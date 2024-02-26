/* See LICENSE file for copyright and license details. */
#include "themes/tokyonight.h"
#include "dwm.h"

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const int swallowfloating    = 0;        /* 1 means swallow floating windows by default */

/* bar configuration */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const int vertpad            = 10;       /* vertical padding of bar */
static const int sidepad            = 0;       /* horizontal padding of bar */
static const int user_bh            = 28;       /* 0 means that dwm will calculate bar height */
static const int horizpadbar        = 10;       /* horizontal padding for statusbar */
static const int vertpadbar         = 0;        /* vertical padding for statusbar */

/* gap configuration */
static const unsigned int gappih    = 10;       /* horiz inner gap between windows */
static const unsigned int gappiv    = 10;       /* vert inner gap between windows */
static const unsigned int gappoh    = 10;       /* horiz outer gap between windows and screen edge */
static const unsigned int gappov    = 10;       /* vert outer gap between windows and screen edge */
static       int smartgaps          = 0;        /* 1 means no outer gap when there is only one window */

/* focus configuration */
static const int focusonwheel       = 0;

static const char *fonts[]          = {
  // "Source Code Pro:Semibold:pixelsize=15:antialias"
  "Source Code Pro:Medium:pixelsize=15:antialias=true"
};
static const char dmenufont[]       = "Source Code Pro:Regular:pixelsize=15:antialias=true";//"Source Code Pro:Semibold:pixelsize=15:antialias:true";
static const char *colors[][5]      = {
  /*               fg         bg         border   */
  [SchemeNorm]   = { col_foreground, col_background, col_background },
  [SchemeSel]    = { col_yellow, col_background, col_blue },
  [SchemeAct]    = { col_blue, col_background, col_background },
  [SchemeIdle]   = { col_grey, col_background, col_background },
  [SchemeLayout] = { col_cyan, col_background, col_blue },
  [SchemeStatus] = { col_yellow, col_background, col_background }
};

/* tagging                     term     ffox    */
// static const char *tags[] = { "\uf192", "\uf192", "\uf192", "\uf192", "\uf192", "\uf192" };
static const char *tags[] = { "\uf417", "\uf417", "\uf417", "\uf417", "\uf417", "\uf417" };

static const Rule rules[] = {
  /* xprop(1):
   *  WM_CLASS(STRING) = instance, class
   *  WM_NAME(STRING) = title
   */
  /* class      instance    title       tags mask     isfloating   isterminal noswallow   monitor */
  { "Gimp",        NULL,       NULL,       0,            1,           0,         0,          -1 },
  { "firefox",     NULL,       NULL,       1 << 1,       0,           0,         -1,         -1 },
  { "discord",     NULL,       NULL,       2 << 1,       0,           0,         0,          -1 },
  { "Slack",       NULL,       NULL,       2 << 1,       0,           0,         0,          -1 },
  { "mpv",         NULL,       NULL,       0,            0,           0,         0,          -1 },
  { "sxiv",        NULL,       NULL,       0,            0,           0,         0,          -1 },
  { "st-256color", NULL,       NULL,       0,            0,           1,         0,          -1 },
};

/* layout(s) */
static const float mfact        = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster        = 1;    /* number of clients in master area */
static const int resizehints    = 0;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1;    /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
  /* symbol     arrange function */
  { "[@]",      spiral },
  { "[]=",      tile },    /* first entry is default */
  { "><>",      NULL },    /* no layout function means floating behavior */
  { "[M]",      monocle },
  { "[\\]",     dwindle },
};

/* key definitions */
#define MODKEY Mod1Mask
#define TAGKEYS(KEY,TAG) \
  { MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
  { MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
  { MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
  { MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
// static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
// static const char *dmenucmd[]    = { "dmenu_run", "-c", "-m", dmenumon, "-l", "10", NULL };

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
// static const char *dmenucmd[]    = { "dmenu_run", "-c", "-m", dmenumon, "-l", "10", NULL };
static const char *dmenucmd[]    = { "dmenu_run", "-h", "28", "-m", dmenumon, NULL };
static const char *termcmd[]     = { "st", NULL };
static const char *powercmd[]    = { "power", NULL };
static const char *gamescmd[]    = { "games", NULL };
static const char maimfullcmd[]  = "maim | xclip -selection clipboard -t image/png";
static const char maimselcmd[]   = "maim --select | xclip -selection clipboard -t image/png";
static const char incvolcmd[]    = "pamixer -i 10 && pkill -RTMIN+10 dwmblocks";
static const char decvolcmd[]    = "pamixer -d 10 && pkill -RTMIN+10 dwmblocks";

static const Key keys[] = {
  /* modifier                     key        function        argument */
  { MODKEY,                       XK_p,         spawn,          {.v = dmenucmd } },
  { MODKEY|ShiftMask,             XK_p,         spawn,          {.v = powercmd } },
  { MODKEY,                       XK_g,         spawn,          {.v = gamescmd } },
  { MODKEY|ShiftMask,             XK_Return,    spawn,          {.v = termcmd } },
  { MODKEY,                       XK_m,         spawn,          SHCMD(maimfullcmd) },
  { MODKEY|ShiftMask,             XK_m,         spawn,          SHCMD(maimselcmd) },
  { MODKEY,                       XK_v,         spawn,          SHCMD(incvolcmd) },
  { MODKEY|ShiftMask,             XK_v,         spawn,          SHCMD(decvolcmd) },

  { MODKEY,                       XK_b,         togglebar,      {0} },
  { MODKEY,                       XK_j,         focusstack,     {.i = +1 } },
  { MODKEY,                       XK_k,         focusstack,     {.i = -1 } },
  { MODKEY|ShiftMask,             XK_j,         movestack,      {.i = +1 } },
  { MODKEY|ShiftMask,             XK_k,         movestack,      {.i = -1 } },
  { MODKEY,                       XK_i,         incnmaster,     {.i = +1 } },
  { MODKEY,                       XK_d,         incnmaster,     {.i = -1 } },
  { MODKEY,                       XK_comma,     setmfact,       {.f = -0.05} },
  { MODKEY,                       XK_period,    setmfact,       {.f = +0.05} },
  { MODKEY,                       XK_Return,    zoom,           {0} },
  { MODKEY,                       XK_Tab,       view,           {0} },
  { MODKEY|ShiftMask,             XK_c,         killclient,     {0} },

  /** Layout management keys **/
  { MODKEY,                       XK_w,      setlayout,      {.v = &layouts[1]} },
  { MODKEY|ShiftMask,             XK_w,      setlayout,      {.v = &layouts[3]} },
  { MODKEY,                       XK_e,      setlayout,      {.v = &layouts[0]} },
  { MODKEY|ShiftMask,             XK_e,      setlayout,      {.v = &layouts[4]} },
  { MODKEY|ShiftMask,             XK_space,  setlayout,      {.v = &layouts[2]} },
  { MODKEY,                       XK_space,  togglefloating, {0} },
  { MODKEY,                       XK_equal,  view,           {.ui = ~0 } },
  { MODKEY|ControlMask,           XK_equal,  tag,            {.ui = ~0 } },

  /** Tag management keys **/
  TAGKEYS(                        XK_1,             0)
  TAGKEYS(                        XK_2,             1)
  TAGKEYS(                        XK_3,             2)
  TAGKEYS(                        XK_4,             3)
  TAGKEYS(                        XK_5,             4)
  TAGKEYS(                        XK_6,             5)
  TAGKEYS(                        XK_7,             6)
  TAGKEYS(                        XK_8,             7)
  TAGKEYS(                        XK_9,             8)

  /** Quit and reload keys **/
  { MODKEY|ShiftMask,             XK_q,      quit,  {0} },
  { MODKEY|ShiftMask,             XK_r,      quit,  {1} },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {

  { ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
  { ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
  { ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },

  { ClkClientWin,         MODKEY,         Button1,        moveorplace,    {0} },
  { ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
  { ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
  { ClkTagBar,            0,              Button1,        view,           {0} },
  { ClkTagBar,            0,              Button3,        toggleview,     {0} },
  { ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
  { ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};

