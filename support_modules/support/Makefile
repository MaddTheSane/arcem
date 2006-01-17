#
# $Id: Makefile,v 1.1 2006/01/17 22:01:40 mhowkins Exp $
#

AS = armas
LD = armld
OBJCOPY = armobjcopy




all: support,ffa

%,ffa: %.elf
	$(OBJCOPY) -O binary $< $@

%.elf: %.o
	$(LD) --section-start .text=0 -o $@ $<

%.o: %.s
	$(AS) -o $@ $<

clean:
	rm -f *.o *.elf *,ffa
