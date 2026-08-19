# Политика записи Btrfs в ATMKoala

## Решение

ATMKoala **не включает частичный Btrfs write driver**. Доступная подсистема остаётся read-only superblock inspection и отклоняет любые запросы на включение записи с явной причиной. Это намеренное fail-closed поведение, а не недостающая команда.

## Техническое обоснование

Btrfs не допускает безопасной модели «перезаписать существующий блок файла». Файловые и metadata blocks используют copy-on-write; обновление требует выделить новые блоки, создать/изменить узлы B-tree, обновить logical-to-physical mapping через chunk tree, extent refcounts и checksum tree, после чего атомарно зафиксировать новые root pointers и superblock generation. Неполная реализация повредит файловую систему даже для одиночного устройства без RAID.

| Компонент, обязательный для записи | Статус в ATMKoala |
|---|---|
| Чтение primary superblock | Реализовано |
| Проверка magic, UUID, label и geometry | Реализовано |
| CRC32C metadata/data checksums | Не реализовано |
| Chunk tree logical-to-physical mapping | Не реализовано |
| Root/extent/checksum B-tree traversal | Не реализовано |
| CoW allocation и extent refcounts | Не реализовано |
| Transaction commit и обновление superblock mirrors | Не реализовано |
| Btrfs mount/VFS operations | Не реализовано |

## Граница реализации

Команда `btrfs rw on` и одноимённый API должны возвращать отказ без выполнения ATA write. Когда перечисленные компоненты будут реализованы и проверены на disposable QEMU images с power-loss tests, write support может быть пересмотрена как отдельный большой проект.

## Источники

1. Btrfs design: <https://btrfs.readthedocs.io/en/latest/dev/dev-btrfs-design.html>
2. Btrfs on-disk format: <https://btrfs.readthedocs.io/en/latest/dev/On-disk-format.html>
3. Btrfs introduction: <https://btrfs.readthedocs.io/en/latest/Introduction.html>
