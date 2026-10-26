/* Finance app: TVM variables are stored for the current VM session. */
static void finance_error(vm_t *v){v->error_kind=3;v->selected=0;v->view=V_ERROR;v->fin_edit=false;}
static long double finance_rate(const vm_t *v,long double annual){
  long double cy=v->fin_value[6],py=v->fin_value[5];
  if(cy<=0||py<=0||1.0+annual/(100.0*cy)<=0)return NAN;
  return powl(1.0+annual/(100.0*cy),cy/py)-1.0;
}
static long double finance_factor(long double r,long double n){
  if(fabsl(r)<1e-12)return n;
  return (powl(1.0+r,n)-1.0)/r;
}
static long double finance_residual(const vm_t *v,long double n,long double annual){
  long double r=finance_rate(v,annual),growth=powl(1.0+r,n);
  return v->fin_value[2]*growth+v->fin_value[3]*(1.0+(v->fin_begin?r:0.0))*finance_factor(r,n)+v->fin_value[4];
}
static void finance_solve(vm_t *v){
  unsigned int f=v->fin_field,j;long double n=v->fin_value[0],r=finance_rate(v,v->fin_value[1]);
  long double growth,factor,result=0,lo,hi,fl,fm,mid;
  if(f>=5||!isfinite(r)||n<0||n>1e7){finance_error(v);return;}
  if(f==1){
    lo=-99.0;hi=10000.0;fl=finance_residual(v,n,lo);
    if(!isfinite(fl)){lo=-50.0;fl=finance_residual(v,n,lo);}
    if(!isfinite(fl)||fl*finance_residual(v,n,hi)>0){finance_error(v);return;}
    for(j=0;j<90;j++){mid=(lo+hi)/2;fm=finance_residual(v,n,mid);
      if(!isfinite(fm)){finance_error(v);return;}
      if(fl*fm<=0)hi=mid;else{lo=mid;fl=fm;}}
    result=(lo+hi)/2;
  }else if(f==0){
    lo=0;hi=1e7;fl=finance_residual(v,lo,v->fin_value[1]);
    if(!isfinite(fl)||fl*finance_residual(v,hi,v->fin_value[1])>0){finance_error(v);return;}
    for(j=0;j<90;j++){mid=(lo+hi)/2;fm=finance_residual(v,mid,v->fin_value[1]);
      if(!isfinite(fm)){finance_error(v);return;}
      if(fl*fm<=0)hi=mid;else{lo=mid;fl=fm;}}
    result=(lo+hi)/2;
  }else{
    growth=powl(1.0+r,n);factor=(1.0+(v->fin_begin?r:0.0))*finance_factor(r,n);
    if(f==2){if(fabsl(growth)<1e-15){finance_error(v);return;}result=-(v->fin_value[3]*factor+v->fin_value[4])/growth;}
    if(f==3){if(fabsl(factor)<1e-15){finance_error(v);return;}result=-(v->fin_value[2]*growth+v->fin_value[4])/factor;}
    if(f==4)result=-(v->fin_value[2]*growth+v->fin_value[3]*factor);
  }
  if(!isfinite(result)){finance_error(v);return;}
  v->fin_value[f]=result;v->fin_edit=false;v->fin_reveal=true;v->fin_input[0]=0;
}
static void finance_commit(vm_t *v){
  long double x;int err;
  if(!v->fin_edit||!v->fin_input[0]){v->fin_edit=false;return;}
  err=vm_eval_long(v->fin_input,v->degree,&x);
  if(err||!isfinite(x)||((v->fin_field==5||v->fin_field==6)&&x<=0)){finance_error(v);return;}
  v->fin_value[v->fin_field]=x;v->fin_edit=false;v->fin_input[0]=0;
}
static int finance_digit(vm_key_t k){
  switch(k){case K_0:return 0;case K_1:return 1;case K_2:return 2;case K_3:return 3;
    case K_4:return 4;case K_5:return 5;case K_6:return 6;case K_7:return 7;
    case K_8:return 8;case K_9:return 9;default:return -1;}
}
static void finance_key(vm_t *v,vm_key_t k,bool sec){
  int d=finance_digit(k);size_t len;char letter=0;
  if(v->view==V_FIN_MENU){
    if(sec&&k==K_MODE){view(v,V_HOME);return;}
    if(k==K_CLEAR){view(v,V_HOME);return;}
    if(k==K_MODE){view(v,V_MODE);return;}
    if(k==K_APPS){view(v,V_APPS);return;}
    if(k==K_LEFT||k==K_RIGHT){v->tab^=1;v->selected=v->top=0;return;}
    if(k==K_UP||k==K_DOWN){move(v,k==K_DOWN?1:-1);return;}
    if(v->alpha||v->alpha_lock){letter=vm_alpha_character(k);v->alpha=false;}
    if(k==K_ENTER||d>=0||(letter>='A'&&letter<='E')){
      unsigned int i=v->selected;
      if(d>=0){i=(unsigned int)(d?d-1:9);if(i>=vm_menu_size(v))return;}
      if(letter>='A'&&letter<='E')i=(unsigned int)(10+letter-'A');
      if(i>=vm_menu_size(v))return;
      if(v->tab){
        static const char *const names[]={"N","I%","PV","PMT","FV","P/Y","C/Y"};
        view(v,V_HOME);type_text(v,names[i]);
      }else if(i==0){view(v,V_FIN_SOLVER);v->fin_field=0;v->fin_edit=false;v->fin_reveal=false;}
      else{
        static const char *const tokens[]={"","tvm_Pmt","tvm_I%","tvm_PV","tvm_N","tvm_FV","npv(","irr(","bal(","SigmaPrn(","SigmaInt(","Nom(","Eff(","dbd(","Pmt_End"};
        if(i==14)v->fin_begin=false;
        view(v,V_HOME);type_text(v,tokens[i]);
      }
    }
    return;
  }
  if((v->alpha||v->alpha_lock)&&k==K_ENTER){
    v->alpha=false;finance_commit(v);if(v->view==V_FIN_SOLVER)finance_solve(v);return;
  }
  if(sec&&k==K_MODE){view(v,V_HOME);return;}
  if(sec&&k==K_ENTER)return;
  if(k==K_CLEAR){v->fin_input[0]=0;v->fin_edit=true;v->fin_reveal=false;return;}
  if(k==K_APPS){view(v,V_APPS);return;}
  if(k==K_MODE){view(v,V_MODE);return;}
  if(k==K_UP||k==K_DOWN||k==K_ENTER){
    finance_commit(v);if(v->view!=V_FIN_SOLVER)return;
    if(k==K_UP)v->fin_field=(uint8_t)((v->fin_field+7)%8);
    else v->fin_field=(uint8_t)((v->fin_field+1)%8);
    v->fin_reveal=false;
    return;
  }
  if(v->fin_field==7){if(k==K_LEFT||k==K_RIGHT)v->fin_begin=!v->fin_begin;return;}
  if(k==K_DEL){len=strlen(v->fin_input);if(v->fin_edit&&len)v->fin_input[len-1]=0;return;}
  {char c=0;
   if(d>=0)c=(char)('0'+d);
   else if(k==K_DOT)c='.';else if(k==K_NEG)c='-';
   else if(k==K_ADD)c='+';else if(k==K_SUB)c='-';
   else if(k==K_MUL)c='*';else if(k==K_DIV)c='/';
   else if(k==K_LPAREN)c='(';else if(k==K_RPAREN)c=')';
   if(c){if(!v->fin_edit){v->fin_input[0]=0;v->fin_edit=true;}
     len=strlen(v->fin_input);if(len<sizeof(v->fin_input)-1){v->fin_input[len]=c;v->fin_input[len+1]=0;}}
  }
}
