# Disasm

Disassembler project. Contains Intel's XED disassembler.
Contains install script and some test programs in /dev.

### Disasm
Build with: `make disasm`
Simply disassembles a constant instruction and presents it. 

### Siggen
Build with: `make siggen`
Disassembles instructions in a signal handler. Prints hex representation.
Verify correctness with: `objdump -D siggen > siggen.dump` and then searching for the affected addresses in the dump file.

### Dependencies

Requires isa: x86-64