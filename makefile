NAME = TI84VM
APPLICATION = YES
APPLICATION_DESCRIPTION = TI84VM Beta App
DESCRIPTION = TI84VM Beta App
CFLAGS = -Wall -Wextra -Oz
include $(shell cedev-config --makefile)

target: $(BINDIR)/TVMSPRT.8xv
$(BINDIR)/TVMSPRT.8xv: src/menu_pixels.bin
	$(Q)$(call MKDIR,$(@D))
	$(Q)$(CONVBIN) -j bin -k 8xv -r -n TVMSPRT -i $< -o $@
