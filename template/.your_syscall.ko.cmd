cmd_/home/ezio/SyscallAdderV2/template/your_syscall.ko := ld -r -m elf_x86_64 -z max-page-size=0x200000 -T ./scripts/module-common.lds --build-id  -o /home/ezio/SyscallAdderV2/template/your_syscall.ko /home/ezio/SyscallAdderV2/template/your_syscall.o /home/ezio/SyscallAdderV2/template/your_syscall.mod.o ;  true
