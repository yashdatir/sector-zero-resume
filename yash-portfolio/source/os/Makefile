CC     = gcc
LD     = ld
NASM   = nasm
CFLAGS = -m32 -march=i686 -mno-sse -mno-mmx -mno-sse2 -Os -ffreestanding -fno-pie -fno-pic \
         -fno-stack-protector -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra \
         -Wno-unused-parameter -c

all: portfolio.img

entry.o: entry.asm
	$(NASM) -f elf32 entry.asm -o entry.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) kernel.c -o kernel.o

kernel.bin: entry.o kernel.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o kernel.bin entry.o kernel.o

# the bootloader is told how many sectors the kernel occupies
boot.bin: boot.asm kernel.bin
	$(NASM) -f bin -dKERNEL_SECTORS=$$(( ($$(stat -c%s kernel.bin)+511)/512 )) boot.asm -o boot.bin

portfolio.img: boot.bin kernel.bin
	dd if=/dev/zero of=portfolio.img bs=512 count=2880 2>/dev/null
	dd if=boot.bin   of=portfolio.img conv=notrunc bs=512 seek=0 2>/dev/null
	dd if=kernel.bin of=portfolio.img conv=notrunc bs=512 seek=1 2>/dev/null
	@echo "kernel: $$(stat -c%s kernel.bin) bytes = $$(( ($$(stat -c%s kernel.bin)+511)/512 )) sectors"

run: portfolio.img
	qemu-system-i386 -fda portfolio.img

clean:
	rm -f *.o *.bin *.img
