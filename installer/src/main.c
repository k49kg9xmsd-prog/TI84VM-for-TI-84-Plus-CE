#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <debug.h>
#include <ti/vars.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include <fileioc.h>

#include "app.h"
#include "flash.h"
#include "ports.h"
#include "payload_check.h"

#define MAX_APPVARS 99

enum
{
    SUCCESS,
    ALREADY_INSTALLED,
    MISSING_VAR,
    PORT_SETUP_FAILED,
    NO_SPACE,
    BAD_PAYLOAD,
    WRITE_VERIFY_FAILED,
};

struct appvar
{
    uint8_t *data;
    uint24_t size;
    uintptr_t app_offset;
};

static struct appvar appvars[MAX_APPVARS];
static char app_name[10];
static uint8_t bad_part=255,bad_reason;
static bool payload_valid(uint24_t size)
{
    static const uint32_t table[16] = {
        0x00000000UL,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,
        0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,
        0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,
        0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL
    };
    static const uint24_t expected_sizes[3]={EXPECTED_CHUNK_0_SIZE,EXPECTED_CHUNK_1_SIZE,EXPECTED_CHUNK_2_SIZE};
    static const uint32_t expected_crcs[3]={EXPECTED_CHUNK_0_CRC,EXPECTED_CHUNK_1_CRC,EXPECTED_CHUNK_2_CRC};
    uint32_t crc=0xFFFFFFFFUL;
    for(uint8_t i=0;i<3;i++){
        uint32_t part_crc=0xFFFFFFFFUL;
        const uint8_t *p=appvars[i].data;
        uint24_t n=appvars[i].size;
        if(n!=expected_sizes[i]){
            bad_part=i;bad_reason=1;return false;
        }
        while(n--){
            part_crc^=*p++;
            part_crc=(part_crc>>4)^table[part_crc&15];
            part_crc=(part_crc>>4)^table[part_crc&15];
        }
        part_crc^=0xFFFFFFFFUL;
        if(part_crc!=expected_crcs[i]){
            bad_part=i;bad_reason=2;return false;
        }
    }
    if(size!=EXPECTED_APP_SIZE){bad_reason=3;return false;}
    for(uint8_t i=0;size;i++){
        const uint8_t *p=appvars[i].data;
        uint24_t n=appvars[i].size;
        if(n>size)return false;
        size-=n;
        while(n--){
            crc^=*p++;
            crc=(crc>>4)^table[crc&15];
            crc=(crc>>4)^table[crc&15];
        }
    }
    return (crc^0xFFFFFFFFUL)==EXPECTED_APP_CRC;
}

static bool check_variable_overlaps(const uint8_t *app_start)
{
    uint8_t first_erased_page = ADDR_TO_PAGE(app_start) - 1;

    void *entry = os_GetSymTablePtr();
    uint24_t type;
    uint24_t length;
    char name[9];
    void *data;

    while ((entry = os_NextSymEntry(entry, &type, &length, name, &data)) != NULL)
    {
        if (data >= (void*)os_RamStart)
        {
            continue;
        }

        const uint8_t page = ADDR_TO_PAGE(data);
        if (page < first_erased_page)
        {
            continue;
        }

        bool ours = false;
        for (uint8_t i = 0; i < MAX_APPVARS; ++i)
        {
            char appvar_name[10];

            sprintf(appvar_name, APPVAR_PREFIX "%u", i);

            if (strncmp(name, appvar_name, length) == 0)
            {
                const uint24_t addr = (uintptr_t)app_start + (APPVAR_SPLIT_SIZE * i);

                if (page >= ADDR_TO_PAGE(addr))
                {
                    dbg_printf("Variable %s (page 0x%x) would be erased before being copied to 0x%x\n",
                        name, page, addr);

                    return false;
                }
                ours = true;
                break; 
            }
        }

        if (!ours)
        {
            dbg_printf("Non-installer variable %s @ %p overlaps app\n", name, data);
            return false;
        }
    }

    return true;
}

/* Construct final relocated bytes in RAM before their first Flash write.
 * Every physical 256-byte program page is touched only once. */
static uint8_t staging[256];
static uint24_t get24(const uint8_t *p)
{
    return (uint24_t)p[0] | ((uint24_t)p[1]<<8) | ((uint24_t)p[2]<<16);
}
static void stage_image(const uint8_t *app_start,uint24_t size,uint24_t offset,uint24_t len)
{
    uint24_t used=0;
    while(used<len && offset+used<size){
        uint24_t at=offset+used,index=at/APPVAR_SPLIT_SIZE,within=at%APPVAR_SPLIT_SIZE;
        uint24_t n=appvars[index].size-within;
        if(n>len-used)n=len-used;
        if(n>size-at)n=size-at;
        memcpy(staging+used,appvars[index].data+within,n);used+=n;
    }
    while(used<len){
        uint24_t tail=offset+used-size;
        staging[used++]=(uint8_t)(size>>(tail*8));
    }
    const uint8_t *records=appvars[0].data+0x12A;
    uint24_t codeoff=0x100+get24(appvars[0].data+0x112);
    uint24_t count=(codeoff-0x12A)/6,lo=0,hi=count;
    /* First relocation whose three-byte field overlaps this block. */
    while(lo<hi){
        uint24_t mid=lo+(hi-lo)/2;
        if(codeoff+get24(records+mid*6)+3<=offset)lo=mid+1;
        else hi=mid;
    }
    for(uint24_t i=lo;i<count;i++){
        const uint8_t *r=records+i*6;
        uint24_t at=codeoff+get24(r),value=get24(r+3);
        if(at>=offset+len)break;
        value+=(uintptr_t)app_start+codeoff;
        for(uint8_t j=0;j<3;j++){
            if(at+j>=offset && at+j<offset+len)
                staging[at+j-offset]=(uint8_t)(value>>(j*8));
        }
    }
}
static bool verify_image(const uint8_t *app_start,uint24_t size)
{
    for(uint24_t offset=0;offset<size+3;){
        uint24_t n=size+3-offset;
        if(n>sizeof staging)n=sizeof staging;
        stage_image(app_start,size,offset,n);
        if(memcmp(app_start+offset,staging,n))return false;
        offset+=n;
    }
    return true;
}

static int install(void)
{
    uint24_t app_size = 0;

    os_ClrHome();
    os_PutStrFull("TI84VM INSTALLER");os_NewLine();
    os_PutStrFull("Preparing environment...");

    if (port_setup())
    {
        return PORT_SETUP_FAILED;
    }

    struct appvar *appvar = &appvars[0];
    for (uint8_t i = 0; i < MAX_APPVARS; ++i)
    {
        char name[10];
        void *data;
        const int namelen = sprintf(name, APPVAR_PREFIX "%u", i);

        if (os_ChkFindSym(OS_TYPE_APPVAR, name, NULL, &data))
        {
            uint8_t *d = data;

            if (d < os_RamStart)
            {
                d += 10 + namelen;
            }
        
            appvar->size = *(uint16_t*)d;
            appvar->data = d + 2;
            appvar->app_offset = app_size;

            /* first appvar contains app name */
            if (i == 0)
            {
                if(appvar->size<300 || memcmp(appvar->data+256,"eZ8TI84VM\0",10))
                    return BAD_PAYLOAD;
                strncpy(app_name, (const char *)(appvar->data + 256 + 3), sizeof app_name);

                dbg_printf("App %s\n", app_name);

                if (os_FindAppStart(app_name))
                {
                    return ALREADY_INSTALLED;
                }
            }

            dbg_printf("AppVar %u (%s, %u bytes)\n", i, name, (uint24_t)appvar->size);

            app_size += appvar->size;

            if (appvar->size != APPVAR_SPLIT_SIZE)
            {
                break;
            }

            appvar++;
        }
        else
        {
            return MISSING_VAR;
        }
    }

    os_ClrHome();os_PutStrFull("TI84VM INSTALLER");os_NewLine();
    os_PutStrFull("Checking app data...");
    if(!payload_valid(app_size))return BAD_PAYLOAD;

    uint8_t *app_end = find_last_app();
    uint8_t *app_start = app_end - app_size - sizeof(uint24_t);

    dbg_printf("App from %p to %p\n", app_start, app_end);

    if (!check_variable_overlaps(app_start))
    {
        return NO_SPACE;
    }
    os_ClrHome();
    os_PutStrFull("Installing: ");
    os_PutStrFull(app_name);
    os_NewLine();
    os_PutStrFull("Please wait...");
    os_NewLine();
    os_NewLine();
    os_PutStrFull("This may take a while.");
    os_NewLine();
    os_PutStrFull("Do not reset!");

    uint8_t *written=app_end;
    /* Preserve existing Apps in the initial partial erase sector. Erase
     * subsequent 64 KiB sectors before descending into them, as app_tools
     * does, including the required spare sector below the new App. */
    for(uint8_t page=ADDR_TO_PAGE(app_end);page>=ADDR_TO_PAGE(app_start);page--){
        while(written>(uint8_t*)PAGE_TO_ADDR(page) && written>app_start){
            uintptr_t address=((uintptr_t)written-1)&~(uintptr_t)255;
            if(address<(uintptr_t)app_start)address=(uintptr_t)app_start;
            uint8_t *dst=(uint8_t*)address;
            uint24_t n=written-dst;
            stage_image(app_start,app_size,dst-app_start,n);
            flash_write(dst,staging,n);
            if(memcmp(dst,staging,n))return WRITE_VERIFY_FAILED;
            written=dst;
        }
        flash_erase(page-1);
        os_NewLine();os_PutStrFull("Flash page completed");
    }
    /* Check all bytes again, including all relocation fields and size. */
    os_ClrHome();os_PutStrFull("TI84VM INSTALLER");os_NewLine();
    os_PutStrFull("Verifying installation...");
    if(!verify_image(app_start,app_size))return WRITE_VERIFY_FAILED;
    return SUCCESS;
}

void delete_vars(void)
{
    for (uint8_t i = 0; i < MAX_APPVARS; ++i)
    {
        char name[10];

        sprintf(name, APPVAR_PREFIX "%u", i);

        if (os_ChkFindSym(OS_TYPE_APPVAR, name, NULL, NULL))
        {
            delete_var(name, OS_TYPE_APPVAR);
        }
        else
        {
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int error;

    (void)argc;

    os_ClrHome();

    error = install();

    os_ClrHome();

    switch (error)
    {
        case SUCCESS:
            os_PutStrFull("Installed + VERIFIED.");
            os_NewLine();
            os_PutStrFull("App: ");
            os_PutStrFull(app_name);
            os_NewLine();
            os_NewLine();
            os_PutStrFull("Delete installer files?");
            if (confirm_delete_vars())
            {
                delete_var(argv[0], OS_TYPE_PROT_PRGM);
                delete_vars();
            }
            return SUCCESS;
            break;

        case ALREADY_INSTALLED:
            os_PutStrFull("Already installed.");
            os_NewLine();
            os_PutStrFull("App: ");
            os_PutStrFull(app_name);
            os_NewLine();
            os_NewLine();
            os_PutStrFull("Delete app from the");
            os_NewLine();
            os_PutStrFull("mem menu to reinstall.");
            break;

        case MISSING_VAR:
            os_PutStrFull("Install failed.");
            os_NewLine();
            os_PutStrFull("Missing an appvar.");
            break;

        case WRITE_VERIFY_FAILED:
            os_PutStrFull("WRITE CHECK FAILED");
            os_NewLine();
            os_PutStrFull("Do NOT launch TI84VM.");
            os_NewLine();
            os_PutStrFull("Delete its APP in MEM.");
            break;

        case BAD_PAYLOAD:
        {
            char name[]="TVM9A0";
            os_PutStrFull("Invalid installer data.");
            os_NewLine();
            if(bad_part<3){
                name[5]=(char)('0'+bad_part);
                os_PutStrFull(name);
                os_PutStrFull(bad_reason==1?" wrong size":" data mismatch");
                os_NewLine();
                os_PutStrFull("Delete ");os_PutStrFull(name);os_NewLine();
                os_PutStrFull("in MEM > AppVars.");os_NewLine();
                os_PutStrFull("Resend matching file.");
            }else{
                os_PutStrFull("TVM9A0 header mismatch.");os_NewLine();
                os_PutStrFull("Resend matching TVM9A0.");
            }
            break;
        }

        case PORT_SETUP_FAILED:
            os_PutStrFull("Install failed.");
            os_NewLine();
            os_PutStrFull("Unsupported OS version.");
            break;

        case NO_SPACE:
            os_PutStrFull("Install failed.");
            os_NewLine();
            os_PutStrFull("Out of archive space.");
            os_NewLine();
            os_PutStrFull("Try running the");
            os_NewLine();
            os_PutStrFull("GarbageCollect command.");
            break;
    }

    while (os_GetCSC());
    while (!os_GetCSC());

    return error;
}


