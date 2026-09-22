# BASIC-11 для RT-11

Готовые [B81FIS/B81FIJ/B81FPU/B81FPD](../../../releases/basic/manifest.json)
собраны из DEC BASIC-11 V2.1. Имена сохранены для совместимости с файлами
на SD и командами пользователя.

- Без FPP: исходный `B81FIS` и адаптированный `B81FIJ`.
- С FPP: `B81FIJ` (FIS), `B81FPU` (single), `B81FPD` (double).
- `FISABI.MAC`: 26-байтовый адаптер стека ошибок .SFPA.
- `B81TST.BAS`: 29 сравнений; `B81DBL.BAS`: проверка double.

Передать SAV и BAS через HG на SD; `RUN B81FIJ`, выбрать `A`, затем
`RUN B81TST`, выход `BYE`. Переключение FPP требует cold RESET.
[Подробности и результаты](../../../docs/basic.md),
[команды сборки](../../../docs/development.md).
