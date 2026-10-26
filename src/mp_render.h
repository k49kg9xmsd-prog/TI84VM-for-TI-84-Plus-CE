#ifndef MP_RENDER_H
#define MP_RENDER_H
#include "mp_digits.h"
#include "mp_symbols.h"
typedef struct {int w,h,base;} mp_box;
static int mp_cursor_x=2,mp_cursor_y=37,mp_cursor_w=10,mp_cursor_h=14,mp_height=14,mp_width=0;
static bool mp_fraction_cursor=false;
static bool mp_edit,mp_paint;
static void mp_rect(int x,int y,int w,int h,uint16_t c){if(mp_paint)rect(x,y,w,h,c);}
static void mp_fraction_placeholder(int x,int y){
  mp_rect(x,y,2,2,BLACK);mp_rect(x+4,y,2,2,BLACK);mp_rect(x+8,y,2,2,BLACK);
  mp_rect(x,y+4,2,2,BLACK);mp_rect(x+8,y+4,2,2,BLACK);
  mp_rect(x,y+8,2,2,BLACK);mp_rect(x+4,y+8,2,2,BLACK);mp_rect(x+8,y+8,2,2,BLACK);
}
static int mp_close(const char *s,int a,int end){
  int depth=0,i;for(i=a;i<end;i++){if(s[i]=='(')depth++;else if(s[i]==')' && --depth==0)return i;}return end;
}
static void mp_caret(int x,int y,bool small){mp_cursor_x=x;mp_cursor_y=y;mp_cursor_w=small?6:10;mp_cursor_h=small?10:14;}
static mp_box mp_seq(const char*,int,int,bool,int,int,bool,unsigned int);
static mp_box mp_token(const char *s,int *pos,int end,bool small,int x,int y,bool draw,unsigned int depth){
  int a=*pos,i=a+1,j;mp_box out={small?6:12,small?10:14,small?10:14};
  if(depth>12){*pos=end;return out;}
  if(!small && a+4<end && (!strncmp(s+a,"10^(",4)||!strncmp(s+a,"exp(",4))){
    bool ten=s[a]=='1';int stop=mp_close(s,a+3,end),start=a+4;
    mp_box e=mp_seq(s,start,stop,true,0,0,false,depth+1);int dx,dy;
    out.w=12+(e.w?e.w:10);out.h=20;out.base=20;
    if(draw){
      if(ten){static const uint16_t base[10]={3192,3324,3276,3276,3276,3276,3276,3276,3324,3192};for(dy=0;dy<10;dy++)for(dx=0;dx<12;dx++)if(base[dy]&(1u<<(11-dx)))mp_rect(x+dx,y+10+dy,1,1,BLACK);
      }else{
        const mp_symbol_t *g=&mp_symbols[1];
        for(dy=0;dy<14;dy++)for(dx=0;dx<g->w;dx++)if(g->row[dy]&(1ULL<<(g->w-1-dx)))mp_rect(x+dx,y+6+dy,1,1,BLACK);
      }
      mp_seq(s,start,stop,true,x+12,y,true,depth+1);
      if(start==stop){static const uint16_t dots[10]={819,819,0,0,771,771,0,0,819,819};for(dy=0;dy<10;dy++)for(dx=0;dx<10;dx++)if(dots[dy]&(1u<<(9-dx)))mp_rect(x+12+dx,y+dy,1,1,BLACK);}
    }
    *pos=stop<end?stop+1:stop;return out;
  }
  if(!small){unsigned int si;
    for(si=0;si<sizeof(mp_symbols)/sizeof(mp_symbols[0]);si++){
      const mp_symbol_t *g=&mp_symbols[si];unsigned int n=(unsigned int)strlen(g->text);
      if(a+(int)n<=end && !strncmp(s+a,g->text,n)){
        int dx,dy;out.w=(g->w+2+11)/12*12;
        if(draw){
          if(mp_edit && vm.caret==a)mp_caret(x,y,false);
          for(dy=0;dy<14;dy++)for(dx=0;dx<g->w;dx++)if(g->row[dy]&(1ULL<<(g->w-1-dx)))mp_rect(x+dx,y+dy,1,1,BLACK);
        }
        *pos=a+(int)n;return out;
      }
    }
  }
  if(a+5<end && !strncmp(s+a,"sqrt(",5)){
    int stop=mp_close(s,a+4,end),inside=a+5;
    mp_box b=mp_seq(s,inside,stop,small,0,0,false,depth+1);
    bool cursor=mp_edit && vm.caret>=inside && vm.caret<=stop;
    int reserve=cursor?(small?6:12):0;
    out.w=10+b.w+reserve;out.h=b.h+6;out.base=b.base+4;
    if(out.w<22)out.w=22;
    if(draw){
      mp_rect(x+4,y,out.w-4,2,BLACK);mp_rect(x+4,y,2,out.h,BLACK);
      for(j=0;j<3;j++)mp_rect(x,y+out.h-7+j,j+1,1,BLACK);
      for(j=0;j<4;j++)mp_rect(x+1+j,y+out.h-4+j,5-j,1,BLACK);
      mp_seq(s,inside,stop,small,x+10,y+4,true,depth+1);
      if(inside==stop){static const uint16_t dots[14]={819,819,0,0,771,771,0,0,771,771,0,0,819,819};int dx,dy;for(dy=0;dy<14;dy++)for(dx=0;dx<10;dx++)if(dots[dy]&(1u<<(9-dx)))mp_rect(x+10+dx,y+4+dy,1,1,BLACK);}
    }
    *pos=stop<end?stop+1:stop;return out;
  }
  if(s[a]=='^'){
    int start=a+1,stop;
    if(start<end && s[start]=='('){stop=mp_close(s,start,end);start++;i=stop<end?stop+1:stop;}
    else {stop=start<end?start+1:start;i=stop;}
    mp_box b=mp_seq(s,start,stop,true,0,0,false,depth+1);
    out.w=b.w;out.base=small?14:20;out.h=out.base;
    if(!out.w)out.w=6;
    if(draw){
      mp_seq(s,start,stop,true,x,y,true,depth+1);
      if(start==stop){int dy;for(dy=0;dy<10;dy+=2){mp_rect(x,y+dy,1,1,BLACK);mp_rect(x+5,y+dy,1,1,BLACK);}}
    }
    *pos=i;return out;
  }
  if(s[a]=='('){
    int nend=mp_close(s,a,end);
    if(nend+2<end && s[nend+1]=='/' && s[nend+2]=='('){
      int dend=mp_close(s,nend+2,end),ds=nend+3;
      mp_box n=mp_seq(s,a+1,nend,true,0,0,false,depth+1);
      mp_box d=mp_seq(s,ds,dend,true,0,0,false,depth+1);
      int nw=n.w,dw=d.w;
      if(mp_edit && vm.caret>=a+1 && vm.caret<=nend)nw+=10;
      if(mp_edit && vm.caret>=ds && vm.caret<=dend)dw+=10;
      {bool active=mp_edit && ((vm.caret>=a+1 && vm.caret<=nend)||(vm.caret>=ds && vm.caret<=dend));
       out.w=(nw>dw?nw:dw)+(active?8:10);if(out.w<(active?18:16))out.w=active?18:16;}
      /* MathPrint aligns following full-size tokens with the fraction bar's
         lower baseline, six pixels below the numerator's text baseline. */
      out.base=n.h+10;out.h=n.h+d.h+6;
      if(draw){
        bool active=mp_edit && ((vm.caret>=a+1 && vm.caret<=nend)||(vm.caret>=ds && vm.caret<=dend));
        int adjust=active?0:-1;
        mp_seq(s,a+1,nend,true,x+(out.w-nw)/2+adjust,y,true,depth+1);
        if(a+1==nend)mp_fraction_placeholder(x+(out.w-10)/2,y);
        mp_rect(x+2,y+n.h+2,out.w-(active?4:8)+(n.w||d.w?(active?1:2):0),2,BLACK);
        mp_seq(s,ds,dend,true,x+(out.w-dw)/2+adjust,y+n.h+6,true,depth+1);
        if(ds==dend)mp_fraction_placeholder(x+(out.w-10)/2,y+n.h+6);
        if(mp_edit && vm.caret>=a+1 && vm.caret<=nend){
          mp_cursor_w=a+1==nend?10:8;
          if(a+1!=nend){mp_cursor_x+=2;mp_fraction_cursor=true;}
        }else if(mp_edit && vm.caret>=ds && vm.caret<=dend){
          mp_cursor_w=ds==dend?10:8;
          if(ds!=dend){mp_cursor_x+=2;mp_fraction_cursor=true;}
        }
      }
      *pos=dend<end?dend+1:dend;return out;
    }
  }
  if(draw){
    char ch[2]={s[a],0};
    if(mp_edit && vm.caret==a)mp_caret(x,y,small);
    if(small && s[a]=='-'){
      mp_rect(x,y+2,4,2,BLACK);
    }else if(small && s[a]>='0' && s[a]<='9'){
      int dx,dy;for(dy=0;dy<10;dy++)for(dx=0;dx<6;dx++)if(mp_digits[s[a]-'0'][dy]&(1u<<(5-dx)))mp_rect(x+dx,y+dy,1,1,BLACK);
    }else if(mp_paint)print_at(x-1,y,ch,!small,BLACK,WHITE);
  }
  *pos=i;return out;
}
static mp_box mp_seq(const char *s,int a,int end,bool small,int x,int y,bool draw,unsigned int depth){
  int i=a,xp=x;mp_box out={0,small?10:14,small?10:14};
  while(i<end){mp_box b=mp_token(s,&i,end,small,0,0,false,depth);out.w+=b.w;if(b.base>out.base)out.base=b.base;}
  i=a;out.h=out.base;
  while(i<end){int start=i;mp_box b=mp_token(s,&i,end,small,0,0,false,depth);int yy=y+out.base-b.base;
    if(draw){int cursor=i;mp_token(s,&start,end,small,xp,yy,true,depth);i=cursor;}
    if(out.base-b.base+b.h>out.h)out.h=out.base-b.base+b.h;xp+=b.w;
  }
  if(draw && mp_edit && vm.caret==end)mp_caret(xp,y+out.base-(small?10:14),small);
  return out;
}
static void mp_layout(const char *s,int y,bool draw,bool edit){
  mp_box b;mp_edit=edit;mp_paint=draw;mp_fraction_cursor=false;mp_caret(2,y,false);
  b=mp_seq(s,0,(int)strlen(s),false,2,y,true,0);mp_height=b.h;mp_width=b.w;
  /* Computing cursor geometry uses the same traversal without painting. */
  if(!draw && edit){
    /* The draw traversal maps insertion positions; paint into a clipped page
       only when the caller is already redrawing the input, then reuse it. */
  }
}
static void mp_output(const char *s,int x,int y){mp_edit=false;mp_paint=true;mp_seq(s,0,(int)strlen(s),false,x,y,true,0);}
#endif
