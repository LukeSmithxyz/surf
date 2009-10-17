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
	const gboolean b;
	const gint i;
	const void *v;
};

typedef struct Client {
	GtkWidget *win, *scroll, *vbox, *uribar, *searchbar, *indicator;
	GtkWidget **items;
	WebKitWebView *view;
	WebKitDownload *download;
	char *title, *linkhover;
	gint progress;
	struct Client *next;
} Client;

typedef struct {
	char *label;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
} Item;

typedef enum {
	Browser = 0x0001,
	SearchBar = 0x0010,
	UriBar = 0x0100,
	Any = ~0,
} KeyFocus;

typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
	KeyFocus focus;
} Key;

static Display *dpy;
static Atom uriprop;
static SoupCookieJar *cookiejar;
static SoupSession *session;
static Client *clients = NULL;
static GdkNativeWindow embed = 0;
static gboolean showxid = FALSE;
static gboolean ignore_once = FALSE;
static char winid[64];
static char *progname;

static const char *autouri(Client *c);
static char *buildpath(const char *path);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);
static void context(WebKitWebView *v, GtkMenu *m, Client *c);
static char *copystr(char **str, const char *src);
static gboolean decidewindow(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, WebKitWebNavigationAction *n, WebKitWebPolicyDecision *p, Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(char *str);
static void download(WebKitDownload *o, GParamSpec *pspec, Client *c);
static void drawindicator(Client *c);
static gboolean exposeindicator(GtkWidget *w, GdkEventExpose *e, Client *c);
static gboolean initdownload(WebKitWebView *v, WebKitDownload *o, Client *c);
static char *geturi(Client *c);
static void hidesearch(Client *c, const Arg *arg);
static void hideuri(Client *c, const Arg *arg);
static void itemclick(GtkMenuItem *mi, Client *c);
static gboolean keypress(GtkWidget *w, GdkEventKey *ev, Client *c);
static void linkhover(WebKitWebView *v, const char* t, const char* l, Client *c);
static void loadcommit(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static void loadfinished(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static void loadstart(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static void newwindow(Client *c, const Arg *arg);
static WebKitWebView *createwindow(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event, gpointer d);
static void print(Client *c, const Arg *arg);
static void progresschange(WebKitWebView *v, gint p, Client *c);
static void reload(Client *c, const Arg *arg);
static void reloadcookie();
static void sigchld(int unused);
static void setup(void);
static void spawn(Client *c, const Arg *arg);
static void scroll(Client *c, const Arg *arg);
static void searchtext(Client *c, const Arg *arg);
static void source(Client *c, const Arg *arg);
static void showsearch(Client *c, const Arg *arg);
static void showuri(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView *v, WebKitWebFrame* frame, const char* title, Client *c);
static gboolean focusview(GtkWidget *w, GdkEventFocus *e, Client *c);
static void usage(void);
static void update(Client *c);
static void updatewinid(Client *c);
static void windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c);
static void zoom(Client *c, const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

const char *
autouri(Client *c) {
	if(GTK_WIDGET_HAS_FOCUS(c->uribar))
		return gtk_entry_get_text(GTK_ENTRY(c->uribar));
	else if(c->linkhover)
		return c->linkhover;
	return NULL;
}

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
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), webkit_web_view_get_uri(c->view), -1);
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

void
destroyclient(Client *c) {
	int i;
	Client *p;

	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->uribar);
	gtk_widget_destroy(c->searchbar);
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
destroywin(GtkWidget* w, Client *c) {
	destroyclient(c);
}

void
die(char *str) {
	fputs(str, stderr);
	exit(EXIT_FAILURE);
}

void
drawindicator(Client *c) {
	gint width;
	char *uri;
	GtkWidget *w;
	GdkGC *gc;
	GdkColor fg;

	uri = geturi(c);
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
download(WebKitDownload *o, GParamSpec *pspec, Client *c) {
	WebKitDownloadStatus status;

	status = webkit_download_get_status(c->download);
	if(status == WEBKIT_DOWNLOAD_STATUS_STARTED || status == WEBKIT_DOWNLOAD_STATUS_CREATED) {
		c->progress = (gint)(webkit_download_get_progress(c->download)*100);
	}
	update(c);
}

gboolean
initdownload(WebKitWebView *view, WebKitDownload *o, Client *c) {
	const char *filename;
	char *uri, *html;

	stop(c, NULL);
	c->download = o;
	filename = webkit_download_get_suggested_filename(o);
	uri = g_strconcat("file://", dldir, "/", filename, NULL);
	webkit_download_set_destination_uri(c->download, uri);
	c->progress = 0;
	g_free(uri);
	html = g_strdup_printf("Download <b>%s</b>...", filename);
	webkit_web_view_load_html_string(c->view, html,
			webkit_download_get_uri(c->download));
	g_signal_connect(c->download, "notify::progress", G_CALLBACK(download), c);
	g_signal_connect(c->download, "notify::status", G_CALLBACK(download), c);
	webkit_download_start(c->download);
	
	c->title = copystr(&c->title, filename);
	update(c);
	g_free(html);
	return TRUE;
}

char *
geturi(Client *c) {
	char *uri;

	if(!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = copystr(NULL, "about:blank");
	return uri;
}

void
hidesearch(Client *c, const Arg *arg) {
	gtk_widget_hide(c->searchbar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

void
hideuri(Client *c, const Arg *arg) {
	gtk_widget_hide(c->uribar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
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
	guint i, focus;
	gboolean processed = FALSE;

	if(ev->type != GDK_KEY_PRESS)
		return FALSE;
	if(GTK_WIDGET_HAS_FOCUS(c->searchbar))
		focus = SearchBar;
	else if(GTK_WIDGET_HAS_FOCUS(c->uribar))
		focus = UriBar;
	else
		focus = Browser;
	updatewinid(c);
	for(i = 0; i < LENGTH(keys); i++) {
		if(focus & keys[i].focus
				&& gdk_keyval_to_lower(ev->keyval) == keys[i].keyval
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
loadcommit(WebKitWebView *view, WebKitWebFrame *f, Client *c) {
	char *uri;

	ignore_once = TRUE;
	uri = geturi(c);
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window), uriprop,
			XA_STRING, 8, PropModeReplace, (unsigned char *)uri,
			strlen(uri) + 1);
}

void
loadfinished(WebKitWebView *v, WebKitWebFrame *f, Client *c) {
	reloadcookie();
}

void
loadstart(WebKitWebView *view, WebKitWebFrame *f, Client *c) {
	reloadcookie();
	c->progress = 0;
	update(c);
}

void
loaduri(Client *c, const Arg *arg) {
	char *u;
	const char *uri = (char *)arg->v;

	if(!uri)
		uri = autouri(c);
	if(!uri)
		return;
	u = g_strrstr(uri, "://") ? g_strdup(uri)
		: g_strdup_printf("http://%s", uri);
	webkit_web_view_load_uri(c->view, u);
	c->progress = 0;
	c->title = copystr(&c->title, u);
	g_free(u);
	update(c);
}

void
navigate(Client *c, const Arg *arg) {
	gint steps = *(gint *)arg;
	webkit_web_view_go_back_or_forward(c->view, steps);
}

Client *
newclient(void) {
	int i;
	Client *c;
	WebKitWebSettings *settings;
	char *uri;

	if(!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");
	/* Window */
	if(embed) {
		c->win = gtk_plug_new(embed);
	}
	else {
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_wmclass(GTK_WINDOW(c->win), "surf", "surf");
	}
	gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);
	g_signal_connect(G_OBJECT(c->win), "destroy", G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(c->win), "key-press-event", G_CALLBACK(keypress), c);

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

	/* scrolled window */
	c->scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
			GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	/* webview */
	c->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	g_signal_connect(G_OBJECT(c->view), "title-changed", G_CALLBACK(titlechange), c);
	g_signal_connect(G_OBJECT(c->view), "load-progress-changed", G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view), "load-finished", G_CALLBACK(loadfinished), c);
	g_signal_connect(G_OBJECT(c->view), "load-committed", G_CALLBACK(loadcommit), c);
	g_signal_connect(G_OBJECT(c->view), "load-started", G_CALLBACK(loadstart), c);
	g_signal_connect(G_OBJECT(c->view), "hovering-over-link", G_CALLBACK(linkhover), c);
	g_signal_connect(G_OBJECT(c->view), "create-web-view", G_CALLBACK(createwindow), c);
	g_signal_connect(G_OBJECT(c->view), "new-window-policy-decision-requested", G_CALLBACK(decidewindow), c);
	g_signal_connect(G_OBJECT(c->view), "download-requested", G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(c->view), "window-object-cleared", G_CALLBACK(windowobjectcleared), c);
	g_signal_connect(G_OBJECT(c->view), "focus-in-event", G_CALLBACK(focusview), c);
	g_signal_connect(G_OBJECT(c->view), "populate-popup", G_CALLBACK(context), c);

	/* uribar */
	c->uribar = gtk_entry_new();
	gtk_entry_set_has_frame(GTK_ENTRY(c->uribar), FALSE);

	/* searchbar */
	c->searchbar = gtk_entry_new();
	gtk_entry_set_has_frame(GTK_ENTRY(c->searchbar), FALSE);

	/* indicator */
	c->indicator = gtk_drawing_area_new();
	gtk_widget_set_size_request(c->indicator, 0, 2);
	g_signal_connect (G_OBJECT (c->indicator), "expose_event",
			G_CALLBACK (exposeindicator), c);

	/* Arranging */
	gtk_container_add(GTK_CONTAINER(c->scroll), GTK_WIDGET(c->view));
	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->searchbar);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->uribar);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->indicator);

	/* Setup */
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->uribar, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->searchbar, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->indicator, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->scroll, TRUE, TRUE, 0, GTK_PACK_START);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_hide_all(c->searchbar);
	gtk_widget_hide_all(c->uribar);
	gtk_widget_show(c->vbox);
	gtk_widget_show(c->indicator);
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	webkit_web_view_set_full_content_zoom(c->view, TRUE);
	settings = webkit_web_view_get_settings(c->view);
	g_object_set(G_OBJECT(settings), "user-agent", useragent, NULL);
	uri = g_strconcat("file://", stylefile, NULL);
	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", uri, NULL);
	g_free(uri);

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
	uri = arg->v ? (char *)arg->v : autouri(c);
	if(uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

WebKitWebView *
createwindow(WebKitWebView  *v, WebKitWebFrame *f, Client *c) {
	Client *n = newclient();
	return n->view;
}

void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d) {
	Arg arg = {.v = text };
	if(text != NULL)
		loaduri((Client *) d, &arg);
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d) {
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Atom adummy;
	gint idummy;
	unsigned long ldummy;
	unsigned char *buf = NULL;
	Arg arg;

	if(((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if(ev->atom == uriprop && ev->state == PropertyNewValue) {
			if(ignore_once)
			       ignore_once = FALSE;
			else {
				XGetWindowProperty(dpy, ev->window, uriprop, 0L, BUFSIZ, False, XA_STRING,
					&adummy, &idummy, &ldummy, &ldummy, &buf);
				arg.v = buf;
				loaduri(c, &arg);
				XFree(buf);
			}
			return GDK_FILTER_REMOVE;
		}
	}
	return GDK_FILTER_CONTINUE;
}

void
print(Client *c, const Arg *arg) {
	webkit_web_frame_print(webkit_web_view_get_main_frame(c->view));
}

void
progresschange(WebKitWebView *v, gint p, Client *c) {
	c->progress = p;
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
reloadcookie(void) {
	SoupSession *s;

	/* This forces the cookie to be written to hdd */
	s = webkit_get_default_session();
	soup_session_remove_feature(s, SOUP_SESSION_FEATURE(cookiejar));
	soup_session_add_feature(s, SOUP_SESSION_FEATURE(cookiejar));
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
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void
setup(void) {
	SoupSession *s;

	/* clean up any zombies immediately */
	sigchld(0);
	gtk_init(NULL, NULL);
	if (!g_thread_supported())
		g_thread_init(NULL);

	dpy = GDK_DISPLAY();
	session = webkit_get_default_session();
	uriprop = XInternAtom(dpy, "_SURF_uri", False);

	/* create dirs and files */
	cookiefile = buildpath(cookiefile);
	dldir = buildpath(dldir);
	scriptfile = buildpath(scriptfile);
	stylefile = buildpath(stylefile);

	/* cookie persistance */
	s = webkit_get_default_session();
	cookiejar = soup_cookie_jar_text_new(cookiefile, FALSE);
	soup_session_add_feature(s, SOUP_SESSION_FEATURE(cookiejar));
}

void
showsearch(Client *c, const Arg *arg) {
	hideuri(c, NULL);
	gtk_widget_show(c->searchbar);
	gtk_widget_grab_focus(c->searchbar);
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
searchtext(Client *c, const Arg *arg) {
	const char *text;
	gboolean forward = *(gboolean *)arg;
	text = gtk_entry_get_text(GTK_ENTRY(c->searchbar));
	webkit_web_view_search_text(c->view, text, FALSE, forward, TRUE);
	webkit_web_view_mark_text_matches(c->view, text, FALSE, 0);
}

void
showuri(Client *c, const Arg *arg) {
	char *uri;

	hidesearch(c, NULL);
	uri = geturi(c);
	gtk_entry_set_text(GTK_ENTRY(c->uribar), uri);
	gtk_widget_show(c->uribar);
	gtk_widget_grab_focus(c->uribar);
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
spawn(Client *c, const Arg *arg) {
	if(fork() == 0) {
		if(dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "tabbed: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

void
titlechange(WebKitWebView *v, WebKitWebFrame *f, const char *t, Client *c) {
	c->title = copystr(&c->title, t);
	update(c);
}

gboolean
focusview(GtkWidget *w, GdkEventFocus *e, Client *c) {
	hidesearch(c, NULL);
	hideuri(c, NULL);
	return FALSE;
}

void
usage(void) {
	fputs("surf - simple browser\n", stderr);
	die("usage: surf [-e Window] [-x] [uri]\n");
}

void
update(Client *c) {
	char *t;

	if(c->progress != 100)
		t = g_strdup_printf("%s [%i%%]", c->title, c->progress);
	else if(c->linkhover)
		t = g_strdup(c->linkhover);
	else
		t = g_strdup(c->title);
	drawindicator(c);
	gtk_window_set_title(GTK_WINDOW(c->win), t);
	g_free(t);

}

void
updatewinid(Client *c) {
	snprintf(winid, LENGTH(winid), "%u",
			(int)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
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
	if(arg->i < 0)		/* zoom out */
		webkit_web_view_zoom_out(c->view);
	else if(arg->i > 0)	/* zoom in */
		webkit_web_view_zoom_in(c->view);
	else			/* reset */
		webkit_web_view_set_zoom_level(c->view, 1.0);
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
	if(arg.v) {
		loaduri(clients, &arg);
	}
	gtk_main();
	cleanup();
	return EXIT_SUCCESS;
}
