# KVM и ATMKoala: реализуемость, архитектура и план внедрения

**Автор:** Manus AI  
**Статус:** архитектурная документация для ATMKoala v0.5  
**Дата:** 18 августа 2026 г.

## Краткий вывод

KVM для ATMKoala возможен, но слово «KVM» может означать два разных проекта. Первый и практически доступный вариант — **запуск ATMKoala как гостевой системы в Linux KVM/QEMU**. В этом случае Linux предоставляет `/dev/kvm`, а QEMU использует KVM для аппаратно ускоренного выполнения гостевого x86-кода. ATMKoala менять почти не требуется: достаточно корректного Multiboot2 ISO и совместимых виртуальных устройств.

Второй вариант — **собственный hypervisor внутри ATMKoala**, который запускает другую ОС или второй экземпляр ATMKoala. Это уже не портирование Linux KVM. Потребуется реализовать собственный VMM на базе Intel VMX (VT-x) и/или AMD SVM, обработку VM-exit, guest physical memory, виртуальные interrupt-контроллеры, timers, device model, загрузку гостя, изоляцию и восстановление после ошибок. Это реалистичная долгосрочная ветка, но не небольшое расширение текущего ядра.

| Вариант | Что реально получить | Сложность | Рекомендуемый порядок |
|---|---|---:|---:|
| ATMKoala как guest в Linux KVM/QEMU | Быстрый аппаратно ускоренный запуск ISO | Низкая | Сделать первым |
| ATMKoala как host для простого guest | Минимальный proof-of-concept с одним vCPU и serial output | Очень высокая | После стабилизации SMP/ACPI |
| Полноценный ATMKoala hypervisor | Запуск разных ОС, виртуальные диски, сеть и GUI | Экстремально высокая | Отдельная ветка v0.7+ |
| Портирование Linux KVM в ATMKoala | Непрактично: KVM опирается на Linux kernel ABI, MM и device infrastructure | Неподходящая цель | Не выбирать |

## Вариант A: запуск ATMKoala внутри Linux KVM

Linux KVM API организован вокруг `/dev/kvm`, file descriptors и ioctl-вызовов: приложение создаёт VM, vCPU и виртуальные устройства через разные классы дескрипторов [1]. В типичном сценарии эти детали скрыты QEMU. ATMKoala выступает обычным ISO-гостем.

Проверка на Linux:

```sh
test -e /dev/kvm && echo "KVM device exists"
ls -l /dev/kvm
egrep -o 'vmx|svm' /proc/cpuinfo | head
```

Запуск ATMKoala:

```sh
qemu-system-x86_64 \
  -enable-kvm \
  -machine q35 \
  -cpu host \
  -m 256M \
  -smp 1 \
  -vga std \
  -cdrom atmkoala-OS-v0.5-users-init.iso
```

Для первой проверки лучше использовать `-smp 1`, поскольку текущая система прежде всего ориентирована на стабильный однопроцессорный boot path. После отдельной SMP-регрессии можно проверить `-smp 2` и больше.

## Вариант B: собственный VMM внутри ATMKoala

Собственный VMM должен быть отдельным подсистемным слоем, а не прямой копией Linux KVM. На Intel требуется VMXON-region, VMCS для каждой виртуальной машины и корректная настройка VM-entry/VM-exit controls; официальное руководство Intel описывает VMX instructions и Intel VT в System Programming Guide, Volume 3 [2]. На AMD аналогичная ветка строится вокруг SVM/VMCB и требует отдельного чтения AMD Architecture Programmer’s Manual.

Минимальная архитектура ATMKoala могла бы выглядеть так:

```text
+------------------------------+
| guest manager / vmmctl       |
+------------------------------+
| vCPU scheduler               |
| VM-exit dispatcher           |
| guest memory / EPT or NPT    |
| virtual PIC/APIC + timer     |
| virtual serial console       |
+------------------------------+
| Intel VMX или AMD SVM        |
+------------------------------+
| ATMKoala physical kernel     |
```

Первый proof-of-concept не должен пытаться запускать Linux с GUI. Безопасная последовательность такая: включить проверку CPU virtualization; выделить одну страницу VMX/SVM state на CPU; создать один guest address space; загрузить маленький freestanding guest; запустить один vCPU; обработать только HLT, CPUID, MSR и serial I/O; затем корректно остановить guest.

## Этапы реализации собственного VMM

| Этап | Обязательная работа | Критерий готовности |
|---:|---|---|
| 0 | Архитектурное обнаружение | `kvm status`-подобная команда сообщает VMX/SVM, NX, EPT/NPT и доступные CPUs без изменения состояния машины. |
| 1 | VMX/SVM lifecycle | Вход и выход из virtualization mode проходят на тестовой машине, включая cleanup при ошибке. |
| 2 | Guest memory | Гость видит согласованную физическую память и получает page faults/выходы без повреждения host memory. |
| 3 | Один vCPU | Guest reset vector, CPUID, HLT и serial output работают в deterministic test. |
| 4 | Прерывания и время | Виртуальные PIC/APIC, PIT/HPET-like timer и IRQ delivery проходят тесты. |
| 5 | Устройства | Добавляются virtio-blk или простой IDE read-only disk, затем virtio-net и framebuffer. |
| 6 | Управление | `vm create/start/stop/info`, лимиты памяти/CPU, журналирование и fail-closed shutdown. |
| 7 | Гостевой ATMKoala | Второй ATMKoala загружается из read-only ATPK-backed disk image. |

## Почему нельзя просто «добавить KVM»

KVM — это не самостоятельный загрузочный бинарник. Его API предполагает Linux kernel subsystem, file descriptors, ioctl ABI, host process address space и userspace VMM вроде QEMU [1]. Поэтому перенос отдельных названий `KVM_CREATE_VM` или `KVM_RUN` в freestanding ATMKoala не даст совместимости.

Для ATMKoala правильнее создать собственный **AKVM — ATMKoala Virtual Machine Monitor API**, сохранив нативный ABI:

```c
typedef struct akvm_vm akvm_vm_t;
typedef struct akvm_vcpu akvm_vcpu_t;

int akvm_probe(akvm_caps_t *out);
int akvm_vm_create(const akvm_vm_config_t *cfg, akvm_vm_t **out);
int akvm_vcpu_create(akvm_vm_t *vm, uint32_t id, akvm_vcpu_t **out);
int akvm_run(akvm_vcpu_t *vcpu, akvm_exit_t *exit);
int akvm_vm_stop(akvm_vm_t *vm);
void akvm_vm_destroy(akvm_vm_t *vm);
```

Публичный API должен использовать versioned structures, explicit capability checks, bounded memory allocations и понятный перечень exit reasons. Нельзя разрешать guest memory mapping за пределы заранее выделенного диапазона или выполнять guest-provided addresses напрямую в host kernel.

## Совместимость с текущим железом

Pentium 4 и Celeron J4105 нельзя автоматически считать одинаково пригодными для собственного VMM. Нужно проверять реальные CPUID/MSR возможности конкретного CPU и BIOS. Для запуска ATMKoala в QEMU/KVM достаточно, чтобы хост Linux поддерживал KVM и QEMU мог предоставить гостю нужный x86-профиль. Для собственного VMM понадобятся аппаратные virtualization extensions и корректная firmware configuration.

## Безопасность

Собственный VMM должен стартовать только после явной проверки всех control bits. Любая неопределённая VM-exit причина, ошибка VM-entry, повреждение guest page tables или невозможность доставить виртуальное прерывание должна приводить к остановке конкретной VM, а не к продолжению с потенциально повреждённым host kernel.

Btrfs и ATPK не следует связывать с первой версией VMM. На первом этапе виртуальный диск лучше сделать read-only и использовать ATPK payload или простой raw image. Write-back storage, snapshots и CoW должны появиться только после отдельной подсистемы журналирования и crash consistency.

## Рекомендуемый план для ATMKoala

Сначала документировать и тестировать ATMKoala как KVM guest. Затем добавить `akvm_probe` и read-only capability report. После этого реализовать serial-only guest и только потом виртуальный диск. Полноценный GUI-гость, network emulation и nested virtualization должны считаться отдельными проектами.

## References

[1]: https://docs.kernel.org/virt/kvm/index.html "Linux kernel KVM documentation"
[2]: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html "Intel 64 and IA-32 Architectures Software Developer Manuals"
[3]: https://edc.intel.com/content/www/us/en/design/products/platforms/processor-and-core-i3-n-series-datasheet-volume-1-of-2/001/intel-virtualization-technology-intel-vt-for-intel-64-and-intel-architecture-int/ "Intel Virtualization Technology (Intel VT-x)"
