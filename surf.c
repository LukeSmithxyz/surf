#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <webkit/webkit.h>

#define LENGTH(x) (sizeof x / sizeof x[0])
/* Plan9-style Argument parsing */
/* Vars: _c -> count; _b -> break; _a -> argument */
#define ARG int _c, _b; char *_a; \
	for(_c = 1; _c < argc && argv[_c][0] == '-' && argv[_c][1] && \
			(strcmp(argv[_c], "--") != 0); _c++) \
		for(_a = &argv[_c][1], _b = 0; !_b && *_a; _a++ ) \
			switch(*_a)
#define ARGVAL()	(!_b && _a[1] && (_b = 1) ? &_a[1] : _c + 1 == argc ? \
		0 : argv[++_c])
#define ARGCHR()	(*_a)
#define ARGC()		_c

GtkWidget *win;
GtkWidget *browser;
WebKitWebView *view;
gchar *title;
gint progress = 100;
gboolean embed = FALSE;

static void setup(void);
static void cleanup(void);
static void updatetitle(void);
static void windestroy(GtkWidget* w, gpointer d);
static gboolean keypress(GtkWidget* w, GdkEventKey *ev);
static void titlechange(WebKitWebView* view, WebKitWebFrame* frame, const gchar* title, gpointer d);
static void progresschange(WebKitWebView *view, gint p, gpointer d);
static void loadcommit(WebKitWebView *view, WebKitWebFrame *f, gpointer d);
static void loadstart(WebKitWebView *view, WebKitWebFrame *f, gpointer d);
static void loadfinish(WebKitWebView *view, WebKitWebFrame *f, gpointer d);
static void linkhover(WebKitWebView* page, const gchar* t, const gchar* l, gpointer d);
static gboolean newwindow(WebKitWebView *view, WebKitWebFrame *f,
		WebKitNetworkRequest *r, WebKitWebNavigationAction *n,
		WebKitWebPolicyDecision *p, gpointer d);
static gboolean download(WebKitWebView *view, GObject *o, gpointer d);
static void loaduri(gchar *uri);
static void loadfile(gchar *f);
static void setupstdin();
static gboolean readstdin(GIOChannel *c, GIOCondition con);

gboolean
readstdin(GIOChannel *c, GIOCondition con) {
	gchar *line, *p;
	GIOStatus ret;

	ret = g_io_channel_read_line(c, &line, NULL, NULL, NULL);
	if(ret == G_IO_STATUS_ERROR || ret == G_IO_STATUS_EOF)
		return FALSE;
	for(p = line; *p && *p != '\n'; p++);
	*p = '\0';
	loaduri(line);
	g_free(line);
	return TRUE;
}

void
setupstdin() {
	GIOChannel *c = NULL;

	c = g_io_channel_unix_new(STDIN_FILENO);
	if(c && !g_io_add_watch(c, G_IO_IN|G_IO_HUP, (GIOFunc) readstdin, NULL))
			g_error("Stdin: could not add watch\n");
}

void
loadfile(gchar *f) {
	GIOChannel *c = NULL;
	GError *e = NULL;
	GString *code = g_string_new("");
	gchar *line;

	/* cannot use fileno in c99 - workaround*/
	if(strcmp(f, "-") == 0)
		c = g_io_channel_unix_new(STDIN_FILENO);
	else
		c = g_io_channel_new_file(f, "r", NULL);
	if (c) {
		while(g_io_channel_read_line(c, &line, NULL, NULL, &e) == G_IO_STATUS_NORMAL) {
			g_string_append(code, line);
			g_free(line);
		}
		webkit_web_view_load_html_string(view, code->str, NULL);
		g_io_channel_shutdown(c, FALSE, NULL);
	}
}

static void loaduri(gchar *uri) {
	GString* u = g_string_new(uri);
	if(g_strrstr(u->str, "://") == NULL)
		g_string_prepend(u, "http://");
	webkit_web_view_load_uri(view, u->str);
	g_string_free(u, TRUE);
}

gboolean
download(WebKitWebView *view, GObject *o, gpointer d) {
	/* TODO */
	return FALSE;
}

gboolean
newwindow(WebKitWebView *view, WebKitWebFrame *f,
		WebKitNetworkRequest *r, WebKitWebNavigationAction *n,
		WebKitWebPolicyDecision *p, gpointer d) {
	/* TODO */
	return FALSE;
}
void
linkhover(WebKitWebView* page, const gchar* t, const gchar* l, gpointer d) {
	/* TODO */
}

void
loadstart(WebKitWebView *view, WebKitWebFrame *f, gpointer d) {
	/* ??? TODO */
}

void
loadcommit(WebKitWebView *view, WebKitWebFrame *f, gpointer d) {
	/* ??? TODO */
}

void
loadfinish(WebKitWebView *view, WebKitWebFrame *f, gpointer d) {
	/* ??? TODO */
}

void
progresschange(WebKitWebView* view, gint p, gpointer d) {
	progress = p;
	updatetitle();
}

void
updatetitle() {
	char t[512];
	snprintf(t, LENGTH(t), "%s [%i%%]", title, progress);
	gtk_window_set_title(GTK_WINDOW(win), t);
}

void
titlechange(WebKitWebView *v, WebKitWebFrame *f, const gchar *t, gpointer d) {
	if(title)
		g_free(title);
	title = g_strdup(t);
	updatetitle();
}

void
windestroy(GtkWidget* w, gpointer d) {
	gtk_main_quit();
}

gboolean
keypress(GtkWidget* w, GdkEventKey *ev) {
	/* TODO */
	return FALSE;
}

void setup(void) {
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
	gtk_widget_set_name(win, "surf window");
	browser = gtk_scrolled_window_new(NULL, NULL);
	g_signal_connect (G_OBJECT(win), "destroy", G_CALLBACK(windestroy), NULL);
	g_signal_connect (G_OBJECT(win), "key-press-event", G_CALLBACK(keypress), NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(browser),
			GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	gtk_container_add(GTK_CONTAINER(browser), GTK_WIDGET(view));

	g_signal_connect(G_OBJECT(view), "title-changed", G_CALLBACK(titlechange), view);
	g_signal_connect(G_OBJECT(view), "load-progress-changed", G_CALLBACK(progresschange), view);
	g_signal_connect(G_OBJECT(view), "load-committed", G_CALLBACK(loadcommit), view);
	g_signal_connect(G_OBJECT(view), "load-started", G_CALLBACK(loadstart), view);
	g_signal_connect(G_OBJECT(view), "load-finished", G_CALLBACK(loadfinish), view);
	g_signal_connect(G_OBJECT(view), "hovering-over-link", G_CALLBACK(linkhover), view);
	g_signal_connect(G_OBJECT(view), "new-window-policy-decision-requested", G_CALLBACK(newwindow), view);
	g_signal_connect(G_OBJECT(view), "download-requested", G_CALLBACK(download), view);
	/* g_signal_connect(G_OBJECT(view), "create-web-view", G_CALLBACK(createwebview), view); */

	gtk_container_add(GTK_CONTAINER(win), browser);
	gtk_widget_grab_focus(GTK_WIDGET(view));
	gtk_widget_show_all(win);
}

void cleanup() {
}

int main(int argc, char *argv[]) {
	gchar *uri = NULL, *file = NULL;

	ARG {
	case 'e':
		embed = TRUE;
		break;
	case 'u':
		if(!(uri = ARGVAL()))
			goto argerr;
		break;
	case 'f':
		if(!(file = ARGVAL()))
			goto argerr;
		break;
	argerr:
	default:
		puts("surf - simple browser");
		printf("usage: %s [-e] [-u uri] [-f file]", argv[0]);
		return EXIT_FAILURE;
	}
	if(argc != ARGC())
		goto argerr;
	gtk_init(NULL, NULL);
	if (!g_thread_supported())
		g_thread_init(NULL);
	setup();
	if(uri)
		loaduri(uri);
	else if(file)
		loadfile(file);
	setupstdin();
	updatetitle();
	gtk_main();
	cleanup();
	return EXIT_SUCCESS;
}
