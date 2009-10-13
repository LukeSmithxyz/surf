/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
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
	GtkWidget *win, *scroll, *vbox, *urlbar, *searchbar, *indicator;
	WebKitWebView *view;
	WebKitDownload *download;
	gchar *title;
	gint progress;
	struct Client *next;
} Client;

typedef struct Cookie {
	char *name;
	char *value;
	char *domain;
	char *path;
	struct Cookie *next;
} Cookie;

typedef enum {
	Browser = 0x0001,
	SearchBar = 0x0010,
	UrlBar = 0x0100,
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
static Atom urlprop;
static SoupCookieJar *cookiejar;
static SoupSession *session;
static Client *clients = NULL;
/*static Cookie *cookies = NULL;*/
static GdkNativeWindow embed = 0;
static gboolean showxid = FALSE;
static gboolean ignore_once = FALSE;

static gchar *buildpath(const gchar *path);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);
static gchar *copystr(gchar **str, const gchar *src);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(char *str);
static void download(WebKitDownload *o, GParamSpec *pspec, Client *c);
static void drawindicator(Client *c);
static gboolean exposeindicator(GtkWidget *w, GdkEventExpose *e, Client *c);
static gboolean initdownload(WebKitWebView *view, WebKitDownload *o, Client *c);
static gchar *geturi(Client *c);
static void hidesearch(Client *c, const Arg *arg);
static void hideurl(Client *c, const Arg *arg);
static gboolean keypress(GtkWidget* w, GdkEventKey *ev, Client *c);
static void linkhover(WebKitWebView* page, const gchar* t, const gchar* l, Client *c);
static void loadcommit(WebKitWebView *view, WebKitWebFrame *f, Client *c);
static void loadstart(WebKitWebView *view, WebKitWebFrame *f, Client *c);
static void loadfile(Client *c, const gchar *f);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static WebKitWebView *newwindow(WebKitWebView  *v, WebKitWebFrame *f, Client *c);
static void pasteurl(GtkClipboard *clipboard, const gchar *text, gpointer d);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event, gpointer d);
static void print(Client *c, const Arg *arg);
static void proccookies(SoupMessage *m, Client *c);
static void progresschange(WebKitWebView *view, gint p, Client *c);
static void request(SoupSession *s, SoupMessage *m, Client *c);
static void reload(Client *c, const Arg *arg);
static void rereadcookies(void);
static void setcookie(char *name, char *val, char *dom, char *path, long exp);
static void setup(void);
static void titlechange(WebKitWebView* view, WebKitWebFrame* frame, const gchar* title, Client *c);
static void scroll(Client *c, const Arg *arg);
static void searchtext(Client *c, const Arg *arg);
static void source(Client *c, const Arg *arg);
static void showsearch(Client *c, const Arg *arg);
static void showurl(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView* view, WebKitWebFrame* frame, const gchar* title, Client *c);
static gboolean unfocusbar(GtkWidget *w, GdkEventFocus *e, Client *c);
static void usage(void);
static void update(Client *c);
static void windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c);
static void zoom(Client *c, const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

gchar *
buildpath(const gchar *path) {
	gchar *apath, *p;
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
	puts(apath);
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
		gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), pasteurl, c);
	else
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), webkit_web_view_get_uri(c->view), -1);
}

gchar *
copystr(gchar **str, const gchar *src) {
	gchar *tmp;
	tmp = g_strdup(src);

	if(str && *str) {
		g_free(*str);
		*str = tmp;
	}
	return tmp;
}

void
destroyclient(Client *c) {
	Client *p;

	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->urlbar);
	gtk_widget_destroy(c->searchbar);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);
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
drawindicator(Client *c) {
	gint width;
	gchar *uri;
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
	const gchar *filename;
	gchar *uri, *html;

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

gchar *
geturi(Client *c) {
	gchar *uri;

	if(!(uri = (gchar *)webkit_web_view_get_uri(c->view)))
		uri = copystr(NULL, "about:blank");
	return uri;
}

void
hidesearch(Client *c, const Arg *arg) {
	gtk_widget_hide(c->searchbar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

void
hideurl(Client *c, const Arg *arg) {
	gtk_widget_hide(c->urlbar);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
}

gboolean
keypress(GtkWidget* w, GdkEventKey *ev, Client *c) {
	guint i, focus;
	gboolean processed = FALSE;

	if(ev->type != GDK_KEY_PRESS)
		return FALSE;
	if(GTK_WIDGET_HAS_FOCUS(c->searchbar))
		focus = SearchBar;
	else if(GTK_WIDGET_HAS_FOCUS(c->urlbar))
		focus = UrlBar;
	else
		focus = Browser;
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
linkhover(WebKitWebView* page, const gchar* t, const gchar* l, Client *c) {
	if(l)
		gtk_window_set_title(GTK_WINDOW(c->win), l);
	else
		update(c);
}

void
loadcommit(WebKitWebView *view, WebKitWebFrame *f, Client *c) {
	gchar *uri;

	ignore_once = TRUE;
	uri = geturi(c);
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window), urlprop,
			XA_STRING, 8, PropModeReplace, (unsigned char *)uri,
			strlen(uri) + 1);
}

void
loadstart(WebKitWebView *view, WebKitWebFrame *f, Client *c) {
	c->progress = 0;
	update(c);
}

void
loadfile(Client *c, const gchar *f) {
	GIOChannel *chan = NULL;
	GError *e = NULL;
	GString *code;
	gchar *line, *uri;
	Arg arg;

	if(strcmp(f, "-") == 0) {
		chan = g_io_channel_unix_new(STDIN_FILENO);
		if (chan) {
			code = g_string_new("");
			while(g_io_channel_read_line(chan, &line, NULL, NULL,
						&e) == G_IO_STATUS_NORMAL) {
				g_string_append(code, line);
				g_free(line);
			}
			webkit_web_view_load_html_string(c->view, code->str,
					"file://.");
			g_io_channel_shutdown(chan, FALSE, NULL);
			g_string_free(code, TRUE);
		}
		arg.v = uri = g_strdup("stdin");
	}
	else {
		arg.v = uri = g_strdup_printf("file://%s", f);
		loaduri(c, &arg);
	}
	c->title = copystr(&c->title, uri);
	update(c);
	g_free(uri);
}

void
loaduri(Client *c, const Arg *arg) {
	gchar *u;
	const gchar *uri = (gchar *)arg->v;
	if(!uri)
		uri = gtk_entry_get_text(GTK_ENTRY(c->urlbar));
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
	Client *c;
	WebKitWebSettings *settings;
	gchar *uri;

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
	g_signal_connect(G_OBJECT(c->view), "load-committed", G_CALLBACK(loadcommit), c);
	g_signal_connect(G_OBJECT(c->view), "load-started", G_CALLBACK(loadstart), c);
	g_signal_connect(G_OBJECT(c->view), "hovering-over-link", G_CALLBACK(linkhover), c);
	g_signal_connect(G_OBJECT(c->view), "create-web-view", G_CALLBACK(newwindow), c);
	g_signal_connect(G_OBJECT(c->view), "download-requested", G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(c->view), "window-object-cleared", G_CALLBACK(windowobjectcleared), c);
	g_signal_connect_after(session, "request-started", G_CALLBACK(request), c);

	/* urlbar */
	c->urlbar = gtk_entry_new();
	gtk_entry_set_has_frame(GTK_ENTRY(c->urlbar), FALSE);
	g_signal_connect(G_OBJECT(c->urlbar), "focus-out-event", G_CALLBACK(unfocusbar), c);

	/* searchbar */
	c->searchbar = gtk_entry_new();
	gtk_entry_set_has_frame(GTK_ENTRY(c->searchbar), FALSE);
	g_signal_connect(G_OBJECT(c->searchbar), "focus-out-event", G_CALLBACK(unfocusbar), c);

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
	gtk_container_add(GTK_CONTAINER(c->vbox), c->urlbar);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->indicator);

	/* Setup */
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->urlbar, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->searchbar, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->indicator, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->scroll, TRUE, TRUE, 0, GTK_PACK_START);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_hide_all(c->searchbar);
	gtk_widget_hide_all(c->urlbar);
	gtk_widget_show(c->vbox);
	gtk_widget_show(c->indicator);
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	webkit_web_view_set_full_content_zoom(c->view, TRUE);
	settings = webkit_web_view_get_settings(c->view);
	g_object_set(G_OBJECT(settings), "user-agent", "surf", NULL);
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

WebKitWebView *
newwindow(WebKitWebView  *v, WebKitWebFrame *f, Client *c) {
	Client *n = newclient();
	return n->view;
}

 
void
pasteurl(GtkClipboard *clipboard, const gchar *text, gpointer d) {
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
		if(ev->atom == urlprop && ev->state == PropertyNewValue) {
			if(ignore_once)
			       ignore_once = FALSE;
			else {
				XGetWindowProperty(dpy, ev->window, urlprop, 0L, BUFSIZ, False, XA_STRING,
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
proccookies(SoupMessage *m, Client *c) {
	GSList *l;
	SoupCookie *co;
	long t;

	rereadcookies();
	for (l = soup_cookies_from_response(m); l; l = l->next){
		co = (SoupCookie *)l->data;
		t = co->expires ?  soup_date_to_time_t(co->expires) : 0;
		setcookie(co->name, co->value, co->domain, co->value, t);
	}
	g_slist_free(l);
}

void
progresschange(WebKitWebView* view, gint p, Client *c) {
	c->progress = p;
	update(c);
}

void
request(SoupSession *s, SoupMessage *m, Client *c) {
	soup_message_add_header_handler(m, "got-headers", "Set-Cookie",
			G_CALLBACK(proccookies), c);
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
rereadcookies(void) {
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
setcookie(char *name, char *val, char *dom, char *path, long exp) {

}

void
setup(void) {
	SoupSession *s;

	gtk_init(NULL, NULL);
	if (!g_thread_supported())
		g_thread_init(NULL);

	dpy = GDK_DISPLAY();
	session = webkit_get_default_session();
	urlprop = XInternAtom(dpy, "_SURF_URL", False);

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
	hideurl(c, NULL);
	gtk_widget_show(c->searchbar);
	gtk_widget_grab_focus(c->searchbar);
}

void
source(Client *c, const Arg *arg) {
	Arg a = { .b = FALSE };
	/*gboolean s;

	s = webkit_web_view_get_view_source_mode(c->view);
	webkit_web_view_set_view_source_mode(c->view, c->source);*/
	reload(c, &a);
}

void
searchtext(Client *c, const Arg *arg) {
	gboolean forward = *(gboolean *)arg;
	webkit_web_view_search_text(c->view,
			gtk_entry_get_text(GTK_ENTRY(c->searchbar)),
			FALSE,
			forward,
			TRUE);
}

void
showurl(Client *c, const Arg *arg) {
	gchar *uri;

	hidesearch(c, NULL);
	uri = geturi(c);
	gtk_entry_set_text(GTK_ENTRY(c->urlbar), uri);
	gtk_widget_show(c->urlbar);
	gtk_widget_grab_focus(c->urlbar);
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
titlechange(WebKitWebView *v, WebKitWebFrame *f, const gchar *t, Client *c) {
	c->title = copystr(&c->title, t);
	update(c);
}

gboolean
unfocusbar(GtkWidget *w, GdkEventFocus *e, Client *c) {
	hidesearch(c, NULL);
	hideurl(c, NULL);
	return FALSE;
}

void
usage(void) {
	fputs("surf - simple browser\n", stderr);
	die("usage: surf [-e Window] [-x] [uri]\n");
}

void
update(Client *c) {
	gchar *t;

	if(c->progress == 100)
		t = g_strdup(c->title);
	else
		t = g_strdup_printf("%s [%i%%]", c->title, c->progress);
	drawindicator(c);
	gtk_window_set_title(GTK_WINDOW(c->win), t);
	g_free(t);

}

void
windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c) {
	JSStringRef jsscript;
	gchar *script;
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
	Client *c;

	/* command line args */
	for(i = 1, arg.v = NULL; i < argc; i++) {
		if(!strcmp(argv[i], "-x"))
			showxid = TRUE;
		else if(!strcmp(argv[i], "-e")) {
			if(++i < argc)
				embed = atoi(argv[i]);
			else
				usage();
		}
		else if(!strcmp(argv[i], "--"))
			break;
		else if(!strcmp(argv[i], "-v"))
			die("surf-"VERSION", © 2009 surf engineers, see LICENSE for details\n");
		else if(argv[i][0] == '-')
			usage();
		else
			arg.v = argv[i];
	}
	setup();
	c = newclient();
	if(arg.v) {
		if(strchr("./", ((char *)arg.v)[0]) || strcmp("-", (char *)arg.v) == 0)
			loadfile(c, (char *)arg.v);
		else
			loaduri(c, &arg);
	}
	gtk_main();
	cleanup();
	return EXIT_SUCCESS;
}
