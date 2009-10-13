/* modifier 0 means no modifier */
static gchar *progress       = "#FF0000";
static gchar *progress_trust = "#00FF00";
static gchar *stylefile      = ".surf/style.css";
static gchar *scriptfile     = ".surf/script.js";
static gchar *cookiefile     = ".surf/cookie.txt";
static gchar *dldir          = ".surf/dl";
#define MODKEY GDK_CONTROL_MASK
static Key keys[] = {
    /* modifier	            keyval      function    arg             Focus */
    { MODKEY|GDK_SHIFT_MASK,GDK_r,      reload,     { .b = TRUE },  Any },
    { MODKEY,               GDK_r,      reload,     { .b = FALSE }, Any },
    { MODKEY,               GDK_g,      showurl,    { 0 },          Any },
    { MODKEY,               GDK_slash,  showsearch, { 0 },          Any },
    { 0,                    GDK_Escape, hidesearch, { 0 },          Any },
    { 0,                    GDK_Escape, hideurl,    { 0 },          Any },
    { MODKEY|GDK_SHIFT_MASK,GDK_p,      print,      { 0 },          Any },
    { MODKEY,               GDK_p,      clipboard,  { .b = TRUE },  Browser },
    { MODKEY,               GDK_y,      clipboard,  { .b = FALSE }, Browser },
    { MODKEY|GDK_SHIFT_MASK,GDK_j,      zoom,       { .i = -1 },    Browser },
    { MODKEY|GDK_SHIFT_MASK,GDK_k,      zoom,       { .i = +1 },    Browser },
    { MODKEY|GDK_SHIFT_MASK,GDK_i,      zoom,       { .i = 0  },    Browser },
    { MODKEY,               GDK_l,      navigate,   { .i = +1 },    Browser },
    { MODKEY,               GDK_h,      navigate,   { .i = -1 },    Browser },
    { MODKEY,               GDK_j,      scroll,     { .i = +1 },    Browser },
    { MODKEY,               GDK_k,      scroll,     { .i = -1 },    Browser },
    { 0,                    GDK_Escape, stop,       { 0 },          Browser },
    { MODKEY,               GDK_o,      source,     { 0 },          Browser },
    { MODKEY,               GDK_n,      searchtext, { .b = TRUE },  Browser|SearchBar },
    { MODKEY|GDK_SHIFT_MASK,GDK_n,      searchtext, { .b = FALSE }, Browser|SearchBar },
    { 0,                    GDK_Return, searchtext, { .b = TRUE },  SearchBar },
    { GDK_SHIFT_MASK,       GDK_Return, searchtext, { .b = FALSE }, SearchBar },
    { 0,                    GDK_Return, loaduri,    { .v = NULL },  UrlBar },
    { 0,                    GDK_Return, hideurl,    { 0 },          UrlBar },
};

