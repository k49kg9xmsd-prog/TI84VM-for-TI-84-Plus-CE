#ifndef MODE_DELTA_H
#define MODE_DELTA_H
typedef struct {uint8_t x,y,w,h;uint16_t offset;} mode_patch_t;
static const mode_patch_t mode_patches[]={{2,46,49,10,0},{2,60,38,10,62},{2,74,47,10,110},{2,88,65,10,169},{2,102,38,10,251},{2,116,80,10,299},{2,130,30,10,399},{2,144,31,10,437},{110,158,22,10,476},{68,172,31,10,504},{134,186,22,10,543},{102,200,15,10,571}};
#define mode_delta_bits (os_menu_pixels+63888u)
#endif
