/* Layout constants shared by target renderer and snapshot tests. */
#ifndef VM_UI_LAYOUT_H
#define VM_UI_LAYOUT_H
#define UI_LCD_W 320
#define UI_LCD_H 240
#define UI_STATUS_H 30
#define UI_STATUS_LABEL_RIGHT 266
#define UI_MOD_X 269
#define UI_MOD_W 17
#define UI_MOD_H 17
#define UI_TAB_Y 37
#define UI_TAB_H 18
#define UI_MENU_FIRST_Y 56
#define UI_ROW_H 20
#define UI_ROWS_ON_SCREEN 9
#define UI_TEXT_X 1
#define UI_HOME_Y 37
#define UI_CURSOR_W 10
#define UI_CURSOR_H 14
#define UI_HISTORY_RULE_GAP 7
/* Based on the user-provided CEmu OS 5.8.5 screenshot (320x240):
 * graph inset x=27..291, y=46..210 inclusive, gray outer background.
 * The plot is divided into 20 tick intervals on each axis. */
#define UI_GRAPH_X 27
#define UI_GRAPH_Y 46
#define UI_GRAPH_W 265
#define UI_GRAPH_H 165
#define UI_GRAPH_CENTER_X 159
#define UI_GRAPH_CENTER_Y 128
#define UI_GRAPH_DIVISIONS 20
#define UI_GRAPH_INSET_XMAX (UI_GRAPH_X+UI_GRAPH_W-1)
#define UI_GRAPH_INSET_YMAX (UI_GRAPH_Y+UI_GRAPH_H-1)
#endif
