# S10+ stability work: kernel 3629caa (issue #44, PR #45)

Status: **candidate, not yet proven stable.** Measured on one SM-G975F (`beyond2lte`, host MB007) on 2026-11-24.

## Problem

Under write load (OpenCode builds, git clone) the kernel panicked in the ext4 writeback path
(`ext4_end_io_rsv_work -> ext4_finish_bio -> queued_spin_lock_slowpath`, KASAN boot `f2a26704`,
about 5400 s under load) or reported `buffer_head` Padding/Redzone/Poison corruption (#42).
A second S10 (MB013) crashed the same way, so this is not a single-unit hardware fault.
Userspace processes (node, python, git, make, busybox, opencode) died with SIGSEGV/SIGILL/SIGTRAP at the same time.
Evidence: issue #42 (frozen, evidence only).

## Change under test

Kernel `7.2.0-rc2-g3629caacc9e1` (commit `3629caacc9e1`, PR #45, branch `issue-44-stable-power`):

- UFS `vdd-hba`/`vcc`/`vccq`/`vccq2` bound to `reg_dummy` instead of "assuming enabled"; `limit-hs-gear = 2`.
- Panfrost `mali-supply` set.
- CPU idle states and PSCI cpuidle removed; A75 capped at 1508 MHz, Mongoose M4 at 1664 MHz; cpufreq boost off; governor `schedutil`.
- Defconfig without `CPU_IDLE`, `ARM_PSCI_CPUIDLE`, `PM_AUTOSLEEP`, `CPU_FREQ_GOV_POWERSAVE`, `SLUB_DEBUG`; pstore on.

Cause is not isolated: UFS supplies, gear limit, OPP caps and idle removal were changed together.

## Measured so far

| Boot | Kernel | Duration | Kernel BUG/Oops/panic/KASAN | Userspace |
| --- | --- | --- | --- | --- |
| `0a98b819` | `3629caacc9e1` | 63 min (16:31-17:34 UTC), ended by a deliberate reboot | 0 | Gateway crash loop (NRestarts 19, SEGV/TRAP about every 45 s); `journalctl` segfaulted. Stopped when CPU6/7 were set offline: 0 restarts afterwards. |
| `79176e3f` | `3629caacc9e1` | running since 17:34 UTC | 0 at last check | Gateway 0 restarts at last check, CPU 0-7 online. Too short to judge. |

Observations that are not explained yet:

- Gateway stable with CPU6/7 offline, and (so far) also stable with all 8 CPUs after the next boot. The "mismatched M4 cores kill JIT processes" idea is **not confirmed**.
- Boot log shows `Mismatched cache type (CTR_EL0)` and `Unsupported CPU feature variation` between the boot CPU and CPU6.
- The long ext4 crash needed about 90 min of load; 63 min without a crash is not a pass.

## Not yet verified

- Sustained ext4 write load (hours) on MB007 and on MB013.
- Whether userspace SEGVs are gone, with `systemd-coredump` `Storage=journal` active to capture stacks.

## Pass criteria for closing #44 and merging PR #45

Same load on MB007 and MB013 for several hours: 0 kernel BUG/Oops/panic, 0 userspace SEGV wave, gateway 0 unplanned restarts.

## Boot menu (MB007, 2026-11-24)

Default and top entry: `S10 STABIL 3629caa (Hauptkernel)`. Fallback `805e96e9fe91`, diagnostic KASAN `22fb7c6`, camera test `gc7f24b740a87-dirty` (issue #4).
Older entries moved to `/boot/loader/entries.archiv/` (nothing deleted; backup in `/boot/loader-backup-20261124-tidy`).
Agents must not change the default entry or reboot the device during a load test.
