/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <signal.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <webkit/webkit.h>
#include <glib/gstdio.h>
#include <JavaScriptCore/JavaScript.h>

#define LENGTH(x)               (sizeof x / sizeof x[0])
#define CLEANMASK(mask)         (mask & ~(GDK_MOD2_MASK))

typedef union Arg Arg;
union Arg {
	gboolean b;
	gint i;
	const void *v;
};

typedef struct Client {
	GtkWidget *win, *scroll, *vbox, *indicator;
	GtkWidget **items;
	WebKitWebView *view;
	WebKitDownload *download;
	char *title, *linkhover;
	const char *uri, *needle;
	gint progress;
	struct Client *next;
	gboolean zoomed;
} Client;

typedef struct {
	char *label;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
} Item;

typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
} Key;

static Display *dpy;
static Atom uriprop, findprop;
static Client *clients = NULL;
static GdkNativeWindow embed = 0;
static gboolean showxid = FALSE;
static int ignorexprop = 0;
static char winid[64];
static char *progname;

static char *buildpath(const char *path);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);
static void context(WebKitWebView *v, GtkMenu *m, Client *c);
static char *copystr(char **str, const char *src);
static WebKitWebView *createwindow(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static gboolean decidedownload(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, gchar *m,  WebKitWebPolicyDecision *p, Client *c);
static gboolean decidewindow(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, WebKitWebNavigationAction *n, WebKitWebPolicyDecision *p, Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(char *str);
static void download(Client *c, const Arg *arg);
static void drawindicator(Client *c);
static gboolean exposeindicator(GtkWidget *w, GdkEventExpose *e, Client *c);
static void find(Client *c, const Arg *arg);
static const char *getatom(Client *c, Atom a);
static char *geturi(Client *c);
static gboolean initdownload(WebKitWebView *v, WebKitDownload *o, Client *c);
static void itemclick(GtkMenuItem *mi, Client *c);
static gboolean keypress(GtkWidget *w, GdkEventKey *ev, Client *c);
static void linkhover(WebKitWebView *v, const char* t, const char* l, Client *c);
static void loadstatuschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static void newwindow(Client *c, const Arg *arg);
static void newrequest(WebKitWebView *v, WebKitWebFrame *f, WebKitWebResource *r, WebKitNetworkRequest *req, WebKitNetworkResponse *res, Client *c);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static void print(Client *c, const Arg *arg);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event, gpointer d);
static void progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void reload(Client *c, const Arg *arg);
static void resize(GtkWidget *w, GtkAllocation *a, Client *c);
static void scroll(Client *c, const Arg *arg);
static void setatom(Client *c, Atom a, const char *v);
static void setup(void);
static void sigchld(int unused);
static void source(Client *c, const Arg *arg);
static void spawn(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView *v, WebKitWebFrame* frame, const char* title, Client *c);
static void update(Client *c);
static void updatedownload(WebKitDownload *o, GParamSpec *pspec, Client *c);
static void updatewinid(Client *c);
static void usage(void);
static void windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c);
static void zoom(Client *c, const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

char *
buildpath(const char *path) {
	char *apath, *p;
	FILE *f;

	/* creating directory */
	if(path[0] == '/')
		apath = g_strdup(path);
	else
		apath = g_strconcat(g_get_home_dir(), "/", path, NULL);
	if((p = strrchr(apath, '/'))) {
		*p = '\0';
		g_mkdir_with_parents(apath, 0755);
		*p = '/';
	}
	/* creating file (gives error when apath ends with "/") */
	if((f = g_fopen(apath, "a")))
		fclose(f);
	return apath;
}

void
cleanup(void) {
	while(clients)
		destroyclient(clients);
	g_free(cookiefile);
	g_free(dldir);
	g_free(scriptfile);
	g_free(stylefile);
}

void
clipboard(Client *c, const Arg *arg) {
	gboolean paste = *(gboolean *)arg;

	if(paste)
		gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), pasteuri, c);
	else
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), c->linkhover ? c->linkhover : geturi(c), -1);
}

void
context(WebKitWebView *v, GtkMenu *m, Client *c) {
	int i;
	GtkContainer *parent;

	gtk_widget_hide_all(GTK_WIDGET(m));
	gtk_widget_show(GTK_WIDGET(m));
	for(i = 0; i < LENGTH(items); i++) {
		parent = GTK_CONTAINER(gtk_widget_get_parent(c->items[i]));
		if(parent)
			gtk_container_remove(parent, c->items[i]);
		gtk_menu_shell_append(GTK_MENU_SHELL(m), c->items[i]);
		gtk_widget_show(c->items[i]);
	}
}

char *
copystr(char **str, const char *src) {
	char *tmp;
	tmp = g_strdup(src);

	if(str && *str) {
		g_free(*str);
		*str = tmp;
	}
	return tmp;
}

WebKitWebView *
createwindow(WebKitWebView  *v, WebKitWebFrame *f, Client *c) {
	Client *n = newclient();
	return n->view;
}

gboolean
decidedownload(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, gchar *m,  WebKitWebPolicyDecision *p, Client *c) {
	if(!webkit_web_view_can_show_mime_type(v, m)) {
		webkit_web_policy_decision_download(p);
		return TRUE;
	}
	return FALSE;
}

gboolean
decidewindow(WebKitWebView *view, WebKitWebFrame *f, WebKitNetworkRequest *r, WebKitWebNavigationAction *n, WebKitWebPolicyDecision *p, Client *c) {
	Arg arg;

	if(webkit_web_navigation_action_get_reason(n) == WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED) {
		webkit_web_policy_decision_ignore(p);
		arg.v = (void *)webkit_network_request_get_uri(r);
		newwindow(NULL, &arg);
		return TRUE;
	}
	return FALSE;
}

void
destroyclient(Client *c) {
	int i;
	Client *p;

	gtk_widget_destroy(c->indicator);
	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);
	for(i = 0; i < LENGTH(items); i++)
		gtk_widget_destroy(c->items[i]);
	free(c->items);

	for(p = clients; p && p->next != c; p = p->next);
	if(p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
	if(clients == NULL)
		gtk_main_quit();
}

void
destroywin(GtkWidget* w, Client *c) {
	destroyclient(c);
}

void
die(char *str) {
	fputs(str, stderr);
	exit(EXIT_FAILURE);
}

void
download(Client *c, const Arg *arg) {
	char *uri;
	WebKitNetworkRequest *r;
	WebKitDownload       *dl;

	if(arg->v)
		uri = (char *)arg->v;
	else
		uri = c->linkhover ? c->linkhover : geturi(c);
	r = webkit_network_request_new(uri);
	dl = webkit_download_new(r);
	initdownload(c->view, dl, c);
}

void
drawindicator(Client *c) {
	gint width;
	const char *uri;
	GtkWidget *w;
	GdkGC *gc;
	GdkColor fg;

	uri = getatom(c, uriprop);
	w = c->indicator;
	width = c->progress * w->allocation.width / 100;
	gc = gdk_gc_new(w->window);
	gdk_color_parse(strstr(uri, "https://") == uri ?
			progress_trust : progress, &fg);
	gdk_gc_set_rgb_fg_color(gc, &fg);
	gdk_draw_rectangle(w->window,
			w->style->bg_gc[GTK_WIDGET_STATE(w)],
			TRUE, 0, 0, w->allocation.width, w->allocation.height);
	gdk_draw_rectangle(w->window, gc, TRUE, 0, 0, width,
			w->allocation.height);
	g_object_unref(gc);
}

gboolean
exposeindicator(GtkWidget *w, GdkEventExpose *e, Client *c) {
	drawindicator(c);
	return TRUE;
}

void
find(Client *c, const Arg *arg) {
	const char *s;

	s = getatom(c, findprop);
	gboolean forward = *(gboolean *)arg;
	webkit_web_view_search_text(c->view, s, FALSE, forward, TRUE);
}

const char *
getatom(Client *c, Atom a) {
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XGetWindowProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window),
			a, 0L, BUFSIZ, False, XA_STRING,
			&adummy, &idummy, &ldummy, &ldummy, &p);
	if(p)
		strncpy(buf, (char *)p, LENGTH(buf)-1);
	else
		buf[0] = '\0';
	XFree(p);
	return buf;
}

char *
geturi(Client *c) {
	char *uri;

	if(!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

gboolean
initdownload(WebKitWebView *view, WebKitDownload *o, Client *c) {
	const char *filename;
	char *uri, *html;

	stop(c, NULL);
	c->download = o;
	filename = webkit_download_get_suggested_filename(o);
	if(!strcmp("", filename))
		filename = "index.html";
	uri = g_strconcat("file://", dldir, "/", filename, NULL);
	webkit_download_set_destination_uri(c->download, uri);
	c->progress = 0;
	g_free(uri);
	html = g_strdup_printf("Download <b>%s</b>...", filename);
	webkit_web_view_load_html_string(c->view, html,
			webkit_download_get_uri(c->download));
	g_signal_connect(c->download, "notify::progress", G_CALLBACK(updatedownload), c);
	g_signal_connect(c->download, "notify::status", G_CALLBACK(updatedownload), c);
	webkit_download_start(c->download);
	
	c->title = copystr(&c->title, filename);
	update(c);
	g_free(html);
	return TRUE;
}

void
itemclick(GtkMenuItem *mi, Client *c) {
	int i;
	const char *label;

	label = gtk_menu_item_get_label(mi);
	for(i = 0; i < LENGTH(items); i++)
		if(!strcmp(items[i].label, label))
			items[i].func(c, &(items[i].arg));
}

gboolean
keypress(GtkWidget* w, GdkEventKey *ev, Client *c) {
	guint i;
	gboolean processed = FALSE;

	updatewinid(c);
	for(i = 0; i < LENGTH(keys); i++) {
		if(gdk_keyval_to_lower(ev->keyval) == keys[i].keyval
				&& CLEANMASK(ev->state) == keys[i].mod
				&& keys[i].func) {
			keys[i].func(c, &(keys[i].arg));
			processed = TRUE;
		}
	}
	return processed;
}

void
linkhover(WebKitWebView *v, const char* t, const char* l, Client *c) {
	if(l)
		c->linkhover = copystr(&c->linkhover, l);
	else if(c->linkhover) {
		free(c->linkhover);
		c->linkhover = NULL;
	}
	update(c);
}

void
loadstatuschange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	switch(webkit_web_view_get_load_status (c->view)) {
	case WEBKIT_LOAD_COMMITTED:
		setatom(c, uriprop, geturi(c));
		break;
	case WEBKIT_LOAD_FINISHED:
		c->progress = 0;
		update(c);
		break;
	case WEBKIT_LOAD_PROVISIONAL:
	case WEBKIT_LOAD_FIRST_VISUALLY_NON_EMPTY_LAYOUT:
		break;
	}
}

void
loaduri(Client *c, const Arg *arg) {
	char *u;
	const char *uri = (char *)arg->v;
	Arg a = { .b = FALSE };

	if(strcmp(uri, "") == 0)
		return;
	u = g_strrstr(uri, "://") ? g_strdup(uri)
		: g_strdup_printf("http://%s", uri);
	/* prevents endless loop */
	if(c->uri && strcmp(u, c->uri) == 0) {
		reload(c, &a);
	}
	else {
		webkit_web_view_load_uri(c->view, u);
		c->progress = 0;
		c->title = copystr(&c->title, u);
		g_free(u);
		update(c);
	}
}

void
navigate(Client *c, const Arg *arg) {
	int steps = *(int *)arg;
	webkit_web_view_go_back_or_forward(c->view, steps);
}

Client *
newclient(void) {
	int i;
	Client *c;
	WebKitWebSettings *settings;
	GdkGeometry hints = { 1, 1 };
	char *uri, *ua;

	if(!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");
	/* Window */
	if(embed) {
		c->win = gtk_plug_new(embed);
	}
	else {
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		/* TA:  20091214:  Despite what the GNOME docs say, the ICCCM
		 * is always correct, so we should still call this function.
		 * But when doing so, we *must* differentiate between a
		 * WM_CLASS and a resource on the window.  By convention, the
		 * window class (WM_CLASS) is capped, while the resource is in
		 * lowercase.   Both these values come as a pair.
		 */
		gtk_window_set_wmclass(GTK_WINDOW(c->win), "surf", "surf");

		/* TA:  20091214:  And set the role here as well -- so that
		 * sessions can pick this up.
		 */
		gtk_window_set_role(GTK_WINDOW(c->win), "Surf");
	}
	gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);
	g_signal_connect(G_OBJECT(c->win), "destroy", G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(c->win), "key-press-event", G_CALLBACK(keypress), c);
	g_signal_connect(G_OBJECT(c->win), "size-allocate", G_CALLBACK(resize), c);

	if(!(c->items = calloc(1, sizeof(GtkWidget *) * LENGTH(items))))
		die("Cannot malloc!\n");

	/* contextmenu */
	for(i = 0; i < LENGTH(items); i++) {
		c->items[i] = gtk_menu_item_new_with_label(items[i].label);
		g_signal_connect(G_OBJECT(c->items[i]), "activate",
				G_CALLBACK(itemclick), c);
	}

	/* VBox */
	c->vbox = gtk_vbox_new(FALSE, 0);

	/* Scrolled Window */
	c->scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
			GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	/* Webview */
	c->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	g_signal_connect(G_OBJECT(c->view), "title-changed", G_CALLBACK(titlechange), c);
	g_signal_connect(G_OBJECT(c->view), "hovering-over-link", G_CALLBACK(linkhover), c);
	g_signal_connect(G_OBJECT(c->view), "create-web-view", G_CALLBACK(createwindow), c);
	g_signal_connect(G_OBJECT(c->view), "new-window-policy-decision-requested", G_CALLBACK(decidewindow), c);
	g_signal_connect(G_OBJECT(c->view), "mime-type-policy-decision-requested", G_CALLBACK(decidedownload), c);
	g_signal_connect(G_OBJECT(c->view), "download-requested", G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(c->view), "window-object-cleared", G_CALLBACK(windowobjectcleared), c);
	g_signal_connect(G_OBJECT(c->view), "populate-popup", G_CALLBACK(context), c);
	g_signal_connect(G_OBJECT(c->view), "notify::load-status", G_CALLBACK(loadstatuschange), c);
	g_signal_connect(G_OBJECT(c->view), "notify::progress", G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view), "resource-request-starting", G_CALLBACK(newrequest), c);

	/* Indicator */
	c->indicator = gtk_drawing_area_new();
	gtk_widget_set_size_request(c->indicator, 0, 2);
	g_signal_connect (G_OBJECT (c->indicator), "expose_event",
			G_CALLBACK (exposeindicator), c);

	/* Arranging */
	gtk_container_add(GTK_CONTAINER(c->scroll), GTK_WIDGET(c->view));
	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->indicator);

	/* Setup */
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->indicator, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->scroll, TRUE, TRUE, 0, GTK_PACK_START);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_show(c->vbox);
	gtk_widget_show(c->indicator);
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gtk_window_set_geometry_hints(GTK_WINDOW(c->win), NULL, &hints, GDK_HINT_MIN_SIZE);
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	webkit_web_view_set_full_content_zoom(c->view, TRUE);
	settings = webkit_web_view_get_settings(c->view);
	if(!(ua = getenv("SURF_USERAGENT")))
		ua = useragent;
	g_object_set(G_OBJECT(settings), "user-agent", ua, NULL);
	uri = g_strconcat("file://", stylefile, NULL);
	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", uri, NULL);
	g_free(uri);
	setatom(c, findprop, "");
	setatom(c, uriprop, "");

	c->download = NULL;
	c->title = NULL;
	c->next = clients;
	clients = c;
	if(showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		printf("%u\n", (guint)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
		fflush(NULL);
	}
	return c;
}

void func(const char *name, const char *value, void *dummy) {
printf("%s = %s\n", name, value);
}


static void newrequest(WebKitWebView *v, WebKitWebFrame *f, WebKitWebResource *r, WebKitNetworkRequest *req, WebKitNetworkResponse *res, Client *c) {
	SoupMessage *msg = webkit_network_request_get_message(req);
	SoupMessageHeaders *h;
	if(!msg)
		return;
	h = msg->request_headers;
	soup_message_headers_foreach(h, func, NULL);
}

void
newwindow(Client *c, const Arg *arg) {
	guint i = 0;
	const char *cmd[7], *uri;
	const Arg a = { .v = (void *)cmd };
	char tmp[64];

	cmd[i++] = progname;
	if(embed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%u\n", (int)embed);
		cmd[i++] = tmp;
	}
	if(showxid) {
		cmd[i++] = "-x";
	}
	cmd[i++] = "--";
	uri = arg->v ? (char *)arg->v : c->linkhover;
	if(uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d) {
	Arg arg = {.v = text };
	if(text != NULL)
		loaduri((Client *) d, &arg);
}

void
print(Client *c, const Arg *arg) {
	webkit_web_frame_print(webkit_web_view_get_main_frame(c->view));
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d) {
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Arg arg;

	if(((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if(ignorexprop)
			ignorexprop--;
		else if(ev->state == PropertyNewValue) {
			if(ev->atom == uriprop) {
				arg.v = getatom(c, uriprop);
				loaduri(c, &arg);
			}
			else if(ev->atom == findprop) {
				arg.b = TRUE;
				find(c, &arg);
			}
			return GDK_FILTER_REMOVE;
		}
	}
	return GDK_FILTER_CONTINUE;
}

void
progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	c->progress = webkit_web_view_get_progress(c->view) * 100;
	update(c);
}

void
reload(Client *c, const Arg *arg) {
	gboolean nocache = *(gboolean *)arg;
	if(nocache)
		 webkit_web_view_reload_bypass_cache(c->view);
	else
		 webkit_web_view_reload(c->view);
}

void
resize(GtkWidget *w, GtkAllocation *a, Client *c) {
	double zoom;

	if(c->zoomed)
		return;
	zoom = webkit_web_view_get_zoom_level(c->view);
	if(a->width * a->height < 300 * 400 && zoom != 0.2)
		webkit_web_view_set_zoom_level(c->view, 0.2);
	else if(zoom != 1.0)
		webkit_web_view_set_zoom_level(c->view, 1.0);
}

void
scroll(Client *c, const Arg *arg) {
	gdouble v;
	GtkAdjustment *a;

	a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(c->scroll));
	v = gtk_adjustment_get_value(a);
	v += gtk_adjustment_get_step_increment(a) * arg->i;
	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

void
setatom(Client *c, Atom a, const char *v) {
	XSync(dpy, False);
	ignorexprop++;
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window), a,
			XA_STRING, 8, PropModeReplace, (unsigned char *)v,
			strlen(v) + 1);
}

void
setup(void) {
	char *proxy;
	char *new_proxy;
	SoupURI *puri;
	SoupSession *s;

	/* clean up any zombies immediately */
	sigchld(0);
	gtk_init(NULL, NULL);
	if (!g_thread_supported())
		g_thread_init(NULL);

	dpy = GDK_DISPLAY();
	s = webkit_get_default_session();
	uriprop = XInternAtom(dpy, "_SURF_URI", False);
	findprop = XInternAtom(dpy, "_SURF_FIND", False);

	/* create dirs and files */
	cookiefile = buildpath(cookiefile);
	dldir = buildpath(dldir);
	scriptfile = buildpath(scriptfile);
	stylefile = buildpath(stylefile);

	s = webkit_get_default_session();

	soup_session_remove_feature_by_type(s, soup_cookie_get_type());

	/* proxy */
	if((proxy = getenv("http_proxy")) && strcmp(proxy, "")) {
		new_proxy = g_strrstr(proxy, "http://") ? g_strdup(proxy) :
			g_strdup_printf("http://%s", proxy);

		puri = soup_uri_new(new_proxy);
		g_object_set(G_OBJECT(s), "proxy-uri", puri, NULL);
		soup_uri_free(puri);
		g_free(new_proxy);
	}
}

void
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void
source(Client *c, const Arg *arg) {
	Arg a = { .b = FALSE };
	gboolean s;

	s = webkit_web_view_get_view_source_mode(c->view);
	webkit_web_view_set_view_source_mode(c->view, !s);
	reload(c, &a);
}

void
spawn(Client *c, const Arg *arg) {
	if(fork() == 0) {
		if(dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "surf: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

void
stop(Client *c, const Arg *arg) {
	if(c->download)
		webkit_download_cancel(c->download);
	else
		webkit_web_view_stop_loading(c->view);
	c->download = NULL;
}

void
titlechange(WebKitWebView *v, WebKitWebFrame *f, const char *t, Client *c) {
	c->title = copystr(&c->title, t);
	update(c);
}

void
update(Client *c) {
	char *t;

	if(c->progress != 100)
		t = g_strdup_printf("[%i%%] %s", c->progress, c->title);
	else if(c->linkhover)
		t = g_strdup(c->linkhover);
	else
		t = g_strdup(c->title);
	drawindicator(c);
	gtk_window_set_title(GTK_WINDOW(c->win), t);
	g_free(t);
}

void
updatedownload(WebKitDownload *o, GParamSpec *pspec, Client *c) {
	WebKitDownloadStatus status;

	status = webkit_download_get_status(c->download);
	if(status == WEBKIT_DOWNLOAD_STATUS_STARTED || status == WEBKIT_DOWNLOAD_STATUS_CREATED) {
		c->progress = (gint)(webkit_download_get_progress(c->download)*100);
	}
	update(c);
}

void
updatewinid(Client *c) {
	snprintf(winid, LENGTH(winid), "%u",
			(int)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
}

void
usage(void) {
	fputs("surf - simple browser\n", stderr);
	die("usage: surf [-e Window] [-x] [uri]\n");
}

void
windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c) {
	JSStringRef jsscript;
	char *script;
	JSValueRef exception = NULL;
	GError *error;
	
	if(g_file_get_contents(scriptfile, &script, NULL, &error)) {
		jsscript = JSStringCreateWithUTF8CString(script);
		JSEvaluateScript(js, jsscript, JSContextGetGlobalObject(js), NULL, 0, &exception);
	}
}

void
zoom(Client *c, const Arg *arg) {
	c->zoomed = TRUE;
	if(arg->i < 0)		/* zoom out */
		webkit_web_view_zoom_out(c->view);
	else if(arg->i > 0)	/* zoom in */
		webkit_web_view_zoom_in(c->view);
	else {			/* reset */
		c->zoomed = FALSE;
		webkit_web_view_set_zoom_level(c->view, 1.0);
	}
}

int main(int argc, char *argv[]) {
	int i;
	Arg arg;

	progname = argv[0];
	/* command line args */
	for(i = 1, arg.v = NULL; i < argc && argv[i][0] == '-'; i++) {
		if(!strcmp(argv[i], "-x"))
			showxid = TRUE;
		else if(!strcmp(argv[i], "-e")) {
			if(++i < argc)
				embed = atoi(argv[i]);
			else
				usage();
		}
		else if(!strcmp(argv[i], "--")) {
			i++;
			break;
		}
		else if(!strcmp(argv[i], "-v"))
			die("surf-"VERSION", © 2009 surf engineers, see LICENSE for details\n");
		else
			usage();
	}
	if(i < argc)
		arg.v = argv[i];
	setup();
	newclient();
	if(arg.v)
		loaduri(clients, &arg);
	gtk_main();
	cleanup();
	return EXIT_SUCCESS;
}
