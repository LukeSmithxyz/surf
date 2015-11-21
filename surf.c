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

#define LENGTH(x)               (sizeof(x) / sizeof(x[0]))
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK))

enum { AtomFind, AtomGo, AtomUri, AtomLast };

enum {
	CaretBrowsing,
	FrameFlattening,
	Geolocation,
	JavaScript,
	LoadImages,
	Plugins,
	ScrollBars,
};

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

typedef union {
	gboolean b;
	gint i;
	const void *v;
} Arg;

typedef struct Client {
	GtkWidget *win;
	WebKitWebView *view;
	WebKitWebInspector *inspector;
	WebKitFindController *finder;
	WebKitHitTestResult *mousepos;
	GTlsCertificateFlags tlsflags;
	Window xid;
	gint progress;
	gboolean fullscreen;
	const char *title, *targeturi;
	const char *needle;
	struct Client *next;
} Client;

typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *a);
	const Arg arg;
} Key;

typedef struct {
	unsigned int target;
	unsigned int mask;
	guint button;
	void (*func)(Client *c, const Arg *a, WebKitHitTestResult *h);
	const Arg arg;
	unsigned int stopevent;
} Button;

typedef struct {
	char *regex;
	char *style;
	regex_t re;
} SiteStyle;

/* Surf */
static void usage(void);
static void die(const char *errstr, ...);
static void setup(void);
static void sigchld(int unused);
static char *buildfile(const char *path);
static char *buildpath(const char *path);
static Client *newclient(Client *c);
static void addaccelgroup(Client *c);
static void loaduri(Client *c, const Arg *a);
static char *geturi(Client *c);
static void setatom(Client *c, int a, const char *v);
static const char *getatom(Client *c, int a);
static void updatetitle(Client *c);
static void gettogglestats(Client *c);
static void getpagestats(Client *c);
static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p);
static const gchar *getstyle(const char *uri);
static void setstyle(Client *c, const char *stylefile);
static void runscript(Client *c);
static void evalscript(Client *c, const char *jsstr, ...);
static void updatewinid(Client *c);
static void handleplumb(Client *c, const gchar *uri);
static void newwindow(Client *c, const Arg *arg, gboolean noembed);
static void spawn(Client *c, const Arg *a);
static void destroyclient(Client *c);
static void cleanup(void);

/* GTK/WebKit */
static WebKitWebView *newview(Client *c, WebKitWebView *rv);
static GtkWidget *createview(WebKitWebView *v, WebKitNavigationAction *a,
		Client *c);
static gboolean buttonreleased(GtkWidget *w, GdkEventKey *e, Client *c);
static gboolean keypress(GtkAccelGroup *group, GObject *obj, guint key,
                         GdkModifierType mods, Client *c);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
                                gpointer d);
static gboolean winevent(GtkWidget *w, GdkEvent *e, Client *c);
static void showview(WebKitWebView *v, Client *c);
static GtkWidget *createwindow(Client *c);
static void loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c);
static void progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c);
static void titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c);
static void mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h,
		guint modifiers, Client *c);
static gboolean permissionrequested(WebKitWebView *v,
		WebKitPermissionRequest *r, Client *c);
static gboolean decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
    WebKitPolicyDecisionType dt, Client *c);
static void decidenavigation(WebKitPolicyDecision *d, Client *c);
static void decidenewwindow(WebKitPolicyDecision *d, Client *c);
static void decideresource(WebKitPolicyDecision *d, Client *c);
static void downloadstarted(WebKitWebContext *wc, WebKitDownload *d,
		Client *c);
static void responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c);
static void download(Client *c, WebKitURIResponse *r);
static void closeview(WebKitWebView *v, Client *c);
static void destroywin(GtkWidget* w, Client *c);

/* Hotkeys */
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static void reload(Client *c, const Arg *arg);
static void print(Client *c, const Arg *a);
static void clipboard(Client *c, const Arg *a);
static void zoom(Client *c, const Arg *a);
static void scroll_v(Client *c, const Arg *a);
static void scroll_h(Client *c, const Arg *a);
static void navigate(Client *c, const Arg *a);
static void stop(Client *c, const Arg *arg);
static void toggle(Client *c, const Arg *a);
static void togglefullscreen(Client *c, const Arg *a);
static void togglecookiepolicy(Client *c, const Arg *arg);
static void togglestyle(Client *c, const Arg *arg);
static void toggleinspector(Client *c, const Arg *a);
static void find(Client *c, const Arg *a);

/* Buttons */
static void clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h);

static char winid[64];
static char togglestats[10];
static char pagestats[2];
static Atom atoms[AtomLast];
static Window embed;
static gboolean showxid = FALSE;
static int cookiepolicy;
static Display *dpy;
static Client *clients;
static char *stylefile;
static const char *useragent;
char *argv0;

/* configuration, allows nested code to access above variables */
#include "config.h"

void
usage(void)
{
	die("usage: %s [-bBdDfFgGiIkKmMnNpPsSvx] [-a cookiepolicies ] "
	    "[-c cookiefile] [-e xid] [-r scriptfile] [-t stylefile] "
	    "[-u useragent] [-z zoomlevel] [uri]\n", basename(argv0));
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
setup(void)
{
	int i;

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
			styles[i].style = g_strconcat(styledir, "/",
			    styles[i].style, NULL);
		}
		g_free(styledir);
	} else {
		stylefile = buildfile(stylefile);
	}
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while (0 < waitpid(-1, NULL, WNOHANG));
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

Client *
newclient(Client *rc)
{
	Client *c;

	if (!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->title = NULL;
	c->progress = 100;

	c->next = clients;
	clients = c;

	c->view = newview(c, rc ? rc->view : NULL);
	c->tlsflags = G_TLS_CERTIFICATE_VALIDATE_ALL + 1;

	return c;
}

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
loaduri(Client *c, const Arg *a)
{
	struct stat st;
	char *url, *path;
	const char *uri = (char *)a->v;

	if (g_strcmp0(uri, "") == 0)
		return;

	if (g_strrstr(uri, "://") || g_str_has_prefix(uri, "about:")) {
		url = g_strdup(uri);
	} else if (!stat(uri, &st) && (path = realpath(uri, NULL))) {
		url = g_strdup_printf("file://%s", path);
		free(path);
	} else {
		url = g_strdup_printf("http://%s", uri);
	}

	setatom(c, AtomUri, url);

	if (strcmp(url, geturi(c)) == 0) {
		reload(c, a);
	} else {
		webkit_web_view_load_uri(c->view, url);
		c->title = geturi(c);
		updatetitle(c);
	}

	g_free(url);
}

char *
geturi(Client *c)
{
	char *uri;

	if (!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

void
setatom(Client *c, int a, const char *v)
{
	XSync(dpy, False);
	XChangeProperty(dpy, c->xid,
			atoms[a], XA_STRING, 8, PropModeReplace,
			(unsigned char *)v, strlen(v) + 1);
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

void
updatetitle(Client *c)
{
	char *title;

	if (showindicators) {
		gettogglestats(c);
		getpagestats(c);

		if (c->progress != 100) {
			title = g_strdup_printf("[%i%%] %s:%s | %s",
			    c->progress, togglestats, pagestats,
			    c->targeturi ? c->targeturi : c->title);
		} else {
			title = g_strdup_printf("%s:%s | %s",
			    togglestats, pagestats,
			    c->targeturi ? c->targeturi : c->title);
		}

		gtk_window_set_title(GTK_WINDOW(c->win), title);
		g_free(title);
	} else {
		gtk_window_set_title(GTK_WINDOW(c->win), c->title ?
		    c->title : "");
	}
}

void
gettogglestats(Client *c)
{
	togglestats[0] = cookiepolicy_set(cookiepolicy_get());
	togglestats[1] = enablecaretbrowsing ? 'C' : 'c';
	togglestats[2] = allowgeolocation ? 'G' : 'g';
	togglestats[3] = enablecache ? 'D' : 'd';
	togglestats[4] = loadimages ? 'I' : 'i';
	togglestats[5] = enablescripts ? 'S': 's';
	togglestats[6] = enableplugins ? 'V' : 'v';
	togglestats[7] = enablestyle ? 'M' : 'm';
	togglestats[8] = enableframeflattening ? 'F' : 'f';
	togglestats[9] = '\0';
}

void
getpagestats(Client *c)
{
	pagestats[0] = c->tlsflags > G_TLS_CERTIFICATE_VALIDATE_ALL ? '-' :
	    c->tlsflags > 0 ? 'U' : 'T';
	pagestats[1] = '\0';
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
setstyle(Client *c, const char *stylefile)
{
	gchar *style;

	if (!g_file_get_contents(stylefile, &style, NULL, NULL)) {
		fprintf(stderr, "Could not read style file: %s\n", stylefile);
		return;
	}

	webkit_user_content_manager_add_style_sheet(
	    webkit_web_view_get_user_content_manager(c->view),
	    webkit_user_style_sheet_new(style,
	    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
	    WEBKIT_USER_STYLE_LEVEL_USER,
	    NULL, NULL));

	g_free(style);
}

void
runscript(Client *c)
{
	gchar *script;
	gsize l;

	if (g_file_get_contents(scriptfile, &script, &l, NULL) && l)
		evalscript(c, script);
	g_free(script);
}

void
evalscript(Client *c, const char *jsstr, ...)
{
	va_list ap;
	gchar *script;

	va_start(ap, jsstr);
	script = g_strdup_vprintf(jsstr, ap);
	va_end(ap);

	webkit_web_view_run_javascript(c->view, script, NULL, NULL, NULL);
	g_free(script);
}

void
updatewinid(Client *c)
{
	snprintf(winid, LENGTH(winid), "%lu", c->xid);
}

void
handleplumb(Client *c, const gchar *uri)
{
	Arg arg;

	arg = (Arg)PLUMB(uri);
	spawn(c, &arg);
}

void
newwindow(Client *c, const Arg *a, int noembed)
{
	int i = 0;
	char tmp[64];
	const char *cmd[26], *uri;
	const Arg arg = { .v = cmd };

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = cookiepolicies;
	cmd[i++] = enablescrollbars ? "-B" : "-b";
	if (cookiefile && g_strcmp0(cookiefile, "")) {
		cmd[i++] = "-c";
		cmd[i++] = cookiefile;
	}
	cmd[i++] = enablecache ? "-D" : "-d";
	if (embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%lu", embed);
		cmd[i++] = tmp;
	}
	cmd[i++] = runinfullscreen ? "-F" : "-f";
	cmd[i++] = allowgeolocation ? "-G" : "-g";
	cmd[i++] = loadimages ? "-I" : "-i";
	cmd[i++] = kioskmode ? "-K" : "-k";
	cmd[i++] = enablestyle ? "-M" : "-m";
	cmd[i++] = enableinspector ? "-N" : "-n";
	cmd[i++] = enableplugins ? "-P" : "-p";
	if (scriptfile && g_strcmp0(scriptfile, "")) {
		cmd[i++] = "-r";
		cmd[i++] = scriptfile;
	}
	cmd[i++] = enablescripts ? "-S" : "-s";
	if (stylefile && g_strcmp0(stylefile, "")) {
		cmd[i++] = "-t";
		cmd[i++] = stylefile;
	}
	if (fulluseragent && g_strcmp0(fulluseragent, "")) {
		cmd[i++] = "-u";
		cmd[i++] = fulluseragent;
	}
	if (showxid)
		cmd[i++] = "-x";
	/* do not keep zoom level */
	cmd[i++] = "--";
	if ((uri = a->v))
		cmd[i++] = uri;
	cmd[i] = NULL;

	spawn(c, &arg);
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
destroyclient(Client *c)
{
	Client *p;

	webkit_web_view_stop_loading(c->view);
	/* Not needed, has already been called
	gtk_widget_destroy(c->win);
	 */

	for (p = clients; p && p->next != c; p = p->next)
		;
	if (p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
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

WebKitWebView *
newview(Client *c, WebKitWebView *rv)
{
	WebKitWebView *v;
	WebKitSettings *settings;
	WebKitUserContentManager *contentmanager;
	WebKitWebContext *context;

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
		/* Have a look at http://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html
		 * for more interesting settings */

		if (strcmp(fulluseragent, "")) {
			webkit_settings_set_user_agent(settings, fulluseragent);
		} else if (surfuseragent) {
			webkit_settings_set_user_agent_with_application_details(
			    settings, "Surf", VERSION);
		}
		useragent = webkit_settings_get_user_agent(settings);

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

		g_signal_connect(G_OBJECT(context), "download-started",
		    G_CALLBACK(downloadstarted), c);

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
	                 "load-changed",
			 G_CALLBACK(loadchanged), c);
	g_signal_connect(G_OBJECT(v),
	                 "notify::estimated-load-progress",
			 G_CALLBACK(progresschanged), c);
	g_signal_connect(G_OBJECT(v),
	                 "button-release-event",
			 G_CALLBACK(buttonreleased), c);
	g_signal_connect(G_OBJECT(v), "close",
			G_CALLBACK(closeview), c);

	return v;
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
buttonreleased(GtkWidget *w, GdkEventKey *e, Client *c)
{
	WebKitHitTestResultContext element;
	GdkEventButton *eb = (GdkEventButton*)e;
	int i;

	element = webkit_hit_test_result_get_context(c->mousepos);

	for (i = 0; i < LENGTH(buttons); ++i) {
		if (element & buttons[i].target &&
		    eb->button == buttons[i].button &&
		    CLEANMASK(eb->state) == CLEANMASK(buttons[i].mask) &&
		    buttons[i].func) {
			buttons[i].func(c, &buttons[i].arg, c->mousepos);
			return buttons[i].stopevent;
		}
	}

	return FALSE;
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
				find(c, NULL);

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

gboolean
winevent(GtkWidget *w, GdkEvent *e, Client *c)
{
	switch (e->type) {
	case GDK_LEAVE_NOTIFY:
		c->targeturi = NULL;
		updatetitle(c);
		break;
	case GDK_WINDOW_STATE: /* fallthrough */
		if (e->window_state.changed_mask ==
		    GDK_WINDOW_STATE_FULLSCREEN) {
			c->fullscreen = e->window_state.new_window_state &
			    GDK_WINDOW_STATE_FULLSCREEN;
			break;
		}
	default:
		return FALSE;
	}

	return TRUE;
}

void
showview(WebKitWebView *v, Client *c)
{
	GdkGeometry hints = { 1, 1 };
	GdkRGBA bgcolor = { 0 };
	GdkWindow *gwin;

	c->win = createwindow(c);

	if (enableinspector)
		c->inspector = webkit_web_view_get_inspector(c->view);

	c->finder = webkit_web_view_get_find_controller(c->view);

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

	if (zoomlevel != 1.0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel);

	if (runinfullscreen)
		togglefullscreen(c, NULL);

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

GtkWidget *
createwindow(Client *c)
{
	GtkWidget *w;

	if (embed) {
		w = gtk_plug_new(embed);
	} else {
		w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

		/* TA:  20091214:  Despite what the GNOME docs say, the ICCCM
		 * is always correct, so we should still call this function.
		 * But when doing so, we *must* differentiate between a
		 * WM_CLASS and a resource on the window.  By convention, the
		 * window class (WM_CLASS) is capped, while the resource is in
		 * lowercase.   Both these values come as a pair.
		 */
		gtk_window_set_wmclass(GTK_WINDOW(w), "surf", "Surf");

		/* TA:  20091214:  And set the role here as well -- so that
		 * sessions can pick this up.
		 */
		gtk_window_set_role(GTK_WINDOW(w), "Surf");

		gtk_window_set_default_size(GTK_WINDOW(w), 800, 600);
	}

	g_signal_connect(G_OBJECT(w), "destroy",
	    G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(w), "leave-notify-event",
	    G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "window-state-event",
	    G_CALLBACK(winevent), c);

	return w;
}

void
loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c)
{
	switch (e) {
	case WEBKIT_LOAD_STARTED:
		c->tlsflags = G_TLS_CERTIFICATE_VALIDATE_ALL + 1;
		break;
	case WEBKIT_LOAD_REDIRECTED:
		setatom(c, AtomUri, geturi(c));
		break;
	case WEBKIT_LOAD_COMMITTED:
		if (!webkit_web_view_get_tls_info(c->view, NULL, &(c->tlsflags)))
			c->tlsflags = G_TLS_CERTIFICATE_VALIDATE_ALL + 1;

		setatom(c, AtomUri, geturi(c));

		if (enablestyle)
			setstyle(c, getstyle(geturi(c)));
		break;
	case WEBKIT_LOAD_FINISHED:
		/* Disabled until we write some WebKitWebExtension for
		 * manipulating the DOM directly.
		evalscript(c, "document.documentElement.style.overflow = '%s'",
		    enablescrollbars ? "auto" : "hidden");
		*/
		runscript(c);
		break;
	}
	updatetitle(c);
}

void
progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c)
{
	c->progress = webkit_web_view_get_estimated_load_progress(c->view) *
	    100;
	updatetitle(c);
}

void
titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c)
{
	c->title = webkit_web_view_get_title(c->view);
	updatetitle(c);
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
	const gchar *uri;
	int i, isascii = 1;
	WebKitResponsePolicyDecision *r = WEBKIT_RESPONSE_POLICY_DECISION(d);
	WebKitURIResponse *res;

	res = webkit_response_policy_decision_get_response(r);
	uri = webkit_uri_response_get_uri(res);

	if (g_str_has_suffix(uri, "/favicon.ico"))
		webkit_uri_request_set_uri(
		    webkit_response_policy_decision_get_request(r),
		    "about:blank");

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
		if (isascii) {
			handleplumb(c, uri);
			webkit_policy_decision_ignore(d);
		}
	}

	if (webkit_response_policy_decision_is_mime_type_supported(r)) {
		webkit_policy_decision_use(d);
	} else {
		webkit_policy_decision_ignore(d);
		download(c, res);
	}
}

void
downloadstarted(WebKitWebContext *wc, WebKitDownload *d, Client *c)
{
	g_signal_connect(G_OBJECT(d), "notify::response",
	    G_CALLBACK(responsereceived), c);
}

void
responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c)
{
	download(c, webkit_download_get_response(d));
	webkit_download_cancel(d);
}

void
download(Client *c, WebKitURIResponse *r)
{
	Arg a;

	a = (Arg)DOWNLOAD(webkit_uri_response_get_uri(r), geturi(c));
	spawn(c, &a);
}

void
closeview(WebKitWebView *v, Client *c)
{
	gtk_widget_destroy(c->win);
}

void
destroywin(GtkWidget* w, Client *c)
{
	destroyclient(c);
	if (clients == NULL)
		gtk_main_quit();
}

void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d)
{
	Arg arg = {.v = text };
	if (text != NULL)
		loaduri((Client *) d, &arg);
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
print(Client *c, const Arg *a)
{
	webkit_print_operation_run_dialog(webkit_print_operation_new(c->view),
	    GTK_WINDOW(c->win));
}

void
clipboard(Client *c, const Arg *a)
{
	if (a->b) { /* load clipboard uri */
		gtk_clipboard_request_text(gtk_clipboard_get(
		                           GDK_SELECTION_PRIMARY),
		                           pasteuri, c);
	} else { /* copy uri */
		gtk_clipboard_set_text(gtk_clipboard_get(
		                       GDK_SELECTION_PRIMARY), c->targeturi
		                       ? c->targeturi : geturi(c), -1);
	}
}

void
zoom(Client *c, const Arg *a)
{
	if (a->i > 0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel + 0.1);
	else if (a->i < 0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel - 0.1);
	else
		webkit_web_view_set_zoom_level(c->view, 1.0);

	zoomlevel = webkit_web_view_get_zoom_level(c->view);
}

void
scroll_v(Client *c, const Arg *a)
{
	evalscript(c, "window.scrollBy(0, %d * (window.innerHeight / 100))",
	    a->i);
}

void
scroll_h(Client *c, const Arg *a)
{
	evalscript(c, "window.scrollBy(%d * (window.innerWidth / 100), 0)",
	    a->i);
}

void
navigate(Client *c, const Arg *a)
{
	if (a->i < 0)
		webkit_web_view_go_back(c->view);
	else if (a->i > 0)
		webkit_web_view_go_forward(c->view);
}

void
stop(Client *c, const Arg *arg)
{
	webkit_web_view_stop_loading(c->view);
}

void
toggle(Client *c, const Arg *a)
{
	WebKitSettings *s;

	s = webkit_web_view_get_settings(c->view);

	switch ((unsigned int)a->i) {
	case CaretBrowsing:
		enablecaretbrowsing = !enablecaretbrowsing;
		webkit_settings_set_enable_caret_browsing(s,
		    enablecaretbrowsing);
		updatetitle(c);
		return; /* do not reload */
		break;
	case FrameFlattening:
		enableframeflattening = !enableframeflattening;
		webkit_settings_set_enable_frame_flattening(s,
		    enableframeflattening);
		break;
	case Geolocation:
		allowgeolocation = !allowgeolocation;
		break;
	case JavaScript:
		enablescripts = !enablescripts;
		webkit_settings_set_enable_javascript(s, enablescripts);
		break;
	case LoadImages:
		loadimages = !loadimages;
		webkit_settings_set_auto_load_images(s, loadimages);
		break;
	case Plugins:
		enableplugins = !enableplugins;
		webkit_settings_set_enable_plugins(s, enableplugins);
		break;
	case ScrollBars:
		/* Disabled until we write some WebKitWebExtension for
		 * manipulating the DOM directly.
		enablescrollbars = !enablescrollbars;
		evalscript(c, "document.documentElement.style.overflow = '%s'",
		    enablescrollbars ? "auto" : "hidden");
		*/
		return; /* do not reload */
		break;
	default:
		break;
	}
	reload(c, a);
}

void
togglefullscreen(Client *c, const Arg *a)
{
	/* toggling value is handled in winevent() */
	if (c->fullscreen)
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	else
		gtk_window_fullscreen(GTK_WINDOW(c->win));
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
togglestyle(Client *c, const Arg *arg)
{
	enablestyle = !enablestyle;
	setstyle(c, enablestyle ? getstyle(geturi(c)) : "");

	updatetitle(c);
}

void
toggleinspector(Client *c, const Arg *a)
{
	if (enableinspector) {
		if (webkit_web_inspector_is_attached(c->inspector))
			webkit_web_inspector_close(c->inspector);
		else
			webkit_web_inspector_show(c->inspector);
	}
}

void
find(Client *c, const Arg *a)
{
	const char *s, *f;

	if (a && a->i) {
		if (a->i > 0)
			webkit_find_controller_search_next(c->finder);
		else
			webkit_find_controller_search_previous(c->finder);
	} else {
		s = getatom(c, AtomFind);
		f = webkit_find_controller_get_search_text(c->finder);

		if (g_strcmp0(f, s) == 0) /* reset search */
			webkit_find_controller_search(c->finder, "", findopts, G_MAXUINT);

		webkit_find_controller_search(c->finder, s, findopts, G_MAXUINT);

		if (strcmp(s, "") == 0)
			webkit_find_controller_search_finish(c->finder);
	}
}

void
clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	navigate(c, a);
}

void
clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg.v = webkit_hit_test_result_get_link_uri(h);
	newwindow(c, &arg, a->b);
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
		enablecache = 0;
		break;
	case 'D':
		enablecache = 1;
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
