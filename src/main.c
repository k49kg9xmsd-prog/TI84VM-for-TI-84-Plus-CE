/* TI84VM Beta - measured OS 5.8.1 panels, native TI-84 Plus CE prototype.
 * This is an App with a simulated TI-OS interface, not a second bootable TI-OS.
 * Exact glyphs are sourced at runtime from the host calculator's OS.
 * Pixel positions are calibrated from reference CE screenshots, and still
 * require screenshot comparison against OS 5.8.5 before a 1:1 claim.
 */
#include <tice.h>
#include <keypadc.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <sys/rtc.h>
#include <sys/util.h>
#include <fileioc.h>
#include <sys/lcd.h>
#include <sys/power.h>
#include "vm.h"
#include <compression.h>
#include "os_menus.h"
#include "os_fixed.h"
#include "mode_delta.h"
#include "y_popup_ref.h"
#include "catalog_tail.h"
#include "error_ref.h"
#include "reset_ref_pixels.h"
#include "ui_layout.h"
#include "cursor_icon.h"
#include "y_reference_sprites.h"

#define VRAM ((volatile uint16_t *)0xD40000)
#define WHITE 0xFFFFu
#define BLACK 0x0000u
#define LTGRAY 0xD69Au
#define MIDGRAY 0x9CD3u
#define GRAPHBG 0xE71Cu /* pale gray from CE screenshot: approx RGB(231,226,231) */
static vm_t vm;
static vm_t before_key;
#define BATTERY_W 16u
#define BATTERY_H 22u
#define BATTERY_GRID_X 20
#define BATTERY_GRID_Y 60
#define BATTERY_CELL 6
static uint16_t battery_pixels[2][BATTERY_W*BATTERY_H],battery_draft[BATTERY_W*BATTERY_H];
static uint16_t battery_original[BATTERY_W*BATTERY_H];
static uint8_t battery_mask[2][(BATTERY_W*BATTERY_H+7u)/8u];
static uint8_t battery_draft_mask[(BATTERY_W*BATTERY_H+7u)/8u];
static uint8_t battery_x,battery_y;
static uint8_t battery_color;
static bool battery_palette_open;
static uint8_t battery_editor_kind;
static bool battery_custom[2];
typedef struct {uint16_t color;const char *name;} bar_color_t;
static const bar_color_t bar_colors[]={
  {0,"OS DEFAULT"},{0x2104u,"CHARCOAL"},{0x4208u,"GRAPHITE"},
  {0x000Fu,"NAVY"},{0x001Fu,"BLUE"},{0x0455u,"TEAL"},
  {0x03A0u,"EMERALD"},{0x0200u,"FOREST"},{0x5340u,"OLIVE"},
  {0x8320u,"MUSTARD"},{0xC280u,"ORANGE"},{0xA000u,"RED"},
  {0x800Au,"WINE"},{0x600Fu,"PURPLE"},{0x681Fu,"VIOLET"},
  {0xA00Fu,"MAGENTA"},{0x0415u,"CYAN"},{0x426Du,"SLATE"},
  {0x528Au,"STEEL"},{0x6200u,"BROWN"},{0xA14Bu,"ROSE"},
  {0x280Fu,"INDIGO"},{0x0450u,"DARK AQUA"},{0x0000u,"BLACK"},
  {0x52AAu,"GRAY"}
};
static uint16_t native_bar_color;
static uint8_t saved_bar_choice,preview_bar_choice;
static bool plugin_bar_active,plugin_bright_active;
static uint16_t plugin_bar_color;
static uint8_t plugin_original_brightness;
static uint8_t resource_handle;
static void resource_reloaded_after_gc(void){
  if(resource_handle)os_menu_pixels=ti_GetDataPtr(resource_handle);
}
static void guest_detach_resources(void){
  /* FileIOC is a LibLoad library. Finish all calls to it before InsertMem
     moves the live library segment inside userMem. */
  ti_SetGCBehavior(NULL,NULL);
  if(resource_handle){ti_Close(resource_handle);resource_handle=0;}
}
static bool guest_restore_resources(void){
  resource_handle=ti_Open("TVMSPRT","r");
  if(!resource_handle)return false;
  os_menu_pixels=ti_GetDataPtr(resource_handle);
  ti_SetGCBehavior(NULL,resource_reloaded_after_gc);
  return true;
}
extern int vm_guest_allocate(unsigned int size);
extern void vm_guest_run_asm(void) __attribute__((noreturn));
extern void vm_guest_run_basic(void) __attribute__((noreturn));
extern void guest_vector_ret(void),guest_vector_putaway(void),guest_vector_restore(void);
void (*const vm_guest_vectors[6])(void)={
  guest_vector_ret,(void (*)(void))0x020850,guest_vector_putaway,
  guest_vector_restore,guest_vector_ret,guest_vector_ret
};

static bool request_asm_direct(const char *name){
  uint8_t type,h;const unsigned char *data;unsigned int size;bool archived;
  for(type=OS_TYPE_PRGM;type<=OS_TYPE_PROT_PRGM;type++){
    h=ti_OpenVar(name,"r",type);
    if(!h)continue;
    size=(unsigned int)ti_GetSize(h);
    data=ti_GetDataPtr(h);
    if(size<3 || data[0]!=0xEF || data[1]!=0x7B){ti_Close(h);return false;}
    archived=ti_IsArchived(h);
    ti_Close(h);
    size-=2;
    guest_detach_resources();
    if(!vm_guest_allocate(size)){guest_restore_resources();return false;}
    memcpy((void *)0xD1A881,data+2+(archived?0:size),size);
    os_ClrLCDFull();
    vm_guest_run_asm();
  }
  return false;
}

static bool request_basic_direct(const char *name){
  uint8_t type,h,temporary,run_type;
  const unsigned char *data;
  unsigned int size;
  bool archived;
  for(type=OS_TYPE_PRGM;type<=OS_TYPE_PROT_PRGM;type++){
    h=ti_OpenVar(name,"r",type);
    if(!h)continue;
    data=ti_GetDataPtr(h);
    size=(unsigned int)ti_GetSize(h);
    archived=ti_IsArchived(h);
    if(size>=2&&data[0]==0xEF&&data[1]==0x7B){ti_Close(h);return false;}
    if(archived){
      ti_Close(h);
      ti_DeleteVar("TVMGTMP",OS_TYPE_TMP_PRGM);
      h=ti_OpenVar(name,"r",type);
      if(!h)return false;
      temporary=ti_OpenVar("TVMGTMP","w",OS_TYPE_TMP_PRGM);
      if(!temporary){ti_Close(h);return false;}
      if(ti_Write(ti_GetDataPtr(h),size,1,temporary)!=1){
        ti_Close(h);ti_Close(temporary);return false;
      }
      ti_Close(h);ti_Close(temporary);
      run_type=OS_TYPE_TMP_PRGM;
    }else {ti_Close(h);run_type=type;}
    guest_detach_resources();
    os_ClrLCDFull();
    memset(os_OP1,0,9);
    os_OP1[0]=run_type;
    memcpy(os_OP1+1,archived?"TVMGTMP":name,archived?7:strlen(name));
    vm_guest_run_basic();
  }
  return false;
}

static bool request_program(const char *name){
  if(request_asm_direct(name))return true;
  if(request_basic_direct(name))return true;
  return false;
}
static unsigned char os_row_buffer[8704];
static uint16_t os_fixed_line[320];
static uint16_t os_sprite_ink=BLACK;
static void draw_os_sprite(unsigned int id,int x,int y);
/* About uses synthetic VM values only. The old TVMINFO file may contain
 * user-entered identification, so it is never read and is removed on launch. */
static void generate_virtual_about(void){
  srandom(rtc_Time());
  snprintf(vm.about_id,sizeof(vm.about_id),"%05u-%05X",
           (unsigned)(random()%100000u),(unsigned)(random()&0xFFFFFu));
  ti_Delete("TVMINFO");
}

#define INTRO_MAGIC 0x314D5654UL
static bool intro_was_shown(void){
  uint32_t value=0;
  uint8_t h=ti_Open("TVMBOOT","r");
  if(!h)return false;
  if(ti_GetSize(h)!=sizeof(value)||ti_Read(&value,sizeof(value),1,h)!=1)value=0;
  ti_Close(h);
  return value==INTRO_MAGIC;
}
static void mark_intro_shown(void){
  const uint32_t value=INTRO_MAGIC;
  uint8_t h=ti_Open("TVMBOOT","w");
  if(!h)return;
  if(ti_Write(&value,sizeof(value),1,h)==1)ti_SetArchiveStatus(true,h);
  ti_Close(h);
}

/* The display is 320*240 pixels, direct 16-bit BGR565 frame buffer. */
static void rect(int x,int y,int w,int h,uint16_t c){
  int xx,yy;
  if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
  if(x+w>UI_LCD_W)w=UI_LCD_W-x;
  if(y+h>UI_LCD_H)h=UI_LCD_H-y;
  if(w<=0||h<=0)return;
  /* Clearing VRAM one pixel at a time was very expensive on eZ80.
   * Use CEdev's assembly-optimized memset for our black/white UI regions.
   * Leave arbitrary BGR565 colors on the 16-bit path. */
  if(c==WHITE || c==BLACK){
    for(yy=y;yy<y+h;yy++){
      void *dst=(void *)(VRAM+(unsigned int)yy*UI_LCD_W+x);
      memset(dst,c==WHITE?0xFF:0x00,(size_t)w*2u);
    }
  }else{
    for(yy=y;yy<y+h;yy++){
      volatile uint16_t *dst=VRAM+(unsigned int)yy*UI_LCD_W+x;
      for(xx=0;xx<w;xx++)dst[xx]=c;
    }
  }
}
static void hline(int x,int y,int w,uint16_t c){rect(x,y,w,1,c);}
static void vline(int x,int y,int h,uint16_t c){rect(x,y,1,h,c);}
static unsigned int tw(const char *s,bool large){
  os_FontSelect(large?os_LargeFont:os_SmallFont);
  return (unsigned int)os_FontGetWidth(s);
}
/* TIOS's actual OS fonts / rendered glyph spacing are used at runtime. */
static void print_at(int x,int y,const char *s,bool large,uint16_t fg,uint16_t bg){
  char clip[64];unsigned int i=0;
  if(!s||!s[0]||x<0||x>=UI_LCD_W||y<0||y>=UI_LCD_H)return;
  os_FontSelect(large?os_LargeFont:os_SmallFont);
  /* Avoid O(n^2) width calculation on every redraw: first measure
   * once; only binary-search for clipping if the text actually overflows. */
  while(s[i]&&i<sizeof(clip)-1){clip[i]=s[i];i++;}
  clip[i]=0;
  if(os_FontGetWidth(clip)>(unsigned int)(UI_LCD_W-x)){
    unsigned int lo=0,hi=i;
    while(lo<hi){
      unsigned int mid=(lo+hi+1u)/2u;
      char saved=clip[mid];clip[mid]=0;
      bool fits=os_FontGetWidth(clip)<=(unsigned int)(UI_LCD_W-x);
      clip[mid]=saved;
      if(fits)lo=mid;else hi=mid-1u;
    }
    clip[lo]=0;
  }
  if(!clip[0])return;
  os_TextFGColor=fg;os_TextBGColor=bg;
  os_SetDrawFGColor(fg);os_SetDrawBGColor(bg);
  os_FontDrawText(clip,(uint16_t)x,(uint8_t)y);
}
static void tx(int x,int y,const char *s){print_at(x,y,s,true,BLACK,WHITE);}
static void body(void){rect(0,UI_STATUS_H,UI_LCD_W,UI_LCD_H-UI_STATUS_H,WHITE);}
typedef struct {char magic[8];uint8_t mask[(BATTERY_W*BATTERY_H+7u)/8u];
                uint16_t pixels[BATTERY_W*BATTERY_H];} battery_disk_t;
static const uint16_t battery_palette[]={
  WHITE,0xC618u,0x8410u,BLACK,0xF800u,
  0xFC10u,0xFD20u,0xFFE0u,0xAFE5u,0x04E0u,
  0x0200u,0x07FFu,0x0455u,0x001Fu,0x000Fu,
  0x681Fu,0xF81Fu,0xFBEFu,0xA145u,0x8200u,
  0x1084u,0x52AAu,0x7BEFu,0xA514u,0u
};
static const char *const battery_color_names[]={
  "WHITE","SILVER","GRAY","BLACK","RED",
  "CORAL","ORANGE","YELLOW","LIME","GREEN",
  "FOREST","CYAN","TEAL","BLUE","NAVY",
  "VIOLET","MAGENTA","PINK","ROSE","BROWN",
  "NIGHT","SLATE","SKY","SAND","BG"
};
#define BATTERY_COLOR_COUNT (sizeof(battery_palette)/sizeof(battery_palette[0]))
#define BATTERY_BG_COLOR (BATTERY_COLOR_COUNT-1u)
static bool battery_masked(const uint8_t *mask,unsigned int index){
  return (mask[index/8u]&(1u<<(index%8u)))!=0;
}
static void battery_set_mask(uint8_t *mask,unsigned int index,bool on){
  if(on)mask[index/8u]|=(uint8_t)(1u<<(index%8u));
  else mask[index/8u]&=(uint8_t)~(1u<<(index%8u));
}
static void battery_capture_native(void){
  unsigned int x,y,index;
  os_DrawStatusBar();
  memcpy(battery_draft_mask,battery_mask[battery_editor_kind],sizeof(battery_draft_mask));
  for(y=0;y<BATTERY_H;y++)for(x=0;x<BATTERY_W;x++){
    index=y*BATTERY_W+x;
    battery_original[index]=VRAM[(y+2u)*UI_LCD_W+303u+x];
    battery_draft[index]=battery_masked(battery_mask[battery_editor_kind],index)?
                         battery_pixels[battery_editor_kind][index]:battery_original[index];
  }
}
static const char *battery_var_name(unsigned int kind){return kind?"TVMCHRG":"TVMBATT";}
static void battery_load(unsigned int kind){
  battery_disk_t data;uint8_t h=ti_Open(battery_var_name(kind),"r");
  battery_custom[kind]=false;memset(battery_pixels[kind],0,sizeof(battery_pixels[kind]));
  memset(battery_mask[kind],0,sizeof(battery_mask[kind]));
  if(!h)return;
  if(ti_GetSize(h)==sizeof(data)&&ti_Read(&data,sizeof(data),1,h)==1&&
     !memcmp(data.magic,"TVMBAT3",8)){
    memcpy(battery_mask[kind],data.mask,sizeof(battery_mask[kind]));
    memcpy(battery_pixels[kind],data.pixels,sizeof(battery_pixels[kind]));
    battery_custom[kind]=true;
  }
  ti_Close(h);
}
static bool battery_save(void){
  battery_disk_t data;uint8_t h;
  memcpy(data.magic,"TVMBAT3",8);
  memcpy(data.mask,battery_draft_mask,sizeof(data.mask));
  memcpy(data.pixels,battery_draft,sizeof(data.pixels));
  h=ti_Open(battery_var_name(battery_editor_kind),"w");if(!h)return false;
  if(ti_Write(&data,sizeof(data),1,h)!=1){ti_Close(h);return false;}
  if(!ti_SetArchiveStatus(true,h)){ti_Close(h);return false;}
  ti_Close(h);
  memcpy(battery_mask[battery_editor_kind],battery_draft_mask,sizeof(battery_draft_mask));
  memcpy(battery_pixels[battery_editor_kind],battery_draft,sizeof(battery_draft));
  battery_custom[battery_editor_kind]=true;return true;
}
static void battery_restore_native(void){
  ti_Delete(battery_var_name(battery_editor_kind));
  memset(battery_mask[battery_editor_kind],0,sizeof(battery_mask[battery_editor_kind]));
  memset(battery_pixels[battery_editor_kind],0,sizeof(battery_pixels[battery_editor_kind]));
  battery_custom[battery_editor_kind]=false;
}
static void battery_status_overlay(uint16_t bar){
  unsigned int x,y,index;
  bool editing=vm.view==V_BATTERY_EDITOR||vm.view==V_CHARGE_EDITOR;
  unsigned int kind=boot_BatteryCharging()?1u:0u;
  if(!battery_custom[kind]&&!editing)return;
  if(editing)rect(302,0,18,UI_STATUS_H,bar);
  for(y=0;y<BATTERY_H;y++)for(x=0;x<BATTERY_W;x++){
    index=y*BATTERY_W+x;
    if(editing)
      rect(303+(int)x,2+(int)y,1,1,battery_draft[index]);
    else if(battery_masked(battery_mask[kind],index))
      rect(303+(int)x,2+(int)y,1,1,battery_pixels[kind][index]);
  }
}
typedef struct {char magic[8];uint8_t choice;} bar_disk_t;
static uint16_t bar_color_for(unsigned int choice){
  return choice?bar_colors[choice].color:native_bar_color;
}
static void bar_load(void){
  bar_disk_t data;uint8_t h;
  native_bar_color=os_StatusBarBGColor;
  saved_bar_choice=preview_bar_choice=0;
  h=ti_Open("TVMBAR","r");if(!h)return;
  if(ti_GetSize(h)==sizeof(data)&&ti_Read(&data,sizeof(data),1,h)==1&&
     !memcmp(data.magic,"TVMBAR1",8)&&
     data.choice<sizeof(bar_colors)/sizeof(bar_colors[0]))
    saved_bar_choice=preview_bar_choice=data.choice;
  ti_Close(h);
  os_StatusBarBGColor=bar_color_for(saved_bar_choice);
}
static bool bar_save(void){
  bar_disk_t data;uint8_t h;
  if(!preview_bar_choice){
    ti_Delete("TVMBAR");saved_bar_choice=0;return true;
  }
  memcpy(data.magic,"TVMBAR1",8);data.choice=preview_bar_choice;
  h=ti_Open("TVMBAR","w");if(!h)return false;
  if(ti_Write(&data,sizeof(data),1,h)!=1){ti_Close(h);return false;}
  if(!ti_SetArchiveStatus(true,h)){ti_Close(h);return false;}
  ti_Close(h);saved_bar_choice=preview_bar_choice;
  os_StatusBarBGColor=bar_color_for(saved_bar_choice);
  return true;
}
/* English-only development marker. Use the calculator's native small font;
 * no CJK bitmap is embedded or required. */
static void draw_unfinished_centered(int y,uint16_t bg){
  static const char marker[]="NOT IMPLEMENTED";
  int x=(UI_LCD_W-(int)tw(marker,false))/2;
  print_at(x,y,marker,false,BLACK,bg);
}
static void draw_unfinished_right(int y,uint16_t bg){
  static const char marker[]="NOT IMPLEMENTED";
  int x=UI_LCD_W-3-(int)tw(marker,false);
  print_at(x,y,marker,false,BLACK,bg);
}
/* The incomplete-page indicator deliberately belongs to the VM, not TI-OS.
 * Main menus retain their authentic layout; only actual placeholders receive
 * a distinct label, so a blank page is never mistaken for a crash. */
static bool blank_placeholder(vm_view_t v){
  switch(v){
    case V_TABLE:
    case V_YVAR:case V_MTRX:case V_FUNC:
      return true;
    default:return false;
  }
}
static bool partial_menu(vm_view_t v){
  switch(v){
    case V_MATH:case V_MODE:case V_STAT:
    case V_FRAC:
    case V_RESET:case V_ANGLE:return true;
    default:return false;
  }
}
static void draw_2nd_glyph(int x,int y){
  int r,c;
  for(r=0;r<14;r++){
    uint16_t bits=vm_2nd_icon[r];
    for(c=0;c<10;c++)
      if(bits&(1u<<(9-c)))rect(x+c,y+r,1,1,BLACK);
  }
}
static void draw_placeholder_cursor(void){
  /* The default is a 10x14 block at (2,37). 2nd uses OS 5.8.5's
   * actual monochrome cursor pixels; no extra mode word by the battery. */
  rect(2,37,10,14,WHITE);
  if(vm.second)draw_2nd_glyph(2,37);
  else rect(2,37,10,14,BLACK);
}
static void draw_blank_placeholder(void){
  body();
  draw_placeholder_cursor();
  draw_unfinished_centered(115,WHITE);
}


/* Crucial: battery shape (silver casing + green/red level) AND yellow charge
   indicator are drawn by the native TI-OS status bar routine. Do not replace
   them with a homemade rectangle as older offline previews mistakenly did.
   The charging symbol appears only when the host battery is charging.
   Virtual mode labels are confined to x < 286 to avoid this area. */
static void draw_status(void){
  /* TI-OS routines can restore their own status color while drawing HOME.
     The saved VM choice is the source of truth, not that mutable OS field. */
  uint16_t bar=plugin_bar_active?plugin_bar_color:bar_color_for(preview_bar_choice);
  unsigned int sx,sy;
  uint16_t native_bg;
  os_StatusBarBGColor=bar;
  os_DrawStatusBar();
  /* TI-OS paints its built-in gray regardless of os_StatusBarBGColor.
     Replace only that exact background pixel, preserving native text,
     battery details and the thin lower edge. */
  if(preview_bar_choice||plugin_bar_active){
    native_bg=VRAM[0];
    for(sy=0;sy<UI_STATUS_H;sy++){
      volatile uint16_t *row=VRAM+sy*UI_LCD_W;
      for(sx=0;sx<UI_LCD_W;sx++)if(row[sx]==native_bg)row[sx]=bar;
    }
  }
  /* Replacing the whole native status bar with a guessed bitmap introduced
     large errors in v0.3. Draw only the virtual mode label if necessary. */
  if(vm.view==V_PRGM){
    /* The TI-BASIC line is rendered in the lower status-bar band. */
    static const uint8_t label[13][11]={{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0}};unsigned int dx,dy;
    rect(0,17,84,13,bar);
    for(dy=0;dy<13;dy++)for(dx=0;dx<84;dx++)if(label[dy][dx/8]&(1u<<(7-dx%8)))rect((int)dx,17+(int)dy,1,1,WHITE);
  }
  /* The primary mode text (including FIX status) follows VM settings. */
  if(vm.fix_digits>=0){
    char fmt[36];
    snprintf(fmt,sizeof(fmt),"NORMAL FIX%d AUTO REAL %s MP",
             (int)vm.fix_digits,vm.degree?"DEGREE":"RADIAN");
    rect(0,0,UI_STATUS_LABEL_RIGHT,17,bar);
    print_at(2,3,fmt,false,WHITE,bar);
  }else if(vm.degree){
    rect(0,0,UI_STATUS_LABEL_RIGHT,UI_STATUS_H,bar);
    print_at(2,4,"NORMAL FLOAT AUTO REAL DEGREE MP",false,WHITE,bar);
  }
  if(vm.view==V_YEQ && vm.y_style_open){
    rect(0,17,268,12,bar);
    print_at(1,18,"PRESS < OR > TO SELECT AN OPTION",false,WHITE,bar);
  }
  battery_status_overlay(bar);
  /* Never paste textual "2nd"/"A" into the battery corner.
   * The key modes are indicated by the blinking HOME cursor; the host
   * TI-OS status bar may also supply its own authentic indicator glyph. */
}
/* The original uses a compact black inverse tab, not a full gray header. */
static void inverted_tab(const char *label,int x,int y){
  int w=(int)tw(label,true)+2;
  if(w>UI_LCD_W-x)w=UI_LCD_W-x;
  rect(x,y,w,UI_TAB_H,BLACK);
  print_at(x+1,y+1,label,true,WHITE,BLACK);
}
static void tab_row(const char *const *tabs,int n,int active){
  int x=0,i;
  for(i=0;i<n;i++){
    int w=(int)tw(tabs[i],true)+2;
    if(i==active)inverted_tab(tabs[i],x,UI_TAB_Y);
    else tx(x-1,UI_TAB_Y+1,tabs[i]);
    x+=w+10;
    if(x>UI_LCD_W-30)break;
  }
}
static void draw_battery_cell(unsigned int x,unsigned int y){
  int px=BATTERY_GRID_X+(int)x*BATTERY_CELL;
  int py=BATTERY_GRID_Y+(int)y*BATTERY_CELL;
  rect(px,py,BATTERY_CELL,BATTERY_CELL,LTGRAY);
  rect(px+1,py+1,BATTERY_CELL-2,BATTERY_CELL-2,battery_draft[y*BATTERY_W+x]);
  if(x==battery_x&&y==battery_y){
    hline(px,py,BATTERY_CELL,0x001Fu);
    hline(px,py+BATTERY_CELL-1,BATTERY_CELL,0x001Fu);
    vline(px,py,BATTERY_CELL,0x001Fu);
    vline(px+BATTERY_CELL-1,py,BATTERY_CELL,0x001Fu);
  }
}
static void draw_battery_palette(void){
  rect(180,60,140,70,WHITE);
  print_at(183,62,"ALPHA: COLORS",false,BLACK,WHITE);
  rect(183,83,24,24,BLACK);
  rect(185,85,20,20,battery_color==BATTERY_BG_COLOR?
       os_StatusBarBGColor:battery_palette[battery_color]);
  print_at(212,88,battery_color_names[battery_color],false,BLACK,WHITE);
}
static void draw_battery_color_menu(void){
  unsigned int i;
  body();inverted_tab("DRAW COLORS",0,UI_TAB_Y);
  for(i=0;i<BATTERY_COLOR_COUNT;i++){
    int x=14+(int)(i%5u)*61,y=58+(int)(i/5u)*29;
    rect(x,y,48,23,i==battery_color?0x001Fu:LTGRAY);
    rect(x+2,y+2,44,19,i==BATTERY_BG_COLOR?
         os_StatusBarBGColor:battery_palette[i]);
  }
  rect(0,207,UI_LCD_W,33,WHITE);
  print_at(3,209,battery_color_names[battery_color],false,BLACK,WHITE);
  print_at(3,226,"ARROWS SELECT  ENTER OK  CLEAR BACK",false,BLACK,WHITE);
}
static void draw_battery_editor(void){
  unsigned int x,y;
  if(battery_palette_open){draw_battery_color_menu();return;}
  body();inverted_tab(battery_editor_kind?"CHARGING ICON":"BATTERY ICON",0,UI_TAB_Y);
  for(y=0;y<BATTERY_H;y++)for(x=0;x<BATTERY_W;x++)draw_battery_cell(x,y);
  draw_battery_palette();
  print_at(2,211,"ARROWS MOVE  ENTER DRAW  DEL ERASE",false,BLACK,WHITE);
  print_at(2,226,"MODE SAVE  CLEAR CANCEL  2nd+DEL OS",false,BLACK,WHITE);
}
static void draw_bar_swatch(unsigned int index){
  int x=12+(int)(index%5u)*61;
  int y=64+(int)(index/5u)*29;
  rect(x,y,55,25,index==preview_bar_choice?BLACK:LTGRAY);
  rect(x+2,y+2,51,21,bar_color_for(index));
  if(index==preview_bar_choice){
    hline(x+1,y+1,53,WHITE);hline(x+1,y+23,53,WHITE);
    vline(x+1,y+1,23,WHITE);vline(x+53,y+1,23,WHITE);
  }
}
static void draw_bar_color(void){
  unsigned int i;
  body();inverted_tab("TOP BAR COLOR",0,UI_TAB_Y);
  for(i=0;i<sizeof(bar_colors)/sizeof(bar_colors[0]);i++)draw_bar_swatch(i);
  print_at(12,212,bar_colors[preview_bar_choice].name,false,BLACK,WHITE);
  print_at(12,227,"ARROWS MOVE  ENTER SAVE  CLEAR CANCEL",false,BLACK,WHITE);
}
static void catalog_pointer(int x,int y){
  static const uint16_t rows[18]={0,0,0,128,192,224,240,248,252,252,248,240,224,192,128,0,0,0};int dx,dy;
  for(dy=0;dy<18;dy++)for(dx=0;dx<10;dx++)if(rows[dy]&(1u<<(9-dx)))rect(x+dx,y-5+dy,1,1,BLACK);
}
/* Small custom glyphs for the CATALOG symbol tail.  Never print ASCII
   "<=", "!=", "theta" as if it were the calculator glyph!  All strokes
   are drawn in the native 320x240 coordinate space. */
static void glyph_line(int x,int y,int dx,int dy){
  int steps=dx<0?-dx:dx,a=dy<0?-dy:dy,i;
  if(a>steps)steps=a;
  if(!steps){rect(x,y,1,1,BLACK);return;}
  for(i=0;i<=steps;i++)rect(x+dx*i/steps,y+dy*i/steps,1,1,BLACK);
}
static void draw_catalog_tail(int x,int y,unsigned int index){
  unsigned int yy,xx;
  if(index>=38)return;
  for(yy=0;yy<18;yy++){
    uint64_t bits=catalog_tail_rows[index][yy];
    for(xx=0;xx<64;xx++)if(bits&(1ULL<<xx))rect(x+(int)xx,y+(int)yy,1,1,BLACK);
  }
}
static int menu_start_y(void){
  return vm.view==V_CATALOG?59:vm.view==V_PRGM?58:UI_MENU_FIRST_Y + (vm.view==V_MEMORY ? 3 : 0);
}
static void settings_icon(int kind,int x,int y){
  /* Compact monochrome symbols keep the calculator's native menu style. */
  if(kind==0 || kind==3 || kind==4){
    rect(x,y+3,13,10,BLACK);rect(x+13,y+6,2,4,BLACK);
    rect(x+2,y+5,9,6,WHITE);
    if(kind==4){rect(x+5,y+6,3,2,BLACK);rect(x+7,y+8,2,2,BLACK);}
    else rect(x+3,y+6,5,4,BLACK);
  }else if(kind==1){
    rect(x,y+3,14,11,BLACK);rect(x+2,y+5,10,7,0x001Fu);
    rect(x+3,y+6,3,2,WHITE);
  }else if(kind==2){
    rect(x+1,y+8,2,5,BLACK);rect(x+4,y+5,2,8,BLACK);
    rect(x+7,y+2,2,11,BLACK);rect(x+10,y,2,13,BLACK);
  }
}
static void draw_item(int index,const char *label,bool selected){
  const int y=menu_start_y()+index*UI_ROW_H;
  char id[4]={0,0,0,0};
  const char *content=label;
  int idw;
  if(y+UI_ROW_H>UI_LCD_H)return;
  if(vm.view==V_PRGM && vm.tab<2){
    char number[8];
    snprintf(number,sizeof number,"%u.",(unsigned int)(vm.top+index+1));
    idw=(int)tw(number,true);
    if(selected){
      rect(0,y-1,idw+1,UI_ROW_H-1,BLACK);
      print_at(0,y,number,true,WHITE,BLACK);
    }else tx(0,y,number);
    tx(idw+3,y,label);
    return;
  }
  if(vm.view==V_CALC && index==6){
    if(selected){rect(0,y-1,(int)tw("7:",true)+1,UI_ROW_H-1,BLACK);
       print_at(0,y,"7:",true,WHITE,BLACK);}
    else tx(0,y,"7:");
    /* Integral is a glyph, not an unsupported UTF-8 font character. */
    glyph_line(33,y+4,-3,12);glyph_line(30,y+16,-4,2);
    glyph_line(33,y+4,4,-2);
    tx(41,y,"f(x)dx");return;
  }
  if(vm.view==V_CATALOG){
    if(selected)catalog_pointer(2,y+3);
    if((unsigned int)(vm.top+index)>=vm_catalog_symbol_start()){
      draw_catalog_tail(13,y,(unsigned int)(vm.top+index)-vm_catalog_symbol_start());
    }else tx(13,y,label);
    return;
  }
  if(label[0] && label[1]==':'){
    id[0]=label[0];id[1]=':';content=label+2;
  }
  if(vm.view==V_SETTINGS || vm.view==V_BATTERY_MENU){
    int kind=vm.view==V_SETTINGS?index:index==0?3:4;
    if(selected){rect(0,y-1,22,UI_ROW_H-1,BLACK);
      print_at(0,y,id,true,WHITE,BLACK);}
    else tx(0,y,id);
    settings_icon(kind,27,y+1);
    tx(47,y,content);
    return;
  }
  if(id[0]){
    idw=(int)tw(id,true);
    if(selected){
      rect(0,y-1,idw+1,UI_ROW_H-1,BLACK);
      print_at(0,y,id,true,WHITE,BLACK);
    }else tx(0,y,id);
    tx(idw+1,y,content);
  }else{
    if(selected)inverted_tab(content,0,y-1);
    else tx(1,y,content);
  }
}
static void draw_menu_rows(void){
  unsigned int i,sz=vm_menu_size(&vm);
  /* Preserve tabs, header and battery while scrolling a long catalogue. */
  rect(0,menu_start_y()-1,UI_LCD_W,UI_LCD_H-menu_start_y()+1,WHITE);
  for(i=vm.top;i<sz && i<(unsigned int)vm.top+UI_ROWS_ON_SCREEN;i++)
    draw_item((int)(i-vm.top),vm_menu_item(&vm,i),i==vm.selected);
  if(sz>(unsigned int)vm.top+UI_ROWS_ON_SCREEN && vm.view!=V_CATALOG)
    tx(0,menu_start_y()+(UI_ROWS_ON_SCREEN-1)*UI_ROW_H,"v");
}
/* Single-line CATALOG scroll: move eight row bands in VRAM and repaint only
 * two rows. Avoid full-screen redraw and OS font calls for all nine entries. */
static void scroll_one_row(const vm_t *old){
  unsigned int first=menu_start_y();
  unsigned int stride=UI_LCD_W;
  unsigned int row_bytes=(unsigned int)UI_ROW_H*stride*2u;
  unsigned int copy_bytes=(unsigned int)(UI_ROWS_ON_SCREEN-1)*row_bytes;
  uint8_t *base=(uint8_t *)(VRAM+first*stride);
  int selected_row=(int)vm.selected-(int)vm.top;
  int old_row=(int)old->selected-(int)vm.top;
  if(vm.top==old->top+1){
    memmove(base,base+row_bytes,copy_bytes);
    rect(0,menu_start_y()+(UI_ROWS_ON_SCREEN-1)*UI_ROW_H-1,
         UI_LCD_W,UI_ROW_H+1,WHITE);
    if(old_row>=0&&old_row<UI_ROWS_ON_SCREEN-1){
      rect(0,menu_start_y()+old_row*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
      draw_item(old_row,vm_menu_item(&vm,old->selected),false);
    }
    draw_item(UI_ROWS_ON_SCREEN-1,vm_menu_item(&vm,vm.top+UI_ROWS_ON_SCREEN-1),
              selected_row==UI_ROWS_ON_SCREEN-1);
  }else if(old->top==vm.top+1){
    memmove(base+row_bytes,base,copy_bytes);
    rect(0,menu_start_y()-1,UI_LCD_W,UI_ROW_H+1,WHITE);
    if(old_row>=1&&old_row<UI_ROWS_ON_SCREEN){
      rect(0,menu_start_y()+old_row*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
      draw_item(old_row,vm_menu_item(&vm,old->selected),false);
    }
    draw_item(0,vm_menu_item(&vm,vm.top),selected_row==0);
  }else draw_menu_rows();
}
static void draw_list(void){
  const char *name=vm_title(&vm);
  body();
  /* TI's MATH / STAT / PRGM / VARS menus have *tabs*, not headers. */
  if(vm.view==V_MATH){
    static const char *const tabs[]={"MATH","NUM","CMPLX","PROB"};
    tab_row(tabs,4,vm.tab);
  }else if(vm.view==V_STAT){
    static const char *const tabs[]={"EDIT","CALC","TESTS"};
    tab_row(tabs,3,vm.tab);
  }else if(vm.view==V_PRGM){
    unsigned int x,y;os_sprite_ink=BLACK;draw_os_sprite(os_prgm_header,0,30);
    if(vm.tab){int target=vm.tab==1?60:120,w=vm.tab==1?50:38;
      for(y=37;y<55;y++){
        for(x=0;x<50;x++)VRAM[y*320+x]^=0xFFFF;
        for(x=(unsigned int)target;x<(unsigned int)(target+w);x++)VRAM[y*320+x]^=0xFFFF;
      }
    }
  }else if(vm.view==V_VARS){
    static const char *const tabs[]={"VARS","Y-VARS"};
    tab_row(tabs,2,vm.tab);
  }else if(vm.view==V_CATALOG)tx(1,39,name);
  else inverted_tab(name,0,UI_TAB_Y+(vm.view==V_MEMORY?6:0));
  draw_menu_rows();
  if(partial_menu(vm.view))draw_unfinished_right(219,WHITE);
}
/* Every ENTER creates a two-row result plus a dotted separator.  The
 * editing cursor begins below that separator, as in the reference screenshot.
 * A caret inside an existing expression is overlaid, but not permanently
 * baked into the text pixels: it can be hidden and redrawn independently. */
#define CURSOR_HALF_PERIOD_MS 500u
static bool cursor_visible=true;
/* Save the pixels under the blinking cursor. Hiding the cursor now restores
 * just 11x17 pixels instead of calling OS text drawing for the whole line. */
static uint16_t cursor_backup[UI_CURSOR_W*UI_CURSOR_H];
static bool cursor_drawn=false;
static int cursor_saved_x=0,cursor_saved_y=0;
static bool input_footprint=false;
static int input_drawn_y=0,input_drawn_w=0,input_drawn_h=0;
static void restore_home_cursor(void){
  int x,y;
  if(!cursor_drawn)return;
  for(y=0;y<UI_CURSOR_H;y++){
    volatile uint16_t *dst=VRAM+(unsigned int)(cursor_saved_y+y)*UI_LCD_W+cursor_saved_x;
    for(x=0;x<UI_CURSOR_W;x++)dst[x]=cursor_backup[y*UI_CURSOR_W+x];
  }
  cursor_drawn=false;
}
#include "mp_render.h"
static int home_input_y(void){
  int y=UI_HOME_Y;
  if(vm.prev_entry[0]){
    mp_layout(vm.prev_entry,y,false,false);y+=mp_height+6;
    if(vm.prev_answer[0]){mp_layout(vm.prev_answer,y,false,false);y+=mp_height+5;}
  }
  if(vm.last_entry[0]){
    mp_layout(vm.last_entry,y,false,false);y+=mp_height+6;
    if(vm.last_answer[0]){mp_layout(vm.last_answer,y,false,false);y+=mp_height+5;}
  }
  return y;
}
static void cursor_pos(int *x,int *y){
  mp_layout(vm.expression,home_input_y(),false,true);
  *x=mp_cursor_x;*y=mp_cursor_y;
  if(*x>UI_LCD_W-UI_CURSOR_W)*x=UI_LCD_W-UI_CURSOR_W;
  if(*y>UI_LCD_H-UI_CURSOR_H)*y=UI_LCD_H-UI_CURSOR_H;
}
static void draw_alpha_block(int x,int y){
  static const uint16_t a[14]={1023,1023,1023,975,903,819,819,819,771,819,819,819,1023,1023};
  int dx,dy;
  for(dy=0;dy<14;dy++)for(dx=0;dx<10;dx++)
    rect(x+dx,y+dy,1,1,(a[dy]&(1u<<(9-dx)))?BLACK:WHITE);
}
static void draw_home_cursor(bool show){
  int x,y,dx,dy;
  restore_home_cursor();
  if(!show)return;
  cursor_pos(&x,&y);
  cursor_saved_x=x;cursor_saved_y=y;
  for(dy=0;dy<UI_CURSOR_H;dy++){
    volatile uint16_t *src=VRAM+(unsigned int)(y+dy)*UI_LCD_W+x;
    for(dx=0;dx<UI_CURSOR_W;dx++)
      cursor_backup[dy*UI_CURSOR_W+dx]=src[dx];
  }
  cursor_drawn=true;
  if(vm.second)draw_2nd_glyph(x,y);
  else if(vm.alpha||vm.alpha_lock)draw_alpha_block(x,y);
  else if(mp_fraction_cursor){
    rect(x,y,8,10,BLACK);
    for(dy=2;dy<8;dy++)
      rect(x+2,y+dy,dy==2||dy==7?2:dy==3||dy==6?3:4,1,WHITE);
  }else rect(x,y,mp_cursor_w,mp_cursor_h,BLACK);
}
static void draw_history_rule(int y){
  int x;y-=3;
  /* 2-pixel dots separated by 4 blank pixels, never a continuous rule. */
  for(x=2;x<=315;x+=6)
    rect(x,y,2,2,BLACK);
}
static void draw_home_input(void){
  int y=home_input_y(),w,h;
  restore_home_cursor();
  mp_layout(vm.expression,y,false,true);
  w=mp_width+16;if(w<UI_CURSOR_W+3)w=UI_CURSOR_W+3;
  if(w>UI_LCD_W)w=UI_LCD_W;
  h=mp_height+3;
  if(mp_cursor_y+UI_CURSOR_H-y+2>h)h=mp_cursor_y+UI_CURSOR_H-y+2;
  if(y+h>UI_LCD_H)h=UI_LCD_H-y;
  if(input_footprint)rect(0,input_drawn_y,input_drawn_w,input_drawn_h,WHITE);
  rect(0,y,w,h,WHITE);
  mp_layout(vm.expression,y,true,true);
  input_drawn_y=y;input_drawn_w=w;input_drawn_h=h;input_footprint=true;
  if(cursor_visible)draw_home_cursor(true);
}
static void draw_home_content(bool whole_page){
  int y=UI_HOME_Y,x;
  restore_home_cursor();
  input_footprint=false;
  /* Full erase only on a view change. Ordinary ENTER/CLEAR edits touch
   * the history area (~100px high) instead of the full 212px content. */
  if(whole_page)body();
  else rect(0,UI_HOME_Y,UI_LCD_W,UI_LCD_H-UI_HOME_Y,WHITE);
  if(vm.prev_entry[0]){
    mp_layout(vm.prev_entry,y,true,false);y+=mp_height+6;
    if(vm.prev_answer[0]){
      mp_layout(vm.prev_answer,y,false,false);
      x=UI_LCD_W-(strstr(vm.prev_answer,")/(")?11:7)-mp_width;
      if(x<UI_TEXT_X)x=UI_TEXT_X;
      mp_output(vm.prev_answer,x,y);
      y+=mp_height+5;draw_history_rule(y);
    }
  }
  if(vm.last_entry[0]){
    mp_layout(vm.last_entry,y,true,false);y+=mp_height+6;
    if(vm.last_answer[0]){
      mp_layout(vm.last_answer,y,false,false);
      x=UI_LCD_W-(strstr(vm.last_answer,")/(")?11:7)-mp_width;
      if(x<UI_TEXT_X)x=UI_TEXT_X;
      mp_output(vm.last_answer,x,y);
      y+=mp_height+5;draw_history_rule(y);
    }
  }
  draw_home_input();
  if(!vm.expression[0]&&!vm.last_entry[0]&&!vm.prev_entry[0] &&
     vm_plugin_home_text(&vm)[0])tx(2,UI_LCD_H-20,vm_plugin_home_text(&vm));
}
static void draw_home(void){draw_home_content(true);}
/* OS-measured XOR spans change only the active MODE choice.  The underlying
 * native screenshot stays on screen, so navigation never wipes the page. */
static void mode_apply_delta(unsigned int selected){
  unsigned int phase;
  if(selected==0 || selected>11)return;
  for(phase=0;phase<2;phase++){
    const mode_patch_t *m=&mode_patches[phase?selected:0];
    unsigned int bit=0,yy,xx;
    for(yy=0;yy<m->h;yy++)for(xx=0;xx<m->w;xx++,bit++){
      if(mode_delta_bits[m->offset+(bit>>3)]&(1u<<(7-(bit&7))))
        VRAM[((unsigned int)m->y+yy)*320u+m->x+xx]^=0xFFFFu;
    }
  }
}
/* OS 5.8.1 GRAPH appearance verified in CEmu reference image:
 * status 0..29, gray content 30..239; plot x27..291, y46..210.
 * Unlike the older fullscreen prototype, this restores the real inset.
 * Tick marks are computed at 21 inclusive points per axis with rounding. */
/* Clip a real function segment to the white plot viewport; no drawing in gray
 * margins. This is a deliberately narrow subset of the TI-OS plotter. */
static void graph_segment(int x0,int y0,int x1,int y1,uint16_t col){
  int d=y1-y0,k,lo,hi;
  if(d>UI_GRAPH_H || d<-UI_GRAPH_H)return; /* break across poles */
  lo=y0<y1?y0:y1;hi=y0>y1?y0:y1;
  if(hi<UI_GRAPH_Y||lo>UI_GRAPH_INSET_YMAX)return;
  if(d==0){
    if(y0>=UI_GRAPH_Y&&y0<=UI_GRAPH_INSET_YMAX){
      hline(x0,y0,x1-x0+1,col);
    }
    return;
  }
  for(k=0;k<=x1-x0;k++){
    int t=y0+d*k/(x1-x0?x1-x0:1);
    if(t>=UI_GRAPH_Y&&t<=UI_GRAPH_INSET_YMAX)rect(x0+k,t,1,1,col);
  }
  /* For steep sections fill the vertical pixel gaps. */
  if(x1==x0+1 && hi-lo>1){
    int y;
    for(y=lo;y<=hi;y++)if(y>=UI_GRAPH_Y&&y<=UI_GRAPH_INSET_YMAX)
      rect(x1,y,1,1,col);
  }
}
static void draw_graph_curves(void){
  static const uint16_t colors[8]={0x001Fu,0xF800u,0x0000u,0xF81Fu,
                                   0x07E0u,0xFD20u,0x8000u,0x001Fu};
  unsigned int j;
  for(j=0;j<10;j++){
    int px,prev_y=0,prev_x=0;bool prev_ok=false;
    const char *expression=vm.y_equations[j];
    if(!vm.y_enabled[j]||!expression[0])continue;
    for(px=0;px<UI_GRAPH_W;px++){
      long double xx=-10.0L+(20.0L*px)/(UI_GRAPH_W-1),yy;
      int py;
      if(vm_eval_graph_x(expression,xx,vm.degree,&yy)!=0 || yy>1e6L || yy< -1e6L){
        prev_ok=false;continue;
      }
      py=UI_GRAPH_CENTER_Y-(int)(yy*(UI_GRAPH_H-1)/20.0L);
      if(prev_ok)graph_segment(prev_x,prev_y,UI_GRAPH_X+px,py,colors[vm.y_color[j]%8]);
      else if(py>=UI_GRAPH_Y&&py<=UI_GRAPH_INSET_YMAX)
        rect(UI_GRAPH_X+px,py,1,1,colors[vm.y_color[j]%8]);
      prev_x=UI_GRAPH_X+px;prev_y=py;prev_ok=true;
    }
  }
}
static void draw_graph(void){
  int i;
  rect(0,UI_STATUS_H,UI_LCD_W,UI_LCD_H-UI_STATUS_H,GRAPHBG);
  rect(UI_GRAPH_X,UI_GRAPH_Y,UI_GRAPH_W,UI_GRAPH_H,WHITE);
  hline(UI_GRAPH_X,UI_GRAPH_CENTER_Y,UI_GRAPH_W,BLACK);
  vline(UI_GRAPH_CENTER_X,UI_GRAPH_Y,UI_GRAPH_H,BLACK);
  for(i=0;i<=UI_GRAPH_DIVISIONS;i++){
    int x=UI_GRAPH_X+(i*(UI_GRAPH_W-1)+UI_GRAPH_DIVISIONS/2)/UI_GRAPH_DIVISIONS;
    int y=UI_GRAPH_Y+(i*(UI_GRAPH_H-1)+UI_GRAPH_DIVISIONS/2)/UI_GRAPH_DIVISIONS;
    vline(x,UI_GRAPH_CENTER_Y-3,7,BLACK);
    hline(UI_GRAPH_CENTER_X-3,y,7,BLACK);
  }
  /* Draw supported virtual Y= expressions on top of the native-sized axes.
     Advanced ZOOM / calculus / non-function modes remain separate work. */
  draw_graph_curves();
}
static void draw_prgm_name_cursor(bool show){
  int x=61+(int)tw(vm.new_program,true);
  int y=UI_MENU_FIRST_Y+3;
  if(x>UI_LCD_W-10)x=UI_LCD_W-10;
  rect(x,y,10,14,WHITE);
  if(show){if(vm.alpha||vm.alpha_lock)draw_alpha_block(x,y);
           else rect(x,y,10,14,BLACK);}
}
static void draw_prgm_name(void){
  body();
  tx(1,UI_TAB_Y+2,"PROGRAM");
  tx(1,UI_MENU_FIRST_Y+3,"Name=");
  tx(61,UI_MENU_FIRST_Y+3,vm.new_program);
  draw_prgm_name_cursor(cursor_visible);
}
static void draw_prgm_editor(void){
  body();
  tx(1,UI_TAB_Y,"PROGRAM:");
  if(vm.active_program<vm.program_count)
    tx(137,UI_TAB_Y,vm.program_names[vm.active_program]);
  tx(1,UI_MENU_FIRST_Y,":");
  /* A minimal virtual editor skeleton. No actual TI-BASIC interpreter. */
  draw_unfinished_centered(207,WHITE);
}
/* The About page is deliberately borderless: OS 5.8.5 has no ABOUT tab.
 * Positions are proportionally mapped from the user's 244x184 screenshot. */
static void draw_about(void){
  body();
  tx(2,40,vm.about_model);
  tx(2,60,vm.about_version);
  print_at(2,80,"PROD #:",true,BLACK,WHITE);
  tx(2+(int)tw("PROD #:",true),80,vm.about_prod);
  print_at(2,100,"ID:",true,BLACK,WHITE);
  tx(2+(int)tw("ID:",true),100,vm.about_id);
  tx(2,178,"Help:education.ti.com");
  tx(2,198,"(C)1990-2023");
  tx(40,218,"TEXAS INSTRUMENTS");
}
/* v0.5.3 Y= original-font fidelity: fixed labels and swatch icons from
 * the user's native 320x240 OS 5.8.5 screenshot.
 *
 * v0.5.1 Y= editor.  The six user's CEmu OS 5.8.5 snapshots have
 * three focus regions (formula, style icon and Plot1..Plot3) and a modal
 * color/line selector. Input text is virtual and is not installed as Y-vars. */
/* Palette sampled from the native 320x240 reference, then mapped to
 * RGB565. v0.5.2 incorrectly used neon green/orange and flat brown. */
/* The OS palette is shared with VARS > COLOR (15 entries). */
#define ycolors os_color_menu
static const char *const ycolor_names[15]={
  "BLUE","RED","BLACK","MAGENTA","GREEN","ORANGE","BROWN","NAVY",
  "LTBLUE","YELLOW","WHITE","LTGRAY","MEDGRAY","GRAY","DARKGRAY"
};
static bool y_cursor_visible=true;
/* OS 5.8.5 Y= geometry measured from the ORIGINAL 320x240 frames,
 * not the 244x184 downscaled copies. Pixel sizes are in LCD pixels. */
#define Y_ROW_TOP 57
#define Y_ROW_STEP 20
#define Y_SWATCH_X 3
#define Y_SWATCH_W 8
#define Y_STYLE_X 14
#define Y_TEXT_X 26
#define Y_PLOT_X 12
#define Y_PLOT_STEP 60
#define Y_PLOT_ACTIVE_W 41
#define Y_MODAL_X 29
#define Y_MODAL_Y 76
#define Y_MODAL_W 264
#define Y_MODAL_H 79
/* Opaque monochrome screenshot sprite renderer. All pixels are drawn
 * directly into VRAM, avoiding the overhead and metrics of the host OS font.
 * Only original CE Y= title/glyph sprites are drawn this way. */
static void y_sprite5(int x,int y,const uint8_t (*sprite)[5],int w,int h){
  int row,col;
  for(row=0;row<h;row++){
    volatile uint16_t *dest=VRAM+(unsigned int)(y+row)*UI_LCD_W+x;
    for(col=0;col<w;col++)if(sprite[row][col>>3]&(uint8_t)(0x80u>>(col&7)))
      dest[col]=BLACK;
  }
}
static void y_wedge(int x,int y){
  int row,col;
  for(row=0;row<Y_REF_STYLE_HEIGHT;row++){
    volatile uint16_t *dest=VRAM+(unsigned int)(y+row)*UI_LCD_W+x;
    for(col=0;col<Y_REF_STYLE_WIDTH;col++)
      if(y_ref_wedge[row][col>>3]&(uint8_t)(0x80u>>(col&7)))dest[col]=BLACK;
  }
}
static void y_style_icon(int x,int y,unsigned int line_style,uint16_t color){
  int j;
  /* v0.5.2 used a 1px diagonal. The native 'thick' style is a tapered
   * filled 10x14 wedge; this pixel sprite matches the 320x240 reference. */
  if(line_style==0)y_wedge(x,y);
  else if(line_style==1){for(j=0;j<3;j++)glyph_line(x,y+j+2,10,10);}
  else if(line_style==2){for(j=0;j<5;j++)rect(x+j*2,y+6,1,2,color);}
  else{rect(x,y+2,3,3,color);rect(x+6,y+9,3,3,color);}
}
/* Draw one complete original CE Y= row. The single 2x2 white spot
 * previously added to disabled functions was a fabricated status marker.
 * The real screenshot has solid swatches; enable is controlled separately. */
static void y_draw_swatch_and_style(unsigned int i,int y){
  uint8_t color=vm.y_color[i]%15;
  rect(Y_SWATCH_X,y+1,Y_SWATCH_W,12,ycolors[color]);
  y_style_icon(Y_STYLE_X,y,vm.y_line_style[i],BLACK);
}
static void draw_y_row(unsigned int i){
  int row=(int)i-(int)vm.top,y=Y_ROW_TOP+row*Y_ROW_STEP;
  char name[9],prefix[VM_Y_EXPR_LEN];
  if(row<0 || row>=9 || i>=10)return;
  rect(0,y-2,UI_LCD_W,Y_ROW_STEP,WHITE);
  /* The focus ring encompasses BOTH the swatch and the line style icon. */
  if(vm.y_focus==VM_Y_FOCUS_STYLE && i==vm.selected){
    rect(0,y-2,26,18,BLACK);
    rect(2,y,22,14,WHITE);
  }
  y_draw_swatch_and_style(i,y);
  if(i<9){
    /* The original uses a calculator glyph different from os_LargeFont
     * ASCII. Reuse the supplied pixel-perfect CE Y1..Y9 label sprites. */
    y_sprite5(26,y,y_ref_label[i],Y_REF_LABEL_WIDTH,Y_REF_LABEL_HEIGHT);
  }else{
    snprintf(name,sizeof(name),"Y0=");tx(Y_TEXT_X,y,name);
  }
  if(vm.y_equations[i][0])
    tx(62,y,vm.y_equations[i]);
  if(i==vm.selected && y_cursor_visible){
    if(vm.y_focus==VM_Y_FOCUS_EQUALS){
      /* x=50..59: true '=' cell, independently selectable. */
      rect(50,y,10,14,BLACK);
    }else if(vm.y_focus==VM_Y_FOCUS_EXPR){
      unsigned int n=vm.y_caret[i];
      int x;
      if(n>=sizeof(prefix))n=sizeof(prefix)-1;
      memcpy(prefix,vm.y_equations[i],n);prefix[n]=0;
      x=62+(int)tw(prefix,true);
      if(x>UI_LCD_W-10)x=UI_LCD_W-10;
      rect(x,y,10,14,BLACK);
    }
  }
}
static void draw_y_popup(void){
  unsigned int p,col=0,y=0,idx=vm.y_preview_color%15;
  zx7_Decompress(os_row_buffer,os_menu_pixels+Y_POPUP_OFFSET);
  for(p=0;p<Y_POPUP_RUNS;p++){
    unsigned int n=(os_row_buffer[p]&31)+1;
    uint16_t color=y_popup_palette[os_row_buffer[p]>>5];
    while(n){unsigned int take=264-col;if(take>n)take=n;
      rect(29+(int)col,76+(int)y,(int)take,1,color);
      n-=take;col+=take;if(col==264){col=0;y++;}
    }
  }
  if(idx){
    const char *name=ycolor_names[idx];
    uint16_t ink=(idx>=9 && idx<14)?BLACK:WHITE;
    int x=142+(108-(int)tw(name,true))/2;
    rect(142,83,108,16,ycolors[idx]);
    print_at(x,84,name,true,ink,ycolors[idx]);
  }
  if(vm.y_preview_line){
    rect(143,103,47,15,WHITE);
    y_style_icon(156,103,vm.y_preview_line,BLACK);
  }
  if(vm.y_style_field!=0){
    if(vm.y_style_field==1){
      unsigned int yy,xx;
      for(yy=0;yy<18;yy++)for(xx=0;xx<20;xx++)
        VRAM[(102+yy)*320+191+xx]=VRAM[(82+yy)*320+251+xx];
    }
    rect(251,82,20,18,0xE71Cu);
  }
  if(vm.y_style_field==2){
    rect(107,129,29,1,BLACK);rect(107,149,29,1,BLACK);
    rect(107,129,1,21,BLACK);rect(135,129,1,21,BLACK);
  }else if(vm.y_style_field==3){
    rect(149,129,68,1,BLACK);rect(149,149,68,1,BLACK);
    rect(149,129,1,21,BLACK);rect(216,129,1,21,BLACK);
  }
}
static void draw_y_plot_tabs(void){
  unsigned int i;
  rect(0,33,UI_LCD_W,21,WHITE);
  for(i=0;i<3;i++){
    int x=12+(int)i*60;
    if(vm.y_focus==VM_Y_FOCUS_PLOT && vm.y_plot_focus==i){
      /* Native focus is a black block over the label, not inverted text. */
      if(i<2)y_sprite5(x,36,y_ref_selected_tab[i],40,14);
      else rect(x,36,40,14,BLACK);
    }else y_sprite5(x,36,y_ref_tab[i],40,14);
  }
}
static void draw_y_equations(void){
  unsigned int i;
  body();
  draw_y_plot_tabs();
  for(i=vm.top;i<10 && i<(unsigned int)vm.top+9u;i++)draw_y_row(i);
  if(vm.y_style_open)draw_y_popup();
}
/* WINDOW reference A. Parameter editing is incomplete but the rows, spacing
   and the two calculated values are visible instead of a placeholder. */
static void draw_window_parameters(void){
  unsigned int i;
  static const char *const rows[]={
    "Xmin=-10","Xmax=10","Xscl=1","Ymin=-10","Ymax=10",
    "Yscl=1","Xres=1","dX=0.075757575757576","TraceStep=0.151515151515..."
  };
  body();
  tx(1,38,"WINDOW");
  for(i=0;i<9;i++){
    int y=59+(int)i*19;
    if(i==vm.selected && i<7){rect(2,y+1,4,15,BLACK);}
    if(i==7){
      glyph_line(11,y+13,5,-12);
      glyph_line(16,y+1,5,12);
      hline(11,y+13,11,BLACK);
      tx(22,y,"X=0.075757575757576");
    }else tx(11,y,rows[i]);
  }
}
/* ZOOM MEMORY: OS 5.8.5 list is scrollable and continues through F.
   The leading numeral, not the whole row, is inverted on selection. */
static void draw_zoom_row(unsigned int i){
  char number[3]={0,0,0};
  int row=(int)i-(int)vm.top,y=53+row*20;
  const char *entry;
  if(row<0 || row>=9 || i>=vm_menu_size(&vm))return;
  entry=vm_menu_item(&vm,i);
  rect(0,y,UI_LCD_W,19,WHITE);
  number[0]=entry[0];number[1]=':';
  if(i==vm.selected){
    rect(0,y,(int)tw(number,true)+1,17,BLACK);
    print_at(0,y,number,true,WHITE,BLACK);
  }else tx(0,y,number);
  tx((int)tw(number,true)+1,y,entry+2);
}
static void draw_zoom_memory(void){
  unsigned int i,sz=vm_menu_size(&vm);
  body();
  tx(0,32,"ZOOM MEMORY");
  for(i=vm.top;i<sz && i<(unsigned int)vm.top+9;i++)draw_zoom_row(i);
}
/* 2nd+Y=: STAT PLOTS. Scatter-plot marker previews are simple lines/boxes,
   not raw images, so the page is responsive without a bitmap bundle. */
static void draw_stat_plots(void){
  unsigned int i;
  body();
  inverted_tab("STAT",0,UI_TAB_Y);
  tx(59,UI_TAB_Y+1,"PLOTS");
  for(i=0;i<5;i++){
    char label[48];
    int y=UI_MENU_FIRST_Y+(int)i*33;
    if(i<3){
      snprintf(label,sizeof(label),"%u:Plot%u...%s",i+1,i+1,vm.plots_enabled[i]?"On":"Off");
      tx(0,y,label);
      /* show miniature L1,L2 point-pair hint like the first three rows */
      print_at(27,y+17,"L1     L2",false,BLACK,WHITE);
      rect(18,y+20,4,4, i==0?0x001F:(i==1?0xF800:BLACK));
    }else tx(0,y,i==3?"4:PlotsOff":"5:PlotsOn");
    if(i==vm.selected)rect(0,y+1,7,14,BLACK);
    if(y+33>UI_LCD_H)break;
  }
}
/* 2nd+WINDOW: Table setup from animated reference B. */
static void draw_table_setup(void){
  body();
  tx(1,37,"TABLE SETUP");
  tx(10,58,"TblStart=0");
  glyph_line(10,92,5,-12);
  glyph_line(15,80,5,12);
  hline(10,92,11,BLACK);
  tx(21,78,"Tbl=1");
  {unsigned int j;
    const char *const title[]={"Indpnt:","Depend:"};
    for(j=0;j<2;j++){
      int y=98+(int)j*20;
      int x;
      tx(1,y,title[j]);
      x=(int)tw(title[j],true)+12;
      if(vm.tbl_choice[j]==0){inverted_tab("Auto",x,y-1);tx(x+69,y,"Ask");}
      else {tx(x,y,"Auto");inverted_tab("Ask",x+69,y-1);}
      if(vm.selected==j+2)rect(2,y+17,7,1,BLACK);
    }
  }
  if(vm.selected==0)rect(94,60,4,15,BLACK);
  if(vm.selected==1)rect(95,80,4,15,BLACK);
}
/* 2nd+ZOOM: FORMAT. The original offers Background: Images/Colors;
   user explicitly requested NO image thumbnails. Show Off/color choices only. */
static void draw_format(void){
  static const char *const lefts[]={
    "RectGC","CoordOn","GridOff","GridColor:","Axes:",
    "LabelOff","ExprOn","BorderColor:","Background:","Detect Asymptotes:"
  };
  static const char *const rights[]={
    "PolarGC","CoordOff","GridDot GridLine","MEDGRAY","BLACK",
    "LabelOn","ExprOff","1","Off","On Off"
  };
  unsigned int i;
  body();
  for(i=0;i<10;i++){
    int y=32+(int)i*20;
    int x=(int)tw(lefts[i],true)+8;
    bool two_choices=i==0||i==1||i==5||i==6;
    if(two_choices){
      if(vm.format_choice[i]==0)inverted_tab(lefts[i],0,y-1);
      else tx(0,y,lefts[i]);
      if(vm.format_choice[i]==1){
        rect(x,y-1,(int)tw(rights[i],true)+3,18,BLACK);
        print_at(x+1,y,rights[i],true,WHITE,BLACK);
      }else tx(x,y,rights[i]);
    }else if(i==2){
      static const char *const gridchoices[]={"GridOff","GridDot","GridLine"};
      int j,ax=0;
      for(j=0;j<3;j++){
        int w=(int)tw(gridchoices[j],true)+3;
        if(vm.format_choice[i]==j){
          rect(ax,y-1,w,18,BLACK);
          print_at(ax+1,y,gridchoices[j],true,WHITE,BLACK);
        }else tx(ax+1,y,gridchoices[j]);
        ax+=w+5;
      }
    }else if(i==9){
      const char *opt=vm.format_choice[i]?"Off":"On";
      tx(0,y,lefts[i]);
      rect(x,y-1,(int)tw(opt,true)+2,18,BLACK);
      print_at(x+1,y,opt,true,WHITE,BLACK);
      tx(x+(int)tw(opt,true)+9,y,vm.format_choice[i]?"On":"Off");
    }else{
      char option[18];
      tx(0,y,lefts[i]);
      if(i==3){static const char *const colors[]={"BLACK","MEDGRAY","LTGRAY"};
        snprintf(option,sizeof(option),"%s",colors[vm.format_choice[i]%3]);}
      else if(i==4)snprintf(option,sizeof(option),"%s",vm.format_choice[i]?"LTGRAY":"BLACK");
      else if(i==7)snprintf(option,sizeof(option),"%u",(unsigned)vm.format_choice[i]+1);
      else if(i==8){static const char *const backgrounds[]={"Off","BLUE","LTGRAY","RED","YELLOW"};
        snprintf(option,sizeof(option),"%s",backgrounds[vm.format_choice[i]%5]);}
      else snprintf(option,sizeof(option),"%s",rights[i]);
      rect(x,y-1,(int)tw(option,true)+3,17,0xD69Au);
      tx(x+1,y,option);
    }
    if(vm.selected==i)rect(0,y+17,7,1,BLACK);
  }
}
static void draw_panel_pixels(const uint8_t *compressed,unsigned int count);
static void invert_two_option_selector(void);
static void draw_confirm(void){
  if(vm.reset_kind==0)draw_panel_pixels(reset_ram_confirm,RESET_RAM_CONFIRM_RUNS);
  else draw_panel_pixels(reset_defaults_confirm,RESET_DEFAULTS_CONFIRM_RUNS);
  if(vm.selected)invert_two_option_selector();
}
static void draw_reset_result(void){
  if(vm.reset_kind==0)draw_panel_pixels(reset_ram_result,RESET_RAM_RESULT_RUNS);
  else draw_panel_pixels(reset_defaults_result,RESET_DEFAULTS_RESULT_RUNS);
}
static void invert_two_option_selector(void){
  unsigned int xx,yy;
  for(yy=0;yy<18;yy++)for(xx=0;xx<25;xx++){
    VRAM[(57u+yy)*320u+xx]^=0xFFFFu;
    VRAM[(77u+yy)*320u+xx]^=0xFFFFu;
  }
}
static void draw_notice(void){
  body();
  inverted_tab("MESSAGE",0,UI_TAB_Y);
  if(strstr(vm.notice,"NOT IMPLEMENTED") ||
     strstr(vm.notice,"DISPLAY ONLY") ||
     strstr(vm.notice,"SHORTCUT NOT IMPLEMENTED"))
    draw_unfinished_centered(115,WHITE);
  else tx(0,UI_MENU_FIRST_Y,vm.notice);
}
static void draw_panel_pixels(const uint8_t *compressed,unsigned int count){
  unsigned int p,col=0,y=30;
  zx7_Decompress(os_row_buffer,compressed);
  for(p=0;p<count;p++){
    unsigned int n=(os_row_buffer[p]&127)+1;
    uint16_t color=(os_row_buffer[p]&128)?BLACK:WHITE;
    while(n){unsigned int take=320-col;if(take>n)take=n;
      rect((int)col,(int)y,(int)take,1,color);
      n-=take;col+=take;if(col==320){col=0;y++;}
    }
  }
}
static void draw_error(void){
  draw_panel_pixels(os_menu_pixels+(vm.error_kind==2?ERROR_REF_OFFSET:
                    vm.error_kind==3?ERROR_DOMAIN_OFFSET:ERROR_SYNTAX_OFFSET),
                    vm.error_kind==2?ERROR_REF_RUNS:
                    vm.error_kind==3?ERROR_DOMAIN_RUNS:ERROR_SYNTAX_RUNS);
  if(vm.selected)invert_two_option_selector();
}
static void draw_other(void){
  body();
  inverted_tab(vm_title(&vm),0,UI_TAB_Y);
  draw_unfinished_centered(207,WHITE);
}

/* ROM-derived monochrome row masks preserve native glyphs, including symbols.
 * Menus remain selectable and scrollable; unsupported actions use a notice. */
static bool draw_os_fixed(void){
  unsigned int i;
  for(i=0;i<sizeof(os_fixed)/sizeof(os_fixed[0]);i++)if(os_fixed[i].view==vm.view){
    const os_fixed_t *f=&os_fixed[i];unsigned int col=0,row=30,p;
    zx7_Decompress(os_row_buffer,os_fixed_pixels+f->offset);
    for(p=0;p<f->len;p++){
      unsigned int n=(os_row_buffer[p]&31)+1;uint16_t color=f->palette[os_row_buffer[p]>>5];
      while(n){unsigned int take=320-col;if(take>n)take=n;
        if(color==WHITE || color==BLACK)
          memset(os_fixed_line+col,color==WHITE?0xFF:0,(size_t)take*2u);
        else {unsigned int j;for(j=0;j<take;j++)os_fixed_line[col+j]=color;}
        n-=take;col+=take;
        if(col==320){
          memcpy((void *)(VRAM+row*320u),os_fixed_line,sizeof(os_fixed_line));
          col=0;row++;
        }
      }
    }
    if(vm.view==V_MODE)mode_apply_delta(vm.selected);
    /* Change the selector without replacing the ROM's shortcut layout. */
    if(vm.selected && (vm.view==V_FRAC || vm.view==V_FUNC || vm.view==V_YVAR)){
      int x=vm.view==V_FRAC?6:vm.view==V_FUNC?37:195;
      int y=vm.view==V_FRAC?143:vm.view==V_FUNC?42:122;
      unsigned int xx,yy;int next=y+(int)(vm.selected%(vm.view==V_YVAR?5:9))*20;
      int nextx=x+(vm.view==V_YVAR && vm.selected>=5?52:0);
      for(yy=0;yy<18;yy++)for(xx=0;xx<22;xx++){
        volatile uint16_t *a=VRAM+(unsigned int)(y+yy)*320+x+xx;
        if(*a==0x18E3u)*a=0xE71Cu;else if(*a==WHITE)*a=BLACK;
        a=VRAM+(unsigned int)(next+yy)*320+nextx+xx;
        if(*a==0xE71Cu)*a=0x18E3u;else if(*a==BLACK)*a=WHITE;
      }
    }
    return true;
  }
  return false;
}
static void draw_os_sprite(unsigned int id,int x,int y){
  const os_sprite_t *s=&os_sprites[id];unsigned int pos=0,i=0,col=0,row=0;
  zx7_Decompress(os_row_buffer,os_menu_pixels+s->offset);
  while(pos<(unsigned int)s->w*s->h){
    unsigned char r=os_row_buffer[i++];unsigned int n=(r&127)+1;
    while(n){unsigned int take=s->w-col;if(take>n)take=n;
      if(r&128)rect(x+(int)col,y+(int)row,(int)take,1,col>=25?os_sprite_ink:BLACK);
      n-=take;pos+=take;col+=take;if(col==s->w){col=0;row++;}
    }
  }
}
static bool draw_os_menu(void){
  const os_menu_t *m=os_find_menu(vm.view,vm.tab);unsigned int i;
  if(!m)return false;
  body();os_sprite_ink=BLACK;draw_os_sprite(m->header,0,30);
  for(i=vm.top;i<m->count && i<(unsigned int)vm.top+9;i++){
    int y=57+(int)(i-vm.top)*20;
    if(vm.view==V_VARS && vm.tab==2){
      rect(26,y,110,18,BLACK);rect(27,y+1,108,16,os_color_menu[i]);os_sprite_ink=os_color_ink[i];
    }else os_sprite_ink=BLACK;
    draw_os_sprite(m->rows[i],0,y);
    /* Native colon; the ROM substitutes a scroll arrow at the list edges. */
    rect(17,y+5,3,3,BLACK);rect(17,y+11,3,3,BLACK);
    if(i==vm.top && vm.top>0){
      static const uint16_t arrow[18]={0,0,96,240,504,1020,1902,1638,96,96,96,96,96,96,96,96,0,0};unsigned int yy,xx;
      rect(12,y,13,18,WHITE);
      for(yy=0;yy<18;yy++)for(xx=0;xx<13;xx++)if(arrow[yy]&(1u<<(12-xx)))rect(12+(int)xx,y+(int)yy,1,1,BLACK);
    }
    if(i==vm.top+8 && i+1<m->count){
      rect(12,y,13,18,WHITE);
      {static const uint16_t arrow[18]={0,0,96,96,96,96,96,96,96,96,1638,1902,1020,504,240,96,0,0};unsigned int yy,xx;
       for(yy=0;yy<18;yy++)for(xx=0;xx<13;xx++)if(arrow[yy]&(1u<<(12-xx)))rect(12+(int)xx,y+(int)yy,1,1,BLACK);}
    }
    if(i==vm.selected){unsigned int xx,yy;
      for(yy=0;yy<18;yy++)for(xx=0;xx<25;xx++)
        VRAM[(unsigned int)(y+yy)*320+xx]^=0xFFFF;
    }
  }
  if(vm.view==V_MEMORY){
    if(vm.selected==8){rect(0,217,25,18,BLACK);print_at(0,218,"9:",true,WHITE,BLACK);tx(25,218,"Quit");}
    else tx(0,218,"9:Quit");
  }
  return true;
}

static void draw_finance_menu(void){
  static const uint16_t sigma[18]={0,0,0x3ff,0x3ff,0x1c0,0xe0,0x70,0x38,0x1c,0x1c,0x38,0x70,0xe0,0x1c0,0x3ff,0x3ff,0,0};
  static const uint16_t triangle[18]={0,0,0,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfc,0xf8,0xf0,0xe0,0xc0,0x80,0,0,0};
  unsigned int i;body();
  if(vm.tab==0){rect(0,37,50,18,BLACK);print_at(0,37,"CALC",true,WHITE,BLACK);tx(60,37,"VARS");}
  else{tx(0,37,"CALC");rect(59,37,52,18,BLACK);print_at(60,37,"VARS",true,WHITE,BLACK);}
  for(i=vm.top;i<vm_menu_size(&vm)&&i<(unsigned int)vm.top+9;i++){
    int y=57+(int)(i-vm.top)*20;const char *label=vm_menu_item(&vm,i);
    if(i==vm.selected){rect(0,y,25,18,BLACK);char id[3]={label[0],':',0};print_at(0,y,id,true,WHITE,BLACK);}
    else{char id[3]={label[0],':',0};tx(0,y,id);}
    if(!vm.tab&&(i==9||i==10||i==11||i==12||i==14)){
      const uint16_t *sprite=i==9||i==10?sigma:triangle;
      unsigned int yy,xx;int w=i==9||i==10?10:9,x=i==9||i==10?26:27;
      for(yy=0;yy<18;yy++)for(xx=0;xx<(unsigned int)w;xx++)
        if(sprite[yy]&(1u<<(w-1-xx)))rect(x+(int)xx,y+(int)yy,1,1,BLACK);
      tx(38,y,i==9?"Prn(":i==10?"Int(":label+2);
    }else tx(25,y,label+2);
  }
}
static void draw_finance_solver_row(unsigned int i){
  static const char *const label[]={"N=","I%=","PV=","PMT=","FV=","P/Y=","C/Y="};
  int y=37+(int)i*20;char buf[VM_ANSWER];int x;
  rect(0,y,320,20,WHITE);
  if(i==7){
    tx(13,y,"PMT:");
    if(vm.fin_field==7){
      if(vm.fin_begin){rect(110,y,50,18,BLACK);print_at(110,y,"BEGIN",true,WHITE,BLACK);tx(60,y,"END");}
      else{rect(60,y,40,18,BLACK);print_at(60,y,"END",true,WHITE,BLACK);tx(110,y,"BEGIN");}
    }else{tx(60,y,"END");tx(110,y,"BEGIN");}
    return;
  }
  tx(13,y,label[i]);x=13+(int)tw(label[i],true);
  if(vm.fin_edit&&vm.fin_field==i)snprintf(buf,sizeof(buf),"%s",vm.fin_input);
  else vm_format_precision((long double)vm.fin_value[i],vm.fix_digits,buf,sizeof(buf));
  if(vm.fin_field==i && !vm.fin_edit && !vm.fin_reveal)rect(x,y,10,14,BLACK);
  else{tx(x,y,buf);if(vm.fin_field==i){x+=(int)tw(buf,true);rect(x,y,10,14,BLACK);}}
}
static void draw_finance_solver(void){unsigned int i;body();for(i=0;i<8;i++)draw_finance_solver_row(i);}

#include "file_manager.h"

static void draw_plugin_page(void){unsigned int i;
  body();inverted_tab(vm_plugin_page_title(&vm),0,UI_TAB_Y);
  for(i=0;i<3;i++)if(vm_plugin_page_line(&vm,i)[0])
    tx(2,UI_MENU_FIRST_Y+(int)i*UI_ROW_H,vm_plugin_page_line(&vm,i));
  if(vm_plugin_page_actions(&vm)){
    tx(2,UI_MENU_FIRST_Y+5*UI_ROW_H,"ENTER: APPLY");
    tx(2,UI_MENU_FIRST_Y+6*UI_ROW_H,"DEL: RESTORE");
  }
  tx(2,UI_MENU_FIRST_Y+7*UI_ROW_H,"CLEAR: APPS");
}

static void draw(void){
  if(vm.sleeping){rect(0,0,320,240,BLACK);return;}
  os_RunIndicOff();
  if(vm.view!=V_APPS && draw_os_fixed()){draw_status();return;}
  if(draw_os_menu()){draw_status();return;}
  if(blank_placeholder(vm.view)){
    draw_blank_placeholder();
    draw_status();
    return;
  }
  switch(vm.view){
    case V_HOME: draw_home();break;
    case V_PRGM_NAME:draw_prgm_name();break;
    case V_PRGM_EDITOR:draw_prgm_editor();break;
    case V_MODE: break;
    case V_GRAPH:case V_TRACE: draw_graph();break;
    case V_ABOUT: draw_about();break;
    case V_YEQ:y_cursor_visible=true;draw_y_equations();break;
    case V_WINDOW:draw_window_parameters();break;
    case V_ZOOM:draw_zoom_memory();break;
    case V_STAT_PLOTS:draw_stat_plots();break;
    case V_TBLSET:draw_table_setup();break;
    case V_FORMAT:draw_format();break;
    case V_RESET_CONFIRM: draw_confirm();break;
    case V_RESET_RESULT:draw_reset_result();break;
    case V_NOTICE: draw_notice();break;
    case V_ERROR: draw_error();break;
    case V_FIN_MENU:draw_finance_menu();break;
    case V_FIN_SOLVER:draw_finance_solver();break;
    case V_FILE_LIST:case V_FILE_INFO:case V_FILE_PREVIEW:
    case V_FILE_RENAME:case V_FILE_DELETE:fm_draw();break;
    case V_BATTERY_EDITOR:case V_CHARGE_EDITOR:draw_battery_editor();break;
    case V_BAR_COLOR:draw_bar_color();break;
    /* PRGM and CATALOG always need their heading/tabs even when empty.
       v0.4.6 incorrectly displayed a standalone PRGM header on first open. */
    case V_PRGM:case V_CATALOG:case V_CALC:draw_list();break;
    case V_PLUGINS:
      if(vm_plugin_count())draw_list();
      else {body();inverted_tab("PLUGINS",0,UI_TAB_Y);tx(2,UI_MENU_FIRST_Y,"No plugins installed");}
      break;
    case V_PLUGIN_DELETE:
      draw_list();
      tx(2,UI_MENU_FIRST_Y+3*UI_ROW_H,vm_plugin_name(vm.plugin_target));
      break;
    case V_PLUGIN_PAGE:draw_plugin_page();break;
    default:if(vm_menu_size(&vm))draw_list();else draw_other();break;
  }
  draw_status();
}

/* In-place updates: redraw whole screen only when the actual page changes,
 * the result history changes, or the menu scrolls. The LCD is live VRAM,
 * so a whole-body wipe on every key caused slow-looking flashes in v0.4.1. */
static void redraw_after_key(const vm_t *old){
  bool status_changed = old->second!=vm.second || old->alpha!=vm.alpha ||
                        old->alpha_lock!=vm.alpha_lock || old->degree!=vm.degree ||
                        old->fix_digits!=vm.fix_digits;
  bool changed = memcmp(old,&vm,sizeof(vm_t))!=0;
  if(!changed)return;
  if(old->sleeping!=vm.sleeping){draw();return;}
  if(old->view!=vm.view){ draw();return; }
  if(vm.view==V_FIN_MENU){
    if(old->tab!=vm.tab||old->top!=vm.top)draw_finance_menu();
    else if(old->selected!=vm.selected){
      unsigned int i,yy,xx;const uint8_t rows[2]={old->selected,vm.selected};
      for(i=0;i<2;i++){
        int y=57+((int)rows[i]-(int)vm.top)*20;
        if(y<57||y+18>240)continue;
        for(yy=0;yy<18;yy++)for(xx=0;xx<25;xx++)VRAM[(unsigned int)(y+yy)*320+xx]^=0xFFFF;
      }
    }
    if(status_changed)draw_status();return;
  }
  if(vm.view==V_FIN_SOLVER){
    if(old->fin_field!=vm.fin_field){draw_finance_solver_row(old->fin_field);draw_finance_solver_row(vm.fin_field);}
    else if(old->fin_edit!=vm.fin_edit||old->fin_reveal!=vm.fin_reveal||strcmp(old->fin_input,vm.fin_input)||
            memcmp(old->fin_value,vm.fin_value,sizeof(vm.fin_value))||old->fin_begin!=vm.fin_begin)
      draw_finance_solver_row(vm.fin_field);
    if(status_changed)draw_status();return;
  }
  if(vm.view==V_ERROR || vm.view==V_RESET_CONFIRM){
    if(old->selected!=vm.selected)invert_two_option_selector();
    if(status_changed)draw_status();return;
  }
  if(vm.view==V_WINDOW || vm.view==V_TBLSET || vm.view==V_FORMAT || vm.view==V_STAT_PLOTS){
    if(vm.view==V_STAT_PLOTS && old->selected!=vm.selected){
      unsigned int n,xx,yy;const uint8_t rows[2]={old->selected,vm.selected};
      for(n=0;n<2;n++){
        unsigned int y=rows[n]<3?57u+rows[n]*40u:177u+(rows[n]-3u)*20u;
        for(yy=0;yy<18;yy++)for(xx=0;xx<25;xx++)
          VRAM[(y+yy)*320u+xx]^=0xFFFFu;
      }
    }
    if(memcmp(old->plots_enabled,vm.plots_enabled,sizeof(vm.plots_enabled)) ||
       memcmp(old->tbl_choice,vm.tbl_choice,sizeof(vm.tbl_choice)) ||
       memcmp(old->format_choice,vm.format_choice,sizeof(vm.format_choice)))
      draw();
    else if(status_changed)draw_status();
    return;
  }
  if(vm.view==V_MODE){
    if(old->selected!=vm.selected){mode_apply_delta(old->selected);mode_apply_delta(vm.selected);}
    if(status_changed)draw_status();return;
  }
  if(vm.view==V_FRAC || vm.view==V_FUNC || vm.view==V_MTRX || vm.view==V_YVAR ||
     vm.view==V_TABLE){
    if(old->selected!=vm.selected || old->tab!=vm.tab)if(!draw_os_fixed())draw();
    if(status_changed)draw_status();return;
  }
  if(os_find_menu(vm.view,vm.tab)){
    if(old->top==vm.top && old->tab==vm.tab && old->selected!=vm.selected &&
       vm.view!=V_MEMORY){
      unsigned int n,xx,yy;const uint8_t rows[2]={old->selected,vm.selected};
      for(n=0;n<2;n++){
        int y=57+((int)rows[n]-(int)vm.top)*20;
        if(y<57||y+18>240)continue;
        for(yy=0;yy<18;yy++)for(xx=0;xx<25;xx++)
          VRAM[(unsigned int)(y+yy)*320+xx]^=0xFFFF;
      }
    }else if(old->selected!=vm.selected || old->top!=vm.top || old->tab!=vm.tab)
      draw_os_menu();
    if(status_changed)draw_status();return;
  }
  if(vm.view==V_HOME){
    if(strcmp(old->last_entry,vm.last_entry)!=0 ||
       strcmp(old->last_answer,vm.last_answer)!=0 ||
       strcmp(old->prev_entry,vm.prev_entry)!=0 ||
       strcmp(old->prev_answer,vm.prev_answer)!=0){
      draw_home_content(false);
    }else if(strcmp(old->expression,vm.expression)!=0 ||
             old->caret!=vm.caret || status_changed){
      draw_home_input();
    }
    if(status_changed)draw_status();
    return;
  }
  if(blank_placeholder(vm.view)){
    if(status_changed){draw_placeholder_cursor();draw_status();}
    return;
  }
  /* Only the edited Y= row is refreshed on normal typing or caret motion.
   * Opening/coloring the style modal, scrolling and Plot focus change redraw
   * their respective layers. */
  if(vm.view==V_YEQ){
    if(old->y_style_open!=vm.y_style_open || old->top!=vm.top){
      draw_y_equations();draw_status();
    }else if(vm.y_style_open){
      draw_y_popup();
      if(status_changed)draw_status();
    }else if(old->y_plot_focus!=vm.y_plot_focus ||
             ((old->y_focus==VM_Y_FOCUS_PLOT)!=(vm.y_focus==VM_Y_FOCUS_PLOT))){
      draw_y_plot_tabs();
      if(vm.y_focus!=VM_Y_FOCUS_PLOT && old->y_focus==VM_Y_FOCUS_PLOT)
        draw_y_row(vm.selected);
      else if(vm.y_focus==VM_Y_FOCUS_PLOT && old->y_focus!=VM_Y_FOCUS_PLOT)
        draw_y_row(old->selected);
    }else if(old->y_focus!=vm.y_focus || old->selected!=vm.selected){
      if(old->selected!=vm.selected)draw_y_row(old->selected);
      draw_y_row(vm.selected);
    }else if(memcmp(old->y_equations,vm.y_equations,sizeof(vm.y_equations)) ||
             memcmp(old->y_caret,vm.y_caret,sizeof(vm.y_caret)) ||
             memcmp(old->y_enabled,vm.y_enabled,sizeof(vm.y_enabled)) ||
             memcmp(old->y_color,vm.y_color,sizeof(vm.y_color)) ||
             memcmp(old->y_line_style,vm.y_line_style,sizeof(vm.y_line_style))){
      draw_y_row(vm.selected);
    }
    if(status_changed)draw_status();
    return;
  }
  if(vm.view==V_ZOOM && old->top==vm.top){
    if(old->selected!=vm.selected){
      draw_zoom_row(old->selected);draw_zoom_row(vm.selected);
    }
    if(status_changed)draw_status();
    return;
  }
  if(vm.view==V_ZOOM){
    if(old->selected!=vm.selected || old->top!=vm.top ||
       memcmp(old->y_enabled,vm.y_enabled,sizeof(vm.y_enabled)) ||
       memcmp(old->plots_enabled,vm.plots_enabled,sizeof(vm.plots_enabled)) ||
       memcmp(old->tbl_choice,vm.tbl_choice,sizeof(vm.tbl_choice)) ||
       memcmp(old->format_choice,vm.format_choice,sizeof(vm.format_choice))){
      draw();
    }else if(status_changed)draw_status();
    return;
  }
  if(vm.view==V_SETTINGS &&
     (old->degree!=vm.degree||old->fix_digits!=vm.fix_digits||
      old->repeat_mode!=vm.repeat_mode||
      old->blink_enabled!=vm.blink_enabled||old->wrap_enabled!=vm.wrap_enabled)){
    int i;
    for(i=0;i<2;i++){
      rect(0,UI_MENU_FIRST_Y+i*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
      draw_item(i,vm_menu_item(&vm,(unsigned int)i),vm.selected==(unsigned int)i);
    }
    draw_status();
    return;
  }
  if(vm.view==V_PLUGINS && old->plugin_disabled!=vm.plugin_disabled){
    int row=(int)vm.selected-(int)vm.top;
    if(row>=0&&row<UI_ROWS_ON_SCREEN){
      rect(0,UI_MENU_FIRST_Y+row*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
      draw_item(row,vm_plugin_label(&vm,vm.selected),true);
    }
    draw_status();return;
  }
  if(vm.view==V_PRGM || vm_menu_size(&vm)>0){
    if(old->tab!=vm.tab){draw_list();draw_status();}
    else if(old->top!=vm.top){
      if(vm.view==V_CATALOG)scroll_one_row(old);else draw_menu_rows();
    }
    else if(old->selected!=vm.selected){
      int a=(int)old->selected-(int)vm.top;
      int b=(int)vm.selected-(int)vm.top;
      if(a>=0&&a<UI_ROWS_ON_SCREEN){
        rect(0,UI_MENU_FIRST_Y+a*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
        draw_item(a,vm_menu_item(&vm,old->selected),false);
      }
      if(b>=0&&b<UI_ROWS_ON_SCREEN){
        rect(0,UI_MENU_FIRST_Y+b*UI_ROW_H-1,UI_LCD_W,UI_ROW_H,WHITE);
        draw_item(b,vm_menu_item(&vm,vm.selected),true);
      }
    }
    if(status_changed)draw_status();
    return;
  }
  if(status_changed)draw_status();
  if(vm.view==V_PRGM_NAME){
    if(strcmp(old->new_program,vm.new_program))draw_prgm_name();
    else if(old->alpha!=vm.alpha||old->alpha_lock!=vm.alpha_lock)
      draw_prgm_name_cursor(cursor_visible);
  }
  /* Other static views need not repaint on cursor/modifier-only changes. */
}
static bool suppress_nav_repeat=false;
static void handle_key(vm_key_t key){
  uint16_t old_file_selected=fm_selected,old_file_top=fm_top;
  uint8_t selected_category=(uint8_t)vm.selected;
  if(vm.view==V_MEM_MGMT){
    int n=key==K_1?0:key==K_2?1:key==K_3?2:key==K_4?3:
          key==K_5?4:key==K_6?5:key==K_7?6:key==K_8?7:key==K_9?8:-1;
    if(n>=0)selected_category=(uint8_t)n;
  }
  if(vm.view==V_HOME)restore_home_cursor();
  cursor_visible=true; y_cursor_visible=true;
  before_key=vm;
  if(vm.second && (key==K_UP||key==K_DOWN)){
    unsigned int level=os_Brightness;
    /* The OS keeps its own brightness byte and may restore the LCD register. */
    if(key==K_UP)level=level>16u?level-16u:0u;
    else level=level<239u?level+16u:255u;
    os_Brightness=(uint8_t)level;
    lcd_BacklightLevel=(uint8_t)level;
    vm.second=false;
    suppress_nav_repeat=true;
    draw_status();
    return;
  }
  if(vm.view==V_PLUGIN_PAGE&&(key==K_ENTER||key==K_DEL)){
    if(key==K_ENTER){
      uint16_t bar;uint8_t brightness;unsigned int actions=vm_plugin_page_actions(&vm);
      vm_plugin_page_effect(&vm,&bar,&brightness);
      if(actions&1u){plugin_bar_color=bar;plugin_bar_active=true;}
      if(actions&2u){
        if(!plugin_bright_active)plugin_original_brightness=os_Brightness;
        plugin_bright_active=true;os_Brightness=brightness;
        lcd_BacklightLevel=brightness;
      }
    }else{
      plugin_bar_active=false;
      if(plugin_bright_active){os_Brightness=plugin_original_brightness;
        lcd_BacklightLevel=plugin_original_brightness;}
      plugin_bright_active=false;
    }
    draw();return;
  }
  if(vm.view==V_BAR_COLOR){
    uint8_t old=preview_bar_choice;
    if(key==K_CLEAR){
      preview_bar_choice=saved_bar_choice;
      os_StatusBarBGColor=bar_color_for(saved_bar_choice);
      vm.view=V_SETTINGS;vm.selected=1;vm.top=0;draw();return;
    }
    if(key==K_ENTER){
      if(!bar_save()){
        strcpy(vm.notice,"BAR COLOR SAVE FAILED");vm.return_view=V_SETTINGS;
        vm.view=V_NOTICE;
      }else{vm.view=V_SETTINGS;vm.selected=1;vm.top=0;}
      draw();return;
    }
    if(key==K_LEFT&&preview_bar_choice%5u>0)preview_bar_choice--;
    else if(key==K_RIGHT&&preview_bar_choice%5u<4u)preview_bar_choice++;
    else if(key==K_UP&&preview_bar_choice>=5u)preview_bar_choice-=5u;
    else if(key==K_DOWN&&preview_bar_choice+5u<sizeof(bar_colors)/sizeof(bar_colors[0]))preview_bar_choice+=5u;
    if(preview_bar_choice!=old){
      os_StatusBarBGColor=bar_color_for(preview_bar_choice);
      draw_bar_swatch(old);draw_bar_swatch(preview_bar_choice);
      rect(0,210,UI_LCD_W,15,WHITE);
      print_at(12,212,bar_colors[preview_bar_choice].name,false,BLACK,WHITE);
      draw_status();
    }
    return;
  }
  if(vm.view==V_BATTERY_EDITOR||vm.view==V_CHARGE_EDITOR){
    uint8_t ox=battery_x,oy=battery_y;
    if(battery_palette_open){
      unsigned int old=battery_color;
      if(key==K_CLEAR||key==K_ALPHA||key==K_ENTER){
        battery_palette_open=false;draw_battery_editor();return;
      }
      if(key==K_LEFT&&battery_color%5u>0)battery_color--;
      else if(key==K_RIGHT&&battery_color%5u<4u)battery_color++;
      else if(key==K_UP&&battery_color>=5u)battery_color-=5u;
      else if(key==K_DOWN&&battery_color+5u<BATTERY_COLOR_COUNT)battery_color+=5u;
      if(battery_color!=old)draw_battery_color_menu();
      return;
    }
    if(key==K_2ND){vm.second=true;return;}
    if(vm.second&&key==K_DEL){
      vm.second=false;battery_restore_native();
      vm.view=V_BATTERY_MENU;vm.selected=battery_editor_kind;vm.top=0;draw();return;
    }
    vm.second=false;
    if(key==K_ALPHA){
      battery_palette_open=true;draw_battery_color_menu();return;
    }
    if(key==K_LEFT&&battery_x>0)battery_x--;
    else if(key==K_RIGHT&&battery_x<BATTERY_W-1)battery_x++;
    else if(key==K_UP&&battery_y>0)battery_y--;
    else if(key==K_DOWN&&battery_y<BATTERY_H-1)battery_y++;
    else if(key==K_ENTER){
      unsigned int at=battery_y*BATTERY_W+battery_x;
      battery_draft[at]=battery_color==BATTERY_BG_COLOR?
                        os_StatusBarBGColor:battery_palette[battery_color];
      battery_set_mask(battery_draft_mask,at,true);
    }else if(key==K_DEL){
      unsigned int at=battery_y*BATTERY_W+battery_x;
      battery_draft[at]=battery_original[at];
      battery_set_mask(battery_draft_mask,at,false);
    }
    else if(key==K_MODE){
      if(!battery_save()){
        strcpy(vm.notice,"ICON SAVE FAILED");vm.return_view=V_BATTERY_MENU;
        vm.view=V_NOTICE;
      }else{vm.view=V_BATTERY_MENU;vm.selected=battery_editor_kind;vm.top=0;}
      draw();return;
    }else if(key==K_CLEAR){vm.view=V_BATTERY_MENU;vm.selected=battery_editor_kind;vm.top=0;draw();return;}
    if(ox!=battery_x||oy!=battery_y)draw_battery_cell(ox,oy);
    draw_battery_cell(battery_x,battery_y);
    if(key==K_ENTER||key==K_DEL)draw_status();
    return;
  }
  if(fm_is_view(vm.view) && key==K_2ND){
    vm.second=!vm.second;
    draw_status();
    return;
  }
  if(vm.view==V_PRGM && vm.tab==0 && !vm.second && key==K_ENTER &&
     vm.selected<vm.program_count){
    /* The OS takes over and runs TI-BASIC or native programs from its VAT.
       A null callback returns to the calculator, not this VM's old RAM. */
    char name[VM_PROGRAM_NAME];
    if(!vm_host_program_name(vm.selected,name) ||
       !request_program(name)){
      strcpy(vm.notice,"Unable to run program");
      vm.return_view=V_PRGM;vm.view=V_NOTICE;draw();
    }
    return;
  }
  if(fm_is_view(vm.view))fm_key(key);
  else vm_key(&vm,key);
  if((vm.view==V_BATTERY_EDITOR||vm.view==V_CHARGE_EDITOR)&&
     before_key.view!=vm.view){
    battery_editor_kind=vm.view==V_CHARGE_EDITOR;
    battery_capture_native();
    battery_x=battery_y=0;
    battery_color=0;
    battery_palette_open=false;
  }
  if(vm.view==V_BAR_COLOR&&before_key.view!=V_BAR_COLOR){
    plugin_bar_active=false;
    preview_bar_choice=saved_bar_choice;
    os_StatusBarBGColor=bar_color_for(preview_bar_choice);
  }
  if(vm.view==V_PRGM && before_key.view!=V_PRGM){
    vm.program_count=(uint16_t)vm_host_program_count();
    vm.selected=vm.top=0;
  }
  if(vm.view==V_FILE_LIST && before_key.view==V_MEM_MGMT){
    fm_category=selected_category;
    fm_selected=fm_top=0;
    fm_refresh();
  }else if(vm.view==V_FILE_LIST && before_key.view!=V_FILE_LIST)fm_refresh();
  if(!vm.exit_requested){
    if(vm.view==V_FILE_LIST && before_key.view==V_FILE_LIST &&
       fm_top==old_file_top && fm_selected!=old_file_selected){
      fm_row(old_file_selected);fm_row(fm_selected);
    }else if(fm_is_view(vm.view)||fm_is_view(before_key.view))draw();
    else redraw_after_key(&before_key);
    if(vm.view==V_HOME && !cursor_drawn)draw_home_cursor(true);
  }
}

typedef struct{uint8_t group,mask;vm_key_t code;} keymap_t;
#define KEYMAP(g,k,c) {g,k,c}
static const keymap_t keymap[]={
  KEYMAP(1,kb_2nd,K_2ND),KEYMAP(2,kb_Alpha,K_ALPHA),
  KEYMAP(1,kb_Mode,K_MODE),KEYMAP(1,kb_Del,K_DEL),KEYMAP(6,kb_Clear,K_CLEAR),
  KEYMAP(6,kb_Enter,K_ENTER),KEYMAP(7,kb_Up,K_UP),KEYMAP(7,kb_Down,K_DOWN),
  KEYMAP(7,kb_Left,K_LEFT),KEYMAP(7,kb_Right,K_RIGHT),
  KEYMAP(1,kb_Yequ,K_YEQ),KEYMAP(1,kb_Window,K_WINDOW),
  KEYMAP(1,kb_Zoom,K_ZOOM),KEYMAP(1,kb_Trace,K_TRACE),KEYMAP(1,kb_Graph,K_GRAPH),
  KEYMAP(3,kb_GraphVar,K_XVAR),KEYMAP(4,kb_Stat,K_STAT),
  KEYMAP(2,kb_Math,K_MATH),KEYMAP(3,kb_Apps,K_APPS),KEYMAP(4,kb_Prgm,K_PRGM),KEYMAP(5,kb_Vars,K_VARS),
  KEYMAP(2,kb_Recip,K_RECIP),KEYMAP(3,kb_Sin,K_SIN),KEYMAP(4,kb_Cos,K_COS),
  KEYMAP(5,kb_Tan,K_TAN),KEYMAP(6,kb_Power,K_POWER),KEYMAP(2,kb_Square,K_SQUARE),
  KEYMAP(3,kb_Comma,K_COMMA),KEYMAP(4,kb_LParen,K_LPAREN),KEYMAP(5,kb_RParen,K_RPAREN),
  KEYMAP(6,kb_Div,K_DIV),KEYMAP(2,kb_Log,K_LOG),
  KEYMAP(3,kb_7,K_7),KEYMAP(4,kb_8,K_8),KEYMAP(5,kb_9,K_9),KEYMAP(6,kb_Mul,K_MUL),
  KEYMAP(2,kb_Ln,K_LN),KEYMAP(3,kb_4,K_4),KEYMAP(4,kb_5,K_5),KEYMAP(5,kb_6,K_6),KEYMAP(6,kb_Sub,K_SUB),
  KEYMAP(2,kb_Sto,K_STO),KEYMAP(3,kb_1,K_1),KEYMAP(4,kb_2,K_2),KEYMAP(5,kb_3,K_3),KEYMAP(6,kb_Add,K_ADD),
  KEYMAP(3,kb_0,K_0),KEYMAP(4,kb_DecPnt,K_DOT),KEYMAP(5,kb_Chs,K_NEG)
};
static vm_key_t scan_key(void){
  unsigned int i;
  if(kb_On)return K_ON;
  for(i=0;i<sizeof(keymap)/sizeof(keymap[0]);i++)
    if(kb_Data[keymap[i].group]&keymap[i].mask)return keymap[i].code;
  return K_NONE;
}
int main(void){
  resource_handle=ti_Open("TVMSPRT","r");
  vm_key_t previous=K_NONE;
  bool show_intro;
  unsigned int held=0;
  unsigned int blink_elapsed_ms=0;
  uint8_t charging_state;
  if(!resource_handle || ti_GetSize(resource_handle)!=OS_MENU_BYTES){
    if(resource_handle)ti_Close(resource_handle);
    os_ClrHome();os_PutStrFull("Install TVMSPRT.8xv");os_GetKey();return 0;
  }
  os_menu_pixels=ti_GetDataPtr(resource_handle);
  ti_SetGCBehavior(NULL,resource_reloaded_after_gc);
  vm_init(&vm);
  vm_settings_load(&vm);
  vm_plugin_refresh(&vm);
  battery_load(0);battery_load(1);
  bar_load();
  generate_virtual_about();
  show_intro=!intro_was_shown();
  /* Keep OS-standard 16bpp mode so built-in OS font & status routines
   * draw to exactly the same framebuffer. DO NOT call gfx_Begin(). */
  os_DisableCursor();
  os_DisableHomeTextBuffer();
  os_ClrLCDFull();
  kb_DisableOnLatch();
  if(show_intro){
    tx((UI_LCD_W-(int)tw("TI84VM beta",true))/2,95,"TI84VM beta");
    print_at((UI_LCD_W-(int)tw("by Tim_Lin",false))/2,119,
             "by Tim_Lin",false,BLACK,WHITE);
    delay(2500);
    mark_intro_shown();
  }
  os_ClrLCDFull();
  draw();
  charging_state=boot_BatteryCharging()!=0;
  while(!vm.exit_requested){
    vm_key_t current;
    kb_Scan();current=scan_key();
    if(current!=previous){
      held=0;
      suppress_nav_repeat=false;
      if(current!=K_NONE){
        blink_elapsed_ms=0;
        handle_key(current);
      }
    }
    else if(vm.repeat_mode!=2&&!suppress_nav_repeat&&current!=K_NONE&&current!=K_2ND&&current!=K_ALPHA&&
            current!=K_CLEAR&&current!=K_ENTER){
      held++;
      if(vm.repeat_mode==0){
        if(held>62 && ((held-63u)%16u)==0u){blink_elapsed_ms=0;handle_key(current);}
      }else if(held>35 && ((held-36u)%8u)==0u){blink_elapsed_ms=0;handle_key(current);}
    }
    previous=current;
    delay(8);
    {uint8_t now=boot_BatteryCharging()!=0;
      if(now!=charging_state){charging_state=now;draw_status();}}
    if(vm.blink_enabled && vm.view==V_YEQ && (vm.y_focus==VM_Y_FOCUS_EXPR ||
                            vm.y_focus==VM_Y_FOCUS_EQUALS) && !vm.y_style_open){
      blink_elapsed_ms+=8u;
      if(blink_elapsed_ms>=CURSOR_HALF_PERIOD_MS){
        blink_elapsed_ms=0;
        y_cursor_visible=!y_cursor_visible;
        draw_y_row(vm.selected);
      }
    }else if(vm.blink_enabled && vm.view==V_PRGM_NAME){
      blink_elapsed_ms+=8u;
      if(blink_elapsed_ms>=(cursor_visible?300u:400u)){
        blink_elapsed_ms=0;
        cursor_visible=!cursor_visible;
        draw_prgm_name_cursor(cursor_visible);
      }
    }else if(vm.blink_enabled && vm.view==V_HOME){
      blink_elapsed_ms+=8u;
      if(blink_elapsed_ms>=CURSOR_HALF_PERIOD_MS){
        blink_elapsed_ms=0;
        cursor_visible=!cursor_visible;
        draw_home_cursor(cursor_visible);
      }
    }else blink_elapsed_ms=0;
  }
  os_ClrLCDFull();
  if(plugin_bright_active){os_Brightness=plugin_original_brightness;
    lcd_BacklightLevel=plugin_original_brightness;}
  os_StatusBarBGColor=native_bar_color;
  os_DrawStatusBar();
  kb_Reset();
  ti_SetGCBehavior(NULL,NULL);
  if(resource_handle)ti_Close(resource_handle);
  return 0;
}
