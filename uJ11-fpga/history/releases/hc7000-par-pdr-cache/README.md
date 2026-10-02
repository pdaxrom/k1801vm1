# HC7000 MMU: PAR/PDR cache, 50 MHz

> Историческая запись: `releases/hc7000-par-pdr-cache/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Two register-based PAR/PDR cache entries remove two clocks on a hit. Access
protection, PDR.W write-through, CSR invalidation and restart metadata are
preserved. Addressing tables issue their first operation directly; MUL and
DIV use shorter microcode. HC1200 and SERV firmware remain unchanged.

Routed resources: **4239 LUT4, 1441 FF, 26 EBR**; Fmax **50.345 MHz**.
The 50 MHz clock and external SRAM timing constraints pass. Compared with
`ac51a20`, this costs 102 LUT and 93 FF, without another EBR. Synthetic mapped
loops improve by 13.8%, 14.3% and 7.5%; see the performance document for scope.

Validation includes cached/uncached MMU comparisons with Lattice models;
2696 integer differential programs at 24/50 MHz; MMU/CPU/ODT/CSM/event tests;
24 MHz microcoded-FPP regression; 11 build-profile tests; and complete BSD,
RT-11 XM, RT-11 V4 and RSX boot scenarios. Individual results and UART logs
are in `validation/`; `qualification.json` records source hashes and passes.
The unchanged SRAM controller retains the previously qualified pin-delay
model; the new routing is checked against the full external timing constraints.

JED checksum **6BC0**, SHA-256
`271e38f0aee2547daf59da0c4a7805175c9f1c38774a2a1f31b6439638afe578`.
**This JED has not been installed on the physical board.**

`sources/` freezes the synthesized and tested inputs. `baseline/` holds the
earlier mapped-loop measurements and generated bench/ROM; the corresponding
CPU source is commit `ac51a20`. Disk images are referenced by hash rather than
duplicated. Lattice's simulation models are not redistributed.

See [CPU changes and measurements](../../../docs/hc7000-cpu-performance.md) and
the user-confirmed [BSD console delay fix](../../../docs/bsd-console.md).
