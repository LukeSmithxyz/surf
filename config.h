/* modifier 0 means no modifier */
static Key searchbar_keys[] = {
    /* modifier	            keyval      function        arg         stop event */
    { 0,                    GDK_Escape, hidesearch,     {0},            TRUE },
    { 0,                    GDK_Return, searchtext,     {.b = TRUE},    TRUE },
    { GDK_SHIFT_MASK,       GDK_Return, searchtext,     {.b = FALSE},   TRUE },
    { GDK_SHIFT_MASK,       GDK_Left,   NULL,           {0},            FALSE },
    { GDK_SHIFT_MASK,       GDK_Right,  NULL,           {0},            FALSE },
};

static Key urlbar_keys[] = {
    /* modifier	            keyval      function        arg         stop event */
    { 0,                    GDK_Escape, hideurl,        {0},            TRUE },
        /* able to "chain" commands; by setting stop event to FALSE */
    { 0,                    GDK_Return, loaduri,        {.v = NULL},    FALSE },
    { 0,                    GDK_Return, hideurl,        {0},            TRUE },
    { GDK_SHIFT_MASK,       GDK_Left,   NULL,           {0},            FALSE },
    { GDK_SHIFT_MASK,       GDK_Right,  NULL,           {0},            FALSE },
};

static Key general_keys[] = {
    /* modifier	            keyval      function        arg         stop event */
    { GDK_CONTROL_MASK,     GDK_P,      print,          {0},            TRUE },
    { GDK_CONTROL_MASK,     GDK_p,      clipboard,      {.b = TRUE },   TRUE },
    { GDK_CONTROL_MASK,     GDK_y,      clipboard,      {.b = FALSE},   TRUE },
    { GDK_CONTROL_MASK,     GDK_R,      reload,         {.b = TRUE},    TRUE },
    { GDK_CONTROL_MASK,     GDK_r,      reload,         {.b = FALSE},   TRUE },
    { GDK_CONTROL_MASK,     GDK_b,      NULL,           {0},            TRUE },
    { GDK_CONTROL_MASK,     GDK_g,      showurl,        {0},            TRUE },
    { GDK_CONTROL_MASK,     GDK_slash,  showsearch,     {0},            TRUE },
    { GDK_CONTROL_MASK,     GDK_plus,   zoompage,       {0},            TRUE },
    { GDK_CONTROL_MASK,     GDK_minus,  zoompage,       {.f = -1.0 },   TRUE },
    { GDK_CONTROL_MASK,     GDK_0,      zoompage,       {.f = +1.0 },   TRUE },
    { GDK_CONTROL_MASK,     GDK_n,      searchtext,     {.b = TRUE},    TRUE },
    { GDK_CONTROL_MASK,     GDK_N,      searchtext,     {.b = FALSE},   TRUE },
    { GDK_CONTROL_MASK,     GDK_h,      navigate,       {.b = TRUE},    TRUE },
    { GDK_CONTROL_MASK,     GDK_l,      navigate,       {.b = FALSE},   TRUE },
    { 0,                    GDK_Escape, stop,           {0},            TRUE },
};

/* Sequence of Keys to match against a keypress */
static KeySet keysets[] = {
    /* keyset (Key[])   numkeys                     focusedwidget/mode */
    { searchbar_keys,   LENGTH(searchbar_keys),     SEARCHBAR },
    { urlbar_keys,      LENGTH(urlbar_keys),        URLBAR },
    { general_keys,     LENGTH(general_keys),       NONE },
};
