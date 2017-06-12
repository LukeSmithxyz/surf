#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <webkit2/webkit-web-extension.h>
#include <webkitdom/webkitdom.h>
#include <webkitdom/WebKitDOMDOMWindowUnstable.h>

#include "common.h"

#define LENGTH(x)   (sizeof(x) / sizeof(x[0]))

typedef struct Page {
	guint64 id;
	WebKitWebPage *webpage;
	struct Page *next;
} Page;

static int pipein, pipeout;
static Page *pages;

Page *
newpage(WebKitWebPage *page)
{
	Page *p;

	if (!(p = calloc(1, sizeof(Page))))
		die("Cannot malloc!\n");

	p->next = pages;
	pages = p;

	p->id = webkit_web_page_get_id(page);
	p->webpage = page;

	return p;
}

static void
msgsurf(Page *p, const char *s)
{
	char msg[MSGBUFSZ];
	int ret;

	msg[0] = p ? p->id : 0;
	ret = snprintf(&msg[1], sizeof(msg) - 1, "%s", s);
	if (ret >= sizeof(msg)) {
		fprintf(stderr, "webext: message too long: %d\n", ret);
		return;
	}

	if (pipeout) {
		if (write(pipeout, msg, sizeof(msg)) < 0)
			fprintf(stderr, "webext: error sending: %s\n", msg);
	}
}

static gboolean
readpipe(GIOChannel *s, GIOCondition c, gpointer unused)
{
	char msg[MSGBUFSZ];
	gsize msgsz;
	WebKitDOMDOMWindow *view;
	GError *gerr = NULL;
	glong wh, ww;
	Page *p;

	if (g_io_channel_read_chars(s, msg, LENGTH(msg), &msgsz, &gerr) !=
	    G_IO_STATUS_NORMAL) {
		fprintf(stderr, "webext: error reading pipe: %s\n",
		        gerr->message);
		g_error_free(gerr);
		return TRUE;
	}
	msg[msgsz] = '\0';

	for (p = pages; p; p = p->next) {
		if (p->id == msg[0])
			break;
	}
	if (!p || !(view = webkit_dom_document_get_default_view(
	            webkit_web_page_get_dom_document(p->webpage))))
		return TRUE;

	switch (msg[1]) {
	case 'h':
		ww = webkit_dom_dom_window_get_inner_width(view);
		webkit_dom_dom_window_scroll_by(view,
		                                (ww / 100) * msg[2], 0);
		break;
	case 'v':
		wh = webkit_dom_dom_window_get_inner_height(view);
		webkit_dom_dom_window_scroll_by(view,
		                                0, (wh / 100) * msg[2]);
		break;
	}

	return TRUE;
}

static void
webpagecreated(WebKitWebExtension *e, WebKitWebPage *wp, gpointer unused)
{
	Page *p = newpage(wp);
}

G_MODULE_EXPORT void
webkit_web_extension_initialize_with_user_data(WebKitWebExtension *e, GVariant *gv)
{
	GIOChannel *gchanpipe;

	g_signal_connect(e, "page-created", G_CALLBACK(webpagecreated), NULL);

	g_variant_get(gv, "(ii)", &pipein, &pipeout);
	msgsurf(NULL, "i");

	gchanpipe = g_io_channel_unix_new(pipein);
	g_io_channel_set_encoding(gchanpipe, NULL, NULL);
	g_io_channel_set_close_on_unref(gchanpipe, TRUE);
	g_io_add_watch(gchanpipe, G_IO_IN, readpipe, NULL);
}
