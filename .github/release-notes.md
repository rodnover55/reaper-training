## Какие файлы для какой системы

| Система | Файл | Состояние |
|---|---|---|
| Linux x86_64 (glibc 2.35+) | `reaper_training.so` | проверено вживую: окно, режимы, count-in, петля, совпадение нот с записью REAPER |
| macOS 12+ (Apple Silicon и Intel) | `reaper_training.dylib` | собирается и проходит тесты в CI; **на Mac ещё не запускался** |

Тестовая сборка: поиск нот проверен на синтетической гитаре и шуме микрофона,
на записях настоящей гитары — ещё нет. Тренажёр рассчитан на чистый сигнал
гитары (DI), без перегруза.

Как поставить — в [руководстве по установке](https://github.com/rodnover55/reaper-training/blob/master/docs/install.md),
как пользоваться — в [руководстве](https://github.com/rodnover55/reaper-training/blob/master/docs/usage.md).
