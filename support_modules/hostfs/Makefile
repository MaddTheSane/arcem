#
# $Id: Makefile,v 1.1 2005/07/27 21:34:41 mhowkins Exp $
#

AS = armas
LD = armld
OBJCOPY = armobjcopy




all: hostfs,ffa

%,ffa: %.elf
	$(OBJCOPY) -O binary $< $@

%.elf: %.o
	$(LD) --section-start .text=0 -o $@ $<

%.o: %.s
	$(AS) -o $@ $<

clean:
	rm -f *.o *.elf *,ffa
