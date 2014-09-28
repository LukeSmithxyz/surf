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
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <webkit/webkit.h>
#include <glib/gstdio.h>
#include <JavaScriptCore/JavaScript.h>
#include <sys/file.h>
#include <libgen.h>
#include <stdarg.h>

#include "arg.h"

char *argv0;

#define LENGTH(x)               (sizeof x / sizeof x[0])
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK))
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
	GtkWidget *win, *scroll, *vbox, *pane;
	WebKitWebView *view;
	WebKitWebInspector *inspector;
	char *title, *linkhover;
	const char *needle;
	gint progress;
	struct Client *next;
	gboolean zoomed, fullscreen, isinspecting, sslfailed;
} Client;

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
static gboolean usingproxy = 0;
static char togglestat[8];
static char pagestat[3];
static GTlsDatabase *tlsdb;
static int policysel = 0;

static void addaccelgroup(Client *c);
static void beforerequest(WebKitWebView *w, WebKitWebFrame *f,
		WebKitWebResource *r, WebKitNetworkRequest *req,
		WebKitNetworkResponse *resp, gpointer d);
static char *buildpath(const char *path);
static gboolean buttonrelease(WebKitWebView *web, GdkEventButton *e,
		GList *gl);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);

/* Cookiejar implementation */
static void cookiejar_changed(SoupCookieJar *self, SoupCookie *old_cookie,
		SoupCookie *new_cookie);
static void cookiejar_finalize(GObject *self);
static SoupCookieJarAcceptPolicy cookiepolicy_get(void);
static SoupCookieJar *cookiejar_new(const char *filename, gboolean read_only,
		SoupCookieJarAcceptPolicy policy);
static void cookiejar_set_property(GObject *self, guint prop_id,
		const GValue *value, GParamSpec *pspec);
static char cookiepolicy_set(const SoupCookieJarAcceptPolicy p);

static char *copystr(char **str, const char *src);
static WebKitWebView *createwindow(WebKitWebView *v, WebKitWebFrame *f,
		Client *c);
static gboolean decidedownload(WebKitWebView *v, WebKitWebFrame *f,
		WebKitNetworkRequest *r, gchar *m,  WebKitWebPolicyDecision *p,
		Client *c);
static gboolean decidewindow(WebKitWebView *v, WebKitWebFrame *f,
		WebKitNetworkRequest *r, WebKitWebNavigationAction *n,
		WebKitWebPolicyDecision *p, Client *c);
static gboolean deletion_interface(WebKitWebView *view,
		WebKitDOMHTMLElement *arg1, Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(const char *errstr, ...);
static void eval(Client *c, const Arg *arg);
static void find(Client *c, const Arg *arg);
static void fullscreen(Client *c, const Arg *arg);
static void geopolicyrequested(WebKitWebView *v, WebKitWebFrame *f,
		WebKitGeolocationPolicyDecision *d, Client *c);
static const char *getatom(Client *c, int a);
static void gettogglestat(Client *c);
static void getpagestat(Client *c);
static char *geturi(Client *c);
static gboolean initdownload(WebKitWebView *v, WebKitDownload *o, Client *c);

static void inspector(Client *c, const Arg *arg);
static WebKitWebView *inspector_new(WebKitWebInspector *i, WebKitWebView *v,
		Client *c);
static gboolean inspector_show(WebKitWebInspector *i, Client *c);
static gboolean inspector_close(WebKitWebInspector *i, Client *c);
static void inspector_finished(WebKitWebInspector *i, Client *c);

static gboolean keypress(GtkAccelGroup *group,
		GObject *obj, guint key, GdkModifierType mods,
		Client *c);
static void linkhover(WebKitWebView *v, const char* t, const char* l,
		Client *c);
static void loadstatuschange(WebKitWebView *view, GParamSpec *pspec,
		Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static void newwindow(Client *c, const Arg *arg, gboolean noembed);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static gboolean contextmenu(WebKitWebView *view, GtkWidget *menu,
		WebKitHitTestResult *target, gboolean keyboard, Client *c);
static void menuactivate(GtkMenuItem *item, Client *c);
static void print(Client *c, const Arg *arg);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
		gpointer d);
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
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void toggle(Client *c, const Arg *arg);
static void togglecookiepolicy(Client *c, const Arg *arg);
static void togglegeolocation(Client *c, const Arg *arg);
static void togglescrollbars(Client *c, const Arg *arg);
static void togglestyle(Client *c, const Arg *arg);
static void updatetitle(Client *c);
static void updatewinid(Client *c);
static void usage(void);
static void windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame,
		JSContextRef js, JSObjectRef win, Client *c);
static void zoom(Client *c, const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

static void
addaccelgroup(Client *c) {
	int i;
	GtkAccelGroup *group = gtk_accel_group_new();
	GClosure *closure;

	for(i = 0; i < LENGTH(keys); i++) {
		closure = g_cclosure_new(G_CALLBACK(keypress), c, NULL);
		gtk_accel_group_connect(group, keys[i].keyval, keys[i].mod,
				0, closure);
	}
	gtk_window_add_accel_group(GTK_WINDOW(c->win), group);
}

static void
beforerequest(WebKitWebView *w, WebKitWebFrame *f, WebKitWebResource *r,
		WebKitNetworkRequest *req, WebKitNetworkResponse *resp,
		gpointer d) {
	const gchar *uri = webkit_network_request_get_uri(req);

	if(g_str_has_suffix(uri, "/favicon.ico"))
		webkit_network_request_set_uri(req, "about:blank");
}

static char *
buildpath(const char *path) {
	char *apath, *p;
	FILE *f;

	/* creating directory */
	if(path[0] == '/') {
		apath = g_strdup(path);
	} else if(path[0] == '~') {
		if(path[1] == '/') {
			apath = g_strconcat(g_get_home_dir(), &path[1], NULL);
		} else {
			apath = g_strconcat(g_get_home_dir(), "/",
					&path[1], NULL);
		}
	} else {
		apath = g_strconcat(g_get_current_dir(), "/", path, NULL);
	}

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
	WebKitHitTestResult *result = webkit_web_view_get_hit_test_result(web,
			e);
	Arg arg;

	g_object_get(result, "context", &context, NULL);
	if(context & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK) {
		if(e->button == 2 ||
				(e->button == 1 && CLEANMASK(e->state) == CLEANMASK(MODKEY))) {
			g_object_get(result, "link-uri", &arg.v, NULL);
			newwindow(NULL, &arg, e->state & GDK_CONTROL_MASK);
			return true;
		}
	}
	return false;
}

static void
cleanup(void) {
	while(clients)
		destroyclient(clients);
	g_free(cookiefile);
	g_free(scriptfile);
	g_free(stylefile);
}

static void
cookiejar_changed(SoupCookieJar *self, SoupCookie *old_cookie,
		SoupCookie *new_cookie) {
	flock(COOKIEJAR(self)->lock, LOCK_EX);
	if(new_cookie && !new_cookie->expires && sessiontime) {
		soup_cookie_set_expires(new_cookie,
				soup_date_new_from_now(sessiontime));
	}
	SOUP_COOKIE_JAR_CLASS(cookiejar_parent_class)->changed(self,
			old_cookie, new_cookie);
	flock(COOKIEJAR(self)->lock, LOCK_UN);
}

static void
cookiejar_class_init(CookieJarClass *klass) {
	SOUP_COOKIE_JAR_CLASS(klass)->changed = cookiejar_changed;
	G_OBJECT_CLASS(klass)->get_property =
		G_OBJECT_CLASS(cookiejar_parent_class)->get_property;
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
cookiejar_new(const char *filename, gboolean read_only,
		SoupCookieJarAcceptPolicy policy) {
	return g_object_new(COOKIEJAR_TYPE,
	                    SOUP_COOKIE_JAR_TEXT_FILENAME, filename,
	                    SOUP_COOKIE_JAR_READ_ONLY, read_only,
			    SOUP_COOKIE_JAR_ACCEPT_POLICY, policy, NULL);
}

static void
cookiejar_set_property(GObject *self, guint prop_id, const GValue *value,
		GParamSpec *pspec) {
	flock(COOKIEJAR(self)->lock, LOCK_SH);
	G_OBJECT_CLASS(cookiejar_parent_class)->set_property(self, prop_id,
			value, pspec);
	flock(COOKIEJAR(self)->lock, LOCK_UN);
}

static SoupCookieJarAcceptPolicy
cookiepolicy_get(void) {
	switch(cookiepolicies[policysel]) {
	case 'a':
		return SOUP_COOKIE_JAR_ACCEPT_NEVER;
	case '@':
		return SOUP_COOKIE_JAR_ACCEPT_NO_THIRD_PARTY;
	case 'A':
	default:
		break;
	}

	return SOUP_COOKIE_JAR_ACCEPT_ALWAYS;
}

static char
cookiepolicy_set(const SoupCookieJarAcceptPolicy ep) {
	switch(ep) {
	case SOUP_COOKIE_JAR_ACCEPT_NEVER:
		return 'a';
	case SOUP_COOKIE_JAR_ACCEPT_NO_THIRD_PARTY:
		return '@';
	case SOUP_COOKIE_JAR_ACCEPT_ALWAYS:
	default:
		break;
	}

	return 'A';
}

static void
evalscript(JSContextRef js, char *script, char* scriptname) {
	JSStringRef jsscript, jsscriptname;
	JSValueRef exception = NULL;

	jsscript = JSStringCreateWithUTF8CString(script);
	jsscriptname = JSStringCreateWithUTF8CString(scriptname);
	JSEvaluateScript(js, jsscript, JSContextGetGlobalObject(js),
			jsscriptname, 0, &exception);
	JSStringRelease(jsscript);
	JSStringRelease(jsscriptname);
}

static void
runscript(WebKitWebFrame *frame) {
	char *script;
	GError *error;

	if(g_file_get_contents(scriptfile, &script, NULL, &error)) {
		evalscript(webkit_web_frame_get_global_context(frame),
				script, scriptfile);
	}
}

static void
clipboard(Client *c, const Arg *arg) {
	gboolean paste = *(gboolean *)arg;

	if(paste) {
		gtk_clipboard_request_text(
				gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				pasteuri, c);
	} else {
		gtk_clipboard_set_text(
				gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				c->linkhover ? c->linkhover : geturi(c), -1);
	}
}

static char *
copystr(char **str, const char *src) {
	char *tmp;
	tmp = g_strdup(src);

	if(str && *str) {
		g_free(*str);
		*str = tmp;
	}
	return tmp;
}

static WebKitWebView *
createwindow(WebKitWebView  *v, WebKitWebFrame *f, Client *c) {
	Client *n = newclient();
	return n->view;
}

static gboolean
decidedownload(WebKitWebView *v, WebKitWebFrame *f, WebKitNetworkRequest *r,
		gchar *m,  WebKitWebPolicyDecision *p, Client *c) {
	if(!webkit_web_view_can_show_mime_type(v, m)) {
		webkit_web_policy_decision_download(p);
		return TRUE;
	}
	return FALSE;
}

static gboolean
decidewindow(WebKitWebView *view, WebKitWebFrame *f, WebKitNetworkRequest *r,
		WebKitWebNavigationAction *n, WebKitWebPolicyDecision *p,
		Client *c) {
	Arg arg;

	if(webkit_web_navigation_action_get_reason(n) ==
			WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED) {
		webkit_web_policy_decision_ignore(p);
		arg.v = (void *)webkit_network_request_get_uri(r);
		newwindow(NULL, &arg, 0);
		return TRUE;
	}
	return FALSE;
}

static gboolean
deletion_interface(WebKitWebView *view,
		WebKitDOMHTMLElement *arg1, Client *c) {
	return FALSE;
}

static void
destroyclient(Client *c) {
	Client *p;

	webkit_web_view_stop_loading(c->view);
	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);

	for(p = clients; p && p->next != c; p = p->next);
	if(p) {
		p->next = c->next;
	} else {
		clients = c->next;
	}
	free(c);
	if(clients == NULL)
		gtk_main_quit();
}

static void
destroywin(GtkWidget* w, Client *c) {
	destroyclient(c);
}

static void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
find(Client *c, const Arg *arg) {
	const char *s;

	s = getatom(c, AtomFind);
	gboolean forward = *(gboolean *)arg;
	webkit_web_view_search_text(c->view, s, FALSE, forward, TRUE);
}

static void
fullscreen(Client *c, const Arg *arg) {
	if(c->fullscreen) {
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	} else {
		gtk_window_fullscreen(GTK_WINDOW(c->win));
	}
	c->fullscreen = !c->fullscreen;
}

static void
geopolicyrequested(WebKitWebView *v, WebKitWebFrame *f,
		WebKitGeolocationPolicyDecision *d, Client *c) {
	if(allowgeolocation) {
		webkit_geolocation_policy_allow(d);
	} else {
		webkit_geolocation_policy_deny(d);
	}
}

static const char *
getatom(Client *c, int a) {
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XGetWindowProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window),
			atoms[a], 0L, BUFSIZ, False, XA_STRING,
			&adummy, &idummy, &ldummy, &ldummy, &p);
	if(p) {
		strncpy(buf, (char *)p, LENGTH(buf)-1);
	} else {
		buf[0] = '\0';
	}
	XFree(p);

	return buf;
}

static char *
geturi(Client *c) {
	char *uri;

	if(!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

static gboolean
initdownload(WebKitWebView *view, WebKitDownload *o, Client *c) {
	Arg arg;

	updatewinid(c);
	arg = (Arg)DOWNLOAD((char *)webkit_download_get_uri(o), geturi(c));
	spawn(c, &arg);
	return FALSE;
}

static void
inspector(Client *c, const Arg *arg) {
	if(c->isinspecting) {
		webkit_web_inspector_close(c->inspector);
	} else {
		webkit_web_inspector_show(c->inspector);
	}
}

static WebKitWebView *
inspector_new(WebKitWebInspector *i, WebKitWebView *v, Client *c) {
	return WEBKIT_WEB_VIEW(webkit_web_view_new());
}

static gboolean
inspector_show(WebKitWebInspector *i, Client *c) {
	WebKitWebView *w;

	if(c->isinspecting)
		return false;

	w = webkit_web_inspector_get_web_view(i);
	gtk_paned_pack2(GTK_PANED(c->pane), GTK_WIDGET(w), TRUE, TRUE);
	gtk_widget_show(GTK_WIDGET(w));
	c->isinspecting = true;

	return true;
}

static gboolean
inspector_close(WebKitWebInspector *i, Client *c) {
	GtkWidget *w;

	if(!c->isinspecting)
		return false;

	w = GTK_WIDGET(webkit_web_inspector_get_web_view(i));
	gtk_widget_hide(w);
	gtk_widget_destroy(w);
	c->isinspecting = false;

	return true;
}

static void
inspector_finished(WebKitWebInspector *i, Client *c) {
	g_free(c->inspector);
}

static gboolean
keypress(GtkAccelGroup *group, GObject *obj,
		guint key, GdkModifierType mods, Client *c) {
	guint i;
	gboolean processed = FALSE;

	mods = CLEANMASK(mods);
	key = gdk_keyval_to_lower(key);
	updatewinid(c);
	for(i = 0; i < LENGTH(keys); i++) {
		if(key == keys[i].keyval
				&& mods == keys[i].mod
				&& keys[i].func) {
			keys[i].func(c, &(keys[i].arg));
			processed = TRUE;
		}
	}

	return processed;
}

static void
linkhover(WebKitWebView *v, const char* t, const char* l, Client *c) {
	if(l) {
		c->linkhover = copystr(&c->linkhover, l);
	} else if(c->linkhover) {
		free(c->linkhover);
		c->linkhover = NULL;
	}
	updatetitle(c);
}

static void
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
			c->sslfailed = !(soup_message_get_flags(msg)
			                & SOUP_MESSAGE_CERTIFICATE_TRUSTED);
		}
		setatom(c, AtomUri, uri);
		break;
	case WEBKIT_LOAD_FINISHED:
		c->progress = 100;
		updatetitle(c);
		break;
	default:
		break;
	}
}

static void
loaduri(Client *c, const Arg *arg) {
	char *u = NULL, *rp;
	const char *uri = (char *)arg->v;
	Arg a = { .b = FALSE };
	struct stat st;

	if(strcmp(uri, "") == 0)
		return;

	/* In case it's a file path. */
	if(stat(uri, &st) == 0) {
		rp = realpath(uri, NULL);
		u = g_strdup_printf("file://%s", rp);
		free(rp);
	} else {
		u = g_strrstr(uri, "://") ? g_strdup(uri)
			: g_strdup_printf("http://%s", uri);
	}

	setatom(c, AtomUri, uri);

	/* prevents endless loop */
	if(strcmp(u, geturi(c)) == 0) {
		reload(c, &a);
	} else {
		webkit_web_view_load_uri(c->view, u);
		c->progress = 0;
		c->title = copystr(&c->title, u);
		updatetitle(c);
	}
	g_free(u);
}

static void
navigate(Client *c, const Arg *arg) {
	int steps = *(int *)arg;
	webkit_web_view_go_back_or_forward(c->view, steps);
}

static Client *
newclient(void) {
	Client *c;
	WebKitWebSettings *settings;
	WebKitWebFrame *frame;
	GdkGeometry hints = { 1, 1 };
	GdkScreen *screen;
	gdouble dpi;
	char *uri, *ua;

	if(!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->title = NULL;
	c->progress = 100;

	/* Window */
	if(embed) {
		c->win = gtk_plug_new(embed);
	} else {
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
	g_signal_connect(G_OBJECT(c->win),
			"destroy",
			G_CALLBACK(destroywin), c);

	if(!kioskmode)
		addaccelgroup(c);

	/* Pane */
	c->pane = gtk_vpaned_new();

	/* VBox */
	c->vbox = gtk_vbox_new(FALSE, 0);
	gtk_paned_pack1(GTK_PANED(c->pane), c->vbox, TRUE, TRUE);

	/* Webview */
	c->view = WEBKIT_WEB_VIEW(webkit_web_view_new());

	g_signal_connect(G_OBJECT(c->view),
			"notify::title",
			G_CALLBACK(titlechange), c);
	g_signal_connect(G_OBJECT(c->view),
			"hovering-over-link",
			G_CALLBACK(linkhover), c);
	g_signal_connect(G_OBJECT(c->view),
			"geolocation-policy-decision-requested",
			G_CALLBACK(geopolicyrequested), c);
	g_signal_connect(G_OBJECT(c->view),
			"create-web-view",
			G_CALLBACK(createwindow), c);
	g_signal_connect(G_OBJECT(c->view),
			"new-window-policy-decision-requested",
			G_CALLBACK(decidewindow), c);
	g_signal_connect(G_OBJECT(c->view),
			"mime-type-policy-decision-requested",
			G_CALLBACK(decidedownload), c);
	g_signal_connect(G_OBJECT(c->view),
			"window-object-cleared",
			G_CALLBACK(windowobjectcleared), c);
	g_signal_connect(G_OBJECT(c->view),
			"notify::load-status",
			G_CALLBACK(loadstatuschange), c);
	g_signal_connect(G_OBJECT(c->view),
			"notify::progress",
			G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view),
			"download-requested",
			G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(c->view),
			"button-release-event",
			G_CALLBACK(buttonrelease), c);
	g_signal_connect(G_OBJECT(c->view),
			"context-menu",
			G_CALLBACK(contextmenu), c);
	g_signal_connect(G_OBJECT(c->view),
			"resource-request-starting",
			G_CALLBACK(beforerequest), c);
	g_signal_connect(G_OBJECT(c->view),
			"should-show-delete-interface-for-element",
			G_CALLBACK(deletion_interface), c);

	/* Scrolled Window */
	c->scroll = gtk_scrolled_window_new(NULL, NULL);

	frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(c->view));
	g_signal_connect(G_OBJECT(frame), "scrollbars-policy-changed",
			G_CALLBACK(gtk_true), NULL);

	if(!enablescrollbars) {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	} else {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	}

	/* Arranging */
	gtk_container_add(GTK_CONTAINER(c->scroll), GTK_WIDGET(c->view));
	gtk_container_add(GTK_CONTAINER(c->win), c->pane);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);

	/* Setup */
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->scroll, TRUE,
			TRUE, 0, GTK_PACK_START);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_show(c->pane);
	gtk_widget_show(c->vbox);
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gtk_window_set_geometry_hints(GTK_WINDOW(c->win), NULL, &hints,
			GDK_HINT_MIN_SIZE);
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	webkit_web_view_set_full_content_zoom(c->view, TRUE);

	runscript(frame);

	settings = webkit_web_view_get_settings(c->view);
	if(!(ua = getenv("SURF_USERAGENT")))
		ua = useragent;
	g_object_set(G_OBJECT(settings), "user-agent", ua, NULL);
	uri = g_strconcat("file://", stylefile, NULL);
	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", uri, NULL);
	g_object_set(G_OBJECT(settings), "auto-load-images", loadimages,
			NULL);
	g_object_set(G_OBJECT(settings), "enable-plugins", enableplugins,
			NULL);
	g_object_set(G_OBJECT(settings), "enable-scripts", enablescripts,
			NULL);
	g_object_set(G_OBJECT(settings), "enable-spatial-navigation",
			enablespatialbrowsing, NULL);
	g_object_set(G_OBJECT(settings), "enable-developer-extras",
			enableinspector, NULL);
	g_object_set(G_OBJECT(settings), "enable-default-context-menu",
			kioskmode ^ 1, NULL);
	g_object_set(G_OBJECT(settings), "default-font-size",
			defaultfontsize, NULL);
	g_object_set(G_OBJECT(settings), "resizable-text-areas",
			1, NULL);

	/*
	 * While stupid, CSS specifies that a pixel represents 1/96 of an inch.
	 * This ensures websites are not unusably small with a high DPI screen.
	 * It is equivalent to firefox's "layout.css.devPixelsPerPx" setting.
	 */
	if(zoomto96dpi) {
		screen = gdk_window_get_screen(GTK_WIDGET(c->win)->window);
		dpi = gdk_screen_get_resolution(screen);
		if(dpi != -1) {
			g_object_set(G_OBJECT(settings), "enforce-96-dpi", true,
					NULL);
			webkit_web_view_set_zoom_level(c->view, dpi/96);
		}
	}
	/* This might conflict with _zoomto96dpi_. */
	if(zoomlevel != 1.0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel);

	if(enableinspector) {
		c->inspector = WEBKIT_WEB_INSPECTOR(
				webkit_web_view_get_inspector(c->view));
		g_signal_connect(G_OBJECT(c->inspector), "inspect-web-view",
				G_CALLBACK(inspector_new), c);
		g_signal_connect(G_OBJECT(c->inspector), "show-window",
				G_CALLBACK(inspector_show), c);
		g_signal_connect(G_OBJECT(c->inspector), "close-window",
				G_CALLBACK(inspector_close), c);
		g_signal_connect(G_OBJECT(c->inspector), "finished",
				G_CALLBACK(inspector_finished), c);
		c->isinspecting = false;
	}

	if(runinfullscreen) {
		c->fullscreen = 0;
		fullscreen(c, NULL);
	}

	g_free(uri);

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");
	if(hidebackground)
		webkit_web_view_set_transparent(c->view, TRUE);

	c->next = clients;
	clients = c;

	if(showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		printf("%u\n",
			(guint)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
		fflush(NULL);
                if (fclose(stdout) != 0) {
			die("Error closing stdout");
                }
	}

	return c;
}

static void
newwindow(Client *c, const Arg *arg, gboolean noembed) {
	guint i = 0;
	const char *cmd[16], *uri;
	const Arg a = { .v = (void *)cmd };
	char tmp[64];

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = cookiepolicies;
	if(!enablescrollbars)
		cmd[i++] = "-b";
	if(embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%u\n", (int)embed);
		cmd[i++] = tmp;
	}
	if(!loadimages)
		cmd[i++] = "-i";
	if(kioskmode)
		cmd[i++] = "-k";
	if(!enableplugins)
		cmd[i++] = "-p";
	if(!enablescripts)
		cmd[i++] = "-s";
	if(showxid)
		cmd[i++] = "-x";
	cmd[i++] = "-c";
	cmd[i++] = cookiefile;
	cmd[i++] = "--";
	uri = arg->v ? (char *)arg->v : c->linkhover;
	if(uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

static gboolean
contextmenu(WebKitWebView *view, GtkWidget *menu, WebKitHitTestResult *target,
		gboolean keyboard, Client *c) {
	GList *items = gtk_container_get_children(GTK_CONTAINER(GTK_MENU(menu)));

	for(GList *l = items; l; l = l->next) {
		g_signal_connect(l->data, "activate", G_CALLBACK(menuactivate), c);
	}

	g_list_free(items);
	return FALSE;
}

static void
menuactivate(GtkMenuItem *item, Client *c) {
	/*
	 * context-menu-action-2000	open link
	 * context-menu-action-1	open link in window
	 * context-menu-action-2	download linked file
	 * context-menu-action-3	copy link location
	 * context-menu-action-7	copy image address
	 * context-menu-action-13	reload
	 * context-menu-action-10	back
	 * context-menu-action-11	forward
	 * context-menu-action-12	stop
	 */

	GtkAction *a = NULL;
	const char *name, *uri;
	GtkClipboard *prisel, *clpbrd;

	a = gtk_activatable_get_related_action(GTK_ACTIVATABLE(item));
	if(a == NULL)
		return;

	name = gtk_action_get_name(a);
	if(!g_strcmp0(name, "context-menu-action-3")) {
		prisel = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(prisel, c->linkhover, -1);
	} else if(!g_strcmp0(name, "context-menu-action-7")) {
		prisel = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		clpbrd = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		uri = gtk_clipboard_wait_for_text(clpbrd);
		if(uri)
			gtk_clipboard_set_text(prisel, uri, -1);
	}
}

static void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d) {
	Arg arg = {.v = text };
	if(text != NULL)
		loaduri((Client *) d, &arg);
}

static void
print(Client *c, const Arg *arg) {
	webkit_web_frame_print(webkit_web_view_get_main_frame(c->view));
}

static GdkFilterReturn
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
			} else if(ev->atom == atoms[AtomGo]) {
				arg.v = getatom(c, AtomGo);
				loaduri(c, &arg);

				return GDK_FILTER_REMOVE;
			}
		}
	}
	return GDK_FILTER_CONTINUE;
}

static void
progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	c->progress = webkit_web_view_get_progress(c->view) * 100;
	updatetitle(c);
}

static void
reload(Client *c, const Arg *arg) {
	gboolean nocache = *(gboolean *)arg;
	if(nocache) {
		 webkit_web_view_reload_bypass_cache(c->view);
	} else {
		 webkit_web_view_reload(c->view);
	}
}

static void
scroll_h(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_hadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

static void
scroll_v(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_vadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

static void
scroll(GtkAdjustment *a, const Arg *arg) {
	gdouble v;

	v = gtk_adjustment_get_value(a);
	switch(arg->i) {
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

static void
setatom(Client *c, int a, const char *v) {
	XSync(dpy, False);
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window),
			atoms[a], XA_STRING, 8, PropModeReplace,
			(unsigned char *)v, strlen(v) + 1);
}

static void
setup(void) {
	char *proxy;
	char *new_proxy;
	SoupURI *puri;
	SoupSession *s;
	GError *error = NULL;

	/* clean up any zombies immediately */
	sigchld(0);
	gtk_init(NULL, NULL);

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
	soup_session_add_feature(s,
			SOUP_SESSION_FEATURE(cookiejar_new(cookiefile, FALSE,
					cookiepolicy_get())));

	/* ssl */
	tlsdb = g_tls_file_database_new(cafile, &error);

	if(error) {
		g_warning("Error loading SSL database %s: %s", cafile, error->message);
		g_error_free(error);
	}
	g_object_set(G_OBJECT(s), "tls-database", tlsdb, NULL);
	g_object_set(G_OBJECT(s), "ssl-strict", strictssl, NULL);

	/* proxy */
	if((proxy = getenv("http_proxy")) && strcmp(proxy, "")) {
		new_proxy = g_strrstr(proxy, "http://") ? g_strdup(proxy) :
			g_strdup_printf("http://%s", proxy);
		puri = soup_uri_new(new_proxy);
		g_object_set(G_OBJECT(s), "proxy-uri", puri, NULL);
		soup_uri_free(puri);
		g_free(new_proxy);
		usingproxy = 1;
	}
}

static void
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void
source(Client *c, const Arg *arg) {
	Arg a = { .b = FALSE };
	gboolean s;

	s = webkit_web_view_get_view_source_mode(c->view);
	webkit_web_view_set_view_source_mode(c->view, !s);
	reload(c, &a);
}

static void
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

static void
eval(Client *c, const Arg *arg) {
	WebKitWebFrame *frame = webkit_web_view_get_main_frame(c->view);
	evalscript(webkit_web_frame_get_global_context(frame),
			((char **)arg->v)[0], "");
}

static void
stop(Client *c, const Arg *arg) {
	webkit_web_view_stop_loading(c->view);
}

static void
titlechange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	const gchar *t = webkit_web_view_get_title(view);
	if (t) {
		c->title = copystr(&c->title, t);
		updatetitle(c);
	}
}

static void
toggle(Client *c, const Arg *arg) {
	WebKitWebSettings *settings;
	char *name = (char *)arg->v;
	gboolean value;
	Arg a = { .b = FALSE };

	settings = webkit_web_view_get_settings(c->view);
	g_object_get(G_OBJECT(settings), name, &value, NULL);
	g_object_set(G_OBJECT(settings), name, !value, NULL);

	reload(c, &a);
}

static void
togglecookiepolicy(Client *c, const Arg *arg) {
	SoupCookieJar *jar;
	SoupCookieJarAcceptPolicy policy;

	jar = SOUP_COOKIE_JAR(
			soup_session_get_feature(
				webkit_get_default_session(),
				SOUP_TYPE_COOKIE_JAR));
	g_object_get(G_OBJECT(jar), "accept-policy", &policy, NULL);

	policysel++;
	if(policysel >= strlen(cookiepolicies))
		policysel = 0;

	g_object_set(G_OBJECT(jar), "accept-policy",
			cookiepolicy_get(), NULL);

	updatetitle(c);
	/* Do not reload. */
}

static void
togglegeolocation(Client *c, const Arg *arg) {
	Arg a = { .b = FALSE };

	allowgeolocation ^= 1;

	reload(c, &a);
}

static void
twitch(Client *c, const Arg *arg) {
	GtkAdjustment *a;
	gdouble v;

	a = gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(c->scroll));

	v = gtk_adjustment_get_value(a);

	v += arg->i;

	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) -
			gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

static void
togglescrollbars(Client *c, const Arg *arg) {
	GtkPolicyType vspolicy;
	Arg a;

	gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(c->scroll), NULL, &vspolicy);

	if(vspolicy == GTK_POLICY_AUTOMATIC) {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	} else {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		a.i = +1;
		twitch(c, &a);
		a.i = -1;
		twitch(c, &a);
	}
}

static void
togglestyle(Client *c, const Arg *arg) {
	WebKitWebSettings *settings;
	char *uri;

	settings = webkit_web_view_get_settings(c->view);
	g_object_get(G_OBJECT(settings), "user-stylesheet-uri", &uri, NULL);
	uri = uri[0] ? g_strdup("") : g_strconcat("file://", stylefile, NULL);
	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", uri, NULL);

	updatetitle(c);
}

static void
gettogglestat(Client *c){
	gboolean value;
	char *uri;
	int p = 0;
	WebKitWebSettings *settings = webkit_web_view_get_settings(c->view);

	togglestat[p++] = cookiepolicy_set(cookiepolicy_get());

	g_object_get(G_OBJECT(settings), "enable-caret-browsing",
			&value, NULL);
	togglestat[p++] = value? 'C': 'c';

	togglestat[p++] = allowgeolocation? 'G': 'g';

	g_object_get(G_OBJECT(settings), "auto-load-images", &value, NULL);
	togglestat[p++] = value? 'I': 'i';

	g_object_get(G_OBJECT(settings), "enable-scripts", &value, NULL);
	togglestat[p++] = value? 'S': 's';

	g_object_get(G_OBJECT(settings), "enable-plugins", &value, NULL);
	togglestat[p++] = value? 'V': 'v';

	g_object_get(G_OBJECT(settings), "user-stylesheet-uri", &uri, NULL);
	togglestat[p++] = uri[0] ? 'M': 'm';

	togglestat[p] = '\0';
}

static void
getpagestat(Client *c) {
	const char *uri = geturi(c);

	if(strstr(uri, "https://") == uri) {
		pagestat[0] = c->sslfailed ? 'U' : 'T';
	} else {
		pagestat[0] = '-';
	}

	pagestat[1] = usingproxy ? 'P' : '-';
	pagestat[2] = '\0';

}

static void
updatetitle(Client *c) {
	char *t;

	if(showindicators) {
		gettogglestat(c);
		getpagestat(c);

		if(c->linkhover) {
			t = g_strdup_printf("%s:%s | %s", togglestat,
					pagestat, c->linkhover);
		} else if(c->progress != 100) {
			t = g_strdup_printf("[%i%%] %s:%s | %s", c->progress,
					togglestat, pagestat,
					(c->title == NULL)? "" : c->title);
		} else {
			t = g_strdup_printf("%s:%s | %s", togglestat, pagestat,
					(c->title == NULL)? "" : c->title);
		}

		gtk_window_set_title(GTK_WINDOW(c->win), t);
		g_free(t);
	} else {
		gtk_window_set_title(GTK_WINDOW(c->win),
				(c->title == NULL)? "" : c->title);
	}
}

static void
updatewinid(Client *c) {
	snprintf(winid, LENGTH(winid), "%u",
			(int)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
}

static void
usage(void) {
	die("usage: %s [-bBfFgGiIkKnNpPsSvx]"
		" [-a cookiepolicies ] "
		" [-c cookiefile] [-e xid] [-r scriptfile]"
		" [-t stylefile] [-u useragent] [-z zoomlevel]"
		" [uri]\n", basename(argv0));
}

static void
windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js,
		JSObjectRef win, Client *c) {
	runscript(frame);
}

static void
zoom(Client *c, const Arg *arg) {
	c->zoomed = TRUE;
	if(arg->i < 0) {
		/* zoom out */
		webkit_web_view_zoom_out(c->view);
	} else if(arg->i > 0) {
		/* zoom in */
		webkit_web_view_zoom_in(c->view);
	} else {
		/* reset */
		c->zoomed = FALSE;
		webkit_web_view_set_zoom_level(c->view, 1.0);
	}
}

int
main(int argc, char *argv[]) {
	Arg arg;
	Client *c;

	memset(&arg, 0, sizeof(arg));

	/* command line args */
	ARGBEGIN {
	case 'a':
		cookiepolicies = EARGF(usage());
		break;
	case 'b':
		enablescrollbars = 0;
		break;
	case 'B':
		enablescrollbars = 1;
		break;
	case 'c':
		cookiefile = EARGF(usage());
		break;
	case 'e':
		embed = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'f':
		runinfullscreen = 1;
		break;
	case 'F':
		runinfullscreen = 0;
		break;
	case 'g':
		allowgeolocation = 0;
		break;
	case 'G':
		allowgeolocation = 1;
		break;
	case 'i':
		loadimages = 0;
		break;
	case 'I':
		loadimages = 1;
		break;
	case 'k':
		kioskmode = 0;
		break;
	case 'K':
		kioskmode = 1;
		break;
	case 'n':
		enableinspector = 0;
		break;
	case 'N':
		enableinspector = 1;
		break;
	case 'p':
		enableplugins = 0;
		break;
	case 'P':
		enableplugins = 1;
		break;
	case 'r':
		scriptfile = EARGF(usage());
		break;
	case 's':
		enablescripts = 0;
		break;
	case 'S':
		enablescripts = 1;
		break;
	case 't':
		stylefile = EARGF(usage());
		break;
	case 'u':
		useragent = EARGF(usage());
		break;
	case 'v':
		die("surf-"VERSION", ©2009-2014 surf engineers, "
				"see LICENSE for details\n");
	case 'x':
		showxid = TRUE;
		break;
	case 'z':
		zoomlevel = strtof(EARGF(usage()), NULL);
		break;
	default:
		usage();
	} ARGEND;
	if(argc > 0)
		arg.v = argv[0];

	setup();
	c = newclient();
	if(arg.v) {
		loaduri(clients, &arg);
	} else {
		updatetitle(c);
	}

	gtk_main();
	cleanup();

	return EXIT_SUCCESS;
}

