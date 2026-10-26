#ifndef TI84VM_H
#define TI84VM_H
#include <stdint.h>
#include <stdbool.h>

#define VM_EXPR 128
#define VM_ANSWER 48
#define VM_NOTICE 72
#define VM_PROGRAM_CAP 12
#define VM_PROGRAM_NAME 9
#define VM_ABOUT_TEXT 36
#define VM_Y_EXPR_LEN 48
#define VM_Y_FOCUS_EXPR 0
#define VM_Y_FOCUS_STYLE 1
#define VM_Y_FOCUS_PLOT 2
#define VM_Y_FOCUS_EQUALS 3

typedef enum {
    K_NONE, K_2ND,K_ALPHA,K_MODE,K_DEL,K_CLEAR,K_ENTER,K_UP,K_DOWN,K_LEFT,K_RIGHT,
    K_YEQ,K_WINDOW,K_ZOOM,K_TRACE,K_GRAPH,K_XVAR,K_STAT,K_MATH,K_APPS,K_PRGM,K_VARS,
    K_RECIP,K_SIN,K_COS,K_TAN,K_POWER,K_SQUARE,K_COMMA,K_LPAREN,K_RPAREN,
    K_DIV,K_LOG,K_7,K_8,K_9,K_MUL,K_LN,K_4,K_5,K_6,K_SUB,
    K_STO,K_1,K_2,K_3,K_ADD,K_0,K_DOT,K_NEG,K_ON
} vm_key_t;

typedef enum {
    V_HOME,V_MEMORY,V_RESET,V_RESET_CONFIRM,V_ABOUT,V_MEM_MGMT,
    V_CATALOG,V_MODE,V_MATH,V_PRGM,V_GRAPH,V_YEQ,V_WINDOW,V_ZOOM,V_TRACE,
    V_STAT,V_VARS,V_APPS,V_TEST,V_MATRIX,V_LIST,V_DRAW,V_DISTR,V_FORMAT,
    V_TABLE,V_TBLSET,V_CALC,V_ANGLE,V_FRAC,V_FUNC,V_MTRX,V_YVAR,V_NOTICE,
    V_PRGM_NAME,V_PRGM_EDITOR,V_STAT_PLOTS,V_RECALL,V_LINK,V_GROUP,V_ERROR,V_RESET_RESULT,
    V_FIN_MENU,V_FIN_SOLVER,V_FILE_LIST,V_FILE_INFO,V_FILE_PREVIEW,V_FILE_RENAME,V_FILE_DELETE,V_SETTINGS,V_PLUGINS,V_PLUGIN_DELETE,V_PLUGIN_PAGE,V_BATTERY_MENU,V_BATTERY_EDITOR,V_CHARGE_EDITOR,V_BAR_COLOR
} vm_view_t;

typedef struct {
    vm_view_t view, return_view;
    uint16_t selected, top;
    uint8_t tab, reset_kind;
    bool second, alpha, alpha_lock, insert, degree, result_valid, exit_requested, sleeping;
    uint16_t resets;
    uint8_t caret;
    uint16_t program_count;
    uint8_t active_program;
    /* -1 = FLOAT; 0..9 = FIX digits. Persist across menus. */
    int8_t fix_digits;
    bool blink_enabled, wrap_enabled;
    uint8_t repeat_mode, plugin_disabled, plugin_target, plugin_active;
    uint8_t mode_precision_cursor;
    char program_names[VM_PROGRAM_CAP][VM_PROGRAM_NAME], new_program[VM_PROGRAM_NAME];
    char expression[VM_EXPR], last_entry[VM_EXPR], prev_entry[VM_EXPR];
    char answer[VM_ANSWER], last_answer[VM_ANSWER], prev_answer[VM_ANSWER];
    char notice[VM_NOTICE];
    uint8_t error_kind;
    /* Virtual About strings. Not the host's actual hardware identification. */
    char about_model[VM_ABOUT_TEXT],about_version[VM_ABOUT_TEXT];
    char about_prod[VM_ABOUT_TEXT],about_id[VM_ABOUT_TEXT];
    bool y_enabled[10];
    /* OS 5.8.5 Y= page: equation editor, graph style icon and Plot1..3
       are separate selectable areas. */
    uint8_t y_focus, y_plot_focus, y_style_field;
    bool y_style_open;
    uint8_t y_color[10], y_line_style[10], y_saved_color, y_saved_line;
    uint8_t y_preview_color, y_preview_line;
    uint8_t y_caret[10];
    char y_equations[10][VM_Y_EXPR_LEN];
    uint8_t format_choice[10];
    uint8_t tbl_choice[2];
    uint8_t plots_enabled[3];
    long double fin_value[7];
    char fin_input[32];
    uint8_t fin_field;
    bool fin_edit,fin_begin,fin_reveal;
} vm_t;

void vm_init(vm_t *v);
void vm_settings_load(vm_t *v);
void vm_plugin_refresh(vm_t *v);
unsigned int vm_plugin_count(void);
const char *vm_plugin_label(const vm_t *v,unsigned int i);
const char *vm_plugin_name(unsigned int i);
const char *vm_plugin_home_text(const vm_t *v);
unsigned int vm_plugin_app_count(const vm_t *v);
unsigned int vm_plugin_app_index(const vm_t *v,unsigned int ordinal);
const char *vm_plugin_app_label(const vm_t *v,unsigned int ordinal);
const char *vm_plugin_page_title(const vm_t *v);
const char *vm_plugin_page_line(const vm_t *v,unsigned int line);
bool vm_plugin_page_effect(const vm_t *v,uint16_t *bar,uint8_t *brightness);
unsigned int vm_plugin_page_actions(const vm_t *v);
void vm_key(vm_t *v, vm_key_t key);
unsigned int vm_menu_size(const vm_t *v);
const char *vm_menu_item(const vm_t *v, unsigned int i);
const char *vm_title(const vm_t *v);
char vm_alpha_character(vm_key_t key);
int vm_eval(const char *expression, bool degrees, double *answer);
int vm_eval_graph_x(const char *expression, long double x, bool degrees, long double *y);
void vm_format(double x, char *out, unsigned int len);
void vm_format_precision(long double x, int8_t fixed_digits, char *out, unsigned int len);
unsigned int vm_catalog_symbol_start(void);
unsigned int vm_host_program_count(void);
bool vm_host_program_name(unsigned int index,char out[VM_PROGRAM_NAME]);
#endif
