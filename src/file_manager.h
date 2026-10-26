/* Native calculator VAT browser. No virtual file list is kept by the VM. */
typedef struct { char name[10]; uint8_t type; } fm_entry_t;
static fm_entry_t fm_active;
static uint16_t fm_count,fm_selected,fm_top;
static char fm_edit[10];
static bool fm_alpha=true;
static char fm_message[40];
static uint16_t fm_offset;
/* 0=all, 1=real, 2=complex, 3=list, 4=matrix, 5=equation,
   6=program, 7=AppVar. Matches MEM MGMT/DEL's first eight rows. */
static uint8_t fm_category;

static bool fm_valid_name(const char *name,uint8_t type){
  unsigned int i;
  if(!name||!name[0]||name[0]=='!'||name[0]=='#')return false;
  for(i=0;i<8&&name[i];i++){
    unsigned char c=(unsigned char)name[i];
    if(type==OS_TYPE_PRGM||type==OS_TYPE_PROT_PRGM||type==OS_TYPE_APPVAR)
      if((c<'A'||c>'Z')&&(c<'0'||c>'9'))return false;
  }
  if(i==8&&name[i])return false;
  if((type==OS_TYPE_PRGM||type==OS_TYPE_PROT_PRGM)&&
     (name[0]<'A'||name[0]>'Z'))return false;
  return true;
}
static bool fm_category_match(uint8_t type){
  switch(fm_category){
    case 0:return type==OS_TYPE_REAL||type==OS_TYPE_REALFRAC||
      type==OS_TYPE_CPLX||type==OS_TYPE_REAL_LIST||type==OS_TYPE_CPLX_LIST||
      type==OS_TYPE_MATRIX||type==OS_TYPE_EQU||type==OS_TYPE_STR||
      type==OS_TYPE_PRGM||type==OS_TYPE_PROT_PRGM||type==OS_TYPE_APPVAR||
      type==OS_TYPE_PIC||type==OS_TYPE_IMAGE||type==OS_TYPE_GDB;
    case 1:return type==OS_TYPE_REAL||type==OS_TYPE_REALFRAC;
    case 2:return type==OS_TYPE_CPLX||type==OS_TYPE_CPLX_LIST;
    case 3:return type==OS_TYPE_REAL_LIST||type==OS_TYPE_CPLX_LIST;
    case 4:return type==OS_TYPE_MATRIX;
    case 5:return type==OS_TYPE_EQU;
    case 6:return type==OS_TYPE_PRGM||type==OS_TYPE_PROT_PRGM;
    case 7:return type==OS_TYPE_APPVAR;
    default:return false;
  }
}
static bool fm_usable(const char *name,uint8_t type){
  uint8_t h;
  if(!fm_valid_name(name,type)||!fm_category_match(type))return false;
  h=ti_OpenVar(name,"r",type);
  if(!h)return false;
  ti_Close(h);
  return true;
}

static const char *fm_type_name(uint8_t type){
  switch(type){
    case OS_TYPE_PRGM:return "TI-BASIC";
    case OS_TYPE_PROT_PRGM:return "Protected Program";
    case OS_TYPE_APPVAR:return "AppVar";
    case OS_TYPE_REAL:return "Real";
    case OS_TYPE_CPLX:return "Complex";
    case OS_TYPE_REAL_LIST:return "List";
    case OS_TYPE_CPLX_LIST:return "Complex List";
    case OS_TYPE_MATRIX:return "Matrix";
    case OS_TYPE_EQU:return "Equation";
    case OS_TYPE_STR:return "String";
    case OS_TYPE_PIC:return "Picture";
    case OS_TYPE_IMAGE:return "Image";
    case OS_TYPE_GDB:return "Graph Database";
    default:return "TI Variable";
  }
}
static bool fm_lookup(uint16_t index,fm_entry_t *out){
  void *vat=NULL;char *name;uint8_t type;char copied[10];
  while((name=ti_DetectAny(&vat,NULL,&type))){
    strncpy(copied,name,9);copied[9]=0;
    if(!fm_usable(copied,type))continue;
    if(!index--){strcpy(out->name,copied);
      out->type=type;return true;}
  }
  return false;
}
static void fm_refresh(void){
  void *vat=NULL;uint8_t type;char *name;char copied[10];fm_count=0;
  while((name=ti_DetectAny(&vat,NULL,&type))){
    strncpy(copied,name,9);copied[9]=0;
    if(fm_usable(copied,type))fm_count++;
  }
  if(fm_selected>=fm_count)fm_selected=fm_count?fm_count-1:0;
  if(fm_top>fm_selected)fm_top=fm_selected;
  memset(&fm_active,0,sizeof(fm_active));
  if(fm_count)fm_lookup(fm_selected,&fm_active);
}
static void fm_select_name(const char *name,uint8_t type){
  fm_entry_t entry;uint16_t i;
  for(i=0;i<fm_count;i++){
    if(fm_lookup(i,&entry)&&entry.type==type&&!strcmp(entry.name,name)){
      fm_selected=i;
      if(fm_selected<fm_top||fm_selected>=fm_top+9)fm_top=fm_selected;
      fm_active=entry;return;
    }
  }
}
static fm_entry_t *fm_current(void){return fm_selected<fm_count?&fm_active:NULL;}
static bool fm_protected(const fm_entry_t *e){
  if(!e)return true;
  if(e->type==OS_TYPE_APPVAR &&
     (!strcmp(e->name,"TVMSPRT")||!strcmp(e->name,"TVMBOOT")||!strcmp(e->name,"TVMSET")||!strcmp(e->name,"TVMEXEC")||
      !strcmp(e->name,"TI84VM0")||!strcmp(e->name,"TI84VM1")))return true;
  return (e->type==OS_TYPE_PRGM||e->type==OS_TYPE_PROT_PRGM)&&
         (!strcmp(e->name,"TI84VM")||!strcmp(e->name,"TVMRUN"));
}
static uint8_t fm_open(void){
  fm_entry_t *e=fm_current();
  return e?ti_OpenVar(e->name,"r",e->type):0;
}
static void fm_header(const char *title){
  body();inverted_tab(title,0,37);
}
static void fm_row(unsigned int i){
  fm_entry_t e;char label[24],safe[10];unsigned int j;
  int y=57+(int)(i-fm_top)*20;
  if(i>=fm_count||i<fm_top||i>=fm_top+9||!fm_lookup((uint16_t)i,&e))return;
  for(j=0;j<8&&e.name[j];j++){
    unsigned char c=(unsigned char)e.name[j];
    safe[j]=(c>=32&&c<=126)?(char)c:'?';
  }
  safe[j]=0;
  snprintf(label,sizeof(label),"%s  %s",safe,fm_type_name(e.type));
  rect(0,y-1,320,20,WHITE);
  if(i==fm_selected){rect(0,y,11,17,BLACK);print_at(0,y,">",true,WHITE,BLACK);}
  tx(14,y,label);
}
static void fm_draw_list(void){
  static const char *const names[]={"ALL FILES","REAL","COMPLEX","LIST","MATRIX","Y-VARS","PRGM","APPVARS"};
  unsigned int i;fm_header(names[fm_category]);
  if(!fm_count)tx(1,58,"No files found");
  for(i=fm_top;i<fm_count&&i<(unsigned int)fm_top+9;i++)fm_row(i);
}
static void fm_draw_info(void){
  fm_entry_t *e=fm_current();uint8_t h;char buf[48];
  fm_header("FILE INFO");if(!e)return;
  tx(1,58,e->name);tx(1,80,fm_type_name(e->type));
  h=fm_open();
  if(h){
    snprintf(buf,sizeof(buf),"Size: %u bytes",(unsigned int)ti_GetSize(h));tx(1,102,buf);
    tx(1,124,ti_IsArchived(h)?"Archived":"RAM");ti_Close(h);
  }else tx(1,102,"Unable to open file");
  tx(1,155,"1:View  2:Rename");
  tx(1,177,"3:Archive/RAM  4:Delete");
  tx(1,218,fm_message);
}
static void fm_draw_preview(void){
  uint8_t h;uint16_t size;unsigned int i;char line[48];uint8_t bytes[8];
  fm_header("FILE DATA");h=fm_open();
  if(!h){tx(1,58,"Unable to open file");return;}
  size=ti_GetSize(h);
  for(i=0;i<8&&fm_offset+i*8u<size;i++){
    unsigned int j,n=size-(fm_offset+i*8u);char *p=line;
    if(n>8)n=8;
    ti_Seek((long)(fm_offset+i*8u),SEEK_SET,h);
    if(ti_Read(bytes,1,n,h)!=n)break;
    p+=sprintf(p,"%04X:",fm_offset+i*8u);
    for(j=0;j<n;j++)p+=sprintf(p," %02X",bytes[j]);
    tx(1,57+(int)i*20,line);
  }
  ti_Close(h);
  tx(1,220,"UP/DOWN: page  CLEAR: back");
}
static void fm_draw_rename(void){
  fm_header("RENAME");tx(1,57,"New name:");tx(1,79,fm_edit);
  rect((int)tw(fm_edit,true)+2,79,10,16,BLACK);
  tx(1,218,"ENTER: save  CLEAR: cancel");
}
static void fm_draw_delete(void){
  fm_entry_t *e=fm_current();fm_header("DELETE FILE");
  if(e)tx(1,58,e->name);
  tx(1,102,"Delete this system file?");
  tx(1,145,"1:Cancel");tx(1,167,"2:Delete");
}
static void fm_draw(void){
  switch(vm.view){
    case V_FILE_LIST:fm_draw_list();break;
    case V_FILE_INFO:fm_draw_info();break;
    case V_FILE_PREVIEW:fm_draw_preview();break;
    case V_FILE_RENAME:fm_draw_rename();break;
    case V_FILE_DELETE:fm_draw_delete();break;
    default:break;
  }
}
static bool fm_is_view(vm_view_t v){
  return v>=V_FILE_LIST&&v<=V_FILE_DELETE;
}
static char fm_key_char(vm_key_t key){
  if(fm_alpha)return vm_alpha_character(key);
  switch(key){
    case K_0:return '0';case K_1:return '1';case K_2:return '2';
    case K_3:return '3';case K_4:return '4';case K_5:return '5';
    case K_6:return '6';case K_7:return '7';case K_8:return '8';
    case K_9:return '9';default:return 0;
  }
}
static void fm_key(vm_key_t key){
  fm_entry_t *e=fm_current();
  if(vm.view==V_FILE_LIST){
    if(key==K_CLEAR){vm.view=V_MEM_MGMT;vm.selected=fm_category;vm.top=0;}
    else if(key==K_UP&&fm_count){
      fm_selected=fm_selected?fm_selected-1:fm_count-1;
      if(fm_selected<fm_top||fm_selected>=fm_top+9)fm_top=fm_selected;
      fm_lookup(fm_selected,&fm_active);
    }else if(key==K_DOWN&&fm_count){
      fm_selected=(fm_selected+1)%fm_count;
      if(fm_selected<fm_top||fm_selected>=fm_top+9)fm_top=fm_selected;
      fm_lookup(fm_selected,&fm_active);
    }else if(key==K_ENTER&&e){vm.view=V_FILE_INFO;fm_message[0]=0;}
    return;
  }
  if(vm.view==V_FILE_INFO){
    if(key==K_CLEAR){vm.view=V_FILE_LIST;return;}
    if(key==K_1||key==K_ENTER){vm.view=V_FILE_PREVIEW;fm_offset=0;return;}
    if(key==K_2&&e){
      if(fm_protected(e)||
         (e->type!=OS_TYPE_PRGM&&e->type!=OS_TYPE_PROT_PRGM&&e->type!=OS_TYPE_APPVAR)){
        strcpy(fm_message,"Rename unavailable for this file");return;
      }
      strcpy(fm_edit,e->name);fm_alpha=true;vm.view=V_FILE_RENAME;return;
    }
    if(key==K_3&&e){
      uint8_t h=fm_open();int ok=0;
      if(fm_protected(e)){strcpy(fm_message,"VM files are protected");return;}
      if(h){
        int archived=ti_IsArchived(h);
        if(archived||ti_ArchiveHasRoom((uint24_t)ti_GetSize(h)+32u))
          ok=ti_SetArchiveStatus(!archived,h);
        ti_Close(h);
      }
      strcpy(fm_message,ok?"Storage changed":"Storage change failed");return;
    }
    if(key==K_4&&e){
      if(fm_protected(e)){strcpy(fm_message,"VM files are protected");return;}
      vm.view=V_FILE_DELETE;return;
    }
    return;
  }
  if(vm.view==V_FILE_PREVIEW){
    if(key==K_CLEAR){vm.view=V_FILE_INFO;return;}
    if(key==K_DOWN&&fm_offset<65472u)fm_offset+=64;
    if(key==K_UP)fm_offset=fm_offset>=64?fm_offset-64:0;
    return;
  }
  if(vm.view==V_FILE_DELETE){
    if(key==K_CLEAR||key==K_1){vm.view=V_FILE_INFO;return;}
    if((key==K_2||key==K_ENTER)&&e){
      int ok=ti_DeleteVar(e->name,e->type);
      vm.view=V_FILE_LIST;fm_refresh();
      if(!ok)strcpy(fm_message,"Delete failed");
    }
    return;
  }
  if(vm.view==V_FILE_RENAME){
    unsigned int n=(unsigned int)strlen(fm_edit);char c;
    if(key==K_CLEAR){vm.view=V_FILE_INFO;return;}
    if(key==K_ALPHA){fm_alpha=!fm_alpha;return;}
    if(key==K_DEL){if(n)fm_edit[n-1]=0;return;}
    if(key==K_ENTER&&n&&e){
      uint8_t old_type=e->type;
      uint8_t result=ti_RenameVar(e->name,fm_edit,e->type);
      vm.view=V_FILE_INFO;
      if(!result){fm_refresh();fm_select_name(fm_edit,old_type);
        strcpy(fm_message,"Renamed");}
      else strcpy(fm_message,result==1?"Name already exists":"Rename failed");
      return;
    }
    c=fm_key_char(key);
    if(c&&n<8&&(n||(c>='A'&&c<='Z'))){fm_edit[n]=c;fm_edit[n+1]=0;}
  }
}
