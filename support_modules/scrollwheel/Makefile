#
# $Id: Makefile,v 1.1 2006/02/28 09:48:37 djclarke Exp $
#

AS = armas
LD = armld
OBJCOPY = armobjcopy

all: scrollwheel,ffa

%,ffa: %.elf
	$(OBJCOPY) -O binary $< $@

%.elf: %.o
	$(LD) --section-start .text=0 -o $@ $<

%.o: %.s
	$(AS) -o $@ $<

clean:
	rm -f *.o *.elf *,ffa
