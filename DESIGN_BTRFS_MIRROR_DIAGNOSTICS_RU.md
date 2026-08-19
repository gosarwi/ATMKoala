# Btrfs mirror и checksum diagnostics

**Статус:** проектное обоснование для ATMKoala v0.5. Реализация остаётся строго read-only и не выполняет ни одной ATA-записи.

Btrfs superblock расположен по фиксированному offset `0x10000` (64 KiB) от начала устройства или раздела. При достаточном размере носителя имеются mirrors по offset `0x04000000` (64 MiB) и `0x4000000000` (256 GiB). Поле checksum занимает первые 32 bytes superblock; область проверки начинается с offset `0x20` и продолжается до конца 4 KiB superblock. Формат предусматривает CRC32C в little-endian с seed `0xffffffff`. [1]

Нативный инспектор ATMKoala должен выбирать только superblock, который одновременно имеет корректный magic `_BHRfS_M`, физический bytenr, допустимые geometry fields и совпадающий CRC32C. При наличии нескольких проверенных зеркал он выбирает самое новое по generation и сообщает состояние каждого зеркала. Это помогает диагностировать повреждение, но не выполняет repair и не изменяет primary/mirror superblock.

> Btrfs использует CoW B-tree, transaction generations, data checksum tree и commit-порядок, в котором superblock фиксирует уже записанный новый root tree. Поэтому частичная запись superblock или файлов без полного transaction engine опасна и должна оставаться fail-closed. [2]

| Возможность | Статус ATMKoala |
|---|---|
| Primary/mirror superblock probe | Реализуется read-only |
| CRC32C superblock verification | Реализуется read-only |
| Выбор newest valid generation | Реализуется read-only |
| Tree traversal, mount, files | Не реализуется |
| CoW transaction, allocators, checksums tree | Не реализуется |
| Btrfs write/repair | Явно отклоняется |

## References

[1] [Btrfs documentation — On-disk Format](https://btrfs.readthedocs.io/en/latest/dev/On-disk-format.html)

[2] [Btrfs documentation — Btrfs design](https://btrfs.readthedocs.io/en/latest/dev/dev-btrfs-design.html)

[3] [David Sterba — Selecting the next checksum for btrfs](https://kdave.github.io/selecting-hash-for-btrfs/)
