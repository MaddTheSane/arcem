#
# $Id: Makefile,v 1.2.2.1 2011/10/27 20:41:54 phlamethrower Exp $
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
