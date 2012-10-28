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
#include <sys/file.h>

#define LENGTH(x)               (sizeof x / sizeof x[0])
#define COOKIEJAR_TYPE          (cookiejar_get_type ())
#define COOKIEJAR(obj)          (G_TYPE_CHECK_INSTANCE_CAST ((obj), COOKIEJAR_TYPE, CookieJar))

enum { AtomFind, AtomGo, AtomUri, AtomLast };

typedef union Arg Arg;
union Arg {
	gboolean b;
	gint i;
	const void *v;
};

typedef struct Client {
	GtkWidget *win, *scroll, *vbox, *indicator;
	WebKitWebView *view;
	char *title, *linkhover;
	const char *uri, *needle;
	gint progress;
	gboolean sslfailed;
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

typedef struct {
	SoupCookieJarText parent_instance;
	int lock;
} CookieJar;

typedef struct {
	SoupCookieJarTextClass parent_class;
} CookieJarClass;

G_DEFINE_TYPE(CookieJar, cookiejar, SOUP_TYPE_COOKIE_JAR_TEXT)

static Display *dpy;
static Atom atoms[AtomLast];
static Client *clients = NULL;
static GdkNativeWindow embed = 0;
static gboolean showxid = FALSE;
static char winid[64];
static char *progname;
static gboolean loadimage = 1, plugin = 1, script = 1;

static char *buildpath(const char *path);
static gboolean buttonrelease(WebKitWebView *web, GdkEventButton *e, GList *gl);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);
static void cookiejar_changed(SoupCookieJar *self, SoupCookie *old_cookie, SoupCookie *new_cookie);
static void cookiejar_finalize(GObject *self);
static SoupCookieJar *cookiejar_new(const char *filename, gboolean read_only);
static void cookiejar_set_property(GObject *self, guint prop_id, const GValue *value, GParamSpec *pspec);
static char *copystr(char **str, const char *src);
static WebKitWebView *createwindow(WebKitWebView *v, WebKitWebFrame *f, Client *c);
static gboolean decidedownload(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, gchar *m,  WebKitWebPolicyDecision *p, Client *c);
static gboolean decidewindow(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r, WebKitWebNavigationAction *n, WebKitWebPolicyDecision *p, Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(char *str);
static void drawindicator(Client *c);
static gboolean exposeindicator(GtkWidget *w, GdkEventExpose *e, Client *c);
static void find(Client *c, const Arg *arg);
static const char *getatom(Client *c, int a);
static char *geturi(Client *c);
static gboolean initdownload(WebKitWebView *v, WebKitDownload *o, Client *c);
static gboolean keypress(GtkWidget *w, GdkEventKey *ev, Client *c);
static void linkhover(WebKitWebView *v, const char* t, const char* l, Client *c);
static void loadstatuschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static void newwindow(Client *c, const Arg *arg, gboolean noembed);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static void populatepopup(WebKitWebView *web, GtkMenu *menu, Client *c);
static void popupactivate(GtkMenuItem *menu, Client *);
static void print(Client *c, const Arg *arg);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event, gpointer d);
static void progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void reload(Client *c, const Arg *arg);
static void scroll_h(Client *c, const Arg *arg);
static void scroll_v(Client *c, const Arg *arg);
static void scroll(GtkAdjustment *a, const Arg *arg);
static void setatom(Client *c, int a, const char *v);
static void setup(void);
static void sigchld(int unused);
static void source(Client *c, const Arg *arg);
static void spawn(Client *c, const Arg *arg);
static void eval(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView *v, WebKitWebFrame* frame, const char* title, Client *c);
static void update(Client *c);
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
		g_mkdir_with_parents(apath, 0700);
		g_chmod(apath, 0700); /* in case it existed */
		*p = '/';
	}
	/* creating file (gives error when apath ends with "/") */
	if((f = fopen(apath, "a"))) {
		g_chmod(apath, 0600); /* always */
		fclose(f);
	}
	return apath;
}

static gboolean
buttonrelease(WebKitWebView *web, GdkEventButton *e, GList *gl) {
	WebKitHitTestResultContext context;
	WebKitHitTestResult *result = webkit_web_view_get_hit_test_result(web, e);
	Arg arg;

	g_object_get(result, "context", &context, NULL);
	if(context & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK) {
		if(e->button == 2) {
			g_object_get(result, "link-uri", &arg.v, NULL);
			newwindow(NULL, &arg, e->state & GDK_CONTROL_MASK);
			return true;
		}
	}
	return false;
}

void
cleanup(void) {
	while(clients)
		destroyclient(clients);
	g_free(cookiefile);
	g_free(scriptfile);
	g_free(stylefile);
}

static void
cookiejar_changed(SoupCookieJar *self, SoupCookie *old_cookie, SoupCookie *new_cookie) {
	flock(COOKIEJAR(self)->lock, LOCK_EX);
	if(new_cookie && !new_cookie->expires && sessiontime)
		soup_cookie_set_expires(new_cookie, soup_date_new_from_now(sessiontime));
	SOUP_COOKIE_JAR_CLASS(cookiejar_parent_class)->changed(self, old_cookie, new_cookie);
	flock(COOKIEJAR(self)->lock, LOCK_UN);
}

static void
cookiejar_class_init(CookieJarClass *klass) {
	SOUP_COOKIE_JAR_CLASS(klass)->changed = cookiejar_changed;
	G_OBJECT_CLASS(klass)->get_property = G_OBJECT_CLASS(cookiejar_parent_class)->get_property;
	G_OBJECT_CLASS(klass)->set_property = cookiejar_set_property;
	G_OBJECT_CLASS(klass)->finalize = cookiejar_finalize;
	g_object_class_override_property(G_OBJECT_CLASS(klass), 1, "filename");
}

static void
cookiejar_finalize(GObject *self) {
	close(COOKIEJAR(self)->lock);
	G_OBJECT_CLASS(cookiejar_parent_class)->finalize(self);
}

static void
cookiejar_init(CookieJar *self) {
	self->lock = open(cookiefile, 0);
}

static SoupCookieJar *
cookiejar_new(const char *filename, gboolean read_only) {
	return g_object_new(COOKIEJAR_TYPE,
	                    SOUP_COOKIE_JAR_TEXT_FILENAME, filename,
	                    SOUP_COOKIE_JAR_READ_ONLY, read_only, NULL);
} 

static void
cookiejar_set_property(GObject *self, guint prop_id, const GValue *value, GParamSpec *pspec) {
	flock(COOKIEJAR(self)->lock, LOCK_SH);
	G_OBJECT_CLASS(cookiejar_parent_class)->set_property(self, prop_id, value, pspec);
	flock(COOKIEJAR(self)->lock, LOCK_UN);
}

void
evalscript(JSContextRef js, char *script, char* scriptname) {
	JSStringRef jsscript, jsscriptname;
	JSValueRef exception = NULL;

	jsscript = JSStringCreateWithUTF8CString(script);
	jsscriptname = JSStringCreateWithUTF8CString(scriptname);
	JSEvaluateScript(js, jsscript, JSContextGetGlobalObject(js), jsscriptname, 0, &exception);
	JSStringRelease(jsscript);
	JSStringRelease(jsscriptname);
}

void
runscript(WebKitWebFrame *frame) {
	char *script;
	GError *error;

	if(g_file_get_contents(scriptfile, &script, NULL, &error)) {
		evalscript(webkit_web_frame_get_global_context(frame), script, scriptfile);
	}
}

void
clipboard(Client *c, const Arg *arg) {
	gboolean paste = *(gboolean *)arg;

	if(paste)
		gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), pasteuri, c);
	else
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), c->linkhover ? c->linkhover : geturi(c), -1);
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
		newwindow(NULL, &arg, 0);
		return TRUE;
	}
	return FALSE;
}

void
destroyclient(Client *c) {
	Client *p;

	webkit_web_view_stop_loading(c->view);
	gtk_widget_destroy(c->indicator);
	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
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
	const char *uri;
	GtkWidget *w;
	GdkGC *gc;
	GdkColor fg;

	uri = geturi(c);
	w = c->indicator;
	width = c->progress * w->allocation.width / 100;
	gc = gdk_gc_new(w->window);
	if(strstr(uri, "https://") == uri) {
		gdk_color_parse(c->sslfailed ?
		                progress_untrust : progress_trust, &fg);
	} else {
		gdk_color_parse(progress, &fg);
	}
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

	s = getatom(c, AtomFind);
	gboolean forward = *(gboolean *)arg;
	webkit_web_view_search_text(c->view, s, FALSE, forward, TRUE);
}

const char *
getatom(Client *c, int a) {
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XGetWindowProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window),
			atoms[a], 0L, BUFSIZ, False, XA_STRING,
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
	Arg arg;

	updatewinid(c);
	arg = (Arg)DOWNLOAD((char *)webkit_download_get_uri(o));
	spawn(c, &arg);
	return FALSE;
}

gboolean
keypress(GtkWidget* w, GdkEventKey *ev, Client *c) {
	guint i;
	gboolean processed = FALSE;

	updatewinid(c);
	for(i = 0; i < LENGTH(keys); i++) {
		if(gdk_keyval_to_lower(ev->keyval) == keys[i].keyval
				&& (ev->state & keys[i].mod) == keys[i].mod
				&& keys[i].func) {
			keys[i].func(c, &(keys[i].arg));
			processed = TRUE;
		}
	}
	return processed;
}

void
linkhover(WebKitWebView *v, const char* t, const char* l, Client *c) {
	if(l) {
		c->linkhover = copystr(&c->linkhover, l);
	}
	else if(c->linkhover) {
		free(c->linkhover);
		c->linkhover = NULL;
	}
	update(c);
}

void
loadstatuschange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	WebKitWebFrame *frame;
	WebKitWebDataSource *src;
	WebKitNetworkRequest *request;
	SoupMessage *msg;
	char *uri;

	switch(webkit_web_view_get_load_status (c->view)) {
	case WEBKIT_LOAD_COMMITTED:
		uri = geturi(c);
		if(strstr(uri, "https://") == uri) {
			frame = webkit_web_view_get_main_frame(c->view);
			src = webkit_web_frame_get_data_source(frame);
			request = webkit_web_data_source_get_request(src);
			msg = webkit_network_request_get_message(request);
			c->sslfailed = soup_message_get_flags(msg)
			               ^ SOUP_MESSAGE_CERTIFICATE_TRUSTED;
		}
		setatom(c, AtomUri, uri);
		break;
	case WEBKIT_LOAD_FINISHED:
		c->progress = 100;
		update(c);
		break;
	default:
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
	Client *c;
	WebKitWebSettings *settings;
	WebKitWebFrame *frame;
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
		gtk_window_set_wmclass(GTK_WINDOW(c->win), "surf", "Surf");

		/* TA:  20091214:  And set the role here as well -- so that
		 * sessions can pick this up.
		 */
		gtk_window_set_role(GTK_WINDOW(c->win), "Surf");
	}
	gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);
	g_signal_connect(G_OBJECT(c->win), "destroy", G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(c->win), "key-press-event", G_CALLBACK(keypress), c);

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
	g_signal_connect(G_OBJECT(c->view), "window-object-cleared", G_CALLBACK(windowobjectcleared), c);
	g_signal_connect(G_OBJECT(c->view), "notify::load-status", G_CALLBACK(loadstatuschange), c);
	g_signal_connect(G_OBJECT(c->view), "notify::progress", G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view), "download-requested", G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(c->view), "button-release-event", G_CALLBACK(buttonrelease), c);
	g_signal_connect(G_OBJECT(c->view), "populate-popup", G_CALLBACK(populatepopup), c);

	/* Indicator */
	c->indicator = gtk_drawing_area_new();
	gtk_widget_set_size_request(c->indicator, 0, indicator_thickness);
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
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gtk_window_set_geometry_hints(GTK_WINDOW(c->win), NULL, &hints, GDK_HINT_MIN_SIZE);
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	webkit_web_view_set_full_content_zoom(c->view, TRUE);
	frame = webkit_web_view_get_main_frame(c->view);
	runscript(frame);
	settings = webkit_web_view_get_settings(c->view);
	if(!(ua = getenv("SURF_USERAGENT")))
		ua = useragent;
	g_object_set(G_OBJECT(settings), "user-agent", ua, NULL);
	uri = g_strconcat("file://", stylefile, NULL);
	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", uri, NULL);
	g_object_set(G_OBJECT(settings), "auto-load-images", loadimage, NULL);
	g_object_set(G_OBJECT(settings), "enable-plugins", plugin, NULL);
	g_object_set(G_OBJECT(settings), "enable-scripts", script, NULL);
	g_object_set(G_OBJECT(settings), "enable-spatial-navigation", SPATIAL_BROWSING, NULL);

	g_free(uri);

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");
	if(HIDE_BACKGROUND)
		webkit_web_view_set_transparent(c->view, TRUE);

	c->title = NULL;
	c->next = clients;
	clients = c;
	if(showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		printf("%u\n", (guint)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
		fflush(NULL);
                if (fclose(stdout) != 0) {
			die("Error closing stdout");
                }
	}
	return c;
}

void
newwindow(Client *c, const Arg *arg, gboolean noembed) {
	guint i = 0;
	const char *cmd[10], *uri;
	const Arg a = { .v = (void *)cmd };
	char tmp[64];

	cmd[i++] = progname;
	if(embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%u\n", (int)embed);
		cmd[i++] = tmp;
	}
	if(!script)
		cmd[i++] = "-s";
	if(!plugin)
		cmd[i++] = "-p";
	if(!loadimage)
		cmd[i++] = "-i";
	if(showxid)
		cmd[i++] = "-x";
	cmd[i++] = "--";
	uri = arg->v ? (char *)arg->v : c->linkhover;
	if(uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

static void
populatepopup(WebKitWebView *web, GtkMenu *menu, Client *c) {
	GList *items = gtk_container_get_children(GTK_CONTAINER(menu));

	for(GList *l = items; l; l = l->next) {
		g_signal_connect(l->data, "activate", G_CALLBACK(popupactivate), c);
	}

	g_list_free(items);
}

static void
popupactivate(GtkMenuItem *menu, Client *c) {
	/*
	 * context-menu-action-2000	open link
	 * context-menu-action-1	open link in window
	 * context-menu-action-2	download linked file
	 * context-menu-action-3	copy link location
	 * context-menu-action-13	reload
	 * context-menu-action-10	back
	 * context-menu-action-11	forward
	 * context-menu-action-12	stop
	 */

	GtkAction *a = NULL;
	const char *name;
	GtkClipboard *prisel;

	a = gtk_activatable_get_related_action(GTK_ACTIVATABLE(menu));
	if(a == NULL)
		return;

	name = gtk_action_get_name(a);
	if(!g_strcmp0(name, "context-menu-action-3")) {
		prisel = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(prisel, c->linkhover, -1);
	}
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
		if(ev->state == PropertyNewValue) {
			if(ev->atom == atoms[AtomFind]) {
				arg.b = TRUE;
				find(c, &arg);
				return GDK_FILTER_REMOVE;
			}
			else if(ev->atom == atoms[AtomGo]) {
				arg.v = getatom(c, AtomGo);
				loaduri(c, &arg);
				return GDK_FILTER_REMOVE;
			}
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
scroll_h(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_hadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

void
scroll_v(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_vadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

void
scroll(GtkAdjustment *a, const Arg *arg) {
	gdouble v;

	v = gtk_adjustment_get_value(a);
	switch (arg->i){
	case +10000:
	case -10000:
		v += gtk_adjustment_get_page_increment(a) *
			(arg->i / 10000);
		break;
	case +20000:
	case -20000:
	default:
		v += gtk_adjustment_get_step_increment(a) * arg->i;
	}

	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) -
			gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

void
setatom(Client *c, int a, const char *v) {
	XSync(dpy, False);
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window), atoms[a],
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

	/* atoms */
	atoms[AtomFind] = XInternAtom(dpy, "_SURF_FIND", False);
	atoms[AtomGo] = XInternAtom(dpy, "_SURF_GO", False);
	atoms[AtomUri] = XInternAtom(dpy, "_SURF_URI", False);

	/* dirs and files */
	cookiefile = buildpath(cookiefile);
	scriptfile = buildpath(scriptfile);
	stylefile = buildpath(stylefile);

	/* request handler */
	s = webkit_get_default_session();

	/* cookie jar */
	soup_session_add_feature(s, SOUP_SESSION_FEATURE(cookiejar_new(cookiefile, FALSE)));

	/* ssl */
	g_object_set(G_OBJECT(s), "ssl-ca-file", cafile, NULL);
	g_object_set(G_OBJECT(s), "ssl-strict", strictssl, NULL);

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
eval(Client *c, const Arg *arg) {
	WebKitWebFrame *frame = webkit_web_view_get_main_frame(c->view);
	evalscript(webkit_web_frame_get_global_context(frame),
			((char **)arg->v)[0], "");
}

void
stop(Client *c, const Arg *arg) {
	webkit_web_view_stop_loading(c->view);
}

void
titlechange(WebKitWebView *v, WebKitWebFrame *f, const char *t, Client *c) {
	c->title = copystr(&c->title, t);
	update(c);
}

void
update(Client *c) {
	char *t;

	if(c->linkhover) {
		t = g_strdup(c->linkhover);
	} else if(c->progress != 100) {
		drawindicator(c);
		gtk_widget_show(c->indicator);
		t = g_strdup_printf("[%i%%] %s", c->progress, c->title);
	} else {
		gtk_widget_hide_all(c->indicator);
		t = g_strdup(c->title);
	}
	gtk_window_set_title(GTK_WINDOW(c->win), t);
	g_free(t);
}

void
updatewinid(Client *c) {
	snprintf(winid, LENGTH(winid), "%u",
			(int)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
}

void
usage(void) {
	fputs("surf - simple browser\n", stderr);
	die("usage: surf [-e xid] [-i] [-p] [-s] [-v] [-x] [uri]\n");
}

void
windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js, JSObjectRef win, Client *c) {
	runscript(frame);
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

int
main(int argc, char *argv[]) {
	int i;
	Arg arg;

	progname = argv[0];
	/* command line args */
	for(i = 1, arg.v = NULL; i < argc && argv[i][0] == '-' &&
			argv[i][1] != '\0' && argv[i][2] == '\0'; i++) {
		if(!strcmp(argv[i], "--")) {
			i++;
			break;
		}
		switch(argv[i][1]) {
		case 'e':
			if(++i < argc)
				embed = strtol(argv[i], NULL, 0);
			else
				usage();
			break;
		case 'i':
			loadimage = 0;
			break;
		case 'p':
			plugin = 0;
			break;
		case 's':
			script = 0;
			break;
		case 'x':
			showxid = TRUE;
			break;
		case 'v':
			die("surf-"VERSION", ©2009-2012 surf engineers, see LICENSE for details\n");
		default:
			usage();
		}
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
