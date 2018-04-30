# Disasm

Disassembler project. Based on Intel's XED disassembler. That must be installed independently.
Contains an install script and some test programs in /dev.

### Disasm
Build with: `make disasm`
**Description**: Simply disassembles a constant instruction and presents it. 

### Siggen
Build with: `make siggen`
**Description**: Disassembles instructions in a signal handler. Prints hex representation.
Verify correctness with: `objdump -D siggen > siggen.dump` and then searching for the affected addresses in the dump file.

### Dependencies

Must be run on isa: x86-64