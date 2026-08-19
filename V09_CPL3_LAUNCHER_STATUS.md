# ATMKoala v0.9 — статус CPL 3 static ELF launcher

**Статус:** проверенный v0.9 baseline execution slice.  
**Дата:** 19 августа 2026 г.

## Что уже реализовано

В дереве v0.9 добавлен `native_app` launcher, который связывает существующие компоненты в единую подготовительную вертикаль: static `ELF64 ET_EXEC` → выделение `user_space_t` → `PT_LOAD` mapping → минимальный 16-byte aligned initial stack → scheduler task → `int 0x80` CPL 3 gate. Launcher намеренно принимает только native static images и не заявляет поддержку `PT_INTERP`, shared libraries, dynamic relocations, `fork` или Linux binaries.

Также добавлен generated ELF64 probe с инструкциями `exit(42)` через `int 0x80`. Это позволило проверить реальный переход на уровне QEMU, а не только работу loader в памяти.

## Обнаруженный blocker

В первом QEMU прогоне probe достиг kernel exit path, но затем воспроизводимо вызвал **General Protection Fault #13** при `iretq` в `irq_common_stub`. Анализ `RIP=0x04001508` показал две взаимосвязанные ошибки: CPL 3 `exit` выполнял nested software IRQ внутри активного `int 0x80` frame, а `context_switch` сохранял RSP до call-return address, хотя восстановление делается через `jmp`, а не `ret`.

Исправление добавило `task_exit_from_syscall()`: CPL 3 `exit` маркирует текущую задачу zombie, удаляет её из run queue и переключается к следующему runnable kernel context, не возвращаясь через уничтоженный user syscall frame. `context_switch` теперь сохраняет post-call `RSP + 8`, сохраняя адрес продолжения отдельно в `RIP`; это предотвращает двойной return и повреждение последующего `iretq`. `queue_remove` также получил bounded `TASK_MAX` traversal guard.

> Это не ошибка user-copy boundary. Stable self-test успешно проверяет `open → write → close → open → read → fstat` через CPL 3-shaped mapped pointers и проверяет отклонение неверного user range с `-ATM_EFAULT`.

## Верификация

Свежая QEMU ISO regression запустила generated static ELF64 probe в отдельном `user_space_t`. Probe перешёл в CPL 3, вызвал `exit(42)` через `int 0x80`, родительская задача получила status через `waitpid`, а экран показал `native-cpl3=OK` вместе с `paging=OK`, `uaccess=OK`, `vfs-posix=OK` и `syscall-usercopy=OK`. Поэтому static ELF launch → CPL 3 → exit/reap lifecycle является проверенным baseline v0.9.

## Следующий технический шаг

Следующий шаг — перенести эту execution slice в полноценный process object: добавить process-owned descriptor table, argv/envp marshalling, memory allocation (`brk`/anonymous mapping) и безопасное продолжение user execution при timer preemption. Поддержка shared libraries, threads и dynamic linking остаётся поздним этапом.
