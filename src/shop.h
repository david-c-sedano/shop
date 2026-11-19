
#ifndef SHOP_ONCE
#define SHOP_ONCE

// C programming is so astronomically painful bro 
typedef struct Shop Shop;

#ifndef CSV_SQL
    // this is kinda just here for reference, `CSV_SQL` should be defined in `shop.c` before `#include "shop.h"` 
    typedef struct {
        char **columns,**cells;
        int width,height;
        char* error;
    } SQL_Result;

    // We took the bounds checker in and gave him a cartel execution
    #define CELL(r, x, y) ((r)->cells[(y)*(r)->width+(x)])
#endif

typedef struct {
    size_t count,capacity;
    char* str;
} SQL_Template;

#define NEW_SQL (SQL_Template){0,8,NULL}

#define APPEND_SQL(t, ...) do {\
    int needed = snprintf(NULL, 0, __VA_ARGS__);\
    if (t.count + (size_t)needed + 1 > t.capacity) {\
        while (t.count + (size_t)needed + 1 > t.capacity) {\
            t.capacity *= 2;\
        }\
        t.str = realloc(t.str, t.capacity);\
    }\
    t.count += snprintf(t.str + t.count, t.capacity - t.count, __VA_ARGS__);\
} while (0)

// not technically individual free because dynamic arrays are the PRECURSOR to arenas
#define NUKE_SQL(t) free(t.str)

typedef struct {
    char **names, **types;
    int count, capacity;
    Arena alloc;
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
    char csv_path_buf[500];

    Arena alloc;
    Arena temp;
} Admin_Panel;

typedef struct {
    bool active;
    char keywords[100];
    char category[100];
    float min_price,max_price;
    int min_stock;
    bool use_min,use_max,use_stock,in_stock_only,descending;
} Adv_Search_Panel;


typedef struct {
    bool success;
    bool is_admin;
    int user_id;
    char username[100];
    char email[160];
    char message[256];
} Auth_Result;

typedef struct {
    bool register_mode;
    bool forgot_password_mode;
    bool logged_in;
    bool is_admin;
    bool show_password;
    bool purchase_history_tab;
    int user_id;

    char username[100];
    char email[160];
    char password[100];

    char current_username[100];
    char current_email[160];
    char current_password[100];
    char new_password[100];
    char confirm_password[100];

    char message[256];
    char settings_message[256];
} Account_State;

typedef enum {
    LOAD_SCREEN,
    HOME_SCREEN,
    DISPLAY_SCREEN,
    ACCOUNT_SCREEN,
    CART_SCREEN,
    CHECKOUT_SCREEN,
    SALES_SCREEN,
    USERS_SCREEN,
} Screen;

typedef void (*Home_Button_Callback)(Shop *shop);

typedef struct {
    Texture2D texture;
    Screen transition;
    Home_Button_Callback callback;
    const char* placeholder_label;
} Home_Button;

typedef struct {
    int id;
    char name[100];
    char description[500];
    double price;
    double sale_price;
    int stock;
    char category[100];
    char display[100];
} Item;

typedef struct {
    bool active;
    bool on_sale;
    bool group_sale;
    Item item;
    sqlite3_int64 stock;
    double original_sale_price;
    char error[160];
} Item_Editor;

static inline bool item_is_on_sale(Item item) {
    return item.sale_price > 0 &&
           item.sale_price < item.price;
}

static inline float item_final_price(Item item) {
    if (item_is_on_sale(item)) {
        return item.sale_price;
    }
    return item.price;
}

typedef struct {
    Item item;
    int quantity;
} Cart_Item;

typedef struct {
    Cart_Item* items;
    int count;
    int capacity;
    int first_visible_row;
} Cart;

typedef struct {
    Item* items;
    int count, capacity;
    Arena alloc;
    const char* category_filter;
} Item_List;

typedef struct {
    int type;
    int sort;
    bool descending;
    char category[100], product[100];
    bool use_min, use_max;
    double minimum, maximum;
    char start_from[16], start_to[16], end_from[16], end_to[16];
} Sales_Filter;

typedef struct {
    int form; // 1 = discount code, 2 = individual sale, 3 = group sale
    sqlite3_int64 editing_id, original_start;
    bool edit_past, edit_started, confirm_delete, start_locked_notice;
    char delete_name[100];
    char name[100], search[100], starts[32], ends[32], expires[32];
    bool scheduled, has_end, percent, enabled;
    double value;
    Item_List products;
    bool* selected;
    int pages[3][3];
    sqlite3_int64 expanded_sale;
    sqlite3_int64 confirm_end;
    bool open_end_confirmation;
    char end_name[100];
    sqlite3_int64 refreshed_at;
    RenderTexture2D page_target;
    char message[256], error[256];
    char search_input[100], keywords[100];
    bool search_active, advanced_open;
    float scroll_y, search_offset;
    bool scroll_to_top, show_back_to_top;
    float back_to_top_alpha;
    bool page_can_scroll;
    int current_tab;
    Sales_Filter filters[3], filter_draft;
} Sales_State;

typedef struct {
    int role, status, sort;
    bool descending, use_min, use_max;
    double minimum, maximum;
    char customer[100], date_from[16], date_to[16];
} Users_Filter;

typedef struct {
    sqlite3_int64 id, user_id;
} Users_Expanded;

typedef struct {
    int current_tab, requested_tab, pages[3], page_sizes[3];
    sqlite3_int64 revealed_id, editing_id, orders_user_id;
    Users_Expanded* expanded;
    int expanded_count, expanded_capacity;
    bool edit_role, original_role, original_enabled;
    int confirmation;
    char username[100], email[160], original_username[100], original_email[160];
    char orders_username[100], search_input[100], keywords[100];
    char orders_search_input[100], orders_keywords[100];
    char message[256], error[256];
    bool search_active, advanced_open, scroll_to_top, page_can_scroll;
    bool orders_completed[2];
    int order_pages[2][2];
    float scroll_y, back_to_top_alpha;
    bool show_back_to_top;
    RenderTexture2D page_target;
    Users_Filter filters[3], filter_draft;
} Users_State;

typedef struct {
    bool is_model;
    float scale;
    union {
        Texture2D texture;
        Model model;
    };
} Item_Resource;

typedef Ht(int, Item_Resource) Item_Resource_Table;

typedef struct Shop {
    Admin_Panel admin;
    Adv_Search_Panel adv_search;
    Account_State account;
    Item_Editor item_editor;
    Sales_State sales;
    Users_State users;

    Item_Resource_Table item_resources;
    bool paused;
    char search_bar[100];
    bool search_bar_active;
    float search_flash;

    // HOME
    Home_Button* home_buttons;
    int home_button_count;
    Item_List featured;
    float top_row_scroll, top_row_scroll_target;
    float bottom_row_scroll, bottom_row_scroll_target;

    // DISPLAY
    Item_List display;
    float scroll, scroll_target;

    //CART
    Cart cart;

    // CHECKOUT
    bool checkout_complete;
    int checkout_first_visible_item;
    float checkout_scrollbar_grab;
    char checkout_message[160];
    char discount_code[32];
    bool discount_box_active;
    int applied_discount_id;
    float applied_discount_amount;
    char discount_message[120];

    // CORE
    Camera3D camera;
    RenderTexture2D render_target;
    Shader shader;
    int time_loc; // cache this for perf
    Screen screen;
    bool transitioning;
    float transition_alpha;
    bool fading_out;
    Screen transition_target;
} Shop;

static inline bool shop_is_admin(const Shop* shop) {
    return shop->account.logged_in && shop->account.is_admin;
}

void strip_file_name(char *path);

void shop_render_pass(Shop* shop);
void ui_render_pass(Shop* shop);
void screen_swap(Shop* shop, Screen screen);
Vector2 mouse_pos_in_shop(Shop* shop);
Vector2 mouse_yaw_pitch(Shop* shop);
void update_carousel(float* scroll, float* target, int count, float spacing, bool active);
void draw_carousel(Shop* shop, Item_List* items, float scroll, float spacing, float y);
int carousel_focused_item_index(Item_List items, float scroll, float spacing);
float carousel_item_alpha(Item_List items, float scroll, float spacing);
Item_List query_items(Shop* shop, const char* sql);
void reset_item_list(Item_List* list);
void draw_button(Shop* shop, Rectangle bounds, const char* label, Color color);
bool panel_begin_modal(const char* title, ImGuiWindowFlags flags);
bool button_event(Shop* shop, Rectangle bounds);
Rectangle cart_button_bounds(Shop* shop);
Rectangle back_button_bounds(Shop* shop);

void add_to_cart(Shop* shop, Item item);
void draw_cart(Shop* shop);
void update_cart(Shop* shop);
int cart_selected_item_count(Shop* shop);
void draw_cart_button_with_count(Shop* shop);

Rectangle checkout_button_bounds(Shop* shop);
void update_checkout_button(Shop* shop);
void draw_checkout_button(Shop* shop);
void update_checkout(Shop* shop);
void draw_checkout(Shop* shop);

bool discount_init(Shop* shop);
void discount_reset(Shop* shop);
bool discount_apply(Shop* shop, float subtotal);

bool order_save_init(Shop* shop);
bool order_save_purchase(Shop* shop, int discount_code_id, float subtotal, float discount_amount, float tax_amount, float total_amount);

bool cart_save_init(Shop* shop);
bool cart_load(Shop* shop);
bool cart_save(Shop* shop);
void cart_save_if_changed(Shop* shop);
void cart_clear(Shop* shop);

bool auth_init(Shop* shop);
Auth_Result auth_login(Shop* shop, const char* username, const char* password);
Auth_Result auth_register(Shop* shop, const char* username, const char* password);
void auth_logout(Shop* shop);
bool auth_refresh_session(Shop* shop);
void users_collapse_all(Shop* shop);
void update_account(Shop* shop);
void draw_account(Shop* shop);
void account_panel(Shop* shop);
void draw_purchase_history(Shop* shop);
void open_item_editor(Shop* shop, Item item);
bool sales_init(Shop* shop);
bool sales_refresh(Shop* shop);
bool sales_checkout_matches(Shop* shop, int code_id, float subtotal, float discount);
Rectangle advanced_search_button_bounds(Shop* shop);
bool search_bar_event(Shop* shop);
void draw_search_bar(Shop* shop);
void sales_close_form(Shop* shop);

bool init_shop(Shop *shop);
void update_shop(Shop *shop);
void draw_shop(Shop *shop);

#define ITEM_ID_COLUMN         0
#define ITEM_NAME_COLUMN       1
#define ITEM_DESC_COLUMN       2
#define ITEM_PRICE_COLUMN      3
#define ITEM_STOCK_COLUMN      4
#define ITEM_CATEGORY_COLUMN   5
#define ITEM_DISPLAY_COLUMN    6
#define ITEM_SALE_PRICE_COLUMN 7

#define ITEM_COLUMNS "id, name, description, price, stock, category, display, sale_price"
#define ITEM_CATEGORY_CHAIRS "chairs"
#define ITEM_CATEGORY_FURNITURE "furniture"
#define ITEM_CATEGORY_LARGE "large"
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
