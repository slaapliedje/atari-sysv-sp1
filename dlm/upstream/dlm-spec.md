# Dynamically Loadable Modules (DLM): functional specification

Specification for an SVR4.2-style loadable kernel module facility added to the AMIX 2.1 (SVR4.0, m68k) kernel through its relink kit. Behaviour is taken from public documentation (sources in §6).

Provenance tags used throughout:

- **[D n]**: documented publicly; *n* refers to the source list in §6.
- **[P]**: proposed by us, where public documentation is silent or where our SVR4.0/68k target differs.

The most complete public description is the UnixWare 7 (SVR5) documentation. UnixWare 7 descends from SVR4.2 and keeps the SVR4.2 DLM interface, but adds DDI 8 ("entry-type 1") drivers, `$interface` versioning and the resource manager. Those later additions are marked; we implement the SVR4.2 subset (entry-type 0, `MOD_*_WRAPPER` modules) unless stated otherwise.

---

## 1. User-visible interface

### 1.1 System calls

All four are declared in `<sys/mod.h>`. All return -1 and set `errno` on failure, and fail with `ENOSYS` if the kernel has no DLM support [D 1–4]. UnixWare requires privilege `P_LOADMOD` (otherwise `EPERM`) [D 1–4]. **[P]** SVR4.0 has no privilege mechanism, so we map `P_LOADMOD` to "effective uid 0" (`suser()`), failing with `EPERM`.

#### `int modload(const char *pathname)` [D 1]

Demand-loads a module and returns its integer module id (positive).

- `pathname` is either a module name, which is searched for in the module search path (§1.1 `modpath`, default `/etc/conf/mod.d`), or an absolute path, which is used as is.
- In order, the kernel: opens the object file; allocates kernel memory; reads the object; loads any modules this one depends on that are not yet loaded; relocates the module; resolves its external references against kernel symbols; runs the module's wrapper (its `_load` routine); links the module logically into the kernel by creating its switch-table entries; and marks the module as demand-loaded, so the auto-unload mechanism never removes it.
- Errors:

| errno | Condition |
|---|---|
| `EACCES` | search permission denied on a path component |
| `ENOENT` | file does not exist |
| `EINVAL` | file not configured for dynamic loading, or invalid dependencies (for example circular) |
| `EPERM` | not privileged |
| `ERELOC` | error processing the object file; reference to a symbol not defined in the running kernel; or reference to a symbol in another loadable module that is not declared as a dependency |
| `EBADVER` | version in the module's wrapper does not match the running kernel |
| `ENAMETOOLONG` | `pathname` longer than `MAXPATHLEN` |
| `ENOSYS` | DLM not configured |

**[P]** Also: `ENOMEM` if kernel memory cannot be allocated; `EEXIST`-style conflicts (module already loaded under that name) return the existing id rather than failing, matching the "load if not loaded" dependency semantics; an error returned by the module's `_load` is passed back to the caller unchanged.

#### `int moduload(int modid)` [D 2]

Demand-unloads module `modid`, or all loadable modules if `modid` is 0. Returns 0 on success.

- A module can be unloaded only if it is not in use, is not being loaded or unloaded, no loaded module depends on it, and kernel profiling is off.
- If one of these conditions fails, the module is flagged as a candidate for the next auto-unload pass (and the call fails).
- Unloading: remove the switch-table entry; run the wrapper's `_unload` routine; free the module's memory. If `_unload` returns an error the module stays loaded [D 7].
- Errors: `EBUSY` (references outstanding, dependents loaded, profiling active, or load/unload in progress), `EINVAL` (not a valid or loaded id), `EPERM`, `ENOSYS`.

The UnixWare manual lists "profiling is not enabled" under `EBUSY`; the modadmin page makes clear the intended meaning is that profiling *is* enabled [D 5]. **[P]** We follow the modadmin wording.

#### `int modstat(int modid, struct modstatus *stbuf, boolean_t next_modid)` [D 3]

Returns information about one loaded module.

- With `next_modid` false, reports module `modid` exactly; `EINVAL` if no such module.
- With `next_modid` true, reports the loaded module with the smallest id greater than or equal to `modid`; `EINVAL` if there is none. Iterating from 1 lists every module.
- Errors: `EINVAL`, `EPERM`, `ENOSYS`. **[P]** plus `EFAULT` for a bad `stbuf`.

The layout of `struct modstatus` is not published. Its content follows from what `modadmin -q` prints [D 5]: module id, path name, load address, size, reference count, dependent count, unload delay, descriptive name, module type, and a type-specific number (character major, block major, file-system switch index or STREAMS switch index). **[P]** Our structure holds exactly those fields, with fixed-size name and path arrays (`MODMAXNAMELEN`, `MAXPATHLEN`) so it can be copied out in one call.

#### `int modpath(const char *pathname)` [D 4]

Changes the global module search path. Changes apply at once to all later loads by name, both demand loads and auto-loads, for all users.

- `pathname` is an absolute directory or a colon-separated list of them. It is prepended to the directories added by earlier calls. The default directory `/etc/conf/mod.d` is always searched, and always last.
- Directories need not exist, either at call time or at load time; missing ones are skipped.
- `NULL` resets the path to the default.
- Errors: `EINVAL` (malformed list), `EPERM`, `ENAMETOOLONG`, `ENOSYS`.

#### `int getksym(char *symname, unsigned long *value, unsigned long *info)` [D 9]

A companion call that exists because of DLM.

- Name to value: looks up a global (`STB_GLOBAL` or `STB_WEAK`) symbol in the running kernel *including loaded modules*. It returns the value, and in `info` the ELF type (`STT_NOTYPE`, `STT_FUNC` or `STT_OBJECT`). If the name is defined more than once, the definition in the static kernel wins; otherwise the first one found among loaded modules.
- Address to name: if the name lookup fails and `*value` is an address inside the static kernel or a loaded module, the call returns the nearest symbol at or below it and, in `info`, the offset from it.
- Errors: `EFAULT`, `ENAMETOOLONG` (longer than `MAXSYMNMLEN`), `ENOMATCH` (not found, or address out of range).
- Motivation (documented): `nlist(3)` on `/stand/unix` no longer describes the running kernel. UnixWare also added `/dev/kmem` ioctls that read or write by symbol name atomically, so a module cannot be unloaded mid-access. **[P]** We implement `getksym`; the kmem ioctls are optional (§5).

### 1.2 Commands

#### `modadmin(1M)` [D 5]

| Form | Action |
|---|---|
| `-l modname…` or `-l pathname…` | Load; print the module id of each module loaded. By name uses the search path. Dependencies declared in the configuration (`mdevice.d`) are loaded first automatically. |
| `-u modid…` | Unload by id; `0` means all. Prints the ids unloaded. Fails if the module is in use, is a dependency of a loaded module, or is being loaded or unloaded. |
| `-U modname…` | Unload by name. |
| `-q modid…` or `-Q modname…` | Full status: id, path name, load address, size, reference count, dependent count, unload delay, descriptive name, type, and the major number or switch index. |
| `-s` | Short status of all loaded modules: names and ids. |
| `-S` | Full status of all loaded modules. |
| `-d dir[:dir…]` | Prepend to the search path; absolute paths only (same rules as `modpath`). |
| `-D` | Reset the search path to `/etc/conf/mod.d`. |

Module types that can be loaded [D 5]: device drivers (block, character, STREAMS, pseudo), host bus adapter (HBA) drivers, STREAMS modules, TCP/IP stack modules, file systems, exec modules, system calls, and miscellaneous modules (support code shared by several modules).

Documented behaviour notes [D 5]:

- Modules loaded with `modadmin` or `idmodload` cannot be auto-unloaded. A failed demand unload puts the module on the auto-unload candidate list.
- Auto-unload removes idle modules. The idle time is `DEF_UNLOAD_DELAY`, or the module's own `PREFIX_UNLOAD_DELAY`.
- HBA drivers can only be demand loaded (never auto-loaded) and can never be unloaded.
- While the kernel profiler (`prf`) is on, every module, including ones loaded while it is on, is locked in memory. Turning profiling off releases the locks.

#### `idbuild(1M)` [D 10]

- `idbuild -M module…` configures only the named loadable modules. It builds each module's loadable file into `/etc/conf/mod.d/<module>`; creates the `/dev` nodes; adds and activates any `/etc/inittab` entries from the module's `Init` file; and **registers the module with the running kernel**, so it can be loaded without a reboot.
- If a driver is already loaded with a different major range, `idbuild -M` fails with `ENXIO`. The fix is to unload the module and retry, or to rebuild the whole kernel and reboot.
- A full rebuild (no `-M`) relinks the static kernel and rebuilds every loadable module into `/etc/conf/modnew.d`, which replaces `mod.d` at the next reboot.
- `-S` links all configured modules statically. `-c` and `-n` treat every module capable of loading as loadable.
- When DLM or a kernel debugger is configured, `idbuild` attaches symbol-table information to the kernel it builds. `-x` limits that information to global symbols.

#### `idmodload(1M)` [D 8]

- `/etc/conf/bin/idmodload [-r root] [-f modlist] [-#]` is run by `init` on every boot (in the sysinit state).
- It demand-loads every module listed in `/etc/loadmods` (or in `modlist`) that is *configured*, meaning at least one `System` entry has `Y` in its configure field.
- A module that fails to load produces an error message; the remaining modules are still loaded, and the exit status is 1.

#### `idtune(1M)`

`idtune -c` changes a tunable and makes it take effect immediately for loadable modules [D 6].

### 1.3 Tunables [D 12]

| Name | Default | Min–max | Meaning |
|---|---|---|---|
| `BDEV_RESERVE` | 20 | 0–255 | spare `bdevsw` slots reserved for loadable block drivers |
| `CDEV_RESERVE` | 55 | 0–255 | spare `cdevsw` slots for loadable character drivers |
| `FMOD_RESERVE` | 50 | 0–255 | spare `fmodsw` slots for loadable STREAMS modules |
| `VFS_RESERVE` | 10 | 0–255 | spare `vfssw` slots for loadable file systems |
| `DEF_UNLOAD_DELAY` | 60 | 0–3600 s | minimum idle time before an auto-loaded module may be auto-unloaded |
| `UNLOAD_WAKE` | 60 | 60–3600 s | interval between runs of the auto-unload daemon |
| `PAGESYMTAB` | 1 | 0–2 | whether the dynamic symbol table may be paged: 0 never, 1 if not locked, 2 always |
| `PREFIX_UNLOAD_DELAY` | – | – | per-module override of the unload delay, declared in the module's `Mtune` file; the name is the Master prefix in upper case |

**[P]** We implement all of these except `PAGESYMTAB`: our symbol table is always resident. We add `EXEC_RESERVE` (spare `execsw` slots; the documented tunables cover no exec table) and `MOD_MAXLOADED` (a cap on the number of loaded modules).

---

## 2. Module packaging

### 2.1 Driver Software Package (DSP) files [D 13–17]

`idinstall` installs a module's configuration files under `/etc/conf`.

| DSP file | Installed as | Purpose relevant to DLM |
|---|---|---|
| `Driver.o` | `pack.d/<mod>/Driver.o` | the module's relocatable object |
| `Master` | `mdevice.d/<mod>` | type, flags, prefix, majors, `$depend`, `$modtype`, `$magic`, `$interface` |
| `System` | `sdevice.d/<mod>` | per-instance configuration; `Y`/`N` configure field; `$static` forces static linking |
| `Space.c` | `pack.d/<mod>/space.c` | configuration-dependent data, compiled at build time |
| `Stubs.c` | `pack.d/<mod>/stubs.c` | placeholder definitions for symbols of a module that is *absent*, so other modules still link |
| `Mtune` | `mtune.d/<mod>` | tunables with default, min and max (including `PREFIX_UNLOAD_DELAY`) |
| `Node`, `Init` | – | `/dev` nodes and `inittab` entries, created by `idbuild -M` |

Master file fields used by DLM [D 13]:

- **Characteristics flags**:
  - `b` block driver, `c` character driver, `S` STREAMS, `m` STREAMS module.
  - `F` VFS file system, `e` exec module, `d` dispatcher class, `h` hardware.
  - `L` **loadable**: the module is built loadable unless its System file contains `$static`.
  - `l` the module contains loadable stubs (`Modstub.o`); base-system modules only.
  - `k` keep the major numbers given in the file, `u` block and character majors must be equal, `o` at most one System entry.
- **Final line**: `module-name prefix characteristics order bmaj cmaj`.
  - The module name is at most 14 characters. The prefix is at most 8 characters and is prepended to entry-point names such as `xxopen`.
  - `idinstall` assigns major numbers and ranges (for example `0-3` requests four).
  - `order` controls the call order of `init`/`start` routines and the order of `execsw` entries: higher values come first.
- **`$depend name…`**: the loadable modules whose symbols this module references. For dependencies it is the only mechanism SVR4.2 had. UnixWare 7 prefers `$depend` lines inside `Interface` files.
- **`$modtype string`**: a type name of up to 40 characters, used in error messages.
- **`$magic n… [wildcard]`**: the magic numbers an exec module handles. `wildcard` adds a catch-all entry after the explicit ones.
- **`$name`**: the visible name, for example the file-system type name given to `mount`.
- **`$entry`**: the named entry points the kernel calls directly. `_load`, `_unload` and `_verify` are *not* listed; the kernel reaches them through the wrapper.
- **`$interface name version`** (UnixWare 7): the symbols a module may reference, checked at load time for loadable modules. The file `interface.d/<name>.<version>` lists them and can rename them. A reference outside the declared interfaces and `$depend` modules is rejected (this is the `ERELOC` case).

The System file format version history [D 14]: in the SVR4.2-era "version 1" format, a `$loadable module-name` line made a module loadable. Version 2 inverts this: loadable by default, `$static` to override.

### 2.2 The loadable file [D 5, 10; P]

- The documented behaviour: `idbuild -M` produces `/etc/conf/mod.d/<module>`, an ELF relocatable object built from `Driver.o`, the compiled `Space.c` and generated configuration data such as assigned majors, tunable values and the interrupt attach information. It must contain module initialisation ("wrapper") code [D 5]. Loadable modules are ordinary `.o` files [D 5].
- The public documents do not describe a special section format.
- **[P]** Our loadable file is a single `ET_REL` ELF object, `EM_68K`, `ELFCLASS32`, big-endian, containing:
  1. The module's code and data sections (`.text`, `.data`, `.rodata`, `.bss`, `COMMON` symbols).
  2. Exactly one global **wrapper object** named `<prefix>_wrapper`, emitted by a `MOD_*_WRAPPER` macro (§2.3).
  3. A `.moddata` section (`SHT_PROGBITS`, not allocated) holding NUL-separated `key=value` strings generated by our `idbuild -M` equivalent: `name=`, `type=`, `depend=` (one per dependency), `bmaj=`, `cmaj=`, `magic=`, `fsname=`, `delay=`, `version=`. This lets `modload(2)` load dependencies and register the module without reading `/etc/conf`.
  4. A full `.symtab`/`.strtab`, which is kept and merged into the dynamic kernel symbol table (§3.2).

### 2.3 Wrappers and entry points

- A `MOD_*_WRAPPER` macro from `<sys/moddefs.h>` generates a module's wrapper, with the arguments `(prefix, load, unload, [halt,] [verify,] description)` [D 7, 11].
- Documented wrapper types [D 7, 11]:

| Macro | Module kind | Extra arguments |
|---|---|---|
| `MOD_DRV_WRAPPER` | block or character (including STREAMS) device driver | `halt` |
| `MOD_HDRV_WRAPPER` | host bus adapter driver | `halt` |
| `MOD_ACDRV_WRAPPER` | autoconfiguring driver (UnixWare 7) | `halt`, `verify` |
| `MOD_ACHDRV_WRAPPER` | autoconfiguring driver with interrupts but no switch entry (UnixWare 7) | `halt`, `verify` |
| `MOD_STR_WRAPPER` | STREAMS module (`fmodsw`) | – |
| `MOD_FS_WRAPPER` | file system type (`vfssw`) | – |
| `MOD_MISC_WRAPPER` | miscellaneous or support code | – |

- The modadmin page also lists exec modules and system-call modules as loadable [D 5], but the fetched pages do not name their wrapper macros. **[P]** We add `MOD_EXEC_WRAPPER(prefix, load, unload, desc)` (for `execsw`), `MOD_SYSCALL_WRAPPER(prefix, load, unload, desc)` (for `sysent` slots) and, for our own needs, `MOD_HOOK_WRAPPER` (misc modules allowed to install exception-vector hooks).
- **`_load`** [D 7]:
  - Called once, in blockable context, after relocation and before any other entry point.
  - Returns 0 or an errno. It initialises the module and, for interrupt-driven drivers, attaches interrupts. The pre-DDI 8 routine for this is `mod_drvattach(&prefixattach_info)`, where the attach-info structure is generated by the configuration tools and opaque to the driver [D 18].
  - It may not access the calling process's address space (for example with `copyout`).
  - For a *statically* linked entry-type 0 module, `init` and `start` are called instead of `_load`.
- **`_unload`** [D 19]:
  - Called only when no device of the driver is open.
  - Must undo `_load`: detach interrupts (`mod_drvdetach`), cancel pending `timeout` and `bufcall` requests, free memory.
  - Returning non-zero keeps the module loaded. A missing `_unload` (NULL) makes the module unloadable only if the wrapper allows it. A module that must stay resident returns `EBUSY`, as the documented clist example does [D 11].
  - Pre-DDI 8: if no `_unload` routine is provided, the driver is never unloaded after it loads successfully [D 19]. The file-system example passes NULL to mean "no cleanup needed" [D 11]. **[P]** Our rule: NULL means no cleanup for `MOD_FS_WRAPPER` and `MOD_STR_WRAPPER`, and never unload for driver wrappers.
- **`halt`**: the driver's shutdown routine, recorded so it can be called at system halt while the module is loaded.
- **Versioning**:
  - The wrapper carries a version number, and a mismatch with the kernel fails the load with `EBADVER` [D 1].
  - **[P]** The version is a single integer `MODREV` defined in `<sys/moddefs.h>`, increased whenever the wrapper layout, the switch-table layout or the DLM kernel interface changes. `$interface` checking (UnixWare 7) is optional for us (§5).

### 2.4 Dependencies

- Declared by `$depend` (Master file, or Interface file in UnixWare 7) [D 13, 15].
- The loader loads missing dependencies first [D 1, 5]. Symbols from other loadable modules resolve only against declared dependencies (`ERELOC` otherwise). Cycles give `EINVAL` [D 1].
- A module on which a loaded module depends cannot be unloaded, and `modstat` reports a dependent count [D 2, 5].
- **Loadable stubs**:
  - The `l` flag with `Modstub.o` lets the *static* kernel contain stubs for functions of a loadable base-system module [D 13]. **[P]** Our reading: calling such a stub triggers an auto-load of the module and then continues into the real function.
  - This is distinct from `Stubs.c`, which supplies inert placeholders when the module is not installed at all [D 16].

---

## 3. Kernel behaviour

### 3.1 Registration (before load)

- A module must be registered with the running kernel before it can be loaded or auto-loaded, which `idbuild -M` does [D 5, 6, 10]. Registration ties the module name to its type-specific key and arms auto-loading: majors for drivers, STREAMS module name, file-system type name, exec magic numbers, system-call number.
- The system call used for registration is not documented publicly.
- **[P]** We add `modadm(int cmd, void *arg)` (a private system call) with these commands:
  - `MOD_REG` registers a module: its name, type and key.
  - `MOD_UNREG` removes a registration.
  - `MOD_REGLIST` returns the registration table.

  `idbuild -M` and a boot-time `idmodreg` run from `/etc/inittab` (before `idmodload`) re-register every configured loadable module after each boot.
- **[P]** Registering a key **reserves** a switch slot and fills it with *autoload trampolines* (§3.4). Registering a key already in use by another module fails with `EBUSY`, or `ENXIO` for a major number conflict with a loaded module, matching `idbuild`.

### 3.2 Loading

These steps restate the documented sequence [D 1, 6] with our concrete rules [P].

1. **Serialise.** A per-module state (`UNLOADED`, `LOADING`, `LOADED`, `UNLOADING`) plus a global DLM lock. A second request for a module in `LOADING` sleeps until the load finishes, then shares its result.
2. **Find the file.** By name through the search path, or by absolute path. Read it with kernel VFS calls (`vn_open` and `vn_rdwr`) using the caller's credentials. For auto-loads, use root credentials.
3. **Validate the ELF header.** Require `ET_REL`, `EM_68K`, `ELFCLASS32`, `ELFDATA2MSB` and `EV_CURRENT`; otherwise `ERELOC`. Require exactly one symbol matching `*_wrapper`, a `.moddata` section and a matching `MODREV`; otherwise `EINVAL` or `EBADVER`.
4. **Load dependencies**, recursively, keeping a visit stack for cycle detection (`EINVAL`). Each dependency's dependent count rises by one.
5. **Allocate** one kernel-memory block for all `SHF_ALLOC` sections plus `COMMON` symbols, with each section aligned to its `sh_addralign`. Zero-fill `.bss` and common storage. Copy the other sections in.
6. **Resolve symbols** in this order:
   1. The module's own definitions.
   2. The static kernel's global symbols.
   3. The global symbols of declared dependencies, direct and transitive.

   An undefined symbol gives `ERELOC`, and the kernel prints the symbol name on the console. A weak undefined symbol resolves to 0.
7. **Relocate** every `SHT_RELA` (and `SHT_REL`) section that targets an allocated section (§4.1).
8. **Flush caches.** Push the data cache and invalidate the instruction cache for the whole block. This is required on 68040 and 68060 (§4.1).
9. **Add the module's global symbols** to the dynamic kernel symbol table used by `getksym` and later loads.
10. **Call `_load`** through the wrapper. On error, undo steps 9 back to 4 and return the error.
11. **Connect**: install the real switch entries (§3.4); the state becomes `LOADED`. Mark the module *demand-loaded* when the load came from `modload`, or *auto-loaded* when it came from a trigger.
12. **Assign the module id**: a monotonically increasing positive integer, not reused while the system runs.

A failure at any step leaves the kernel exactly as before, apart from dependencies that were loaded and have no other users. Those are released through the normal auto-unload candidate path.

### 3.3 Switch tables and reserved slots

- UnixWare reserves spare slots in `bdevsw`, `cdevsw`, `fmodsw` and `vfssw` at kernel build time (§1.3) [D 12]; loadable modules occupy these.
- **[P]**
  - The relinked kernel provides `BDEV_RESERVE`, `CDEV_RESERVE`, `FMOD_RESERVE`, `VFS_RESERVE` and `EXEC_RESERVE` spare entries, and spare `sysent` slots.
  - The table-size variables the SVR4.0 core reads (`bdevcnt`, `cdevcnt`, `fmodcnt`, `nfstype`, `nexectype` or the local equivalents) cover the reserved entries.
  - An empty slot behaves exactly like an unconfigured one: `ENXIO` on device open, name lookup misses, `ENOEXEC`.
  - The DLM layer fills and clears slots while holding the table lock and at raised priority, so the core never sees a half-filled entry.

### 3.4 Auto-load triggers and reference counting

Documented triggers [D 6, 20]:

- First `open` of any device of a loadable driver.
- First `I_PUSH` of a loadable STREAMS module.
- `mount` of a file-system type whose module is not loaded.
- **[P]** Execution of a file whose magic number is registered to an exec module.
- **[P]** A call to a registered system call.

HBA drivers are never auto-loaded [D 6, 20].

Documented reference and unload conditions:

- A driver becomes an auto-unload candidate at the last close of all its devices [D 6].
- A STREAMS module becomes a candidate at its last `I_POP` [D 6].
- A module may be unloaded only if it has no references, no dependents and no load or unload in progress [D 2].

**[P]** Our mechanism for each table:

| Table | Trigger hook | Reference taken | Released |
|---|---|---|---|
| `cdevsw`/`bdevsw` | Registration installs trampoline `open`/`close`/`strategy` routines in the slot. The trampoline `open` auto-loads if needed, then calls the real routine. After load, the slot's other entries are real routines, but `open` and `close` stay trampolines for counting. | successful open (per open, including clones and block opens by mount/swap) | matching close; block devices at unmount/swapoff |
| STREAMS driver (`d_str`) | Before load, `d_str` points to a stub `streamtab` whose open auto-loads, repoints `d_str`, and redoes the open. | stream open | stream close |
| `fmodsw` | Interpose on the core's name lookup for STREAMS modules. A miss that matches a registered name triggers auto-load and a retry. | successful `I_PUSH` (and autopush) | `I_POP` or stream close |
| `vfssw` | Interpose on the file-system-type lookup used by `mount` and `sysfs`. | each mounted instance | unmount |
| `execsw` | A registered entry holds the magic number and a trampoline `exec` function that auto-loads, then calls the real function. | for the duration of the exec call, and for each process whose image the module keeps using (the module decides through `mod_hold` and `mod_rele`) | exec return / `mod_rele` |
| `sysent` | trampoline system-call entry | for the duration of the call | return |
| misc | no trigger; loaded as a dependency or on demand | dependent count | dependent unloaded |

- **[P]** Modules can take and drop references explicitly with `mod_hold(modid)` and `mod_rele(modid)`, for example for long-lived callbacks. `mod_self()` returns the calling module's id; the wrapper records it.
- **[P]** Interposing on core routines (the name lookups, exec dispatch) happens at kernel relink time by symbol renaming, in the same way as our other kernel hooks. It is not patched at run time.

### 3.5 Unloading

- **Demand unload** [D 2, 5, 21]:
  - Refused with `EBUSY` if references, dependents, load or unload in progress, or profiling are present; the module then becomes an auto-unload candidate.
  - The unload delay is ignored for demand unloads [D 20].
  - `modid` 0 means attempt every loaded module. **[P]** It repeats passes until no further module unloads, so dependency chains empty.
- **Order** [D 2]:
  1. Disconnect: the switch entry goes back to the registered trampoline state; auto-loading stays armed while the module remains registered.
  2. Call `_unload`.
  3. Free memory.
  4. Remove the module's symbols from the dynamic symbol table.
  5. Decrement the dependent counts of its dependencies.
- **[P]** If `_unload` fails, the switch entries are reconnected and the error is returned.
- **Auto-unload** [D 6, 10, 12, 20]:
  - A kernel daemon wakes every `UNLOAD_WAKE` seconds. It unloads auto-loaded modules that have had no references, no dependents, and no access for at least their unload delay (`PREFIX_UNLOAD_DELAY` or `DEF_UNLOAD_DELAY`).
  - One page [D 10] describes auto-unload as memory-pressure driven, running until a high-water mark is reached; the tuning guide describes a periodic daemon [D 12, 20].
  - **[P]** We implement the periodic daemon, and also wake it early when free memory drops below a low-water mark.
- Demand-loaded modules are never auto-unloaded [D 1, 5, 20]. **[P]** A demand-loaded module that also gains auto-loaded use keeps its demand flag.
- **[P]** "Last access time" is updated whenever the reference count drops to zero.
- Profiling locks all modules [D 5]. HBA drivers never unload [D 5, 20]. **[P]** We generalise the HBA rule into a wrapper flag, `MODF_NOUNLOAD`, which also applies to modules that hook exception vectors or `sysent`, unless they provide an `_unload` that restores the hooks.

### 3.6 Static linking and boot

- Modules without `L`, or with `$static`, are linked into the kernel image at build time [D 13, 14]. `idbuild -S` links everything statically [D 10].
- Static entry-type 0 modules use `init`/`start`, not `_load` [D 7, 15].
- The boot sequence [D 8, 17] is: kernel boot, then `init` sysinit runs `idmodload`, which loads `/etc/loadmods`.
- **[P]** Our rules:
  - Modules needed before the root file system is mounted (the root disk driver, console, the file-system type of root) must be static. The loader reads files through VFS, so it cannot run before root is mounted.
  - Our own modules (the A/UX personality, `uinter`, AppleTalk, platform drivers) are written with a `MOD_*_WRAPPER` and also carry `init` and `start` names, so the same object can be linked statically or loaded.
  - When a static module has a wrapper, the kernel calls its `_load` from `init` (a boot-time table built by our relink step), so there is one initialisation path.

### 3.7 Failure modes (summary)

| Situation | Result |
|---|---|
| Bad ELF or unsupported relocation | `ERELOC`; nothing changed |
| Undefined symbol, or symbol only in an undeclared module | `ERELOC`; name printed on the console **[P]** |
| Wrapper version mismatch | `EBADVER` |
| Not registered or not configured, or dependency cycle | `EINVAL` |
| No free switch slot, or registered key in conflict | **[P]** `ENXIO` (majors) or `ENOSPC` (tables) |
| `_load` fails | its errno; everything undone |
| Auto-load fails during open, push, mount or exec | the triggering call fails with the load's errno **[P]**; `ENXIO`, `EINVAL`, `ENOEXEC` or `ENOSYS` respectively if the module is not registered |
| Unload with references or dependents | `EBUSY`; queued for auto-unload |
| `_unload` fails | module stays loaded and reconnected |

---

## 4. Target requirements (our implementation)

### 4.1 Object format and relocation (68k)

- R1. Accept ELF32 big-endian `EM_68K` `ET_REL` objects produced by the AMIX cross toolchain and by our C900 tools. Handle `SHT_RELA` (the SysV m68k ABI form) and, defensively, `SHT_REL` with the addend taken from the section contents.
- R2. Support at least `R_68K_NONE`, `R_68K_32`, `R_68K_16`, `R_68K_8`, `R_68K_PC32`, `R_68K_PC16` and `R_68K_PC8`, with range checks on the 16- and 8-bit forms (overflow gives `ERELOC`).
  - Reject GOT and PLT forms (`R_68K_GOT*`, `R_68K_PLT*`), `R_68K_COPY`, `R_68K_GLOB_DAT`, `R_68K_JMP_SLOT` and `R_68K_RELATIVE` with `ERELOC`: modules are not position-independent shared objects.
  - Section-symbol and local-symbol relocations resolve to the address where the target section was loaded.
- R3. Honour `SHN_COMMON` (allocated in the module's block), `SHN_ABS` and `SHN_UNDEF`. Weak definitions lose to strong ones.
- R4. Handle 68k alignment: at least 2-byte alignment for code, and the section's `sh_addralign` otherwise.
- R5. After relocation, make instruction fetch coherent: push modified data-cache lines and invalidate the instruction cache (`CPUSHA` on 040/060, `CACR` clear on 020/030). The branch cache on the 060 must also be cleared.
- R6. Load modules into kernel virtual memory that is supervisor-only, and mapped identically in every address space, like the rest of the AMIX kernel.

### 4.2 Kernel symbol table

- R7. The kernel image is an unstripped ELF executable with a full `.symtab`. The static kernel symbol table must be available in kernel memory at run time. **[P]** Two options, in order of preference:
  1. Our relink step appends a compact table (the global `STB_GLOBAL`/`STB_WEAK` names and values) as an allocated section of the kernel image, reached through linker-defined start and end symbols.
  2. The loader reads `.symtab` from the booted kernel file (`/stand/unix` or its equivalent) on first use.

  Local symbols are kept only for `getksym` address-to-name lookups.
- R8. Hash the dynamic symbol table (static kernel plus loaded modules) by name, and sort it by address for reverse lookups.

### 4.3 What the SVR4.0 AMIX kernel lacks and we must add

SVR4.0 has no loader and no DLM system calls; switch tables are fixed-size arrays sized by the configuration step (as the ASV/AMIX `cunix`/`master.d` flow does). Required additions:

- A1. **The DLM core module** (statically linked). It contains:
  - the ELF loader and relocator;
  - the dynamic symbol table;
  - the module list (id, name, path, address, size, type, key, state, flags, reference count, dependent count, unload delay, last-use time, wrapper pointer);
  - locking;
  - the dependency loader;
  - the auto-unload daemon, started from the kernel's process-creation path at boot like the other system daemons.
- A2. **System calls** `modload`, `moduload`, `modstat`, `modpath`, `getksym`, plus the private registration call (§3.1), in spare `sysent` slots of our syscall table. **[P]** Numbers are chosen from those free in AMIX and A/UX; see §5.
- A3. **Errno values** `ERELOC`, `EBADVER` and `ENOMATCH`, which SVR4.0 lacks. They are added to `<sys/errno.h>`, with messages in `strerror`/`perror` tables.
- A4. **Spare switch slots** in `cdevsw`, `bdevsw`, `fmodsw`, `vfssw` and `execsw` (and `sysent`), generated by our relink configuration step from the `*_RESERVE` tunables. The corresponding count variables cover them.
- A5. **Trampolines and interposed lookups**: for the `open`/`close` reference counting and auto-load of §3.4, plus renamed core lookups for STREAMS module names, file-system type names and exec dispatch.
- A6. **Reference-count hooks** in open/close, push/pop, mount/unmount and exec. They are implemented in the trampolines and interposed routines wherever possible, so the SVR4.0 core objects stay unmodified.
- A7. **`<sys/mod.h>` and `<sys/moddefs.h>`**: the wrapper structure and `MOD_*_WRAPPER` macros, `struct modstatus`, the `MODREV` version, and `mod_hold`, `mod_rele`, `mod_self`, `mod_drvattach` and `mod_drvdetach`.
- A8. **Kernel-memory allocation** of physically contiguous or virtually mapped blocks, freed on unload, with cache maintenance (R5).
- A9. **A clean `halt` path**: call `halt` for each loaded driver whose wrapper has one.
- A10. **User space**:
  - `modadmin`;
  - an `idbuild -M` equivalent. This is a cross tool in the C900 toolchain style that links `Driver.o`, `Space.c` and generated data and emits `.moddata`; on the target it is a shell or C tool;
  - `idmodload` and `/etc/loadmods`;
  - the boot-time registration step;
  - the `/etc/conf/{mdevice.d,sdevice.d,mtune.d,pack.d,mod.d}` layout.

  The Master and System file syntax is the documented version 2 syntax, restricted to entry-type 0 and 68k-meaningful fields. PC-specific System fields (I/O port ranges, DMA channel) are accepted and ignored.
- A11. **Profiling interlock**: if a kernel profiler is present, lock modules while it is on.
- A12. **Kernel debugger support**: the debugger must see module symbols through the dynamic symbol table.

---

## 5. Open questions (public documentation silent or ambiguous)

1. The layout of `struct modstatus`, the wrapper structure, and the in-kernel module list. UnixWare headers are not public documentation; we define our own (§1.1, §2.3).
2. The registration system call behind `idbuild -M`, and how the kernel learns about dependencies at `modload(2)` time. The documents say both the command and the call load dependencies, but not where the kernel reads them from. We use `.moddata` (§2.2).
3. Wrapper macros for exec and system-call modules, and whether SVR4.2 had `MOD_FS_WRAPPER` under that name. It appears only in an SVR5 example.
4. The exact semantics of loadable stubs (`Modstub.o`, flag `l`): whether calling a stub auto-loads or fails.
5. When exactly an exec module's reference is released: during the exec only, or for the life of processes using its format. This matters for the A/UX `.lib` exec loader, which must not unload while A/UX processes run.
6. Auto-unload trigger: periodic (tuning guide) or memory-pressure driven (modadmin notices). We do both.
7. The inconsistent `EBUSY` wording about profiling in `moduload(2)`.
8. Numeric values of `ERELOC`, `EBADVER` and `ENOMATCH`, and of the system-call numbers. i386 UnixWare values are not an ABI concern on 68k. Reuse them if they are free in the AMIX and A/UX tables, otherwise pick free ones. To be settled together with the syscall translation tables.
9. Whether the modid of `modstat` iteration starts at 1, and whether ids are reused. We use 1 and never reuse.
10. Whether unloading a STREAMS *driver* (a `cdevsw` entry with `d_str`) must also wait for persistent links and `I_LINK`ed lower streams. We count them as references.
11. The DDI 8 and `$interface` versioning of UnixWare 7. `$interface` checking could replace per-symbol trust, but it needs interface lists for the AMIX kernel. SVR4.2 behaviour, with only `$depend` checking, is the baseline.

---

## 6. Public sources

UnixWare 7.1.4 online documentation (SCO/Xinuos), `http://uw714doc.sco.com/en/…`:

1. `modload(2)`: `man/html.2/modload.2.html`
2. `moduload(2)`: `man/html.2/moduload.2.html`
3. `modstat(2)`: `man/html.2/modstat.2.html`
4. `modpath(2)`: `man/html.2/modpath.2.html`
5. `modadmin(1M)`, including its Notices section (auto-load, auto-unload, HBA, profiler): `man/html.1M/modadmin.1M.html`
6. HDK Technical Reference, "Dynamically-loadable kernel modules (DLKM)": `HDK_concepts/ddT_dlkm.html`
7. `_load(D2)`, including the wrapper macro list: `man/html.D2/_load.D2.html`
8. `idmodload(1M)`: `man/html.1M/idmodload.1M.html`
9. `getksym(2)`: `man/html.2/getksym.2.html`
10. `idbuild(1M)`: `man/html.1M/idbuild.1M.html`
11. HDK "Sample wrapper code" (`MOD_DRV`, `MOD_FS` and `MOD_MISC` wrapper examples): `HDK_ddi/wrap_ex.html`
12. System performance guide, "Dynamically loadable kernel module (DLKM) parameters": `SM_perform/_Dynamically_Loadable_Module_DLKM.html`
13. `Master(4dsp)`: `man/html.4dsp/Master.4dsp.html`
14. `System(4dsp)`: `man/html.4dsp/System.4dsp.html`
15. `Interface(4dsp)`: `man/html.4dsp/Interface.4dsp.html`
16. `Stubs.c(4dsp)`: `man/html.4dsp/Stubs.c.4dsp.html`
17. `Mtune(4dsp)`: `man/html.4dsp/Mtune.4dsp.html`
18. `mod_drvattach(D3)`: `man/html.D3/mod_drvattach.D3.html` (and `mod_drvdetach.D3.html`)
19. `_unload(D2)`: `man/html.D2/_unload.D2.html`
20. "Automatic loading of a DLKM", "Automatic unloading" and "Demand unloading a DLKM": `SM_perform/_Automatic_Loading_of_a_DLKM.html`, `SM_perform/_Automatic_Unloading.html`, `SM_perform/_Demand_Unloading_a_DLKM.html`

Comparison only (not used for behaviour): the Solaris DDI loadable-module interface, `_init(9E)`, `_fini(9E)`, `_info(9E)`, `mod_install(9F)` and `modlinkage(9S)`: https://docs.oracle.com/cd/E19253-01/816-5180/mod-install-9f/index.html, https://docs.oracle.com/cd/E36784_01/html/E36887/modlinkage-9s.html. Solaris uses a data-only linkage structure and explicit `mod_install`, where SVR4.2 uses a wrapper with callbacks and kernel-side switch installation.
