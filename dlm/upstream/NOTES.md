# DLM: loadable modules

Loadable MISC modules for the Mac kernel, as specified in `docs/dlm-spec.md`, with exec-format and hook linkages for guest processes (`kernel/guest/`). Sources: `docs/dlm-spec.md` and the AMIX headers.

## Files

| File | Role |
|---|---|
| `dlm_core.c` | module list, ids, counts, candidate list, load/unload sequences, guard, the six system calls, `dlm_init`, `mod_miscops`, `mod_hold`/`mod_rele` |
| `dlm_ld.c` | ELF checks, layout, symbol resolution, commons, relocation, module table |
| `dlm_sym.c` | table blocks (`<sys/ksym.h>`): build, hash, check, lookup by name/address; shared with `mkksym` |
| `dlm_slot.c` | exec-format and hook linkages: enlarged `execsw`, per-slot trampolines that load the module on first use, `EXF_FIRST` fall-through |
| `dlm_fs.c` | filesystem-type linkage: enlarged `vfssw` with spare rows, `vfs_getvfssw` loading the module named by an unknown type, no unload while mounted |
| `dlm_hooksw.s` | weak empty `hooksw`, replaced by the guest shim |
| `dlmconf.c` | tunables, static-module list, `dmainit` override |
| `dlm_cache.s` | `dlm_cacheflush` (060 / 040 / 020-030), weak default `plat_dmainit` |
| `dlmksym.s` | `dlm_ksym`, 160 KiB of `.data` |
| `include/sys/{mod,moddefs,ksym}.h` | user and module-writer headers |
| `libmod/*.s`, `modadmin.c` | AMIX user side (`build/target/`) |
| `tools/mkksym.c` | fill / check (`-c`) / export list (`-x`) / block file (`-o`) |
| `tools/mkmod`, `tools/modfix.c` | module packaging and fail-closed checks; `modfix` sets `sh_type` 13 |
| `tools/chkdlm.py` | static image checks used by `kernel/build.sh` |
| `test/` | relocation oracle, error paths, state harness (`run.sh` drives them) |

`dlm_ld.c`, `dlm_sym.c` and `dlm_core.c` read ELF and tables by byte offset, so the same sources run in the kernel and on the LP64 host (`test/dlmhost.h`, `test/hostk.c`).

## Deviations from the spec

1. **`dlm_ksym` is in `.data`**, not a `.ksym` input section: in the `ld -r` image a separate writable section would become the validator's "data" section and draw a complaint for every reference into it. The fixed size means filling it moves nothing; `mac.ld` is unchanged.
2. **`dlm_syscred` is created on first use**, not in `dlm_init`: `main` calls `startup` (→ `dmainit`) before `cred_init`, which resets `crsize`/`crfreelist`.
3. **Errno values live in `<sys/mod.h>`**: the AMIX gcc wrapper puts the sysroot include dir ahead of every `-I`, so an overlay `<sys/errno.h>` cannot win. Values: `ENOLOAD` 164, `ERELOC` 165, `ENOMATCH` 166, `EBADVER` 167, `ECONFIG` 168, free in both AMIX (max 151) and ASV (see below).
4. **Symbols in non-allocated sections** are an error only when a relocation uses them (gcc emits a `.comment` section symbol in every object).
5. **Wrapper pointer outside the image → `ERELOC`** (the spec is silent); `EBADVER` is for a readable wrapper with `mw_rev` ≠ 1. A modlink array running outside the image → `ERELOC`.
6. **Oversized pieces**: a section, section header table, symtab, strtab or common larger than `dlm_maximage` → `ENOMEM` (sections, headers, commons) or `ERELOC` (tables), before allocation. Range checks are safe against 32-bit wrap.
7. **Wrapper types**: `MOD_MISC_WRAPPER`, `MOD_HDRV_WRAPPER`, `MOD_EXEC_WRAPPER`, `MOD_HOOK_WRAPPER`. `modadm` accepts `MOD_TY_MISC` registration and returns `EINVAL` for the other slot types and `MOD_C_AUTOUNLD`. Unknown linkage ops are rejected (`EINVAL`).
8. **Image alignment is 16**; a section asking for more gets 16.
9. **Weak `cputype`**: `dlm_cacheflush` takes the 020/030 path on a base without the 040/060 port.
10. **The six `sysent` slots (64–69) are written at boot**, so the image holds `nosys` there. `chkdlm.py` checks that the slots are free and that `dlm_init`'s table maps them to the six handlers with narg 1,1,1,3,3,3; the state harness checks that `dlm_init` writes them (narg, `SETJUMP`) and refuses a non-`nosys` slot.
11. **Not implemented**: `dlmslots`, the static-module list generator (the list in `dlmconf.c` is empty), stubs (§11), the auto-unload daemon, `MOD_TY_SYS`.
12. **Known limit** (as in SVR4.2): two processes loading modules that depend on each other in opposite order can sleep on each other's record; a same-process cycle gives `EINVAL`.

Converted A/UX objects may define names the kernel also defines (`ddp` has its own `adjmsg`). The loader binds to the module's own definition (§6.3); `ld --just-symbols` would bind to the kernel's, so the oracle gives ld the kernel names minus the module's own.

## ASV compatibility

From the ASV (Atari System V) `/boot/KERNEL` and `/usr/include/sys/{syscall,errno}.h`:

- **System calls**: ASV `sysent` (151 entries) has `nosys` at 64–77, 82, 83, 105, 140 and 142–149; 150 is `sigreturn`. No conflict with 64–69.
- **Errno**: ASV uses BSD-style network numbers (`ETIMEDOUT` 152, `ECONNREFUSED` 153, `EHOSTDOWN` 156; 158–163 used, 235–242 XENIX), hence the DLM errnos at 164–168.

## Tests

```sh
sh kernel/dlm/build.sh [image.elf]   # default kernel/build/unix-mac.elf
```

Builds the kernel objects, host tools and AMIX programs, then runs `test/run.sh` against the image (its filled `dlm_ksym`, or a table built from its symbols). One PASS/FAIL line per check:

- **Kernel table**: `mkksym -c` resolves every global; an independent check compares the table with `m68k-elf-nm`.
- **Relocation oracle**: the host build of `dlm_ld.c` must produce byte-identical output to `m68k-elf-ld` at the same base, for C modules (`dlmdep`, `dlmtest` `-O` with a dependency, `dlmtest2` `-O2 -m68040`), an assembler module using every relocation type including negative 16/8-bit values (`rall`, plus `R_68K_NONE`), and A/UX objects converted by `coff2elf` (`atsig`, `ddp`), at several bases.
- **Error paths**: damaged ELF (magic, machine, type, class, byte order, `e_shentsize`, type-13 sections) → `EINVAL`; unsupported or bad relocations, symbol indices, offsets, section links, unterminated strtab, truncated file, 16/8-bit overflow, undefined symbol → `ERELOC`.
- **State harness** (`dlm_core.c` on the host): ids, `modstat`/`getksym`/`modpath`/`modadm` semantics and errnos, `EPERM`, `mod_hold`/`mod_rele`, dependency chains, cycles and depth limit, transitive symbols, weak undefined, commons, `_load`/`_unload` errors, `ENOMEM`, `EINTR` at every open and read of a chain load with nothing leaked, candidate-list invariants over 20000 random operations, `dlm_init` slot claims, exec and hook linkages.

Runtime tests are `t_dlm` in `tests/`.
