# Simple translation and cache hierarchy model

## Usage
- Place the whole folder under {pin-root}/source/tools/
- Compile in this folder with
```bash
mkdir obj-intel64
make V=1
```
- Run with
```bash
../../../pin obj-intel64/memory_simulator.so <simulator options> -- {path/to/executable} <option for executable>
```
see `memory_simulator.cpp` for simulator options

## Support
- [x] 2 level tlb
- [x] 4 level page tables
- [x] 3 page walk cache (pgd->pud, pud->pmd, pmd->pte)
- [x] 3 level cache
- [x] Page table contents cachable
- [x] Integrate with pin

## TODO
- [] TOC in pwc
- [] TODO

## Optimization
