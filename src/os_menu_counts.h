#ifndef OS_MENU_COUNTS_H
#define OS_MENU_COUNTS_H
static unsigned int os_menu_count(vm_view_t v,unsigned int tab){switch(v){
case V_DRAW:{static const uint8_t n[]={11,7,4,2};return n[tab%4];}
case V_DISTR:{static const uint8_t n[]={17,4};return n[tab%2];}
case V_LIST:{static const uint8_t n[]={6,12,8};return n[tab%3];}
case V_MATRIX:{static const uint8_t n[]={10,16,10};return n[tab%3];}
case V_TEST:{static const uint8_t n[]={6,4,16};return n[tab%3];}
case V_ANGLE:{static const uint8_t n[]={8};return n[tab%1];}
case V_MATH:{static const uint8_t n[]={13,14,7,8,4};return n[tab%5];}
case V_STAT:{static const uint8_t n[]={5,15,18};return n[tab%3];}
case V_VARS:{static const uint8_t n[]={7,4,15};return n[tab%3];}
case V_CALC:{static const uint8_t n[]={7};return n[tab%1];}
case V_ZOOM:{static const uint8_t n[]={17,4};return n[tab%2];}
case V_LINK:{static const uint8_t n[]={15,1};return n[tab%2];}
case V_RESET:{static const uint8_t n[]={2,3,1};return n[tab%3];}
case V_GROUP:{static const uint8_t n[]={1,0};return n[tab%2];}
default:return 0;}}
static unsigned int os_tab_count(vm_view_t v){switch(v){
case V_DRAW:return 4;
case V_DISTR:return 2;
case V_LIST:return 3;
case V_MATRIX:return 3;
case V_TEST:return 3;
case V_ANGLE:return 1;
case V_MATH:return 5;
case V_STAT:return 3;
case V_VARS:return 3;
case V_CALC:return 1;
case V_ZOOM:return 2;
case V_LINK:return 2;
case V_RESET:return 3;
case V_GROUP:return 2;
default:return 0;}}
#endif