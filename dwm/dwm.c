/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>
#ifdef __OpenBSD__
#include <sys/sysctl.h>
#include <kvm.h>
#endif /* __OpenBSD */

#include "util.h"

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c)
{
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = { NULL, NULL };

  /* rule matching */
  c->isfloating = 0;
  c->tags = 0;
  XGetClassHint(dpy, c->win, &ch);
  class    = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name  ? ch.res_name  : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title))
    && (!r->class || strstr(class, r->class))
    && (!r->instance || strstr(instance, r->instance)))
    {
      c->isterminal = r->isterminal;
      c->noswallow = r->noswallow;
      c->isfloating = r->isfloating;
      c->tags |= r->tags;
      for (m = mons; m && m->num != r->monitor; m = m->next);
      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
  int baseismin;
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > sw)
      *x = sw - WIDTH(c);
    if (*y > sh)
      *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)
      *x = 0;
    if (*y + *h + 2 * c->bw < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)
      *y = m->wy;
  }
  if (*h < bh)
    *h = bh;
  if (*w < bh)
    *w = bh;
  if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
    if (!c->hintsvalid)
      updatesizehints(c);
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0) {
      if (c->maxa < (float)*w / *h)
        *w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)
        *h = *w * c->mina + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;
    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
  if (m)
    showhide(m->stack);
  else for (m = mons; m; m = m->next)
    showhide(m->stack);
  if (m) {
    arrangemon(m);
    restack(m);
  } else for (m = mons; m; m = m->next)
    arrangemon(m);
}

void
arrangemon(Monitor *m)
{
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
  c->next = c->mon->clients;
  c->mon->clients = c;
}

void
attachbottom(Client *c)
{
  Client **tc;
  c->next = NULL;
  for (tc = &c->mon->clients; *tc; tc = &(*tc)->next);
  *tc = c;
}

void
attachstack(Client *c)
{
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void
swallow(Client *p, Client *c)
{

	if (c->noswallow || c->isterminal)
		return;
	if (c->noswallow && !swallowfloating && c->isfloating)
		return;

	detach(c);
	detachstack(c);

	setclientstate(c, WithdrawnState);
	XUnmapWindow(dpy, p->win);

	p->swallowing = c;
	c->mon = p->mon;

	Window w = p->win;
	p->win = c->win;
	c->win = w;
	updatetitle(p);
	XMoveResizeWindow(dpy, p->win, p->x, p->y, p->w, p->h);
	arrange(p->mon);
	configure(p);
	updateclientlist();
}

void
unswallow(Client *c)
{
	c->win = c->swallowing->win;

	free(c->swallowing);
	c->swallowing = NULL;

	/* unfullscreen the client */
	setfullscreen(c, 0);
	updatetitle(c);
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	setclientstate(c, NormalState);
	focus(NULL);
	arrange(c->mon);
}

void
buttonpress(XEvent *e)
{
  unsigned int i, x, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon
    && (focusonwheel || (ev->button != Button4 && ev->button != Button5))) {
    
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  if (ev->window == selmon->barwin) {
    i = 0;
    x = (m->ww - (LENGTH(tags) / 2) * (TEXTW(tags[0]) + lrpad)) / 2;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (ev->x < TEXTW(selmon->ltsymbol))
      click = ClkLtSymbol;
    else if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
    } else
      click = ClkStatusText;
  } else if ((c = wintoclient(ev->window))) {
    if (focusonwheel || (ev->button != Button4 && ev->button != Button5))
      focus(c);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
    && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void
cleanup(void)
{
  Arg a = {.ui = ~0};
  Layout foo = { "", NULL };
  Monitor *m;
  size_t i;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;
  for (m = mons; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, 0);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  free(scheme);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
  Monitor *m;

  if (mon == mons)
    mons = mons->next;
  else {
    for (m = mons; m && m->next != mon; m = m->next);
    m->next = mon->next;
  }
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

void
clientmessage(XEvent *e)
{
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen]
    || cme->data.l[2] == netatom[NetWMFullscreen])
      setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
        || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selmon->sel && !c->isurgent)
      seturgent(c, 1);
  }
}

void
configure(Client *c)
{
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->bw;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty) {
      drw_resize(drw, sw, bh);
      updatebars();
      for (m = mons; m; m = m->next) {
        for (c = m->clients; c; c = c->next)
          if (c->isfullscreen)
            resizeclient(c, m->mx, m->my, m->mw, m->mh);
        XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, m->ww -  2 * sp, bh);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

void
configurerequest(XEvent *e)
{
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = wintoclient(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
        c->oldx = c->x;
        c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
        c->oldy = c->y;
        c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
        c->oldw = c->w;
        c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
        c->oldh = c->h;
        c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
        configure(c);
      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *
createmon(void)
{
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->tagset[0] = m->tagset[1] = 1;
  m->mfact = mfact;
  m->nmaster = nmaster;
  m->showbar = showbar;
  m->topbar = topbar;
  m->gappih = gappih;
  m->gappiv = gappiv;
  m->gappoh = gappoh;
  m->gappov = gappov;
  m->lt[0] = &layouts[0];
  m->lt[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
  return m;
}

void
destroynotify(XEvent *e)
{
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, 1);
  else if ((c = swallowingclient(ev->window)))
    unmanage(c->swallowing, 1);
}

void
detach(Client *c)
{
  Client **tc;

  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
  *tc = c->next;
}

void
detachstack(Client *c)
{
  Client **tc, *t;

  for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
    c->mon->sel = t;
  }
}

#ifdef MULTI_MONITOR
Monitor *
dirtomon(int dir)
{
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selmon->next))
      m = mons;
  } else if (selmon == mons)
    for (m = mons; m->next; m = m->next);
  else
    for (m = mons; m->next != selmon; m = m->next);
  return m;
}
#endif

void
drawbar(Monitor *m)
{
  int x, w, tw = 0, tagpad;
  unsigned int i, occ = 0, urg = 0;
  Client *c;
  unsigned int tagscheme;

  if (!m->showbar)
    return;

  /* draw status first so it can be overdrawn by tags later */
  if (m == selmon) { /* status is only drawn on selected monitor */
    drw_setscheme(drw, scheme[SchemeStatus]);
    tw = TEXTW(stext);
    drw_text(drw, m->ww - tw - 2 * sp, 0, tw, bh, lrpad / 2, stext, 0);
  }

  for (c = m->clients; c; c = c->next) {
    occ |= c->tags;
    if (c->isurgent)
      urg |= c->tags;
  }
  x = 0;//barpad;
  w = TEXTW(m->ltsymbol);
  drw_setscheme(drw, scheme[SchemeLayout]);
  x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

  if ((w = m->ww - tw - x) > bh) {
    drw_setscheme(drw, scheme[SchemeNorm]);
    drw_rect(drw, x, 0, w - 2 * sp, bh, 1, 1);
  }

  for (i = 0; i < LENGTH(tags); i++) {
    tagscheme = SchemeIdle;
    w = TEXTW(tags[i]);
    tagpad = (m->ww - (LENGTH(tags) / 2 - i) * (w + lrpad)) / 2;
    if (m->tagset[m->seltags] & 1 << i) tagscheme = SchemeSel;
    else if (occ & 1 << i) tagscheme = SchemeAct;
    drw_setscheme(drw, scheme[tagscheme]);
    drw_text(drw, tagpad, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
  }

  drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);
}

void
enternotify(XEvent *e)
{
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
    return;
  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  if (m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  } else if (!c || c == selmon->sel)
    return;
  focus(c);
}

void
expose(XEvent *e)
{
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = wintomon(ev->window)))
    drawbar(m);
}

void
focus(Client *c)
{
  if (!c || !ISVISIBLE(c))
    for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
  if (selmon->sel && selmon->sel != c)
    unfocus(selmon->sel, 0);
  if (c) {
    if (c->mon != selmon)
      selmon = c->mon;
    if (c->isurgent)
      seturgent(c, 0);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, 1);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;
  drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

#ifdef MULTI_MONITOR
void
focusmon(const Arg *arg)
{
  Monitor *m;

  if (!mons->next)
    return;
  if ((m = dirtomon(arg->i)) == selmon)
    return;
  unfocus(selmon->sel, 0);
  selmon = m;
  focus(NULL);
}
#endif

void
focusstack(const Arg *arg)
{
  Client *c = NULL, *i;

  if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
    return;
  if (arg->i > 0) {
    for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
    if (!c)
      for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
  } else {
    for (i = selmon->clients; i != selmon->sel; i = i->next)
      if (ISVISIBLE(i))
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i))
          c = i;
  }
  if (c) {
    focus(c);
    restack(selmon);
  }
}

void
movestack(const Arg *arg) {
  Client *c = NULL, *p = NULL, *pc = NULL, *i;

  if(arg->i > 0) {
    /* find the client after selmon->sel */
    for(c = selmon->sel->next; c && (!ISVISIBLE(c) || c->isfloating); c = c->next);
    if(!c)
      for(c = selmon->clients; c && (!ISVISIBLE(c) || c->isfloating); c = c->next);

  }
  else {
    /* find the client before selmon->sel */
    for(i = selmon->clients; i != selmon->sel; i = i->next)
      if(ISVISIBLE(i) && !i->isfloating)
        c = i;
    if(!c)
      for(; i; i = i->next)
        if(ISVISIBLE(i) && !i->isfloating)
          c = i;
  }
  /* find the client before selmon->sel and c */
  for(i = selmon->clients; i && (!p || !pc); i = i->next) {
    if(i->next == selmon->sel)
      p = i;
    if(i->next == c)
      pc = i;
  }

  /* swap c and selmon->sel selmon->clients in the selmon->clients list */
  if(c && c != selmon->sel) {
    Client *temp = selmon->sel->next==c?selmon->sel:selmon->sel->next;
    selmon->sel->next = c->next==selmon->sel?c:c->next;
    c->next = temp;

    if(p && p != c)
      p->next = c;
    if(pc && pc != selmon->sel)
      pc->next = selmon->sel;

    if(selmon->sel == selmon->clients)
      selmon->clients = c;
    else if(c == selmon->clients)
      selmon->clients = selmon->sel;

    arrange(selmon);
  }
}

Atom
getatomprop(Client *c, Atom prop)
{
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
    &da, &di, &dl, &dl, &p) == Success && p) {
    atom = *(Atom *)p;
    XFree(p);
  }
  return atom;
}

int
getrootptr(int *x, int *y)
{
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
    &real, &format, &n, &extra, (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void
grabbuttons(Client *c, int focused)
{
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
        BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabButton(dpy, buttons[i].button,
            buttons[i].mask | modifiers[j],
            c->win, False, BUTTONMASK,
            GrabModeAsync, GrabModeSync, None, None);
  }
}

void
grabkeys(void)
{
  updatenumlockmask();
  {
    unsigned int i, j, k;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    int start, end, skip;
    KeySym *syms;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XDisplayKeycodes(dpy, &start, &end);
    syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
    if (!syms)
      return;
    for (k = start; k <= end; k++)
      for (i = 0; i < LENGTH(keys); i++)
        /* skip modifier codes, we do that ourselves */
        if (keys[i].keysym == syms[(k - start) * skip])
          for (j = 0; j < LENGTH(modifiers); j++)
            XGrabKey(dpy, k,
               keys[i].mod | modifiers[j],
               root, True,
               GrabModeAsync, GrabModeAsync);
    XFree(syms);
  }
}

void
incnmaster(const Arg *arg)
{
  selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
  arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
    && unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  for (i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym
    && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
    && keys[i].func)
      keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (!sendevent(selmon->sel, wmatom[WMDelete])) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void
manage(Window w, XWindowAttributes *wa)
{
  Client *c, *t = NULL, *term = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  c->pid = winpid(w);
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  updatetitle(c);
  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  } else {
    c->mon = selmon;
    applyrules(c);
    term = termforwin(c);
  }

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);
  c->bw = borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
  grabbuttons(c, 0);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attachbottom(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
    (unsigned char *) &(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, 0);
  c->mon->sel = c;
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  if (term)
    swallow(term, c);
  focus(NULL);
}

void
mappingnotify(XEvent *e)
{
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void
maprequest(XEvent *e)
{
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
  unsigned int n = 0;
  int oh, ov, ih, iv;
  Client *c;

  getgaps(m, &oh, &ov, &ih, &iv, &n);
  
  if (n > 0) /* override layout symbol */
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    if (selmon->showbar)
      resize(c, m->wx + oh, m->wy + ov, m->ww - 2 * c->bw - 2 * oh, m->wh - 2 * c->bw - 2* ov, 0);
    else
      resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
   
}

void
fibonacci(Monitor *m, int s)
{
  unsigned int i, n;
  int nx, ny, nw, nh;
  int oh, ov, ih, iv;
  int nv, hrest = 0, wrest = 0, r = 1;
  Client *c;

  getgaps(m, &oh, &ov, &ih, &iv, &n);
  if (n == 0)
    return;

  nx = m->wx + ov;
  ny = m->wy + oh;
  nw = m->ww - 2*ov;
  nh = m->wh - 2*oh;

  for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
    if (r) {
      if ((i % 2 && (nh - ih) / 2 <= (bh + 2*c->bw))
         || (!(i % 2) && (nw - iv) / 2 <= (bh + 2*c->bw))) {
        r = 0;
      }
      if (r && i < n - 1) {
        if (i % 2) {
          nv = (nh - ih) / 2;
          hrest = nh - 2*nv - ih;
          nh = nv;
        } else {
          nv = (nw - iv) / 2;
          wrest = nw - 2*nv - iv;
          nw = nv;
        }

        if ((i % 4) == 2 && !s)
          nx += nw + iv;
        else if ((i % 4) == 3 && !s)
          ny += nh + ih;
      }

      if ((i % 4) == 0) {
        if (s) {
          ny += nh + ih;
          nh += hrest;
        }
        else {
          nh -= hrest;
          ny -= nh + ih;
        }
      }
      else if ((i % 4) == 1) {
        nx += nw + iv;
        nw += wrest;
      }
      else if ((i % 4) == 2) {
        ny += nh + ih;
        nh += hrest;
        if (i < n - 1)
          nw += wrest;
      }
      else if ((i % 4) == 3) {
        if (s) {
          nx += nw + iv;
          nw -= wrest;
        } else {
          nw -= wrest;
          nx -= nw + iv;
          nh += hrest;
        }
      }
      if (i == 0)  {
        if (n != 1) {
          nw = (m->ww - iv - 2*ov) - (m->ww - iv - 2*ov) * (1 - m->mfact);
          wrest = 0;
        }
        ny = m->wy + oh;
      }
      else if (i == 1)
        nw = m->ww - nw - iv - 2*ov;
      i++;
    }

    resize(c, nx, ny, nw - (2*c->bw), nh - (2*c->bw), False);
  }
}

void
dwindle(Monitor *mon) {
  fibonacci(mon, 1);
}

void
spiral(Monitor *mon) {
  fibonacci(mon, 0);
}

void
motionnotify(XEvent *e)
{
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  mon = m;
}

void
placemouse(const Arg *arg)
{
  int x, y, px, py, ocx, ocy, nx = -9999, ny = -9999, freemove = 0;
  Client *c, *r = NULL, *at, *prevr;
  Monitor *m;
  XEvent ev;
  XWindowAttributes wa;
  Time lasttime = 0;
  int attachmode, prevattachmode;
  attachmode = prevattachmode = -1;

  if (!(c = selmon->sel) || !c->mon->lt[c->mon->sellt]->arrange) /* no support for placemouse when floating layout is used */
    return;
  if (c->isfullscreen) /* no support placing fullscreen windows by mouse */
    return;
  restack(selmon);
  prevr = c;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
    None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;

  c->isfloating = 0;
  c->beingmoved = 1;

  XGetWindowAttributes(dpy, c->win, &wa);
  ocx = wa.x;
  ocy = wa.y;

  if (arg->i == 2) // warp cursor to client center
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, WIDTH(c) / 2, HEIGHT(c) / 2);

  if (!getrootptr(&x, &y))
    return;

  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);

      if (!freemove && (abs(nx - ocx) > snap || abs(ny - ocy) > snap))
        freemove = 1;

      if (freemove)
        XMoveWindow(dpy, c->win, nx, ny);

      if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != selmon)
        selmon = m;

      if (arg->i == 1) { // tiled position is relative to the client window center point
        px = nx + wa.width / 2;
        py = ny + wa.height / 2;
      } else { // tiled position is relative to the mouse cursor
        px = ev.xmotion.x;
        py = ev.xmotion.y;
      }

      r = recttoclient(px, py, 1, 1);

      if (!r || r == c)
        break;

      attachmode = 0; // below
      if (((float)(r->y + r->h - py) / r->h) > ((float)(r->x + r->w - px) / r->w)) {
        if (abs(r->y - py) < r->h / 2)
          attachmode = 1; // above
      } else if (abs(r->x - px) < r->w / 2)
        attachmode = 1; // above

      if ((r && r != prevr) || (attachmode != prevattachmode)) {
        detachstack(c);
        detach(c);
        if (c->mon != r->mon) {
          arrangemon(c->mon);
          c->tags = r->mon->tagset[r->mon->seltags];
        }

        c->mon = r->mon;
        r->mon->sel = r;

        if (attachmode) {
          if (r == r->mon->clients)
            attach(c);
          else {
            for (at = r->mon->clients; at->next != r; at = at->next);
            c->next = at->next;
            at->next = c;
          }
        } else {
          c->next = r->next;
          r->next = c;
        }

        attachstack(c);
        arrangemon(r->mon);
        prevr = r;
        prevattachmode = attachmode;
      }
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);

  if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != c->mon) {
    detach(c);
    detachstack(c);
    arrangemon(c->mon);
    c->mon = m;
    c->tags = m->tagset[m->seltags];
    attach(c);
    attachstack(c);
    selmon = m;
  }

  focus(c);
  c->beingmoved = 0;

  if (nx != -9999)
    resize(c, nx, ny, c->w, c->h, 0);
  arrangemon(c->mon);
}

void
movemouse(const Arg *arg)
{
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
    None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);
      if (abs(selmon->wx - nx) < snap)
        nx = selmon->wx;
      else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
        nx = selmon->wx + selmon->ww - WIDTH(c);
      if (abs(selmon->wy - ny) < snap)
        ny = selmon->wy;
      else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
        ny = selmon->wy + selmon->wh - HEIGHT(c);
      if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
      && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
        togglefloating(NULL);
      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
        resize(c, nx, ny, c->w, c->h, 1);
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void
moveorplace(const Arg *arg) {
  if ((!selmon->lt[selmon->sellt]->arrange || (selmon->sel && selmon->sel->isfloating)))
    movemouse(arg);
  else
    placemouse(arg);
}

Client *
nexttiled(Client *c)
{
  for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
  return c;
}

void
pop(Client *c)
{
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch(ev->atom) {
    default: break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
        (c->isfloating = (wintoclient(trans)) != NULL))
        arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      c->hintsvalid = 0;
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
      drawbars();
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName])
      updatetitle(c);
    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void
quit(const Arg *arg)
{
  if(arg->i) restart = 1;
  running = 0;
}

Client *
recttoclient(int x, int y, int w, int h)
{
  Client *c, *r = NULL;
  int a, area = 0;

  for (c = nexttiled(selmon->clients); c; c = nexttiled(c->next)) {
    if ((a = INTERSECTC(x, y, w, h, c)) > area) {
      area = a;
      r = c;
    }
  }
  return r;
}


Monitor *
recttomon(int x, int y, int w, int h)
{
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
  if (applysizehints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
  XWindowChanges wc;

  c->oldx = c->x; c->x = wc.x = x;
  c->oldy = c->y; c->y = wc.y = y;
  c->oldw = c->w; c->w = wc.width = w;
  c->oldh = c->h; c->h = wc.height = h;
  
  if (c->beingmoved) return;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
  configure(c);
  XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
  int ocx, ocy, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
    None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;

  if (c->isfloating || NULL == c->mon->lt[c->mon->sellt]->arrange) {
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  } else {
    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
      selmon->mx + (selmon->ww * selmon->mfact),
      selmon->my + (selmon->wh / 2)
    );
  }

  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);

      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
        resize(c, c->x, c->y, nw, nh, 1);
      break;
    }
  } while (ev.type != ButtonRelease);

  if (c->isfloating || NULL == c->mon->lt[c->mon->sellt]->arrange) {
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  } else {
    selmon->mfact = (double) (ev.xmotion.x_root - selmon->mx) / (double) selmon->ww;
    arrange(selmon);
    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
      selmon->mx + (selmon->ww * selmon->mfact),
      selmon->my + (selmon->wh / 2)
    );
  }

  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void
restack(Monitor *m)
{
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawbar(m);
  if (!m->sel)
    return;
  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);
  if (m->lt[m->sellt]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev))
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa)
      || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;
      if (XGetTransientForHint(dpy, wins[i], &d1)
      && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void
sendmon(Client *c, Monitor *m)
{
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachstack(c);
  c->mon = m;
  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
  attachbottom(c);
  attachstack(c);
  focus(NULL);
  arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
  long data[] = { state, None };

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
    PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
  int n;
  Atom *protocols;
  int exists = 0;
  XEvent ev;

  if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
    while (!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wmatom[WMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  }
  return exists;
}

void
setfocus(Client *c)
{
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow],
      XA_WINDOW, 32, PropModeReplace,
      (unsigned char *) &(c->win), 1);
  }
  sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
      PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
    c->isfullscreen = 1;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isfullscreen){
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
      PropModeReplace, (unsigned char*)0, 0);
    c->isfullscreen = 0;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void
setlayout(const Arg *arg)
{
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
    selmon->sellt ^= 1;
  if (arg && arg->v)
    selmon->lt[selmon->sellt] = (Layout *)arg->v;
  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
  float f;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
  if (f < 0.05 || f > 0.95)
    return;
  selmon->mfact = f;
  arrange(selmon);
}

void
setup(void)
{
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sigaction sa;

  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0);

  sigchld(0);

  signal(SIGHUP, sighup);
  signal(SIGTERM, sigterm);

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  drw = drw_create(dpy, screen, root, sw, sh);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    die("no fonts could be loaded.");
  lrpad = drw->fonts->h + horizpadbar;
  bh = user_bh ? user_bh : drw->fonts->h + 2 + vertpadbar;
  updategeom();
  sp = sidepad;
  vp = (topbar == 1) ? vertpad : - vertpad;

  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] = drw_cur_create(drw, XC_sizing);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);
  /* init bars */
  updatebars();
  updatestatus();
  updatebarpos(selmon);
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
    PropModeReplace, (unsigned char *) "dwm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
    PropModeReplace, (unsigned char *) netatom, NetLast);
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
    |ButtonPressMask|PointerMotionMask|EnterWindowMask
    |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  focus(NULL);
}

void
seturgent(Client *c, int urg)
{
  XWMHints *wmh;

  c->isurgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void
showhide(Client *c)
{
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    showhide(c->snext);
  } else {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void 
sigchld(int unused)
{
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    die("can't install SIGCHLD handler:");
  while (0 < waitpid(-1, NULL, WNOHANG))
    ;
}

void
sighup(int unused)
{
  Arg a = {.i = 1};
  quit(&a);
}

void
sigterm(int unused)
{
  Arg a = {.i = 0};
  quit(&a);
}

void
spawn(const Arg *arg)
{
  struct sigaction sa;

  if (arg->v == dmenucmd)
    dmenumon[0] = '0' + selmon->num;
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    execvp(((char **)arg->v)[0], (char **)arg->v);
    die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
  }
}

void
tag(const Arg *arg)
{
  if (selmon->sel && arg->ui & TAGMASK) {
    selmon->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
  }
}

#ifdef MULTI_MONITOR
void
tagmon(const Arg *arg)
{
  if (!selmon->sel || !mons->next)
    return;
  sendmon(selmon->sel, dirtomon(arg->i));
}
#endif

void
tile(Monitor *m)
{
  unsigned int i, n;
  int oh, ov, ih, iv;
  int mx = 0, my = 0, mh = 0, mw = 0;
  int sx = 0, sy = 0, sh = 0, sw = 0;
  float mfacts, sfacts;
  int mrest, srest;
  Client *c;

  getgaps(m, &oh, &ov, &ih, &iv, &n);
  if (n == 0)
    return;

  sx = mx = m->wx + ov;
  sy = my = m->wy + oh;
  mh = m->wh - 2*oh - ih * (MIN(n, m->nmaster) - 1);
  sh = m->wh - 2*oh - ih * (n - m->nmaster - 1);
  sw = mw = m->ww - 2*ov;

  if (m->nmaster && n > m->nmaster) {
    sw = (mw - iv) * (1 - m->mfact);
    mw = mw - iv - sw;
    sx = mx + mw + iv;
  }

  getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

  for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
    if (i < m->nmaster) {
      resize(c, mx, my, mw - (2*c->bw), (mh / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
      my += HEIGHT(c) + ih;
    } else {
      resize(c, sx, sy, sw - (2*c->bw), (sh / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), 0);
      sy += HEIGHT(c) + ih;
    }
}

void
togglebar(const Arg *arg)
{
  selmon->showbar = !selmon->showbar;
  updatebarpos(selmon);
  XMoveResizeWindow(dpy, selmon->barwin, selmon->wx + sp, selmon->by + vp, selmon->ww - 2 * sp, bh);
  arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
    return;
  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  if (selmon->sel->isfloating)
    resize(selmon->sel, selmon->sel->x, selmon->sel->y,
      selmon->sel->w, selmon->sel->h, 0);
  arrange(selmon);
}

void
toggletag(const Arg *arg)
{
  unsigned int newtags;

  if (!selmon->sel)
    return;
  newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
  }
}

void
toggleview(const Arg *arg)
{
  unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

  if (newtagset) {
    selmon->tagset[selmon->seltags] = newtagset;
    focus(NULL);
    arrange(selmon);
  }
}

void
unfocus(Client *c, int setfocus)
{
  if (!c)
    return;
  grabbuttons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void
unmanage(Client *c, int destroyed)
{
  Monitor *m = c->mon;
  XWindowChanges wc;

  if (c->swallowing) {
    unswallow(c);
    return;
  }

  Client *s = swallowingclient(c->win);
  if (s) {
    free(s->swallowing);
    s->swallowing = NULL;
    arrange(m);
    focus(NULL);
    return;
  }

  detach(c);
  detachstack(c);
  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XSelectInput(dpy, c->win, NoEventMask);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);
  if (!s) {
    arrange(m);
    focus(NULL);
    updateclientlist();
  }
}

void
unmapnotify(XEvent *e)
{
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, 0);
  }
}

void
updatebars(void)
{
  Monitor *m;
  XSetWindowAttributes wa = {
    .override_redirect = True,
    .background_pixmap = ParentRelative,
    .event_mask = ButtonPressMask|ExposureMask
  };
  XClassHint ch = {"dwm", "dwm"};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    m->barwin = XCreateWindow(dpy, root, m->wx + sp, m->by + vp, m->ww - 2 * sp, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->barwin);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void
updatebarpos(Monitor *m)
{
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar) {
    m->wh = m->wh - vertpad - bh;
    m->by = m->topbar ? m->wy : m->wy + m->wh + vertpad;
    m->wy = m->topbar ? m->wy + bh + vp : m->wy;
  } else
    m->by = -bh - vp;
}

void
updateclientlist(void)
{
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList],
        XA_WINDOW, 32, PropModeAppend,
        (unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++);
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;

    /* new monitors if nn > n */
    for (i = n; i < nn; i++) {
      for (m = mons; m && m->next; m = m->next);
      if (m)
        m->next = createmon();
      else
        mons = createmon();
    }
    for (i = 0, m = mons; i < nn && m; m = m->next, i++)
      if (i >= n
      || unique[i].x_org != m->mx || unique[i].y_org != m->my
      || unique[i].width != m->mw || unique[i].height != m->mh)
      {
        dirty = 1;
        m->num = i;
        m->mx = m->wx = unique[i].x_org;
        m->my = m->wy = unique[i].y_org;
        m->mw = m->ww = unique[i].width;
        m->mh = m->wh = unique[i].height;
        updatebarpos(m);
      }
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = mons; m && m->next; m = m->next);
      while ((c = m->clients)) {
        dirty = 1;
        m->clients = c->next;
        detachstack(c);
        c->mon = mons;
        attachbottom(c);
        attachstack(c);
      }
      if (m == selmon)
        selmon = mons;
      cleanupmon(m);
    }
    free(unique);
  } else
#endif /* XINERAMA */
  { /* default monitor setup */
    if (!mons)
      mons = createmon();
    if (mons->mw != sw || mons->mh != sh) {
      dirty = 1;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      updatebarpos(mons);
    }
  }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void
updatenumlockmask(void)
{
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j]
        == XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
  c->hintsvalid = 1;
}

void
updatestatus(void)
{
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "dwm-"VERSION);
  drawbar(selmon);
}

void
updatetitle(Client *c)
{
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, 1);
  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
      c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = 0;
    XFree(wmh);
  }
}

void
view(const Arg *arg)
{
  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;
  selmon->seltags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK)
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
  focus(NULL);
  arrange(selmon);
}

pid_t
winpid(Window w)
{

	pid_t result = 0;

#ifdef __linux__
	xcb_res_client_id_spec_t spec = {0};
	spec.client = w;
	spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
	xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

	if (!r)
		return (pid_t)0;

	xcb_res_client_id_value_iterator_t i = xcb_res_query_client_ids_ids_iterator(r);
	for (; i.rem; xcb_res_client_id_value_next(&i)) {
		spec = i.data->spec;
		if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(i.data);
			result = *t;
			break;
		}
	}

	free(r);

	if (result == (pid_t)-1)
		result = 0;

#endif /* __linux__ */

#ifdef __OpenBSD__
        Atom type;
        int format;
        unsigned long len, bytes;
        unsigned char *prop;
        pid_t ret;

        if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", 0), 0, 1, False, AnyPropertyType, &type, &format, &len, &bytes, &prop) != Success || !prop)
               return 0;

        ret = *(pid_t*)prop;
        XFree(prop);
        result = ret;

#endif /* __OpenBSD__ */
	return result;
}

pid_t
getparentprocess(pid_t p)
{
	unsigned int v = 0;

#ifdef __linux__
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
#endif /* __linux__*/

#ifdef __OpenBSD__
	int n;
	kvm_t *kd;
	struct kinfo_proc *kp;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, NULL);
	if (!kd)
		return 0;

	kp = kvm_getprocs(kd, KERN_PROC_PID, p, sizeof(*kp), &n);
	v = kp->p_ppid;
#endif /* __OpenBSD__ */

	return (pid_t)v;
}

int
isdescprocess(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = getparentprocess(c);

	return (int)c;
}

Client *
termforwin(const Client *w)
{
	Client *c;
	Monitor *m;

	if (!w->pid || w->isterminal)
		return NULL;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
				return c;
		}
	}

	return NULL;
}

Client *
swallowingclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->swallowing && c->swallowing->win == w)
				return c;
		}
	}

	return NULL;
}

Client *
wintoclient(Window w)
{
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

Monitor *
wintomon(Window w)
{
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
  if (ee->error_code == BadWindow
  || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
  || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
  || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
  || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
  || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
  || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
  || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
  || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
    ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
  return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
  die("dwm: another window manager is already running");
  return -1;
}

void
zoom(const Arg *arg)
{
  Client *c = selmon->sel;

  if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
    return;
  if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
    return;
  pop(c);
}

/* Internals */

/* Settings */
#if !PERTAG_PATCH
static int enablegaps = 1;
#endif // PERTAG_PATCH


void
getgaps(Monitor *m, int *oh, int *ov, int *ih, int *iv, unsigned int *nc)
{
  unsigned int n, oe, ie;
  #if PERTAG_PATCH
  oe = ie = selmon->pertag->enablegaps[selmon->pertag->curtag];
  #else
  oe = ie = enablegaps;
  #endif // PERTAG_PATCH
  Client *c;

  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
  if (smartgaps && n == 1) {
    oe = 0; // outer gaps disabled when only one client
  }

  *oh = m->gappoh*oe; // outer horizontal gap
  *ov = m->gappov*oe; // outer vertical gap
  *ih = m->gappih*ie; // inner horizontal gap
  *iv = m->gappiv*ie; // inner vertical gap
  *nc = n;            // number of clients
}

void
getfacts(Monitor *m, int msize, int ssize, float *mf, float *sf, int *mr, int *sr)
{
  unsigned int n;
  float mfacts, sfacts;
  int mtotal = 0, stotal = 0;
  Client *c;

  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
  mfacts = MIN(n, m->nmaster);
  sfacts = n - m->nmaster;

  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
    if (n < m->nmaster)
      mtotal += msize / mfacts;
    else
      stotal += ssize / sfacts;

  *mf = mfacts; // total factor of master area
  *sf = sfacts; // total factor of stack area
  *mr = msize - mtotal; // the remainder (rest) of pixels after an even master split
  *sr = ssize - stotal; // the remainder (rest) of pixels after an even stack split
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm-"VERSION);
  else if (argc != 1)
    die("usage: dwm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display\n");
  if (!(xcon = XGetXCBConnection(dpy)))
    die("dwm : cannot get xcb connection\n");
  checkotherwm();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();
  run();
  if(restart) execvp(argv[0], argv);
  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
