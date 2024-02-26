#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include "drw.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define INTERSECTC(x,y,w,h,z)   (MAX(0, MIN((x)+(w),(z)->x+(z)->w) - MAX((x),(z)->x)) \
                               * MAX(0, MIN((y)+(h),(z)->y+(z)->h) - MAX((y),(z)->y)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel, SchemeAct,
       SchemeIdle, SchemeLayout, SchemeStatus }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkClientWin,
       ClkRootWin, ClkLast }; /* clicks */

typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct {
  unsigned int click;
  unsigned int mask;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
  char name[256];
  float mina, maxa;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
  int bw, oldbw;
  unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
	pid_t pid;
  unsigned int beingmoved;
  Client *next;
  Client *snext;
	Client *swallowing;
  Monitor *mon;
  Window win;
};

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  const char *symbol;
  void (*arrange)(Monitor *);
} Layout;

struct Monitor {
  char ltsymbol[16];
  float mfact;
  int nmaster;
  int num;
  int by;               /* bar geometry */
  int mx, my, mw, mh;   /* screen size */
  int wx, wy, ww, wh;   /* window area  */
  int gappih;           /* horizontal gap between windows */
  int gappiv;           /* vertical gap between windows */
  int gappoh;           /* horizontal outer gaps */
  int gappov;           /* vertical outer gaps */
  unsigned int seltags;
  unsigned int sellt;
  unsigned int tagset[2];
  int showbar;
  int topbar;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
  const Layout *lt[2];
};

typedef struct {
  const char *class;
  const char *instance;
  const char *title;
  unsigned int tags;
  int isfloating;
	int isterminal;
	int noswallow;
  int monitor;
} Rule;

/* function declarations */
void applyrules(Client *c);
int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
void arrange(Monitor *m);
void arrangemon(Monitor *m);
void attach(Client *c);
void attachbottom(Client *c);
void attachstack(Client *c);
void buttonpress(XEvent *e);
void checkotherwm(void);
void cleanup(void);
void cleanupmon(Monitor *mon);
void clientmessage(XEvent *e);
void configure(Client *c);
void configurenotify(XEvent *e);
void configurerequest(XEvent *e);
Monitor *createmon(void);
void destroynotify(XEvent *e);
void detach(Client *c);
void detachstack(Client *c);
#ifdef MULTI_MONITOR
static Monitor *dirtomon(int dir);
static void focusmon(const Arg *arg);
static void tagmon(const Arg *arg);
#endif
void drawbar(Monitor *m);
void drawbars(void);
void expose(XEvent *e);
void focus(Client *c);
void focusin(XEvent *e);
void focusstack(const Arg *arg);
void movestack(const Arg *arg);
Atom getatomprop(Client *c, Atom prop);
int getrootptr(int *x, int *y);
long getstate(Window w);
int gettextprop(Window w, Atom atom, char *text, unsigned int size);
void grabbuttons(Client *c, int focused);
void grabkeys(void);
void incnmaster(const Arg *arg);
void keypress(XEvent *e);
void killclient(const Arg *arg);
void manage(Window w, XWindowAttributes *wa);
void mappingnotify(XEvent *e);
void maprequest(XEvent *e);
void monocle(Monitor *m);
void dwindle(Monitor *mon);
void spiral(Monitor *mon);
void fibonacci(Monitor *mon, int s);
void placemouse(const Arg *arg);
void movemouse(const Arg *arg);
void moveorplace(const Arg *arg);
Client *nexttiled(Client *c);
void pop(Client *c);
void propertynotify(XEvent *e);
void quit(const Arg *arg);
Client *recttoclient(int x, int y, int w, int h);
Monitor *recttomon(int x, int y, int w, int h);
void resize(Client *c, int x, int y, int w, int h, int interact);
void resizeclient(Client *c, int x, int y, int w, int h);
void resizemouse(const Arg *arg);
void restack(Monitor *m);
void run(void);
void scan(void);
int sendevent(Client *c, Atom proto);
void sendmon(Client *c, Monitor *m);
void setclientstate(Client *c, long state);
void setfocus(Client *c);
void setfullscreen(Client *c, int fullscreen);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void setup(void);
void seturgent(Client *c, int urg);
void showhide(Client *c);
void sigchld(int unused);
void sighup(int unused);
void sigterm(int unused);
void spawn(const Arg *arg);
void tag(const Arg *arg);
void tile(Monitor *m);
void togglebar(const Arg *arg);
void togglefloating(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void unfocus(Client *c, int setfocus);
void unmanage(Client *c, int destroyed);
void unmapnotify(XEvent *e);
void updatebarpos(Monitor *m);
void updatebars(void);
void updateclientlist(void);
int updategeom(void);
void updatenumlockmask(void);
void updatesizehints(Client *c);
void updatestatus(void);
void updatetitle(Client *c);
void updatewindowtype(Client *c);
void updatewmhints(Client *c);
void view(const Arg *arg);
Client *wintoclient(Window w);
Monitor *wintomon(Window w);
int xerror(Display *dpy, XErrorEvent *ee);
int xerrordummy(Display *dpy, XErrorEvent *ee);
int xerrorstart(Display *dpy, XErrorEvent *ee);
void zoom(const Arg *arg);
void getgaps(Monitor *m, int *oh, int *ov, int *ih, int *iv, unsigned int *nc);
void getfacts(Monitor *m, int msize, int ssize, float *mf, float *sf, int *mr, int *sr);

pid_t getparentprocess(pid_t p);
int isdescprocess(pid_t p, pid_t c);
Client *swallowingclient(Window w);
Client *termforwin(const Client *c);
pid_t winpid(Window w);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int vp;               /* vertical padding for bar */
static int sp;               /* side padding for bar */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
  [ButtonPress] = buttonpress,
  [ClientMessage] = clientmessage,
  [ConfigureRequest] = configurerequest,
  [ConfigureNotify] = configurenotify,
  [DestroyNotify] = destroynotify,
  [Expose] = expose,
  [FocusIn] = focusin,
  [KeyPress] = keypress,
  [MappingNotify] = mappingnotify,
  [MapRequest] = maprequest,
  [PropertyNotify] = propertynotify,
  [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int restart = 0;
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

static xcb_connection_t *xcon;


