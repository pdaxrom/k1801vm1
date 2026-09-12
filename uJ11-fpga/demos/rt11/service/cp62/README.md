# UJLOAD ABI2 для CP62

Этот UJLOAD предназначен для FPGA-профиля **CP62**. Он читает файлы через
RT-11 и обращается к HALT FRAM через постоянный вектор резидента.
На плате пока CP56a; CP62 в этом этапе не прошивался. Старый UJLOAD ABI1
для CP60 сохранён в родительском каталоге.

```text
.RUN UJLOAD
UJLOAD> STATUS
ODT=0 FP11=0

.RUN UJLOAD
UJLOAD> ODT
```

Вторая команда устанавливает SY:ODT.BIN. Имя — 1–6 заглавных букв/цифр,
тип ODT/FP11 определяется заголовком ABI2. OFFODT/OFFFP отключают нужную
службу, EXIT завершает программу. Каждый запуск принимает одну команду.
Полных ODT и FP11 файлов пока нет; UJCHEK.MAC проверяет только тестовые
обработчики и возврат контекста.

Создание отдельной копии RT-11 с загрузчиком, из корня uJ11-fpga:

```sh
python3 tools/build_vector_disk_cp62.py --output build/rt11-ujload-cp62-new.dsk
```

Опциональные модули добавляются через `--module path/ODT.BIN`; они
проверяются на ABI2, диапазоны и контрольные суммы. Исходный диск не меняется.
Файлы можно перенести на носитель обычными средствами RT-11 через HG.

Пересборка:

```sh
python3 tools/build_loader_cp62.py
python3 tools/rt11_build.py demos/rt11/service/cp62/UJLOAD.MAC demos/rt11/service/cp62/UJCHEK.MAC --out build/cp62-asm-new
```

UJLOAD.SAV: **4096 байт (8 блоков)**, code/data 3566 байт, entry `001000`.
MACRO/LINK: 0 ошибок. SHA-256: `106d2459e5f87cea70c03f1672c4f322f84bd071750e27df154ec22544dd64b6`.

456-байтный helper в начале запуска копируется из UJLOAD в HALT FRAM.
Его исходник — `firmware/cp62/loader.asm`; `build_loader_cp62.py --assemble`
пересобирает его и обновляет вложенную таблицу в UJLOAD.MAC. FPGA EBR при
этом не меняется.

[ABI2: размещение, вызовы, возврат модулей и ограничения](../../../../docs/vector-loader-cp62.md).
