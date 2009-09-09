/* modifier 0 means no modifier */
static gchar *progress       = "#FF0000";
static gchar *progress_trust = "#00FF00";
#define MODKEY GDK_CONTROL_MASK
static Key keys[] = {
    /* modifier	            keyval      function    arg             Focus */
    { MODKEY,               GDK_R,      reload,     { .b = TRUE },  ALWAYS },
    { MODKEY,               GDK_r,      reload,     { .b = FALSE }, ALWAYS },
    { MODKEY,               GDK_g,      showurl,    { 0 },          ALWAYS },
    { MODKEY,               GDK_slash,  showsearch, { 0 },          ALWAYS },
    { 0,                    GDK_Escape, hidesearch, { 0 },          ALWAYS },
    { 0,                    GDK_Escape, hideurl,    { 0 },          ALWAYS },
    { MODKEY,               GDK_P,      print,      { 0 },          ALWAYS },
    { MODKEY,               GDK_p,      clipboard,  { .b = TRUE },  BROWSER },
    { MODKEY,               GDK_y,      clipboard,  { .b = FALSE }, BROWSER },
    { MODKEY,               GDK_j,      zoom,       { .i = +1 },    BROWSER },
    { MODKEY,               GDK_k,      zoom,       { .i = -1 },    BROWSER },
    { MODKEY,               GDK_i,      zoom,       { .i = 0  },    BROWSER },
    { MODKEY,               GDK_l,      navigate,   { .i = +1 },    BROWSER },
    { MODKEY,               GDK_h,      navigate,   { .i = -1 },    BROWSER },
    { 0,                    GDK_Escape, stop,       { 0 },          BROWSER },
    { MODKEY,               GDK_o,      source,     { 0 },          BROWSER },
    { MODKEY,               GDK_n,      searchtext, { .b = TRUE },  BROWSER|SEARCHBAR },
    { MODKEY,               GDK_N,      searchtext, { .b = FALSE }, BROWSER|SEARCHBAR },
    { 0,                    GDK_Return, searchtext, { .b = TRUE },  SEARCHBAR },
    { GDK_SHIFT_MASK,       GDK_Return, searchtext, { .b = FALSE }, SEARCHBAR },
    { 0,                    GDK_Return, loaduri,    { .v = NULL },  URLBAR },
    { 0,                    GDK_Return, hideurl,    { 0 },          URLBAR },
};

