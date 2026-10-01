#pragma once
// argv[0]: the ELF to load; argv[1..]: its own argv. Returns only if the embedded loader is not an ELF.
void run_loader(int argc, char *argv[]);
