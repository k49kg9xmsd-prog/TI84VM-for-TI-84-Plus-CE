/* TI84VM Lite v0.3: a tiny UI interpreter, NOT an eZ80 hardware emulator. */
#include "vm.h"
#include "os_menu_counts.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <fileioc.h>

static const char *const mem_items[]={
  "1:About","2:Mem Management/Delete...","3:Clear Entries","4:ClrAllLists",
  "5:Archive","6:UnArchive","7:Reset...","8:Group...","9:Quit"
};
static const char *const reset_items[]={"1:All RAM...","2:Defaults..."};
static const char *const math_items[]={"1:>Frac","2:>Dec","3:^3","4:cube root(","5:x root(","6:fMin(","7:fMax(","8:nDeriv(","9:fnInt("};
static const char *const frac_items[]={"1:n/d","2:Un/d","3:>F<>D","4:>n/d<>Un/d","5:>Frac","6:>Dec"};
static const char *const func_items[]={"1:abs(","2:sum(","3:nDeriv(","4:fnInt(","5:logBASE(","6:n root(","7:nPr","8:nCr","9:!"};
static const char *const mtrx_items[]={"ROW: 1 2 3 4 5 6","COL: 1 2 3 4 5 6","OK"};
static const char *const yvar_items[]={"1:Y1","2:Y2","3:Y3","4:Y4","5:Y5","6:Y6","7:Y7","8:Y8","9:Y9","0:Y0"};
static const char *const stat_items[]={"EDIT","CALC","TESTS"};
static const char *const vars_items[]={"VARS","Y-VARS"};
static const char *const y_items[]={"Y1=","Y2=","Y3=","Y4=","Y5=","Y6=","Y7=","Y8=","Y9=","Y0="};
static const char *const window_items[]={"Xmin=-10","Xmax=10","Xscl=1","Ymin=-10","Ymax=10","Yscl=1","Xres=1","\x7fX=0.075757575757576","TraceStep=0.151515151515..."};
static const char *const zoom_items[]={
 "1:ZBox","2:Zoom In","3:Zoom Out","4:ZDecimal","5:ZSquare",
 "6:ZStandard","7:ZTrig","8:ZInteger","9:ZoomStat","0:ZoomFit",
 "A:ZQuadrant1","B:ZFrac1/2","C:ZFrac1/3","D:ZFrac1/4","E:ZFrac1/5","F:ZFrac1/8"
};
static const char *const plot_items[]={"1:Plot1...Off","2:Plot2...Off","3:Plot3...Off","4:PlotsOff","5:PlotsOn"};
static const char *const trace_items[]={"TRACE","X=0","Y=0"};
static const char *const calculate_items[]={
 "1:value","2:zero","3:minimum","4:maximum","5:intersect","6:dy/dx","7:integral"
};
static const char *const test_items[]={"1:=","2:!=","3:>","4:>=","5:<","6:<="};
static const char *const mem_mgmt_items[]={"1:All...","2:Real...","3:Complex...","4:List...","5:Matrix...","6:Y-Vars...","7:Prgm...","8:AppVars...","9:Apps..."};
static const char *const app_items[]={"1:Settings","2:Plugins","3:Finance"};
static const char *const settings_items[]={"1:Battery icons...","2:Top bar color..."};
static const char *const battery_items[]={"1:Normal battery...","2:Charging battery..."};
static const char *const plugin_delete_items[]={"1:Cancel","2:Delete"};
typedef struct {uint32_t magic;uint8_t degree;int8_t fix_digits;} vm_settings_old_t;
typedef struct {uint32_t magic;uint8_t degree;int8_t fix_digits;uint8_t legacy_splash,repeat,blink,wrap,plugin_disabled;} vm_settings_disk_t;
#define SETTINGS_MAGIC 0x31535654UL
#define PLUGIN_MAX 8
typedef struct {
  char var[9],title[17],home[33],app[17],page[17],lines[3][33];
  uint16_t bar;uint8_t brightness,actions,hotkey;
} vm_plugin_t;
typedef struct {char magic[8];char enabled[PLUGIN_MAX][9];} vm_plugin_config_t;
static vm_plugin_t plugins[PLUGIN_MAX];
static uint8_t plugins_count;
static bool plugin_config_load(vm_plugin_config_t *config){
  uint8_t h=ti_Open("TVMPLCFG","r");bool valid=false;
  if(!h)return false;
  if(ti_GetSize(h)==sizeof(*config)&&ti_Read(config,sizeof(*config),1,h)==1&&
     !memcmp(config->magic,"TVMPCF2",8))valid=true;
  ti_Close(h);
  return valid;
}
static bool plugin_config_save(const vm_t *v){
  vm_plugin_config_t config={0};uint8_t h;unsigned int i,j=0;bool ok;
  memcpy(config.magic,"TVMPCF2",8);
  for(i=0;i<plugins_count;i++)if(!(v->plugin_disabled&(1u<<i)))
    memcpy(config.enabled[j++],plugins[i].var,9);
  h=ti_Open("TVMPLCFG","w");if(!h)return false;
  ok=ti_Write(&config,sizeof(config),1,h)==1;
  if(ok)ok=ti_SetArchiveStatus(true,h)!=0;
  ti_Close(h);return ok;
}
void vm_plugin_refresh(vm_t *v){
  void *vat=NULL;char *name;uint8_t type;
  plugins_count=0;
  while(plugins_count<PLUGIN_MAX&&(name=ti_DetectAny(&vat,NULL,&type))){
    uint8_t h;char data[320]={0},*title=NULL,*home=NULL,*app=NULL,*page=NULL;
    char *rows[3]={NULL,NULL,NULL},*bar=NULL,*bright=NULL,*hotkey=NULL,*line,*end;unsigned int size;
    bool version2;
    if(type!=OS_TYPE_APPVAR||strlen(name)>8)continue;
    h=ti_OpenVar(name,"r",type);if(!h)continue;
    size=(unsigned int)ti_GetSize(h);if(size>=sizeof(data))size=sizeof(data)-1;
    if(size&&ti_Read(data,1,size,h)!=size){ti_Close(h);continue;}
    ti_Close(h);
    if(size<17)continue;
    version2=!memcmp(data,"TI84VM-PLUGIN/2\n",16);
    if(!version2&&memcmp(data,"TI84VM-PLUGIN/1\n",16))continue;
    line=data+16;
    while(*line){
      end=strchr(line,'\n');if(end)*end=0;
      if(!strncmp(line,"NAME=",5))title=line+5;
      else if(!strncmp(line,"HOME=",5))home=line+5;
      else if(version2&&!strncmp(line,"APP=",4))app=line+4;
      else if(version2&&!strncmp(line,"PAGE=",5))page=line+5;
      else if(version2&&!strncmp(line,"LINE1=",6))rows[0]=line+6;
      else if(version2&&!strncmp(line,"LINE2=",6))rows[1]=line+6;
      else if(version2&&!strncmp(line,"LINE3=",6))rows[2]=line+6;
      else if(version2&&!strncmp(line,"BAR=",4))bar=line+4;
      else if(version2&&!strncmp(line,"BRIGHT=",7))bright=line+7;
      else if(version2&&!strncmp(line,"HOTKEY=",7))hotkey=line+7;
      if(!end)break;
      line=end+1;
    }
    if(!title||!*title)continue;
    if(!home)home="";
    memset(&plugins[plugins_count],0,sizeof(plugins[plugins_count]));
    strncpy(plugins[plugins_count].var,name,8);plugins[plugins_count].var[8]=0;
    strncpy(plugins[plugins_count].title,title,16);plugins[plugins_count].title[16]=0;
    strncpy(plugins[plugins_count].home,home,32);plugins[plugins_count].home[32]=0;
    if(app&&*app){
      unsigned int i;
      strncpy(plugins[plugins_count].app,app,16);
      strncpy(plugins[plugins_count].page,page&&*page?page:app,16);
      for(i=0;i<3;i++)if(rows[i])strncpy(plugins[plugins_count].lines[i],rows[i],32);
      if(bar&&*bar){char *stop;unsigned long value=strtoul(bar,&stop,16);
        if(!*stop&&value<=65535u){plugins[plugins_count].bar=(uint16_t)value;plugins[plugins_count].actions|=1;}}
      if(bright&&*bright){char *stop;unsigned long value=strtoul(bright,&stop,10);
        if(!*stop&&value<=255u){plugins[plugins_count].brightness=(uint8_t)value;plugins[plugins_count].actions|=2;}}
      if(hotkey){
        if(!strcmp(hotkey,"ALPHA+STAT"))plugins[plugins_count].hotkey=K_STAT;
        else if(!strcmp(hotkey,"ALPHA+PRGM"))plugins[plugins_count].hotkey=K_PRGM;
        else if(!strcmp(hotkey,"ALPHA+MATH"))plugins[plugins_count].hotkey=K_MATH;
        else if(!strcmp(hotkey,"ALPHA+VARS"))plugins[plugins_count].hotkey=K_VARS;
      }
    }
    plugins_count++;
  }
  /* VAT order can change after archiving; keep persisted toggles stable. */
  {unsigned int i,j;for(i=0;i<plugins_count;i++)for(j=i+1;j<plugins_count;j++)
    if(strcmp(plugins[i].var,plugins[j].var)>0){vm_plugin_t tmp=plugins[i];
      plugins[i]=plugins[j];plugins[j]=tmp;}}
  {vm_plugin_config_t config;unsigned int i,j;
   v->plugin_disabled=(uint8_t)((1u<<plugins_count)-1u);
   if(plugin_config_load(&config)){
     for(i=0;i<plugins_count;i++)for(j=0;j<PLUGIN_MAX;j++)
       if(!strcmp(plugins[i].var,config.enabled[j]))v->plugin_disabled&=(uint8_t)~(1u<<i);
   }}
}
static bool vm_plugin_toggle(vm_t *v,unsigned int i){
  if(i>=plugins_count)return false;
  v->plugin_disabled^=(uint8_t)(1u<<i);
  if(plugin_config_save(v))return true;
  v->plugin_disabled^=(uint8_t)(1u<<i);return false;
}
static int vm_plugin_delete(vm_t *v){
  char name[9];
  if(v->plugin_target>=plugins_count)return 0;
  memcpy(name,plugins[v->plugin_target].var,sizeof(name));
  if(!ti_DeleteVar(name,OS_TYPE_APPVAR))return 0;
  vm_plugin_refresh(v);
  if(v->selected>=plugins_count&&plugins_count)v->selected=plugins_count-1;
  if(v->top>v->selected)v->top=v->selected;
  return plugin_config_save(v)?1:2;
}
unsigned int vm_plugin_count(void){return plugins_count;}
const char *vm_plugin_name(unsigned int i){return i<plugins_count?plugins[i].title:"";}
const char *vm_plugin_label(const vm_t *v,unsigned int i){
  static char label[40];
  if(i>=plugins_count)return "";
  snprintf(label,sizeof(label),"%u:%s %s",i+1,plugins[i].title,
           (v->plugin_disabled&(1u<<i))?"Off":"On");
  return label;
}
const char *vm_plugin_home_text(const vm_t *v){unsigned int i;
  for(i=0;i<plugins_count;i++)if(!(v->plugin_disabled&(1u<<i))&&plugins[i].home[0])
    return plugins[i].home;
  return "";
}
unsigned int vm_plugin_app_count(const vm_t *v){unsigned int i,count=0;
  for(i=0;i<plugins_count;i++)if(!(v->plugin_disabled&(1u<<i))&&plugins[i].app[0])count++;
  return count;
}
unsigned int vm_plugin_app_index(const vm_t *v,unsigned int ordinal){unsigned int i;
  for(i=0;i<plugins_count;i++)if(!(v->plugin_disabled&(1u<<i))&&plugins[i].app[0]){
    if(!ordinal)return i;ordinal--;
  }
  return PLUGIN_MAX;
}
const char *vm_plugin_app_label(const vm_t *v,unsigned int ordinal){
  static char label[32];unsigned int i=vm_plugin_app_index(v,ordinal);
  if(i>=plugins_count)return "";
  snprintf(label,sizeof(label),"%u:%s",ordinal+4,plugins[i].app);return label;
}
const char *vm_plugin_page_title(const vm_t *v){
  return v->plugin_active<plugins_count?plugins[v->plugin_active].page:"PLUGIN";
}
const char *vm_plugin_page_line(const vm_t *v,unsigned int line){
  return v->plugin_active<plugins_count&&line<3?plugins[v->plugin_active].lines[line]:"";
}
bool vm_plugin_page_effect(const vm_t *v,uint16_t *bar,uint8_t *brightness){
  const vm_plugin_t *p;
  if(v->plugin_active>=plugins_count)return false;
  p=&plugins[v->plugin_active];
  if(bar)*bar=p->bar;
  if(brightness)*brightness=p->brightness;
  return p->actions!=0;
}
unsigned int vm_plugin_page_actions(const vm_t *v){
  return v->plugin_active<plugins_count?plugins[v->plugin_active].actions:0;
}
static unsigned int vm_plugin_hotkey_index(const vm_t *v,vm_key_t key){unsigned int i;
  for(i=0;i<plugins_count;i++)if(!(v->plugin_disabled&(1u<<i))&&
     plugins[i].hotkey==(uint8_t)key&&plugins[i].app[0])return i;
  return PLUGIN_MAX;
}
void vm_settings_load(vm_t *v){
  vm_settings_disk_t s;vm_settings_old_t old;uint8_t h=ti_Open("TVMSET","r");
  if(!h)return;
  if(ti_GetSize(h)==sizeof(old)&&ti_Read(&old,sizeof(old),1,h)==1&&
     old.magic==SETTINGS_MAGIC&&old.degree<=1&&old.fix_digits>=-1&&old.fix_digits<=9){
    v->degree=old.degree;v->fix_digits=old.fix_digits;
  }else if(ti_GetSize(h)==sizeof(s)&&ti_Read(&s,sizeof(s),1,h)==1&&
     s.magic==SETTINGS_MAGIC&&s.degree<=1&&s.fix_digits>=-1&&s.fix_digits<=9&&
     s.legacy_splash<=1&&s.repeat<=2&&s.blink<=1&&s.wrap<=1){
    v->degree=s.degree;v->fix_digits=s.fix_digits;
    v->repeat_mode=s.repeat;v->blink_enabled=s.blink;v->wrap_enabled=s.wrap;
    v->plugin_disabled=s.plugin_disabled;
  }
  ti_Close(h);
}
static const char *const catalog[]={
 "abs(","and","angle(","ANOVA(","Ans","Archive","augment(","AUTO Answer","AxesOff","AxesOn",
 "binomcdf(","binompdf(","Boxplot","ceil(","Circle(","ClrAllLists","ClrDraw",
 "ClrHome","ClrList","ClrTable","conj(","cos(","cosh(","cumSum(","dayOfWk(",
 "dbd(","Dec","degree","det(","dim(","Disp","DrawF","e^(","Else","End",
 "exp(","expr(","Fill(","Fix","Float","fMax(","fMin(","For(","getDate",
 "getKey","getTime","gcd(","GraphStyle(","GridOff","GridOn","Histogram",
 "identity(","If","imag(","Input","inString(","int(","invNorm(","isClockOn",
 "Lbl","lcm(","length(","Line(","LinReg(","ln(","log(","logBASE(","lower(",
 "max(","mean(","median(","Menu(","min(","mod(","Normal","not(","or",
 "Output(","Pause","piecewise(","PlotsOff","PlotsOn","prgm","Prompt",
 "Pt-On(","Pt-Off(","rand","randInt(","real(","Recall","Repeat","Return",
 "round(","rowSwap(","rref(","seq(","SetUpEditor","sin(","sinReg","SortA(",
 "SortD(","startTmr","stdDev(","Stop","sum(","tan(","Then","timeCnv(",
 "Trace","UnArchive","variance(","While",
 /* OS 5.8.1 sequence sampled from the user's ROM by scrolling CATALOG. */
 "xor","xyLine","ZBox","ZDecimal","ZFrac1/2","ZFrac1/3","ZFrac1/4",
 "ZFrac1/5","ZFrac1/8","ZFrac1/10","ZInteger","ZInterval","Zoom In","Zoom Out",
 "ZoomFit","ZoomRcl","ZoomStat","ZoomSto","ZPrevious","ZQuadrant1",
 "ZSquare","ZStandard","Z-Test(","ZTrig",
 "!","deg","r","theta","xroot","^3","cuberoot(","=","!=","<",">",
 "<=",">=","box","/","*","dot","^-1","^2","^","neg","10^(","e^(",
 "pi","/","+","-","{","[","(","}","]",")",",","'","\"",":"
};
#define CATALOG_X_START (COUNT(catalog)-61u)
#define CATALOG_SYMBOL_START (CATALOG_X_START+23u)
#define COUNT(a) ((unsigned int)(sizeof(a)/sizeof((a)[0])))
unsigned int vm_catalog_symbol_start(void){return CATALOG_SYMBOL_START;}
static bool host_program_usable(const char *name,uint8_t type){
  uint8_t h;unsigned int i;
  if(type!=OS_TYPE_PRGM&&type!=OS_TYPE_PROT_PRGM)return false;
  if(!name||name[0]<'A'||name[0]>'Z')return false;
  for(i=0;i<8&&name[i];i++)
    if((name[i]<'A'||name[i]>'Z')&&(name[i]<'0'||name[i]>'9'))return false;
  if(i==8&&name[i])return false;
  h=ti_OpenVar(name,"r",type);
  if(!h)return false;
  i=(unsigned int)ti_GetSize(h);
  ti_Close(h);
  return i>0;
}
unsigned int vm_host_program_count(void){
  void *vat=NULL;uint8_t type;unsigned int count=0;char *name,copy[9];
  while((name=ti_DetectAny(&vat,NULL,&type))){
    strncpy(copy,name,8);copy[8]=0;
    if(host_program_usable(copy,type))count++;
  }
  return count;
}
bool vm_host_program_name(unsigned int index,char out[VM_PROGRAM_NAME]){
  void *vat=NULL;char *name;uint8_t type;char copy[9];
  while((name=ti_DetectAny(&vat,NULL,&type))){
    strncpy(copy,name,8);copy[8]=0;
    if(!host_program_usable(copy,type))continue;
    if(!index--){strncpy(out,copy,VM_PROGRAM_NAME-1);
      out[VM_PROGRAM_NAME-1]=0;return true;}
  }
  return false;
}

char vm_alpha_character(vm_key_t k) {
  switch(k) {
    case K_MATH:return 'A';case K_APPS:return 'B';case K_PRGM:return 'C';
    case K_RECIP:return 'D';case K_SIN:return 'E';case K_COS:return 'F';
    case K_TAN:return 'G';case K_POWER:return 'H';case K_SQUARE:return 'I';
    case K_COMMA:return 'J';case K_LPAREN:return 'K';case K_RPAREN:return 'L';
    case K_DIV:return 'M';case K_LOG:return 'N';case K_7:return 'O';
    case K_8:return 'P';case K_9:return 'Q';case K_MUL:return 'R';
    case K_LN:return 'S';case K_4:return 'T';case K_5:return 'U';
    case K_6:return 'V';case K_SUB:return 'W';case K_STO:return 'X';
    case K_1:return 'Y';case K_2:return 'Z';
    default:return 0;
  }
}

const char *vm_title(const vm_t *v) {
  switch(v->view){
   case V_MEMORY:return "MEMORY";case V_RESET:return "RESET";
   case V_RESET_CONFIRM:return "RESET";case V_ABOUT:return "ABOUT";
   case V_MEM_MGMT:return "MEM MGMT/DEL";case V_CATALOG:return "CATALOG";
   case V_MODE:return "MODE";case V_MATH:return "MATH";case V_PRGM:return "PRGM";
   case V_PRGM_NAME:return "PROGRAM NAME";case V_PRGM_EDITOR:return "PROGRAM EDITOR";
   case V_GRAPH:return "GRAPH";case V_YEQ:return "Y=";case V_WINDOW:return "WINDOW";
   case V_STAT_PLOTS:return "STAT PLOTS";
   case V_ZOOM:return "ZOOM";case V_TRACE:return "TRACE";case V_STAT:return "STAT";
   case V_VARS:return "VARS";case V_APPS:return "APPS";case V_SETTINGS:return "SETTINGS";case V_PLUGINS:return "PLUGINS";case V_PLUGIN_DELETE:return "DELETE PLUGIN?";case V_PLUGIN_PAGE:return vm_plugin_page_title(v);case V_BATTERY_MENU:return "BATTERY ICONS";case V_BATTERY_EDITOR:return "BATTERY ICON";case V_CHARGE_EDITOR:return "CHARGING ICON";case V_BAR_COLOR:return "TOP BAR COLOR";case V_TEST:return "TEST";
   case V_FIN_MENU:return "FINANCE";case V_FIN_SOLVER:return "TVM Solver";
   case V_MATRIX:return "MATRIX";case V_LIST:return "LIST";case V_DRAW:return "DRAW";
   case V_DISTR:return "DISTR";case V_FORMAT:return "FORMAT";case V_TABLE:return "TABLE";
   case V_TBLSET:return "TABLE SETUP";case V_CALC:return "CALCULATE";
   case V_ANGLE:return "ANGLE";case V_FRAC:return "FRAC";case V_FUNC:return "FUNC";case V_MTRX:return "MTRX";case V_YVAR:return "YVAR";case V_NOTICE:return "MESSAGE";
   default:return "";
  }
}
unsigned int vm_menu_size(const vm_t *v) {
  unsigned int measured=os_menu_count(v->view,v->tab);if(measured)return measured;
  switch(v->view){
    case V_APPS:return COUNT(app_items)+vm_plugin_app_count(v);
    case V_SETTINGS:return COUNT(settings_items);
    case V_BATTERY_MENU:return COUNT(battery_items);
    case V_PLUGINS:return vm_plugin_count();
    case V_PLUGIN_DELETE:return COUNT(plugin_delete_items);
    case V_FIN_MENU:return v->tab?7:15;
    case V_MEMORY:return COUNT(mem_items);case V_RESET:return COUNT(reset_items);
    case V_MEM_MGMT:return COUNT(mem_mgmt_items);case V_CATALOG:return COUNT(catalog);
    case V_MODE:return 12;case V_MATH:return COUNT(math_items);
    case V_PRGM:return v->tab==2?1u:v->program_count;case V_STAT:return COUNT(stat_items);
    case V_VARS:return COUNT(vars_items);case V_YEQ:return COUNT(y_items);
    case V_WINDOW:return COUNT(window_items);case V_ZOOM:return COUNT(zoom_items);
    case V_STAT_PLOTS:return COUNT(plot_items);
    case V_TRACE:return COUNT(trace_items);case V_CALC:return COUNT(calculate_items);case V_TEST:return COUNT(test_items);
    case V_FRAC:return 4;case V_FUNC:return 9;
    case V_MTRX:return 3;case V_YVAR:return 10;
    default:return 0;
  }
}
const char *vm_menu_item(const vm_t *v,unsigned int i) {
  if(i>=vm_menu_size(v)) return "";
  /* These labels are stored as ROM-derived glyph rows, not ASCII arrays. */
  if(os_menu_count(v->view,v->tab))return "";
  switch(v->view){
    case V_APPS:return i<COUNT(app_items)?app_items[i]:vm_plugin_app_label(v,i-COUNT(app_items));
    case V_SETTINGS:return settings_items[i];
    case V_BATTERY_MENU:return battery_items[i];
    case V_PLUGINS:return vm_plugin_label(v,i);
    case V_PLUGIN_DELETE:return plugin_delete_items[i];
    case V_FIN_MENU:{
      static const char *const calc[]={"1:TVM Solver...","2:tvm_Pmt","3:tvm_I%","4:tvm_PV","5:tvm_N","6:tvm_FV","7:npv(","8:irr(","9:bal(","0:SigmaPrn(","A:SigmaInt(","B:Nom(","C:Eff(","D:dbd(","E:Pmt_End"};
      static const char *const vars[]={"1:N","2:I%","3:PV","4:PMT","5:FV","6:P/Y","7:C/Y"};
      return v->tab?vars[i]:calc[i];
    }
    case V_MEMORY:return mem_items[i];case V_RESET:return reset_items[i];
    case V_MEM_MGMT:return mem_mgmt_items[i];case V_CATALOG:return catalog[i];
    case V_MODE:return "";case V_MATH:return math_items[i];
    case V_PRGM:{
      static char label[16];
      if(v->tab==2)return "1:Create New";
      vm_host_program_name(i,label);
      return label;
    }case V_STAT:return stat_items[i];
    case V_VARS:return vars_items[i];case V_YEQ:return y_items[i];
    case V_WINDOW:return window_items[i];case V_ZOOM:return zoom_items[i];
    case V_STAT_PLOTS:return plot_items[i];
    case V_TRACE:return trace_items[i];case V_CALC:return calculate_items[i];case V_TEST:return test_items[i];
    case V_FRAC:return frac_items[i];case V_FUNC:return func_items[i];
    case V_MTRX:return mtrx_items[i];case V_YVAR:return yvar_items[i];
    default:return "";
  }
}

typedef struct {const char *p;unsigned int depth;int error;bool degree;bool graph_x;long double x;} parse_t;
static long double expr(parse_t *p);
static void whitespace(parse_t *p){while(*p->p==' ')p->p++;}
static long double atom(parse_t *p){
  long double n=0;char *end;char fun[8]={0};unsigned int i=0;
  whitespace(p);
  if(++p->depth>20){p->error=1;return 0;}
  if(*p->p=='+'){p->p++;n=atom(p);goto done;}
  if(*p->p=='-'){p->p++;n=-atom(p);goto done;}
  if(*p->p=='('){p->p++;n=expr(p);whitespace(p);if(*p->p==')')p->p++;else p->error=1;goto done;}
  if(p->graph_x && (*p->p=='X'||*p->p=='x') && !isalpha((unsigned char)p->p[1])){p->p++;n=p->x;goto done;}
  if(isalpha((unsigned char)*p->p)){
    while(isalpha((unsigned char)*p->p) && i<sizeof(fun)-1) fun[i++]=*p->p++;
    if(!strcmp(fun,"pi") || !strcmp(fun,"PI")){n=3.1415926535897932384626433832795L;goto done;}
    if(!strcmp(fun,"e")){n=2.7182818284590452353602874713527L;goto done;}
    if(*p->p!='('){p->error=1;goto done;}
    p->p++;n=expr(p);whitespace(p);
    if(*p->p==')')p->p++;else {p->error=1;goto done;}
    if(!strcmp(fun,"sqrt")) n=sqrtl(n);
    else if(!strcmp(fun,"abs")) n=fabsl(n);
    else if(!strcmp(fun,"ln")) n=logl(n);
    else if(!strcmp(fun,"log")) n=log10l(n);
    else if(!strcmp(fun,"sin")) n=sinl(p->degree?n*0.0174532925199432957692369076849L:n);
    else if(!strcmp(fun,"cos")) n=cosl(p->degree?n*0.0174532925199432957692369076849L:n);
    else if(!strcmp(fun,"exp")) n=expl(n);
    else if(!strcmp(fun,"tan")) n=tanl(p->degree?n*0.0174532925199432957692369076849L:n);
    else if(!strcmp(fun,"asin")) {n=asinl(n);if(p->degree)n*=57.295779513082320876798154814105L;}
    else if(!strcmp(fun,"acos")) {n=acosl(n);if(p->degree)n*=57.295779513082320876798154814105L;}
    else if(!strcmp(fun,"atan")) {n=atanl(n);if(p->degree)n*=57.295779513082320876798154814105L;}
    else p->error=1;
    goto done;
  }
  n=strtold(p->p,&end);if(end==p->p)p->error=1;else p->p=end;
 done:p->depth--;return n;
}
static long double powered(parse_t *p){long double n=atom(p);whitespace(p);if(*p->p=='^'){p->p++;n=powl(n,powered(p));}return n;}
static long double product(parse_t *p){long double n=powered(p),d;for(;;){whitespace(p);if(*p->p=='*'){p->p++;n*=powered(p);}else if(*p->p=='/'){p->p++;d=powered(p);if(d==0){p->error=2;return 0;}n/=d;}else break;}return n;}
static long double expr(parse_t *p){long double n=product(p);for(;;){whitespace(p);if(*p->p=='+'){p->p++;n+=product(p);}else if(*p->p=='-'){p->p++;n-=product(p);}else break;}return n;}
static int vm_eval_core(const char *expression,bool degrees,bool graph_x,long double x,long double *answer){
  parse_t p;long double n;
  if(!expression||!*expression||!answer)return 1;
  p.p=expression;p.depth=0;p.error=0;p.degree=degrees;
  p.graph_x=graph_x;p.x=x;
  n=expr(&p);whitespace(&p);
  if(p.error)return p.error;
  if(*p.p)return 1;
  if(!isfinite(n))return 3;
  *answer=n;return 0;
}
static int vm_eval_long(const char *expression,bool degrees,long double *answer){
  return vm_eval_core(expression,degrees,false,0.0L,answer);
}
/* Graph-only evaluator: accepts X inside existing arithmetic/trig syntax.
 * Does not change HOME token handling or TI-OS variables. */
int vm_eval_graph_x(const char *expression,long double x,bool degrees,long double *y){
  return vm_eval_core(expression,degrees,true,x,y);
}
int vm_eval(const char *expression,bool degrees,double *answer){
  long double n;int err=vm_eval_long(expression,degrees,&n);
  if(err)return err;
  if(!answer)return 1;
  *answer=(double)n;return 0;
}
/* CEdev treats double as 32-bit float. Restrict precision so FLOAT never
   exposes IEEE artifacts such as 1.001999974, and remove padded zeros.
   Long-double operations can preserve higher precision when supported. */
void vm_format_precision(long double x,int8_t fix,char *out,unsigned int len){
  char tmp[VM_ANSWER];size_t n;
  if(!out||!len)return;
  if(x==0)x=0;
  if(fix>=0&&fix<=9){
    snprintf(tmp,sizeof(tmp),"%.*Lf",(int)fix,x);
  }else{
    /* 10 significant digits matching TI FLOAT's visible upper bound. */
    int places=x==0?9:9-(int)floorl(log10l(fabsl(x)));
    if(places>=0 && places<=12)snprintf(tmp,sizeof(tmp),"%.*Lf",places,x);
    else snprintf(tmp,sizeof(tmp),"%.9Le",x);
    /* Some CE printf implementations append unnecessary trailing zeros.
       Strip only the fractional zeros before an exponent. */
    char *exp=strchr(tmp,'e');if(!exp)exp=strchr(tmp,'E');
    n=exp?(size_t)(exp-tmp):strlen(tmp);
    if(memchr(tmp,'.',n)){
      size_t end=n;
      while(end && tmp[end-1]=='0')end--;
      if(end && tmp[end-1]=='.')end--;
      if(exp)memmove(tmp+end,exp,strlen(exp)+1);
      else tmp[end]=0;
    }
  }
  strncpy(out,tmp,len-1);out[len-1]=0;
}
void vm_format(double x,char *out,unsigned int len){
  vm_format_precision((long double)x,-1,out,len);
}

static void view(vm_t *v,vm_view_t to){
  v->view=to;v->selected=v->top=v->tab=0;v->second=v->alpha=v->alpha_lock=false;
  if(to==V_YEQ){v->y_focus=VM_Y_FOCUS_EXPR;v->y_plot_focus=0;v->y_style_open=false;}
}
void vm_init(vm_t *v){
  memset(v,0,sizeof(*v));v->view=V_HOME;v->fix_digits=-1;
  v->blink_enabled=v->wrap_enabled=true;
  strcpy(v->about_model,"TI-84 Plus CE");
  strcpy(v->about_version,"5.8.1.0012");
  strcpy(v->about_prod,"13-07-05-0C00");
  strcpy(v->about_id,"00000-00000");
  /* OS 5.8.5 Y= list starts with Y1 enabled and the remaining Y rows off. */
  v->y_enabled[0]=true;
  {unsigned int i;const uint8_t colors[10]={0,1,2,3,4,5,6,0,1,2};
   for(i=0;i<10;i++)v->y_color[i]=colors[i];}
  v->format_choice[3]=1;  /* MEDGRAY grid */
  v->fin_value[5]=1;v->fin_value[6]=1;
}
static void notice(vm_t *v,const char *txt){v->return_view=v->view;strncpy(v->notice,txt,VM_NOTICE-1);v->notice[VM_NOTICE-1]=0;v->view=V_NOTICE;v->second=v->alpha=v->alpha_lock=false;}
static void type_text(vm_t *v,const char *s){
  unsigned int m=(unsigned int)strlen(s),n=(unsigned int)strlen(v->expression);
  if(m+n>=VM_EXPR){notice(v,"ENTRY FULL");return;}
  if(v->caret<n)memmove(v->expression+v->caret+m,v->expression+v->caret,n-v->caret+1);

  else v->expression[n+m]=0;
  memcpy(v->expression+v->caret,s,m);v->caret=(uint8_t)(v->caret+m);v->result_valid=false;
}
static void backspace(vm_t *v){unsigned int n=(unsigned int)strlen(v->expression);if(v->caret>=n)return;memmove(v->expression+v->caret,v->expression+v->caret+1,n-v->caret);v->result_valid=false;}
static void clear_input(vm_t *v){v->expression[0]=0;v->caret=0;v->result_valid=false;v->answer[0]=0;}
static bool simple_fraction(const char *s,long *n,long *d){
  unsigned int field;
  for(field=0;field<2;field++){
    long value=0,sign=1;bool digit=false;
    if(*s++!='(')return false;if(*s=='-'){sign=-1;s++;}
    while(*s>='0' && *s<='9'){digit=true;value=value*10+(*s++-'0');if(value>1000000)return false;}
    if(!digit || *s++!=')')return false;
    if(field==0){*n=value*sign;if(*s++!='/')return false;}else *d=value*sign;
  }
  return *s==0 && *d!=0;
}
static void finance_solve(vm_t *v);
static void evaluate(vm_t *v){
  long double d;int err;long n=0,den=0;
  char complete[VM_EXPR+20];unsigned int len=0,depth=0,i;
  if(!v->expression[0]){
    if(!v->last_entry[0])return;
    type_text(v,v->last_entry);
  }
  if(!strcmp(v->expression,"Pmt_End")||!strcmp(v->expression,"Pmt_Bgn")){
    v->fin_begin=!strcmp(v->expression,"Pmt_Bgn");
    strcpy(v->prev_entry,v->last_entry);strcpy(v->prev_answer,v->last_answer);
    strcpy(v->last_entry,v->expression);strcpy(v->last_answer,"Done");
    v->expression[0]=0;v->caret=0;v->result_valid=true;return;
  }
  {
    static const char *const functions[]={"tvm_N","tvm_I%","tvm_PV","tvm_Pmt","tvm_FV"};
    static const char *const variables[]={"N","I%","PV","PMT","FV","P/Y","C/Y"};
    unsigned int j,old=v->fin_field;
    for(j=0;j<5;j++)if(!strcmp(v->expression,functions[j])){
      v->fin_field=(uint8_t)j;finance_solve(v);v->fin_field=(uint8_t)old;
      if(v->view==V_ERROR)return;
      d=v->fin_value[j];err=0;goto finance_done;
    }
    for(j=0;j<7;j++)if(!strcmp(v->expression,variables[j])){
      d=v->fin_value[j];err=0;goto finance_done;
    }
  }
  if(!strncmp(v->expression,"Nom(",4)||!strncmp(v->expression,"Eff(",4)){
    bool nominal=v->expression[0]=='N';const char *start=v->expression+4;
    const char *comma=strchr(start,',');long double rate,periods;
    if(!comma||comma-start>=VM_EXPR){err=1;goto finance_done;}
    memcpy(complete,start,(size_t)(comma-start));complete[comma-start]=0;
    err=vm_eval_long(complete,v->degree,&rate);if(err)goto finance_done;
    strncpy(complete,comma+1,sizeof(complete)-1);complete[sizeof(complete)-1]=0;
    {char *end=strchr(complete,')');if(end)*end=0;}
    err=vm_eval_long(complete,v->degree,&periods);if(err)goto finance_done;
    if(periods<=0||rate<=-100){err=3;goto finance_done;}
    d=nominal?100*periods*(powl(1+rate/100,1/periods)-1):
              100*(powl(1+rate/(100*periods),periods)-1);
    if(!isfinite(d)){err=3;goto finance_done;}err=0;goto finance_done;
  }
  len=(unsigned int)strlen(v->expression);
  memcpy(complete,v->expression,len+1);
  for(i=0;i<len;i++){
    if(complete[i]=='(')depth++;
    else if(complete[i]==')' && depth)depth--;
  }
  if(depth>19){err=1;}
  else{
    while(depth){complete[len++]=')';depth--;}
    complete[len]=0;
    err=vm_eval_long(complete,v->degree,&d);
  }
finance_done:
  if(err){v->error_kind=(uint8_t)err;v->view=V_ERROR;v->selected=0;v->second=v->alpha=v->alpha_lock=false;return;}
  strcpy(v->prev_entry,v->last_entry);
  strcpy(v->prev_answer,v->last_answer);
  snprintf(v->last_entry,VM_EXPR,"%s",v->expression);
  vm_format_precision(d,v->fix_digits,v->answer,VM_ANSWER);
  snprintf(v->last_answer,VM_ANSWER,"%s",v->answer);
  if(v->fix_digits<0 && simple_fraction(v->expression,&n,&den)){
    long a=n<0?-n:n,b=den<0?-den:den,t;
    while(b){t=a%b;a=b;b=t;}
    if(a){n/=a;den/=a;}if(den<0){den=-den;n=-n;}
    if(den!=1)snprintf(v->last_answer,VM_ANSWER,"(%ld)/(%ld)",n,den);
  }
  v->result_valid=true;v->expression[0]=0;v->caret=0;
}
static void move(vm_t *v,int delta){int sz=(int)vm_menu_size(v),pos;if(sz<=0)return;pos=(int)v->selected+delta;if(pos<0)pos=v->wrap_enabled?sz-1:0;else if(pos>=sz)pos=v->wrap_enabled?0:sz-1;v->selected=(uint16_t)pos;if(pos<v->top)v->top=(uint16_t)pos;else if(pos>=(int)v->top+9)v->top=(uint16_t)(pos-8);}
static void select_menu(vm_t *v){unsigned int index=v->selected;switch(v->view){
  case V_APPS:
    if(index==0)view(v,V_SETTINGS);
    else if(index==2)view(v,V_FIN_MENU);
    else if(index==1){vm_plugin_refresh(v);view(v,V_PLUGINS);}
    else {unsigned int plugin=vm_plugin_app_index(v,index-COUNT(app_items));
      if(plugin<vm_plugin_count()){view(v,V_PLUGIN_PAGE);v->plugin_active=(uint8_t)plugin;}}
    break;
  case V_MEM_MGMT:
    if(index<=7)view(v,V_FILE_LIST);
    else notice(v,"APPS ARE MANAGED BY OS");
    break;
  case V_SETTINGS:
    if(index==0)view(v,V_BATTERY_MENU);
    else if(index==1)view(v,V_BAR_COLOR);
    break;
  case V_BATTERY_MENU:
    if(index==0)view(v,V_BATTERY_EDITOR);
    else if(index==1)view(v,V_CHARGE_EDITOR);
    break;
  case V_PLUGINS:
    if(index<vm_plugin_count()&&!vm_plugin_toggle(v,index))notice(v,"PLUGIN SAVE FAILED");
    break;
  case V_PLUGIN_DELETE:
    if(index==0)view(v,V_PLUGINS);
    else if(index==1){
      int result=vm_plugin_delete(v);
      if(result){view(v,V_PLUGINS);if(result==2)notice(v,"DELETED; SAVE FAILED");}
      else notice(v,"PLUGIN DELETE FAILED");
    }
    break;
  case V_PRGM:
    if(v->tab==2)notice(v,"HOST CREATE NOT READY");
    else if(v->tab==1 && index<v->program_count)
      notice(v,"HOST EDITOR NOT READY");
    break;
  case V_MEMORY:
    switch(index){
      case 0:view(v,V_ABOUT);break;case 1:view(v,V_MEM_MGMT);break;
      case 2:v->last_entry[0]=0;notice(v,"ENTRIES CLEARED");break;
      case 3:notice(v,"LISTS EMPTY");break;
      case 4:case 5:notice(v,"NO VIRTUAL VARIABLES");break;
      case 6:view(v,V_RESET);break;
      case 7:view(v,V_GROUP);break;
      case 8:v->exit_requested=true;break;
    }break;
  case V_RESET:v->reset_kind=(uint8_t)index;view(v,V_RESET_CONFIRM);break;
  case V_CATALOG:{
    /* The CATALOG must paste the selected token and return to the entry
       screen.  This lightweight VM stores a text approximation internally;
       unsupported tokens can be edited but may fail evaluation. */
    const char *token=catalog[index];
    view(v,V_HOME);
    type_text(v,token);
    break;
  }
  case V_MODE:
    if(index==2){
      v->fix_digits=v->mode_precision_cursor==0?-1:(int8_t)(v->mode_precision_cursor-1);
    }else if(index==3){v->degree=!v->degree;}
    break;
  case V_FRAC:if(index==0){view(v,V_HOME);type_text(v,"()/()");v->caret-=4;}else notice(v,"FRACTION OPTION NOT IMPLEMENTED");break;
  case V_FUNC:if(index==0){view(v,V_HOME);type_text(v,"abs(");}else notice(v,"SHORTCUT NOT IMPLEMENTED");break;
  case V_YVAR:notice(v,"Y-VARIABLE NOT STORED");break;
  case V_MATH:if(index==2){view(v,V_HOME);type_text(v,"^3");}else notice(v,"MATH TOKEN NOT IMPLEMENTED");break;
  case V_ZOOM:notice(v,"ZOOM FEATURE NOT IMPLEMENTED");break;
  case V_CALC:notice(v,"CALCULATE NOT IMPLEMENTED");break;
  default:notice(v,"FEATURE NOT IMPLEMENTED");break;
}}
static void catalog_jump(vm_t *v,char c){unsigned int i;if(c>='a'&&c<='z')c=(char)(c-'a'+'A');for(i=0;i<COUNT(catalog);i++){char t=catalog[i][0];if(t>='a'&&t<='z')t=(char)(t-'a'+'A');if(t>=c){v->selected=(uint8_t)i;v->top=(uint8_t)i;return;}}}
static void second_key(vm_t *v,vm_key_t k){
  switch(k){
    case K_XVAR:view(v,V_LINK);break;
    case K_ON:v->sleeping=true;break;
    case K_7:view(v,V_HOME);type_text(v,"u");break;
    case K_8:view(v,V_HOME);type_text(v,"v");break;
    case K_9:view(v,V_HOME);type_text(v,"w");break;
    case K_DOT:view(v,V_HOME);type_text(v,"i");break;
    case K_STO:view(v,V_RECALL);break;
    case K_0:view(v,V_CATALOG);break;
    case K_ADD:view(v,V_MEMORY);break;
    case K_MODE:view(v,V_HOME);break;
    case K_DEL:v->insert=!v->insert;break;
    case K_ENTER:view(v,V_HOME);clear_input(v);type_text(v,v->last_entry);break;
    case K_NEG:view(v,V_HOME);type_text(v,v->last_answer[0]?v->last_answer:"0");break;
    case K_YEQ:case K_WINDOW:case K_ZOOM:case K_TRACE:case K_GRAPH:
      view(v,k==K_YEQ?V_STAT_PLOTS:k==K_WINDOW?V_TBLSET:k==K_ZOOM?V_FORMAT:k==K_TRACE?V_CALC:V_TABLE);break;
    case K_MATH:view(v,V_TEST);break;
    case K_APPS:view(v,V_ANGLE);break;
    case K_PRGM:view(v,V_DRAW);break;
    case K_VARS:view(v,V_DISTR);break;
    case K_STAT:view(v,V_LIST);break;
    case K_RECIP:view(v,V_MATRIX);break;
    case K_SIN:view(v,V_HOME);type_text(v,"asin(");break;
    case K_COS:view(v,V_HOME);type_text(v,"acos(");break;
    case K_TAN:view(v,V_HOME);type_text(v,"atan(");break;
    case K_SQUARE:view(v,V_HOME);type_text(v,"sqrt()");v->caret--;break;
    case K_LOG:view(v,V_HOME);type_text(v,"10^()");v->caret--;break;
    case K_LN:view(v,V_HOME);type_text(v,"exp()");v->caret--;break;
    case K_POWER:view(v,V_HOME);type_text(v,"pi");break;
    case K_DIV:view(v,V_HOME);type_text(v,"e");break;
    case K_LPAREN:view(v,V_HOME);type_text(v,"[");break;
    case K_RPAREN:view(v,V_HOME);type_text(v,"]");break;
    case K_1:case K_2:case K_3:case K_4:case K_5:case K_6:{char s[3]={'L','1',0};s[1]=(char)('1'+(k==K_1?0:k==K_2?1:k==K_3?2:k==K_4?3:k==K_5?4:5));view(v,V_HOME);type_text(v,s);break;}
    default:break;
  }
}
/* Y= editor stores its text in virtual RAM, never in the actual TI-OS Y-vars. */
static void y_insert(vm_t *v,const char *txt){
  char *dest=v->y_equations[v->selected];
  unsigned int n=(unsigned int)strlen(dest),m=(unsigned int)strlen(txt);
  unsigned int pos=v->y_caret[v->selected];
  if(m+n>=VM_Y_EXPR_LEN)return;
  if(pos>n)pos=n;
  memmove(dest+pos+m,dest+pos,n-pos+1);
  memcpy(dest+pos,txt,m);
  v->y_caret[v->selected]=(uint8_t)(pos+m);
}
static void y_delete(vm_t *v){
  char *dest=v->y_equations[v->selected];
  unsigned int pos=v->y_caret[v->selected],n=(unsigned int)strlen(dest);
  if(pos>0 && pos<=n){memmove(dest+pos-1,dest+pos,n-pos+1);v->y_caret[v->selected]--;}
}
static void y_select_row(vm_t *v,int direction){
  int row=(int)v->selected+direction;
  if(row<0){v->y_focus=VM_Y_FOCUS_PLOT;v->y_plot_focus=0;return;}
  if(row>9)row=9;
  v->selected=(uint8_t)row;
  if(row<v->top)v->top=(uint8_t)row;
  else if(row>=(int)v->top+9)v->top=(uint8_t)(row-8);
}
static bool y_key(vm_t *v,vm_key_t k){
  if(v->y_style_open){
    switch(k){
      case K_CLEAR:v->y_style_open=false;return true;
      case K_UP:v->y_style_field=(uint8_t)(v->y_style_field==0?2:v->y_style_field==3?2:v->y_style_field-1);return true;
      case K_DOWN:v->y_style_field=(uint8_t)(v->y_style_field==2||v->y_style_field==3?0:v->y_style_field+1);return true;
      case K_ENTER:
        if(v->y_style_field<2){v->y_style_field++;return true;}
        if(v->y_style_field==2){
          v->y_color[v->selected]=v->y_preview_color;
          v->y_line_style[v->selected]=v->y_preview_line;
        }
        v->y_style_open=false;return true;
      case K_LEFT:case K_RIGHT:
        if(v->y_style_field==0){
          v->y_preview_color=(uint8_t)((v->y_preview_color+(k==K_LEFT?14:1))%15);
        }else if(v->y_style_field==1){
          v->y_preview_line=(uint8_t)((v->y_preview_line+(k==K_LEFT?3:1))%4);
        }
        else if(k==K_RIGHT && v->y_style_field==2)v->y_style_field=3;
        else if(k==K_LEFT && v->y_style_field==3)v->y_style_field=2;
        return true;
      default:return true;
    }
  }
  if(v->y_focus==VM_Y_FOCUS_PLOT){
    if(k==K_LEFT){if(v->y_plot_focus>0)v->y_plot_focus--;}
    else if(k==K_RIGHT){if(v->y_plot_focus<2)v->y_plot_focus++;}
    else if(k==K_DOWN){v->y_focus=VM_Y_FOCUS_EXPR;v->selected=0;v->top=0;}
    else if(k==K_ENTER)notice(v,"PLOT SETTINGS NOT IMPLEMENTED");
    else if(k==K_CLEAR)view(v,V_HOME);
    return true;
  }
  if(k==K_UP){y_select_row(v,-1);return true;}
  if(k==K_DOWN){y_select_row(v,1);return true;}
  if(v->y_focus==VM_Y_FOCUS_STYLE){
    if(k==K_ENTER){v->y_style_open=true;v->y_style_field=0;
      v->y_saved_color=v->y_color[v->selected];v->y_saved_line=v->y_line_style[v->selected];
      v->y_preview_color=v->y_saved_color;v->y_preview_line=v->y_saved_line;}
    else if(k==K_RIGHT)v->y_focus=VM_Y_FOCUS_EQUALS;
    else if(k==K_CLEAR)view(v,V_HOME);
    return true;
  }
  /* OS 5.8.5 has a separate selectable equals-sign cell.  The cursor
   * steps formula -> [=] -> line-style, not directly formula -> style. */
  if(v->y_focus==VM_Y_FOCUS_EQUALS){
    if(k==K_LEFT)v->y_focus=VM_Y_FOCUS_STYLE;
    else if(k==K_RIGHT)v->y_focus=VM_Y_FOCUS_EXPR;
    else if(k==K_ENTER)v->y_enabled[v->selected]=!v->y_enabled[v->selected];
    else if(k==K_CLEAR)view(v,V_HOME);
    return true;
  }
  if(k==K_LEFT){
    if(v->y_caret[v->selected])v->y_caret[v->selected]--;
    else v->y_focus=VM_Y_FOCUS_EQUALS;
    return true;
  }
  if(k==K_RIGHT){
    if(v->y_caret[v->selected]<strlen(v->y_equations[v->selected]))v->y_caret[v->selected]++;
    return true;
  }
  if(k==K_ENTER){y_select_row(v,1);return true;}
  if(k==K_DEL){y_delete(v);return true;}
  if(k==K_CLEAR){
    if(v->y_equations[v->selected][0]){
      v->y_equations[v->selected][0]=0;v->y_caret[v->selected]=0;
    }else view(v,V_HOME);
    return true;
  }
  {const char *text=0;
    switch(k){
      case K_XVAR:text="X";break;case K_0:text="0";break;case K_1:text="1";break;
      case K_2:text="2";break;case K_3:text="3";break;case K_4:text="4";break;
      case K_5:text="5";break;case K_6:text="6";break;case K_7:text="7";break;
      case K_8:text="8";break;case K_9:text="9";break;
      case K_ADD:text="+";break;case K_SUB:text="-";break;
      case K_MUL:text="*";break;case K_DIV:text="/";break;
      case K_POWER:text="^";break;case K_SQUARE:text="^2";break;
      case K_LPAREN:text="(";break;case K_RPAREN:text=")";break;
      case K_DOT:text=".";break;case K_NEG:text="-";break;
      case K_SIN:text="sin(";break;case K_COS:text="cos(";break;
      case K_TAN:text="tan(";break;case K_LN:text="ln(";break;
      case K_LOG:text="log(";break;default:break;
    }
    if(text)y_insert(v,text);
  }
  return true;
}
#include "finance_vm.h"
void vm_key(vm_t *v,vm_key_t k){
  char ch;bool sec;
  if(v->sleeping){if(k==K_ON)v->sleeping=false;return;}
  if(k==K_NONE||v->exit_requested)return;
  if(v->view==V_ERROR){
    if(k==K_UP||k==K_DOWN){v->selected^=1;return;}
    if(k==K_2)v->selected=1;
    if(k==K_1)v->selected=0;
    if(k==K_2||(k==K_ENTER&&v->selected==1)){
      v->view=V_HOME;v->caret=(uint8_t)strlen(v->expression);return;
    }
    if(k==K_1||k==K_ENTER||k==K_CLEAR){
      strcpy(v->prev_entry,v->last_entry);
      strcpy(v->prev_answer,v->last_answer);
      strcpy(v->last_entry,v->expression);
      strcpy(v->last_answer,"Error");
      v->expression[0]=0;v->caret=0;v->view=V_HOME;return;
    }
    return;
  }
  if(k==K_2ND){
    v->second=!v->second;v->alpha=false;return;
  }
  if(k==K_ALPHA){if(v->second){v->alpha_lock=true;v->alpha=false;v->second=false;}else if(v->alpha_lock){v->alpha_lock=false;v->alpha=false;}else v->alpha=!v->alpha;return;}
  sec=v->second;v->second=false;
  if(v->view==V_FIN_MENU||v->view==V_FIN_SOLVER){finance_key(v,k,sec);return;}
  if(sec){second_key(v,k);return;}
  if(v->view==V_RESET_CONFIRM){
    if(k==K_UP||k==K_DOWN){v->selected^=1;return;}
    if(k==K_1)v->selected=0;
    if(k==K_2)v->selected=1;
    if(k==K_ENTER||k==K_1||k==K_2){
      if(v->selected==0){view(v,V_HOME);return;}
      else {unsigned int n=v->resets+1;uint8_t kind=v->reset_kind;
        vm_init(v);v->resets=(uint16_t)n;v->reset_kind=kind;v->view=V_RESET_RESULT;return;}
    }
    if(k==K_CLEAR){view(v,V_RESET);}
    return;
  }
  if(v->view==V_RESET_RESULT){if(k==K_ENTER||k==K_CLEAR||k==K_MODE)view(v,V_HOME);return;}
  if(v->view==V_NOTICE){v->view=v->return_view;return;}
  /* New-program name editor owns the keys. NEW begins in ALPHA LOCK,
   * like the CE OS. Do not treat A-Z as commands or global shortcuts. */
  if(v->view==V_PRGM_NAME){
    size_t n=strlen(v->new_program);
    if(k==K_CLEAR){view(v,V_PRGM);v->tab=2;return;}
    if(k==K_DEL){if(n)v->new_program[n-1]=0;return;}
    if(k==K_ENTER){
      unsigned int i;
      if(!n)return;
      for(i=0;i<v->program_count;i++)
        if(!strcmp(v->program_names[i],v->new_program)){notice(v,"NAME ALREADY EXISTS");return;}
      if(v->program_count>=VM_PROGRAM_CAP){notice(v,"PROGRAM LIST FULL");return;}
      v->active_program=v->program_count++;
      strcpy(v->program_names[v->active_program],v->new_program);
      view(v,V_PRGM_EDITOR);return;
    }
    {char c=v->alpha_lock?vm_alpha_character(k):0;
     if(!c){
       switch(k){
         case K_0:c='0';break;case K_1:c='1';break;
         case K_2:c='2';break;case K_3:c='3';break;
         case K_4:c='4';break;case K_5:c='5';break;
         case K_6:c='6';break;case K_7:c='7';break;
         case K_8:c='8';break;case K_9:c='9';break;
         default:break;
       }
     }
     if(c && n<VM_PROGRAM_NAME-1 && (n>0|| (c>='A'&&c<='Z'))){
       v->new_program[n]=c;v->new_program[n+1]=0;
     }}return;
  }
  if(v->view==V_PRGM_EDITOR){
    if(k==K_CLEAR||k==K_MODE){view(v,V_PRGM);v->tab=1;}
    return;
  }
  /* TI's ALPHA F1-F4 shortcut menus (F5 is context-dependent). */
  if(v->alpha||v->alpha_lock){
    if(v->view==V_HOME){unsigned int plugin=vm_plugin_hotkey_index(v,k);
      if(plugin<plugins_count){view(v,V_PLUGIN_PAGE);v->plugin_active=(uint8_t)plugin;return;}}
    if(v->view==V_CATALOG && k==K_XVAR){
      v->selected=(uint8_t)CATALOG_X_START;
      v->top=(uint8_t)CATALOG_X_START;return;
    }
    if(v->view==V_HOME && k==K_XVAR){
      type_text(v,"()/()");v->caret-=4;
      if(v->alpha&&!v->alpha_lock)v->alpha=false;
      return;
    }
    if(k==K_YEQ||k==K_WINDOW||k==K_ZOOM||k==K_TRACE||k==K_GRAPH){
      view(v,k==K_YEQ?V_FRAC:k==K_WINDOW?V_FUNC:k==K_ZOOM?V_MTRX:
                k==K_TRACE?V_YVAR:V_GRAPH);return;
    }
    if(k==K_LEFT||k==K_RIGHT||k==K_UP||k==K_DOWN){v->alpha=false;}
  }
  ch=(v->alpha||v->alpha_lock)?vm_alpha_character(k):0;
  if(v->alpha&&!v->alpha_lock)v->alpha=false;
  if(ch){char s[2]={ch,0};if(v->view==V_CATALOG)catalog_jump(v,ch);else if(v->view==V_HOME)type_text(v,s);else if(v->view==V_YEQ && v->y_focus==VM_Y_FOCUS_EXPR && !v->y_style_open)y_insert(v,s);return;}
  if(v->view==V_YEQ){y_key(v,k);return;}
  if(k==K_CLEAR){
    if(v->view==V_HOME){
      if(v->expression[0]){
        /* First CLEAR cancels an active edit without erasing previous work. */
        clear_input(v);
      }else{
        /* When no text is being edited, CLEAR must actually clear HOME,
         * including the previous ENTER result and its dotted separator. */
        clear_input(v);
        v->last_entry[0]=0;
        v->last_answer[0]=0;
        v->prev_entry[0]=0;
        v->prev_answer[0]=0;
      }
    }else if(v->view==V_PLUGIN_DELETE)view(v,V_PLUGINS);
    else if(v->view==V_PLUGIN_PAGE)view(v,V_APPS);
    else if(v->view==V_BATTERY_MENU)view(v,V_SETTINGS);
    else if(v->view==V_SETTINGS)view(v,V_APPS);
    else view(v,V_HOME);
    return;
  }
  if(k==K_MODE&&v->view==V_PLUGIN_DELETE){view(v,V_PLUGINS);return;}
  if(k==K_MODE&&v->view==V_PLUGIN_PAGE){view(v,V_APPS);return;}
  if(k==K_MODE){view(v,V_MODE);v->mode_precision_cursor=
                        v->fix_digits<0?0:(uint8_t)(v->fix_digits+1);return;}
  if(k==K_YEQ){view(v,V_YEQ);return;}
  if(k==K_WINDOW){view(v,V_WINDOW);return;}
  if(k==K_ZOOM){view(v,V_ZOOM);return;}
  if(k==K_TRACE){view(v,V_TRACE);return;}
  if(k==K_GRAPH){view(v,V_GRAPH);return;}
  if(k==K_MATH){view(v,V_MATH);return;}
  if(k==K_PRGM){view(v,V_PRGM);return;}
  if(k==K_STAT){view(v,V_STAT);return;}
  if(k==K_VARS){view(v,V_VARS);return;}
  if(k==K_APPS){view(v,V_APPS);return;}
  if(k==K_ON){notice(v,"USE MEMORY 9:QUIT TO EXIT");return;}
  if(v->view==V_ABOUT){if(k==K_ENTER||k==K_MODE)view(v,V_MEMORY);return;}
  if(v->view==V_GRAPH){return;}
  if(v->view==V_FORMAT){
    if(k==K_UP) v->selected=(uint8_t)((v->selected+9)%10);
    else if(k==K_DOWN) v->selected=(uint8_t)((v->selected+1)%10);
    else if(k==K_LEFT||k==K_RIGHT||k==K_ENTER){
      static const uint8_t counts[10]={2,2,3,3,2,2,2,9,5,2};
      uint8_t idx=v->selected;
      v->format_choice[idx]=(uint8_t)((v->format_choice[idx]+(k==K_LEFT?counts[idx]-1:1))%counts[idx]);
    }
    return;
  }
  if(v->view==V_TBLSET){
    if(k==K_UP) v->selected=(uint8_t)((v->selected+3)%4);
    else if(k==K_DOWN)v->selected=(uint8_t)((v->selected+1)%4);
    else if((k==K_LEFT||k==K_RIGHT||k==K_ENTER) && v->selected>=2)
      v->tbl_choice[v->selected-2]^=1;
    return;
  }
  if(v->view==V_STAT_PLOTS){
    if(k==K_UP)move(v,-1);
    else if(k==K_DOWN)move(v,1);
    else if(k==K_ENTER){
      if(v->selected<3)v->plots_enabled[v->selected]^=1;
      else if(v->selected==3){memset(v->plots_enabled,0,sizeof(v->plots_enabled));}
      else memset(v->plots_enabled,1,sizeof(v->plots_enabled));
    }
    return;
  }
  if(v->view==V_MODE){
    if(k==K_UP||k==K_DOWN){move(v,k==K_DOWN?1:-1);return;}
    if(v->selected==2){
      if(k==K_LEFT){v->mode_precision_cursor=(uint8_t)((v->mode_precision_cursor+10)%11);return;}
      if(k==K_RIGHT){v->mode_precision_cursor=(uint8_t)((v->mode_precision_cursor+1)%11);return;}
      if(k==K_0||k==K_1||k==K_2||k==K_3||k==K_4||k==K_5||
         k==K_6||k==K_7||k==K_8||k==K_9){
        int digit=k==K_0?0:k==K_1?1:k==K_2?2:k==K_3?3:
                  k==K_4?4:k==K_5?5:k==K_6?6:k==K_7?7:k==K_8?8:9;
        v->fix_digits=(int8_t)digit;
        v->mode_precision_cursor=(uint8_t)(digit+1);
        return;
      }
    }
    if(k==K_ENTER)select_menu(v);
    return;
  }
  if(os_tab_count(v->view)){
    unsigned int tabs=os_tab_count(v->view);
    if(k==K_LEFT||k==K_RIGHT){
      if(tabs<=1)return;
      v->tab=(uint8_t)((v->tab+tabs+(k==K_LEFT?-1:1))%tabs);
      v->selected=v->top=0;return;
    }
    if(k==K_UP||k==K_DOWN){move(v,k==K_UP?-1:1);return;}
    if(k==K_ENTER){
      if(!vm_menu_size(v))return;
      if(v->view==V_RESET){select_menu(v);return;}
      if(v->view==V_MATH && v->tab==0 && v->selected==2){view(v,V_HOME);type_text(v,"^3");}
      else notice(v,"FEATURE NOT IMPLEMENTED");
      return;
    }
    {int n=k==K_1?0:k==K_2?1:k==K_3?2:k==K_4?3:k==K_5?4:k==K_6?5:k==K_7?6:k==K_8?7:k==K_9?8:k==K_0?9:-1;
      if(n>=0 && (unsigned int)n<vm_menu_size(v)){
        v->selected=(uint8_t)n;if(v->view==V_RESET)select_menu(v);else notice(v,"FEATURE NOT IMPLEMENTED");
      }}
    return;
  }
  if(v->view==V_PRGM){
    if(k==K_LEFT||k==K_RIGHT){
      int t=(int)v->tab+(k==K_RIGHT?1:-1);
      if(t<0)t=2;else if(t>2)t=0;
      v->tab=(uint8_t)t;v->selected=v->top=0;return;
    }
    if(k==K_UP)move(v,-1);else if(k==K_DOWN)move(v,1);
    else if(k==K_ENTER)select_menu(v);
    return;
  }
  if(v->view==V_FRAC && k==K_1){v->selected=0;select_menu(v);return;}
  if((v->view==V_FRAC || v->view==V_FUNC || v->view==V_MTRX || v->view==V_YVAR) && (k==K_LEFT||k==K_RIGHT)){
    static const vm_view_t tabs[4]={V_FRAC,V_FUNC,V_MTRX,V_YVAR};unsigned int i;
    for(i=0;i<4;i++)if(tabs[i]==v->view){view(v,tabs[(i+(k==K_LEFT?3:1))%4]);return;}
  }
  if(v->view!=V_HOME){
    if(v->view==V_PLUGINS&&k==K_DEL&&v->selected<vm_plugin_count()){
      v->plugin_target=(uint8_t)v->selected;view(v,V_PLUGIN_DELETE);return;
    }
    if(k==K_UP)move(v,-1);
    else if(k==K_DOWN)move(v,1);
    else if(k==K_ENTER)select_menu(v);
    if(v->view==V_APPS || v->view==V_SETTINGS || v->view==V_BATTERY_MENU || v->view==V_MEM_MGMT || v->view==V_PLUGINS || v->view==V_PLUGIN_DELETE){
      int n=k==K_1?0:k==K_2?1:k==K_3?2:k==K_4?3:k==K_5?4:k==K_6?5:k==K_7?6:k==K_8?7:k==K_9?8:-1;
      if(n>=0 && (unsigned int)n<vm_menu_size(v)){v->selected=(uint8_t)n;select_menu(v);}return;
    }
    if(v->view==V_MEMORY){int n=k==K_1?1:k==K_2?2:k==K_3?3:k==K_4?4:k==K_5?5:k==K_6?6:k==K_7?7:k==K_8?8:k==K_9?9:0;if(n){v->selected=(uint8_t)(n-1);select_menu(v);}}
    else if(v->view==V_RESET){int n=k==K_1?1:k==K_2?2:0;if(n){v->selected=(uint8_t)(n-1);select_menu(v);}}
    return;
  }
  if(k==K_LEFT){v->insert=false;if(v->caret>0)v->caret--;return;}
  if(k==K_RIGHT){v->insert=false;if(v->caret<strlen(v->expression))v->caret++;return;}
  if(k==K_DOWN){char *d=strstr(v->expression,")/(");if(d){v->caret=(uint8_t)(d-v->expression+3);return;}}
  if(k==K_UP){char *d=strstr(v->expression,")/(");if(d){v->caret=1;return;}clear_input(v);type_text(v,v->last_entry);return;}
  if(k==K_DEL){backspace(v);return;}
  if(k==K_ENTER){evaluate(v);return;}
  if(k==K_0)type_text(v,"0");else if(k==K_1)type_text(v,"1");
  else if(k==K_2)type_text(v,"2");else if(k==K_3)type_text(v,"3");
  else if(k==K_4)type_text(v,"4");else if(k==K_5)type_text(v,"5");
  else if(k==K_6)type_text(v,"6");else if(k==K_7)type_text(v,"7");
  else if(k==K_8)type_text(v,"8");else if(k==K_9)type_text(v,"9");
  else if(k==K_DOT)type_text(v,".");else if(k==K_COMMA)type_text(v,",");
  else if(k==K_ADD)type_text(v,"+");else if(k==K_SUB)type_text(v,"-");
  else if(k==K_MUL)type_text(v,"*");else if(k==K_DIV)type_text(v,"/");
  else if(k==K_NEG)type_text(v,"-");else if(k==K_POWER){type_text(v,"^()");v->caret--;}
  else if(k==K_LPAREN)type_text(v,"(");else if(k==K_RPAREN){if(v->expression[v->caret]==')')v->caret++;else type_text(v,")");}
  else if(k==K_SQUARE)type_text(v,"^2");else if(k==K_RECIP){if(!v->expression[0])type_text(v,"Ans");type_text(v,"^(-1)");}
  else if(k==K_SIN)type_text(v,"sin(");else if(k==K_COS)type_text(v,"cos(");
  else if(k==K_TAN)type_text(v,"tan(");else if(k==K_LOG)type_text(v,"log(");
  else if(k==K_LN)type_text(v,"ln(");else if(k==K_XVAR)type_text(v,"X");
 }
