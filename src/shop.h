
#ifndef SHOP_ONCE
#define SHOP_ONCE

typedef struct {
    char **columns,**cells;
    int width,height;
    char* error;
} SQL_Result;

typedef struct {
    char **names, **types;
    int count, capacity;
} Schema_List;

typedef struct {
    char* data;
    size_t count,capacity;
    bool dirty;
    Arena alloc; 
} Text_Editor;

typedef struct {
    Text_Editor* current_ed;
    float split_h,split_v;
    sqlite3* db;
    SQL_Result prev_result; 
    Schema_List schema;
    bool active;
} Admin_Panel;

// I am trying REALLY HARD right now to NOT write an entity system...
typedef struct {
    Model model;
    Vector3 size;
    float scale;
} Item;

typedef enum {
    LOAD_SCREEN,
    HOME_SCREEN,
    DISPLAY_SCREEN,
    // ACCOUNT_SCREEN,
    // CHECKOUT_SCREEN,
    // IDK??
} Screen_Kind;

typedef struct {
    Item* items;
    int item_count, item_cap;
    float scroll, scroll_target;
    Camera3D camera;
    RenderTexture2D texture;
    Shader shader;
    int time_loc; // cache this for perf
    Screen_Kind screen;
    Admin_Panel admin;
} Shop;

void shop_render_pass(Shop* shop);
void ui_render_pass(Shop* shop);

bool init_shop(Shop *shop);
void update_shop(Shop *shop);
void draw_shop(Shop *shop);

#endif
