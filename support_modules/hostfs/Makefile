#
# $Id: Makefile,v 1.4 2012/05/12 17:34:51 phlamethrower Exp $
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
