#include <tice.h>
#include <ti/vars.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../installer/src/payload_check.h"

static uint32_t crc=0xFFFFFFFFUL;
static const uint32_t table[16]={
0,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,
0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL};
static void byte_crc(uint8_t b){crc^=b;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}
static uint24_t get24(const uint8_t *p){return (uint24_t)p[0]|((uint24_t)p[1]<<8)|((uint24_t)p[2]<<16);}
static void line(const char *s){os_PutStrFull(s);os_NewLine();}
static void variable(const char *name){
 void *data;char text[32];
 if(os_ChkFindSym(OS_TYPE_APPVAR,name,NULL,&data))snprintf(text,sizeof text,"%s: %s",name,(uintptr_t)data<0xD00000?"ARCH":"RAM");
 else snprintf(text,sizeof text,"%s: MISSING",name);
 line(text);
}
int main(void){
 const uint8_t *app=NULL,*p;char text[40];
 os_ClrHome();line("TI84VM CHECK r9");line("Reading Flash... wait");
 /* Apps occupy the top of Flash below 3B0000. No Flash write calls. */
 for(uintptr_t a=0x3AFF00;a>=0x300100;a--){
  p=(const uint8_t*)a;
  if(*p=='e'&&!memcmp(p,"eZ8TI84VM\0",10)){app=p-0x100;break;}
 }
 os_ClrHome();line("TI84VM CHECK r9");
 if(!app){line("TI84VM APP: NOT FOUND");goto vars;}
 snprintf(text,sizeof text,"App address: %06lX",(unsigned long)(uintptr_t)app);line(text);
 uint24_t codeoff=0x100+get24(app+0x112);
 if(codeoff<0x12A || codeoff>=EXPECTED_APP_SIZE || (codeoff-0x12A)%6 ||
    (uintptr_t)app+EXPECTED_APP_SIZE+3>0x3B0000 || get24(app+EXPECTED_APP_SIZE)!=EXPECTED_APP_SIZE){
  line("BUILD/SIZE: DIFFERENT OR BAD");goto vars;
 }
 uint24_t pos=0,record=0x12A,bad=0,count=0;
 while(pos<EXPECTED_APP_SIZE){
  uint24_t loc=record<codeoff?codeoff+get24(app+record):EXPECTED_APP_SIZE;
  if(record<codeoff && (loc<pos || loc+3>EXPECTED_APP_SIZE)){line("RELOCATION TABLE: BAD");goto vars;}
  while(pos<loc && pos<EXPECTED_APP_SIZE)byte_crc(app[pos++]);
  if(pos>=EXPECTED_APP_SIZE)break;
  uint24_t value=get24(app+record+3);
  if(value>=0x800000){line("BUILD: DIFFERENT RELOC TYPE");goto vars;}
  if(get24(app+pos)!=(uint24_t)((uintptr_t)app+codeoff+value))bad++;
  /* convbin leaves FF FF FF at each relocation site in the raw file. */
  byte_crc(0xFF);byte_crc(0xFF);byte_crc(0xFF);
  pos+=3;record+=6;count++;
 }
 snprintf(text,sizeof text,"APP data: %s",(crc^0xFFFFFFFFUL)==EXPECTED_APP_CRC?"PASS (r9)":"FAIL / OTHER BUILD");line(text);
 snprintf(text,sizeof text,"Relocs: %u bad / %u",bad,count);line(text);
vars:
 variable("TVMSPRT");variable("LibLoad");variable("FILEIOC");variable("KEYPADC");
 os_PutStrFull("ENTER: saved settings");
 delay(500);os_GetKey();
 os_ClrHome();line("TI84VM STORAGE r9");
 variable("TVMSET");variable("TVMBATT");variable("TVMCHRG");variable("TVMBAR");
 os_PutStrFull("ENTER to close");
 delay(500);os_GetKey();return 0;
}






