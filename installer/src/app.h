#include <stddef.h>

void *os_FindAppStart(const char *name);
void *find_last_app();
bool confirm_delete_vars(void);
void delete_var(const char *name, uint8_t type);
