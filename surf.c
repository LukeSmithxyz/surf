/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <signal.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtkx.h>
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
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>
#include <JavaScriptCore/JavaScript.h>
#include <sys/file.h>
#include <libgen.h>
#include <stdarg.h>
#include <regex.h>
#include <pwd.h>
#include <string.h>

#include "arg.h"

char *argv0;

#define LENGTH(x)               (sizeof(x) / sizeof(x[0]))
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK))

enum { AtomFind, AtomGo, AtomUri, AtomLast };

enum {
	OnDoc   = WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT,
	OnLink  = WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK,
	OnImg   = WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE,
	OnMedia = WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA,
	OnEdit  = WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE,
	OnBar   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR,
	OnSel   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION,
	OnAny   = OnDoc | OnLink | OnImg | OnMedia | OnEdit | OnBar | OnSel,
};

typedef union Arg Arg;
union Arg {
	gboolean b;
	gint i;
	const void *v;
};

typedef struct Client {
	GtkWidget *win;
	Window xid;
	WebKitWebView *view;
	WebKitWebInspector *inspector;
	WebKitHitTestResult *mousepos;
	const char *title, *targeturi;
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
	unsigned int click;
	unsigned int mask;
	guint button;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
} Button;

typedef struct {
	char *regex;
	char *style;
	regex_t re;
} SiteStyle;

static Display *dpy;
static Atom atoms[AtomLast];
static Client *clients = NULL;
static Window embed = 0;
static gboolean showxid = FALSE;
static char winid[64];
static char togglestat[9];
static char pagestat[3];
static GTlsDatabase *tlsdb;
static int cookiepolicy;
static char *stylefile = NULL;

static void addaccelgroup(Client *c);
static void beforerequest(WebKitWebView *w, WebKitWebFrame *f,
                          WebKitWebResource *r, WebKitNetworkRequest *req,
                          WebKitNetworkResponse *resp, Client *c);
static char *buildfile(const char *path);
static char *buildpath(const char *path);
static gboolean buttonrelease(WebKitWebView *web, GdkEventButton *e, Client *c);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);

static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p);

static char *copystr(char **str, const char *src);
static GtkWidget *createview(WebKitWebView *v, WebKitNavigationAction *a,
		Client *c);
static gboolean decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
    WebKitPolicyDecisionType dt, Client *c);
static void decidenavigation(WebKitPolicyDecision *d, Client *c);
static void decidenewwindow(WebKitPolicyDecision *d, Client *c);
static void decideresource(WebKitPolicyDecision *d, Client *c);
static gboolean deletion_interface(WebKitWebView *view,
                                   WebKitDOMHTMLElement *arg1, Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(const char *errstr, ...);
static void eval(Client *c, const Arg *arg);
static void find(Client *c, const Arg *arg);
static void fullscreen(Client *c, const Arg *arg);
static gboolean permissionrequested(WebKitWebView *v,
		WebKitPermissionRequest *r, Client *c);
static const char *getatom(Client *c, int a);
static void gettogglestat(Client *c);
static void getpagestat(Client *c);
static char *geturi(Client *c);
static const gchar *getstyle(const char *uri);
static void setstyle(Client *c, const char *style);

static void handleplumb(Client *c, WebKitWebView *w, const gchar *uri);

static gboolean initdownload(WebKitWebView *v, WebKitDownload *o, Client *c);

static void inspector(Client *c, const Arg *arg);
static WebKitWebView *inspector_new(WebKitWebInspector *i, WebKitWebView *v,
                                    Client *c);
static gboolean inspector_show(WebKitWebInspector *i, Client *c);
static gboolean inspector_close(WebKitWebInspector *i, Client *c);
static void inspector_finished(WebKitWebInspector *i, Client *c);

static gboolean keypress(GtkAccelGroup *group, GObject *obj, guint key,
                         GdkModifierType mods, Client *c);
static void mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h,
		guint modifiers, Client *c);
static void loadstatuschange(WebKitWebView *view, GParamSpec *pspec,
                             Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(Client *c);
static WebKitWebView *newview(Client *c, WebKitWebView *rv);
static void showview(WebKitWebView *v, Client *c);
static void newwindow(Client *c, const Arg *arg, gboolean noembed);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static gboolean contextmenu(WebKitWebView *view, GtkWidget *menu,
                            WebKitHitTestResult *target, gboolean keyboard,
			    Client *c);
static void menuactivate(GtkMenuItem *item, Client *c);
static void print(Client *c, const Arg *arg);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
                                gpointer d);
static void progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void linkopen(Client *c, const Arg *arg);
static void linkopenembed(Client *c, const Arg *arg);
static void reload(Client *c, const Arg *arg);
static void scroll_h(Client *c, const Arg *arg);
static void scroll_v(Client *c, const Arg *arg);
static void scroll(GtkAdjustment *a, const Arg *arg);
static void setatom(Client *c, int a, const char *v);
static void setup(void);
static void sigchld(int unused);
static void spawn(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c);
static void titlechangeleave(void *a, void *b, Client *c);
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

void
addaccelgroup(Client *c)
{
	int i;
	GtkAccelGroup *group = gtk_accel_group_new();
	GClosure *closure;

	for (i = 0; i < LENGTH(keys); i++) {
		closure = g_cclosure_new(G_CALLBACK(keypress), c, NULL);
		gtk_accel_group_connect(group, keys[i].keyval, keys[i].mod, 0,
		                        closure);
	}
	gtk_window_add_accel_group(GTK_WINDOW(c->win), group);
}

void
beforerequest(WebKitWebView *w, WebKitWebFrame *f, WebKitWebResource *r,
              WebKitNetworkRequest *req, WebKitNetworkResponse *resp,
	      Client *c)
{
	const gchar *uri = webkit_network_request_get_uri(req);
	int i, isascii = 1;

	if (g_str_has_suffix(uri, "/favicon.ico"))
		webkit_network_request_set_uri(req, "about:blank");

	if (!g_str_has_prefix(uri, "http://")
	    && !g_str_has_prefix(uri, "https://")
	    && !g_str_has_prefix(uri, "about:")
	    && !g_str_has_prefix(uri, "file://")
	    && !g_str_has_prefix(uri, "data:")
	    && !g_str_has_prefix(uri, "blob:")
	    && strlen(uri) > 0) {
		for (i = 0; i < strlen(uri); i++) {
			if (!g_ascii_isprint(uri[i])) {
				isascii = 0;
				break;
			}
		}
		if (isascii)
			handleplumb(c, w, uri);
	}
}

char *
buildfile(const char *path)
{
	char *dname, *bname, *bpath, *fpath;
	FILE *f;

	dname = g_path_get_dirname(path);
	bname = g_path_get_basename(path);

	bpath = buildpath(dname);
	g_free(dname);

	fpath = g_build_filename(bpath, bname, NULL);
	g_free(bpath);
	g_free(bname);

	if (!(f = fopen(fpath, "a")))
		die("Could not open file: %s\n", fpath);

	g_chmod(fpath, 0600); /* always */
	fclose(f);

	return fpath;
}

char *
buildpath(const char *path)
{
	struct passwd *pw;
	char *apath, *name, *p, *fpath;

	if (path[0] == '~') {
		if (path[1] == '/' || path[1] == '\0') {
			p = (char *)&path[1];
			pw = getpwuid(getuid());
		} else {
			if ((p = strchr(path, '/')))
				name = g_strndup(&path[1], --p - path);
			else
				name = g_strdup(&path[1]);

			if (!(pw = getpwnam(name))) {
				die("Can't get user %s home directory: %s.\n",
				    name, path);
			}
			g_free(name);
		}
		apath = g_build_filename(pw->pw_dir, p, NULL);
	} else {
		apath = g_strdup(path);
	}

	/* creating directory */
	if (g_mkdir_with_parents(apath, 0700) < 0)
		die("Could not access directory: %s\n", apath);

	fpath = realpath(apath, NULL);
	g_free(apath);

	return fpath;
}

gboolean
buttonrelease(WebKitWebView *web, GdkEventButton *e, Client *c)
{
	WebKitHitTestResultContext context;
	WebKitHitTestResult *result;
	Arg arg;
	unsigned int i;

	result = webkit_web_view_get_hit_test_result(web, e);
	g_object_get(result, "context", &context, NULL);
	g_object_get(result, "link-uri", &arg.v, NULL);
	for (i = 0; i < LENGTH(buttons); i++) {
		if (context & buttons[i].click
		    && e->button == buttons[i].button
		    && CLEANMASK(e->state) == CLEANMASK(buttons[i].mask)
		    && buttons[i].func) {
			buttons[i].func(c, buttons[i].click == ClkLink
			    && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
			return true;
		}
	}
	return false;
}

void
cleanup(void)
{
	while (clients)
		destroyclient(clients);
	g_free(cookiefile);
	g_free(scriptfile);
	g_free(stylefile);
	g_free(cachedir);
}

WebKitCookieAcceptPolicy
cookiepolicy_get(void)
{
	switch (cookiepolicies[cookiepolicy]) {
	case 'a':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
	case '@':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
	case 'A':
	default:
		break;
	}

	return WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
}

char
cookiepolicy_set(const WebKitCookieAcceptPolicy ep)
{
	switch (ep) {
	case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER:
		return 'a';
	case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY:
		return '@';
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS:
	default:
		break;
	}

	return 'A';
}

void
evalscript(JSContextRef js, char *script, char* scriptname)
{
	JSStringRef jsscript, jsscriptname;
	JSValueRef exception = NULL;

	jsscript = JSStringCreateWithUTF8CString(script);
	jsscriptname = JSStringCreateWithUTF8CString(scriptname);
	JSEvaluateScript(js, jsscript, JSContextGetGlobalObject(js),
	                 jsscriptname, 0, &exception);
	JSStringRelease(jsscript);
	JSStringRelease(jsscriptname);
}

void
runscript(WebKitWebFrame *frame)
{
	char *script;
	GError *error;

	if (g_file_get_contents(scriptfile, &script, NULL, &error)) {
		evalscript(webkit_web_frame_get_global_context(frame), script,
		           scriptfile);
	}
}

void
clipboard(Client *c, const Arg *arg)
{
	gboolean paste = *(gboolean *)arg;

	if (paste) {
		gtk_clipboard_request_text(gtk_clipboard_get(
		                           GDK_SELECTION_PRIMARY),
		                           pasteuri, c);
	} else {
		gtk_clipboard_set_text(gtk_clipboard_get(
		                       GDK_SELECTION_PRIMARY), c->linkhover
		                       ? c->linkhover : geturi(c), -1);
	}
}

char *
copystr(char **str, const char *src)
{
	char *tmp;
	tmp = g_strdup(src);

	if (str && *str) {
		g_free(*str);
		*str = tmp;
	}
	return tmp;
}

GtkWidget *
createview(WebKitWebView *v, WebKitNavigationAction *a, Client *c)
{
	Client *n;

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
		/*
		 * popup windows of type “other” are almost always triggered
		 * by user gesture, so inverse the logic here
		 */
/* instead of this, compare destination uri to mouse-over uri for validating window */
		if (webkit_navigation_action_is_user_gesture(a)) {
			return NULL;
			break;
		}
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		n = newclient(c);
		break;
	default:
		return NULL;
		break;
	}

	return GTK_WIDGET(n->view);
}

gboolean
decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
    WebKitPolicyDecisionType dt, Client *c)
{
	switch (dt) {
	case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
		decidenavigation(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
		decidenewwindow(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
		decideresource(d, c);
		break;
	default:
		webkit_policy_decision_ignore(d);
		break;
	}
	return TRUE;
}

void
decidenavigation(WebKitPolicyDecision *d, Client *c)
{
	WebKitNavigationAction *a;

	a = webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		/* Do not navigate to links with a "_blank" target (popup) */
		if (webkit_navigation_policy_decision_get_frame_name(
		    WEBKIT_NAVIGATION_POLICY_DECISION(d))) {
			webkit_policy_decision_ignore(d);
		} else {
			/* Filter out navigation to different domain ? */
			/* get action→urirequest, copy and load in new window+view
			 * on Ctrl+Click ? */
			webkit_policy_decision_use(d);
		}
		break;
	}
}

void
decidenewwindow(WebKitPolicyDecision *d, Client *c)
{
	WebKitNavigationAction *a;
	Arg arg;

	a = webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		/* Filter domains here */
/* If the value of “mouse-button” is not 0, then the navigation was triggered by a mouse event.
 * test for link clicked but no button ? */
		arg.v = webkit_uri_request_get_uri(
		    webkit_navigation_action_get_request(a));
		newwindow(c, &arg, 0);
		break;
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		break;
	}

	webkit_policy_decision_ignore(d);
}

void
decideresource(WebKitPolicyDecision *d, Client *c)
{
	WebKitResponsePolicyDecision *r = WEBKIT_RESPONSE_POLICY_DECISION(d);
	WebKitURIResponse *res;

	if (webkit_response_policy_decision_is_mime_type_supported(r)) {
		webkit_policy_decision_use(d);
	} else {
res = webkit_response_policy_decision_get_response(r);
		webkit_policy_decision_ignore(d);
		download(c, res);
	}
}

gboolean
deletion_interface(WebKitWebView *view, WebKitDOMHTMLElement *arg1, Client *c)
{
	return FALSE;
}

void
destroyclient(Client *c)
{
	Client *p;

	webkit_web_view_stop_loading(c->view);
	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);

	for (p = clients; p && p->next != c; p = p->next)
		;
	if (p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
	if (clients == NULL)
		gtk_main_quit();
}

void
destroywin(GtkWidget* w, Client *c)
{
	destroyclient(c);
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
find(Client *c, const Arg *arg)
{
	const char *s;

	s = getatom(c, AtomFind);
	gboolean forward = *(gboolean *)arg;
	webkit_web_view_search_text(c->view, s, FALSE, forward, TRUE);
}

void
fullscreen(Client *c, const Arg *arg)
{
	if (c->fullscreen)
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	else
		gtk_window_fullscreen(GTK_WINDOW(c->win));
	c->fullscreen = !c->fullscreen;
}

gboolean
permissionrequested(WebKitWebView *v, WebKitPermissionRequest *r, Client *c)
{
	if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(r)) {
		if (allowgeolocation)
			webkit_permission_request_allow(r);
		else
			webkit_permission_request_deny(r);
		return TRUE;
	}

	return FALSE;
}

const char *
getatom(Client *c, int a)
{
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XGetWindowProperty(dpy, c->xid,
			   atoms[a], 0L, BUFSIZ, False, XA_STRING,
			   &adummy, &idummy, &ldummy, &ldummy, &p);
	if (p)
		strncpy(buf, (char *)p, LENGTH(buf)-1);
	else
		buf[0] = '\0';
	XFree(p);

	return buf;
}

char *
geturi(Client *c)
{
	char *uri;

	if (!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

const gchar *
getstyle(const char *uri)
{
	int i;

	if (stylefile != NULL)
		return stylefile;

	for (i = 0; i < LENGTH(styles); i++) {
		if (styles[i].regex && !regexec(&(styles[i].re), uri, 0,
		    NULL, 0))
			return styles[i].style;
	}

	return "";
}

void
setstyle(Client *c, const char *style)
{
	WebKitWebSettings *settings = webkit_web_view_get_settings(c->view);

	g_object_set(G_OBJECT(settings), "user-stylesheet-uri", style, NULL);
}

void
handleplumb(Client *c, WebKitWebView *w, const gchar *uri)
{
	Arg arg;

	webkit_web_view_stop_loading(w);
	arg = (Arg)PLUMB((char *)uri);
	spawn(c, &arg);
}

gboolean
initdownload(WebKitWebView *view, WebKitDownload *o, Client *c)
{
	Arg arg;

	updatewinid(c);
	arg = (Arg)DOWNLOAD((char *)webkit_download_get_uri(o), geturi(c));
	spawn(c, &arg);
	return FALSE;
}

void
inspector(Client *c, const Arg *arg)
{
	if (enableinspector) {
		if (c->isinspecting)
			webkit_web_inspector_close(c->inspector);
		else
			webkit_web_inspector_show(c->inspector);
	}
}

WebKitWebView *
inspector_new(WebKitWebInspector *i, WebKitWebView *v, Client *c)
{
	return WEBKIT_WEB_VIEW(webkit_web_view_new());
}

gboolean
inspector_show(WebKitWebInspector *i, Client *c)
{
	WebKitWebView *w;

	if (c->isinspecting)
		return false;

	w = webkit_web_inspector_get_web_view(i);
	gtk_paned_pack2(GTK_PANED(c->pane), GTK_WIDGET(w), TRUE, TRUE);
	gtk_widget_show(GTK_WIDGET(w));
	c->isinspecting = true;

	return true;
}

gboolean
inspector_close(WebKitWebInspector *i, Client *c)
{
	GtkWidget *w;

	if (!c->isinspecting)
		return false;

	w = GTK_WIDGET(webkit_web_inspector_get_web_view(i));
	gtk_widget_hide(w);
	gtk_widget_destroy(w);
	c->isinspecting = false;

	return true;
}

void
inspector_finished(WebKitWebInspector *i, Client *c)
{
	g_free(c->inspector);
}

gboolean
keypress(GtkAccelGroup *group, GObject *obj, guint key, GdkModifierType mods,
         Client *c)
{
	guint i;
	gboolean processed = FALSE;

	mods = CLEANMASK(mods);
	key = gdk_keyval_to_lower(key);
	updatewinid(c);
	for (i = 0; i < LENGTH(keys); i++) {
		if (key == keys[i].keyval
		    && mods == keys[i].mod
		    && keys[i].func) {
			keys[i].func(c, &(keys[i].arg));
			processed = TRUE;
		}
	}

	return processed;
}

void
mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h, guint modifiers,
    Client *c)
{
	WebKitHitTestResultContext hc;

	/* Keep the hit test to know where is the pointer on the next click */
	c->mousepos = h;

	hc = webkit_hit_test_result_get_context(h);

	if (hc & OnLink)
		c->targeturi = webkit_hit_test_result_get_link_uri(h);
	else if (hc & OnImg)
		c->targeturi = webkit_hit_test_result_get_image_uri(h);
	else if (hc & OnMedia)
		c->targeturi = webkit_hit_test_result_get_media_uri(h);
	else
		c->targeturi = NULL;
	updatetitle(c);
}

void
loadstatuschange(WebKitWebView *view, GParamSpec *pspec, Client *c)
{
	WebKitWebFrame *frame;
	WebKitWebDataSource *src;
	WebKitNetworkRequest *request;
	SoupMessage *msg;
	char *uri;

	switch (webkit_web_view_get_load_status (c->view)) {
	case WEBKIT_LOAD_COMMITTED:
		uri = geturi(c);
		if (strstr(uri, "https://") == uri) {
			frame = webkit_web_view_get_main_frame(c->view);
			src = webkit_web_frame_get_data_source(frame);
			request = webkit_web_data_source_get_request(src);
			msg = webkit_network_request_get_message(request);
			c->sslfailed = !(soup_message_get_flags(msg)
			               & SOUP_MESSAGE_CERTIFICATE_TRUSTED);
		}
		setatom(c, AtomUri, uri);

		if (enablestyle)
			setstyle(c, getstyle(uri));
		break;
	case WEBKIT_LOAD_FINISHED:
		c->progress = 100;
		updatetitle(c);
		break;
	default:
		break;
	}
}

void
loaduri(Client *c, const Arg *arg)
{
	char *u = NULL, *rp;
	const char *uri = (char *)arg->v;
	Arg a = { .b = FALSE };
	struct stat st;

	if (strcmp(uri, "") == 0)
		return;

	/* In case it's a file path. */
	if (stat(uri, &st) == 0) {
		rp = realpath(uri, NULL);
		u = g_strdup_printf("file://%s", rp);
		free(rp);
	} else {
		u = g_strrstr(uri, "://") ? g_strdup(uri)
		    : g_strdup_printf("http://%s", uri);
	}

	setatom(c, AtomUri, uri);

	/* prevents endless loop */
	if (strcmp(u, geturi(c)) == 0) {
		reload(c, &a);
	} else {
		webkit_web_view_load_uri(c->view, u);
		c->progress = 0;
		c->title = copystr(&c->title, u);
		updatetitle(c);
	}
	g_free(u);
}

void
navigate(Client *c, const Arg *arg)
{
	int steps = *(int *)arg;
	webkit_web_view_go_back_or_forward(c->view, steps);
}

Client *
newclient(Client *rc)
{
	Client *c;
	gdouble dpi;

	if (!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->title = NULL;
	c->progress = 100;

	c->next = clients;
	clients = c;

	c->view = newview(c, rc ? rc->view : NULL);

	return c;
}

WebKitWebView *
newview(Client *c, WebKitWebView *rv)
{
	WebKitWebView *v;
	WebKitSettings *settings;
	WebKitUserContentManager *contentmanager;
	WebKitWebContext *context;
	char *ua;

	/* Webview */
	if (rv) {
		v = WEBKIT_WEB_VIEW(
		    webkit_web_view_new_with_related_view(rv));
	} else {
		settings = webkit_settings_new_with_settings(
		    "auto-load-images", loadimages,
		    "default-font-size", defaultfontsize,
		    "enable-caret-browsing", enablecaretbrowsing,
		    "enable-developer-extras", enableinspector,
		    "enable-dns-prefetching", enablednsprefetching,
		    "enable-frame-flattening", enableframeflattening,
		    "enable-html5-database", enablecache,
		    "enable-html5-local-storage", enablecache,
		    "enable-javascript", enablescripts,
		    "enable-plugins", enableplugins,
		    NULL);
		if (!(ua = getenv("SURF_USERAGENT")))
			ua = useragent;
		webkit_settings_set_user_agent(settings, ua);
		/* Have a look at http://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html
		 * for more interesting settings */

		contentmanager = webkit_user_content_manager_new();

		context = webkit_web_context_new_with_website_data_manager(
		    webkit_website_data_manager_new(
		    "base-cache-directory", cachedir,
		    "base-data-directory", cachedir,
		    NULL));

		/* rendering process model, can be a shared unique one or one for each
		 * view */
		webkit_web_context_set_process_model(context,
		    WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
		/* ssl */
		webkit_web_context_set_tls_errors_policy(context, strictssl ?
		    WEBKIT_TLS_ERRORS_POLICY_FAIL : WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		/* disk cache */
		webkit_web_context_set_cache_model(context, enablecache ?
		    WEBKIT_CACHE_MODEL_WEB_BROWSER : WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

		/* Currently only works with text file to be compatible with curl */
		webkit_cookie_manager_set_persistent_storage(
		    webkit_web_context_get_cookie_manager(context), cookiefile,
		    WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
		/* cookie policy */
		webkit_cookie_manager_set_accept_policy(
		    webkit_web_context_get_cookie_manager(context),
		    cookiepolicy_get());

		v = g_object_new(WEBKIT_TYPE_WEB_VIEW,
		    "settings", settings,
		    "user-content-manager", contentmanager,
		    "web-context", context,
		    NULL);
	}

	g_signal_connect(G_OBJECT(v),
	                 "notify::title",
			 G_CALLBACK(titlechanged), c);
	g_signal_connect(G_OBJECT(v),
	                 "mouse-target-changed",
			 G_CALLBACK(mousetargetchanged), c);
	g_signal_connect(G_OBJECT(v),
	                 "permission-request",
			 G_CALLBACK(permissionrequested), c);
	g_signal_connect(G_OBJECT(v),
	                 "create",
			 G_CALLBACK(createview), c);
	g_signal_connect(G_OBJECT(v), "ready-to-show",
			 G_CALLBACK(showview), c);
	g_signal_connect(G_OBJECT(v),
	                 "decide-policy",
			 G_CALLBACK(decidepolicy), c);
	g_signal_connect(G_OBJECT(v),
	                 "window-object-cleared",
			 G_CALLBACK(windowobjectcleared), c);
	g_signal_connect(G_OBJECT(v),
	                 "notify::load-status",
			 G_CALLBACK(loadstatuschange), c);
	g_signal_connect(G_OBJECT(v),
	                 "notify::progress",
			 G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(v),
	                 "download-requested",
			 G_CALLBACK(initdownload), c);
	g_signal_connect(G_OBJECT(v),
	                 "button-release-event",
			 G_CALLBACK(buttonrelease), c);
	g_signal_connect(G_OBJECT(v),
	                 "context-menu",
			 G_CALLBACK(contextmenu), c);
	g_signal_connect(G_OBJECT(v),
	                 "resource-request-starting",
			 G_CALLBACK(beforerequest), c);
	g_signal_connect(G_OBJECT(v),
	                 "should-show-delete-interface-for-element",
			 G_CALLBACK(deletion_interface), c);

	return v;
}

void
showview(WebKitWebView *v, Client *c)
{
	GdkGeometry hints = { 1, 1 };
	GdkRGBA bgcolor = { 0 };
	GdkWindow *gwin;

	/* Window */
	if (embed) {
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
	g_signal_connect(G_OBJECT(c->win),
	                 "leave_notify_event",
			 G_CALLBACK(titlechangeleave), c);

	if (!kioskmode)
		addaccelgroup(c);

	/* Arranging */
	gtk_container_add(GTK_CONTAINER(c->win), GTK_WIDGET(c->view));

	/* Setup */
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gwin = gtk_widget_get_window(GTK_WIDGET(c->win));
	c->xid = gdk_x11_window_get_xid(gwin);
	gtk_window_set_geometry_hints(GTK_WINDOW(c->win), NULL, &hints,
				      GDK_HINT_MIN_SIZE);
	gdk_window_set_events(gwin, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(gwin, processx, c);

	runscript(frame);

	/* This might conflict with _zoomto96dpi_. */
	if (zoomlevel != 1.0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel);

	if (runinfullscreen)
		fullscreen(c, NULL);

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");
	if (hidebackground)
		webkit_web_view_set_background_color(c->view, &bgcolor);

	if (showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		printf("%lu\n", c->xid);
		fflush(NULL);
                if (fclose(stdout) != 0) {
			die("Error closing stdout");
                }
	}
}

void
newwindow(Client *c, const Arg *arg, gboolean noembed)
{
	guint i = 0;
	const char *cmd[18], *uri;
	const Arg a = { .v = (void *)cmd };
	char tmp[64];

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = cookiepolicies;
	if (!enablescrollbars)
		cmd[i++] = "-b";
	if (embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%u", (int)embed);
		cmd[i++] = tmp;
	}
	if (!allowgeolocation)
		cmd[i++] = "-g";
	if (!loadimages)
		cmd[i++] = "-i";
	if (kioskmode)
		cmd[i++] = "-k";
	if (!enableplugins)
		cmd[i++] = "-p";
	if (!enablescripts)
		cmd[i++] = "-s";
	if (showxid)
		cmd[i++] = "-x";
	if (enablediskcache)
		cmd[i++] = "-D";
	cmd[i++] = "-c";
	cmd[i++] = cookiefile;
	cmd[i++] = "--";
	uri = arg->v ? (char *)arg->v : c->linkhover;
	if (uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

gboolean
contextmenu(WebKitWebView *view, GtkWidget *menu, WebKitHitTestResult *target,
            gboolean keyboard, Client *c)
{
	GList *items = gtk_container_get_children(GTK_CONTAINER(GTK_MENU(menu)));

	for (GList *l = items; l; l = l->next)
		g_signal_connect(l->data, "activate", G_CALLBACK(menuactivate), c);

	g_list_free(items);
	return FALSE;
}

void
menuactivate(GtkMenuItem *item, Client *c)
{
	/*
	 * context-menu-action-2000 open link
	 * context-menu-action-1    open link in window
	 * context-menu-action-2    download linked file
	 * context-menu-action-3    copy link location
	 * context-menu-action-7    copy image address
	 * context-menu-action-13   reload
	 * context-menu-action-10   back
	 * context-menu-action-11   forward
	 * context-menu-action-12   stop
	 */

	const gchar *name, *uri;
	GtkClipboard *prisel, *clpbrd;

	name = gtk_actionable_get_action_name(GTK_ACTIONABLE(item));
	if (name == NULL)
		return;

	if (!g_strcmp0(name, "context-menu-action-3")) {
		prisel = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(prisel, c->linkhover, -1);
	} else if (!g_strcmp0(name, "context-menu-action-7")) {
		prisel = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		clpbrd = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		uri = gtk_clipboard_wait_for_text(clpbrd);
		if (uri)
			gtk_clipboard_set_text(prisel, uri, -1);
	}
}

void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d)
{
	Arg arg = {.v = text };
	if (text != NULL)
		loaduri((Client *) d, &arg);
}

void
print(Client *c, const Arg *arg)
{
	webkit_web_frame_print(webkit_web_view_get_main_frame(c->view));
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d)
{
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Arg arg;

	if (((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if (ev->state == PropertyNewValue) {
			if (ev->atom == atoms[AtomFind]) {
				arg.b = TRUE;
				find(c, &arg);

				return GDK_FILTER_REMOVE;
			} else if (ev->atom == atoms[AtomGo]) {
				arg.v = getatom(c, AtomGo);
				loaduri(c, &arg);

				return GDK_FILTER_REMOVE;
			}
		}
	}
	return GDK_FILTER_CONTINUE;
}

void
progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c)
{
	c->progress = webkit_web_view_get_progress(c->view) * 100;
	updatetitle(c);
}

void
linkopen(Client *c, const Arg *arg)
{
	newwindow(NULL, arg, 1);
}

void
linkopenembed(Client *c, const Arg *arg)
{
	newwindow(NULL, arg, 0);
}

void
reload(Client *c, const Arg *arg)
{
	gboolean nocache = *(gboolean *)arg;
	if (nocache)
		webkit_web_view_reload_bypass_cache(c->view);
	else
		webkit_web_view_reload(c->view);
}

void
scroll_h(Client *c, const Arg *arg)
{
	scroll(gtk_scrolled_window_get_hadjustment(
	       GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

void
scroll_v(Client *c, const Arg *arg)
{
	scroll(gtk_scrolled_window_get_vadjustment(
	       GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

void
scroll(GtkAdjustment *a, const Arg *arg)
{
	gdouble v;

	v = gtk_adjustment_get_value(a);
	switch (arg->i) {
	case +10000:
	case -10000:
		v += gtk_adjustment_get_page_increment(a) * (arg->i / 10000);
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
setatom(Client *c, int a, const char *v)
{
	XSync(dpy, False);
	XChangeProperty(dpy, c->xid,
			atoms[a], XA_STRING, 8, PropModeReplace,
			(unsigned char *)v, strlen(v) + 1);
}

void
setup(void)
{
	int i;
	char *styledirfile, *stylepath;
	WebKitWebContext *context;
	GError *error = NULL;

	/* clean up any zombies immediately */
	sigchld(0);
	gtk_init(NULL, NULL);

	dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

	/* atoms */
	atoms[AtomFind] = XInternAtom(dpy, "_SURF_FIND", False);
	atoms[AtomGo] = XInternAtom(dpy, "_SURF_GO", False);
	atoms[AtomUri] = XInternAtom(dpy, "_SURF_URI", False);

	/* dirs and files */
	cookiefile = buildfile(cookiefile);
	scriptfile = buildfile(scriptfile);
	cachedir   = buildpath(cachedir);
	if (stylefile == NULL) {
		styledir = buildpath(styledir);
		for (i = 0; i < LENGTH(styles); i++) {
			if (regcomp(&(styles[i].re), styles[i].regex,
			    REG_EXTENDED)) {
				fprintf(stderr,
				        "Could not compile regex: %s\n",
				        styles[i].regex);
				styles[i].regex = NULL;
			}
			styledirfile    = g_strconcat(styledir, "/",
			                              styles[i].style, NULL);
			stylepath       = buildfile(styledirfile);
			styles[i].style = g_strconcat("file://", stylepath,
			                              NULL);
			g_free(styledirfile);
			g_free(stylepath);
		}
		g_free(styledir);
	} else {
		stylepath = buildfile(stylefile);
		stylefile = g_strconcat("file://", stylepath, NULL);
		g_free(stylepath);
	}
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

void
spawn(Client *c, const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "surf: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

void
eval(Client *c, const Arg *arg)
{
	WebKitWebFrame *frame = webkit_web_view_get_main_frame(c->view);
	evalscript(webkit_web_frame_get_global_context(frame),
	           ((char **)arg->v)[0], "");
}

void
stop(Client *c, const Arg *arg)
{
	webkit_web_view_stop_loading(c->view);
}

void
titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c)
{
	c->title = webkit_web_view_get_title(c->view);
	updatetitle(c);
}

void
titlechangeleave(void *a, void *b, Client *c)
{
	c->linkhover = NULL;
	updatetitle(c);
}

void
toggle(Client *c, const Arg *arg)
{
	WebKitWebSettings *settings;
	char *name = (char *)arg->v;
	gboolean value;
	Arg a = { .b = FALSE };

	settings = webkit_web_view_get_settings(c->view);
	g_object_get(G_OBJECT(settings), name, &value, NULL);
	g_object_set(G_OBJECT(settings), name, !value, NULL);

	reload(c, &a);
}

void
togglecookiepolicy(Client *c, const Arg *arg)
{
	++cookiepolicy;
	cookiepolicy %= strlen(cookiepolicies);

	webkit_cookie_manager_set_accept_policy(
	    webkit_web_context_get_cookie_manager(
	    webkit_web_view_get_context(c->view)),
	    cookiepolicy_get());

	updatetitle(c);
	/* Do not reload. */
}

void
togglegeolocation(Client *c, const Arg *arg)
{
	Arg a = { .b = FALSE };

	allowgeolocation ^= 1;
	reload(c, &a);
}

void
twitch(Client *c, const Arg *arg)
{
	GtkAdjustment *a;
	gdouble v;

	a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(
	                                        c->scroll));

	v = gtk_adjustment_get_value(a);

	v += arg->i;

	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) -
	        gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

void
togglescrollbars(Client *c, const Arg *arg)
{
	GtkPolicyType vspolicy;
	Arg a;

	gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(c->scroll), NULL,
	                               &vspolicy);

	if (vspolicy == GTK_POLICY_AUTOMATIC) {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
		                               GTK_POLICY_NEVER,
					       GTK_POLICY_NEVER);
	} else {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
		                               GTK_POLICY_AUTOMATIC,
					       GTK_POLICY_AUTOMATIC);
		a.i = +1;
		twitch(c, &a);
		a.i = -1;
		twitch(c, &a);
	}
}

void
togglestyle(Client *c, const Arg *arg)
{
	enablestyle = !enablestyle;
	setstyle(c, enablestyle ? getstyle(geturi(c)) : "");

	updatetitle(c);
}

void
gettogglestat(Client *c)
{
	gboolean value;
	int p = 0;
	WebKitWebSettings *settings = webkit_web_view_get_settings(c->view);

	togglestat[p++] = cookiepolicy_set(cookiepolicy_get());

	g_object_get(G_OBJECT(settings), "enable-caret-browsing", &value,
	             NULL);
	togglestat[p++] = value? 'C': 'c';

	togglestat[p++] = allowgeolocation? 'G': 'g';

	togglestat[p++] = enablediskcache? 'D': 'd';

	g_object_get(G_OBJECT(settings), "auto-load-images", &value, NULL);
	togglestat[p++] = value? 'I': 'i';

	g_object_get(G_OBJECT(settings), "enable-scripts", &value, NULL);
	togglestat[p++] = value? 'S': 's';

	g_object_get(G_OBJECT(settings), "enable-plugins", &value, NULL);
	togglestat[p++] = value? 'V': 'v';

	togglestat[p++] = enablestyle ? 'M': 'm';

	togglestat[p] = '\0';
}

void
getpagestat(Client *c)
{
	const char *uri = geturi(c);

	if (strstr(uri, "https://") == uri)
		pagestat[0] = c->sslfailed ? 'U' : 'T';
	else
		pagestat[0] = '-';

	pagestat[1] = '\0';
}

void
updatetitle(Client *c)
{
	char *t;

	if (showindicators) {
		gettogglestat(c);
		getpagestat(c);

		if (c->linkhover) {
			t = g_strdup_printf("%s:%s | %s", togglestat, pagestat,
			                    c->linkhover);
		} else if (c->progress != 100) {
			t = g_strdup_printf("[%i%%] %s:%s | %s", c->progress,
			                    togglestat, pagestat,
			                    c->title == NULL ? "" : c->title);
		} else {
			t = g_strdup_printf("%s:%s | %s", togglestat, pagestat,
			                    c->title == NULL ? "" : c->title);
		}

		gtk_window_set_title(GTK_WINDOW(c->win), t);
		g_free(t);
	} else {
		gtk_window_set_title(GTK_WINDOW(c->win), (c->title == NULL) ?
		                     "" : c->title);
	}
}

void
updatewinid(Client *c)
{
	snprintf(winid, LENGTH(winid), "%lu", c->xid);
}

void
usage(void)
{
	die("usage: %s [-bBdDfFgGiIkKmMnNpPsSvx] [-a cookiepolicies ] "
	    "[-c cookiefile] [-e xid] [-r scriptfile] [-t stylefile] "
	    "[-u useragent] [-z zoomlevel] [uri]\n", basename(argv0));
}

void
windowobjectcleared(GtkWidget *w, WebKitWebFrame *frame, JSContextRef js,
                    JSObjectRef win, Client *c)
{
	runscript(frame);
}

void
zoom(Client *c, const Arg *arg)
{
	c->zoomed = TRUE;
	if (arg->i < 0) {
		/* zoom out */
		webkit_web_view_zoom_out(c->view);
	} else if (arg->i > 0) {
		/* zoom in */
		webkit_web_view_zoom_in(c->view);
	} else {
		/* reset */
		c->zoomed = FALSE;
		webkit_web_view_set_zoom_level(c->view, 1.0);
	}
}

int
main(int argc, char *argv[])
{
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
	case 'd':
		enablediskcache = 0;
		break;
	case 'D':
		enablediskcache = 1;
		break;
	case 'e':
		embed = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'f':
		runinfullscreen = 0;
		break;
	case 'F':
		runinfullscreen = 1;
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
	case 'm':
		enablestyle = 0;
		break;
	case 'M':
		enablestyle = 1;
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
		die("surf-"VERSION", ©2009-2015 surf engineers, "
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
	if (argc > 0)
		arg.v = argv[0];

	setup();
	c = newclient(NULL);
	showview(NULL, c);
	if (arg.v)
		loaduri(clients, &arg);
	else
		updatetitle(c);

	gtk_main();
	cleanup();

	return EXIT_SUCCESS;
}

