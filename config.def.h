/* modifier 0 means no modifier */
static GdkColor progress       = { 65535,65535,0,0 };
static GdkColor progress_trust = { 65535,0,65535,0 };
static Key keys[] = {
    /* modifier	            keyval      function        arg             Focus */
    { GDK_CONTROL_MASK,     GDK_R,      reload,         {.b = TRUE},    ALWAYS },
    { GDK_CONTROL_MASK,     GDK_r,      reload,         {.b = FALSE},   ALWAYS },
    { GDK_CONTROL_MASK,     GDK_g,      showurl,        {0},            ALWAYS },
    { GDK_CONTROL_MASK,     GDK_slash,  showsearch,     {0},            ALWAYS },
    { 0,                    GDK_Escape, hidesearch,     {0},            ALWAYS },
    { 0,                    GDK_Escape, hideurl,        {0},            ALWAYS },
    { GDK_CONTROL_MASK,     GDK_P,      print,          {0},            ALWAYS },
    { GDK_CONTROL_MASK,     GDK_p,      clipboard,      {.b = TRUE },   BROWSER },
    { GDK_CONTROL_MASK,     GDK_y,      clipboard,      {.b = FALSE},   BROWSER },
    { GDK_CONTROL_MASK,     GDK_equal,  zoom,           {.i = +1 },     BROWSER },
    { GDK_CONTROL_MASK,     GDK_plus,   zoom,           {.i = +1 },     BROWSER },
    { GDK_CONTROL_MASK,     GDK_minus,  zoom,           {.i = -1 },     BROWSER },
    { GDK_CONTROL_MASK,     GDK_0,      zoom,           {.i = 0 },      BROWSER },
    { GDK_CONTROL_MASK,     GDK_l,      navigate,       {.i = +1},      BROWSER },
    { GDK_CONTROL_MASK,     GDK_h,      navigate,       {.i = -1},      BROWSER },
    { 0,                    GDK_Escape, stop,           {0},            BROWSER },
    { GDK_CONTROL_MASK,     GDK_n,      searchtext,     {.b = TRUE},    BROWSER|SEARCHBAR },
    { GDK_CONTROL_MASK,     GDK_N,      searchtext,     {.b = FALSE},   BROWSER|SEARCHBAR },
    { 0,                    GDK_Return, searchtext,     {.b = TRUE},    SEARCHBAR },
    { GDK_SHIFT_MASK,       GDK_Return, searchtext,     {.b = FALSE},   SEARCHBAR },
    { 0 },
    { 0,                    GDK_Return, loaduri,        {.v = NULL},    URLBAR },
    { 0,                    GDK_Return, hideurl,        {0},            URLBAR },
};

