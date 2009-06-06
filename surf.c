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

Display *dpy;
Atom urlprop;
typedef struct Client {
	GtkWidget *win;
	GtkWidget *browser;
	WebKitWebView *view;
	gchar *title;
	gint progress;
	struct Client *next;
} Client;
Client *clients = NULL;
gboolean embed = FALSE;
gboolean showxid = FALSE;
gboolean ignore_once = FALSE;

static Client *newclient();
static void die(char *str);
static void setup(void);
static void cleanup(void);
static void updatetitle(Client *c);
static void destroywin(GtkWidget* w, gpointer d);
static gboolean keypress(GtkWidget* w, GdkEventKey *ev, gpointer d);
static void titlechange(WebKitWebView* view, WebKitWebFrame* frame, const gchar* title, gpointer d);
static void progresschange(WebKitWebView *view, gint p, gpointer d);
static void loadcommit(WebKitWebView *view, WebKitWebFrame *f, gpointer d);
static void linkhover(WebKitWebView* page, const gchar* t, const gchar* l, gpointer d);
static void destroyclient(Client *c);
static gboolean newwindow(WebKitWebView *view, WebKitWebFrame *f,
		WebKitNetworkRequest *r, WebKitWebNavigationAction *n,
		WebKitWebPolicyDecision *p, gpointer d);
static gboolean download(WebKitWebView *view, GObject *o, gpointer d);
static void loaduri(const Client *c, const gchar *uri);
static void loadfile(const Client *c, const gchar *f);
GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event, gpointer data);

void
cleanup(void) {
	while(clients)
		destroyclient(clients);
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d) {
	XPropertyEvent *ev;
	Client *c = (Client *)d;
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *buf = NULL;
	if(((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if(ignore_once == FALSE && ev->atom == urlprop && ev->state == PropertyNewValue) {
			XGetWindowProperty(dpy, ev->window, urlprop, 0L, BUFSIZ, False, XA_STRING, 
				&adummy, &idummy, &ldummy, &ldummy, &buf);
			loaduri(c, (gchar *)buf);
			XFree(buf);
			return GDK_FILTER_REMOVE;
		}
		ignore_once = FALSE;
	}
	return GDK_FILTER_CONTINUE;
}

void
loadfile(const Client *c, const gchar *f) {
	GIOChannel *chan = NULL;
	GError *e = NULL;
	GString *code = g_string_new("");
	GString *uri = g_string_new(f);
	gchar *line;

	if(strcmp(f, "-") == 0) {
		chan = g_io_channel_unix_new(STDIN_FILENO);
		if (chan) {
			while(g_io_channel_read_line(chan, &line, NULL, NULL, &e) == G_IO_STATUS_NORMAL) {
				g_string_append(code, line);
				g_free(line);
			}
			webkit_web_view_load_html_string(c->view, code->str, NULL);
			g_io_channel_shutdown(chan, FALSE, NULL);
		}
	}
	else {
		g_string_prepend(uri, "file://");
		loaduri(c, uri->str);
	}
	
}

static void loaduri(const Client *c, const gchar *uri) {
	GString* u = g_string_new(uri);
	if(g_strrstr(u->str, ":") == NULL)
		g_string_prepend(u, "http://");
	webkit_web_view_load_uri(c->view, u->str);
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
	puts("new");
	Client *c = newclient();
	webkit_web_view_load_request(c->view, r);
	return TRUE;
}
void
linkhover(WebKitWebView* page, const gchar* t, const gchar* l, gpointer d) {
	Client *c = (Client *)d;

	if(l)
		gtk_window_set_title(GTK_WINDOW(c->win), l);
	else
		updatetitle(c);
}

void
loadcommit(WebKitWebView *view, WebKitWebFrame *f, gpointer d) {
	Client *c = (Client *)d;
	gchar *uri;

	if(!(uri = (gchar *)webkit_web_view_get_uri(view)))
		uri = "(null)";
	ignore_once = TRUE;
	XChangeProperty(dpy, GDK_WINDOW_XID(GTK_WIDGET(c->win)->window), urlprop,
			XA_STRING, 8, PropModeReplace, (unsigned char *)uri,
			strlen(uri) + 1);
}

void
progresschange(WebKitWebView* view, gint p, gpointer d) {
	Client *c = (Client *)d;

	c->progress = p;
	updatetitle(c);
}

void
updatetitle(Client *c) {
	char t[512];
	if(c->progress == 100)
		snprintf(t, LENGTH(t), "%s", c->title);
	else
		snprintf(t, LENGTH(t), "%s [%i%%]", c->title, c->progress);
	gtk_window_set_title(GTK_WINDOW(c->win), t);
}

void
titlechange(WebKitWebView *v, WebKitWebFrame *f, const gchar *t, gpointer d) {
	Client *c = (Client *)d;

	if(c->title)
		g_free(c->title);
	c->title = g_strdup(t);
	updatetitle(c);
}

void
destroywin(GtkWidget* w, gpointer d) {
	Client *c = (Client *)d;

	destroyclient(c);
}

void
destroyclient(Client *c) {
	Client *p;
	gtk_widget_destroy(c->win);
	if(clients == c && c->next == NULL)
		gtk_main_quit();
	for(p = clients; p && p->next != c; p = p->next);
	if(p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
}

gboolean
keypress(GtkWidget* w, GdkEventKey *ev, gpointer d) {
	Client *c = (Client *)d;

	if(ev->type == GDK_KEY_PRESS && (ev->state == GDK_CONTROL_MASK
			|| ev->state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
		switch(ev->keyval) {
		case GDK_r:
		case GDK_R:
			if((ev->state & GDK_SHIFT_MASK))
				 webkit_web_view_reload_bypass_cache(c->view);
			else
				 webkit_web_view_reload(c->view);
			return TRUE;
		case GDK_go:
			/* TODO */
			return TRUE;
		case GDK_slash:
			/* TODO */
			return TRUE;
		case GDK_Left:
			webkit_web_view_go_back(c->view);
			return TRUE;
		case GDK_Right:
			webkit_web_view_go_forward(c->view);
			return TRUE;
		}
	}
	return FALSE;
}

void setup(void) {
	dpy = GDK_DISPLAY();
	urlprop = XInternAtom(dpy, "_SURF_URL", False);
}

void die(char *str) {
	fputs(str, stderr);
	exit(EXIT_FAILURE);
}

Client *
newclient(void) {
	Client *c;
	if(!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");
	if(embed) {
		c->win = gtk_plug_new(0);
	}
	else {
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_wmclass(GTK_WINDOW(c->win), "surf", "surf");
	}
	gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);
	c->browser = gtk_scrolled_window_new(NULL, NULL);
	g_signal_connect (G_OBJECT(c->win), "destroy", G_CALLBACK(destroywin), c);
	g_signal_connect (G_OBJECT(c->win), "key-press-event", G_CALLBACK(keypress), c);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->browser),
			GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	c->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	gtk_container_add(GTK_CONTAINER(c->browser), GTK_WIDGET(c->view));

	g_signal_connect(G_OBJECT(c->view), "title-changed", G_CALLBACK(titlechange), c);
	g_signal_connect(G_OBJECT(c->view), "load-progress-changed", G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view), "load-committed", G_CALLBACK(loadcommit), c);
	g_signal_connect(G_OBJECT(c->view), "hovering-over-link", G_CALLBACK(linkhover), c);
	g_signal_connect(G_OBJECT(c->view), "new-window", G_CALLBACK(newwindow), c);
	g_signal_connect(G_OBJECT(c->view), "download-requested", G_CALLBACK(download), c);

	gtk_container_add(GTK_CONTAINER(c->win), c->browser);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_widget_show_all(c->win);
	if(showxid)
		printf("%u\n", (unsigned int)GDK_WINDOW_XID(GTK_WIDGET(c->win)->window));
	c->next = clients;
	clients = c;
	gdk_window_set_events(GTK_WIDGET(c->win)->window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(GTK_WIDGET(c->win)->window, processx, c);
	return c;
}

int main(int argc, char *argv[]) {
	gchar *uri = NULL, *file = NULL;
	Client *c;

	gtk_init(NULL, NULL);
	if (!g_thread_supported())
		g_thread_init(NULL);
	setup();
	ARG {
	case 'x':
		showxid = TRUE;
		break;
	case 'e':
		showxid = TRUE;
		embed = TRUE;
		break;
	case 'u':
		if(!(uri = ARGVAL()))
			goto argerr;
		c = newclient();
		loaduri(c, uri);
		updatetitle(c);
		break;
	case 'f':
		if(!(file = ARGVAL()))
			goto argerr;
		c = newclient();
		loadfile(c, file);
		updatetitle(c);
		break;
	argerr:
	default:
		puts("surf - simple browser");
		printf("usage: %s [-e] [-x] [-u uri] [-f file]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if(argc != ARGC())
		goto argerr;
	if(!clients)
		newclient();
	gtk_main();
	cleanup();
	return EXIT_SUCCESS;
}
