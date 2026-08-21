# Статус устройств и мультимедиа ATMKoala

**Статус документа:** локальная инженерная фиксация для ATMKoala v0.9. Она описывает только реализованные и проверенные границы. Наличие PCI-устройства не означает, что ОС уже управляет этим устройством.

## Итоговая матрица

| Подсистема | Проверенно реализовано | Намеренно не заявляется |
|---|---|---|
| MP3 | Bounded анализ MPEG Audio Layer III: ID3v2 skip, проверка серии frame headers, metadata, `mp3info <path>`, MIME `audio/mpeg` | MPEG-to-PCM decoding, HDA playback, аудиомикшер, файловый player |
| Intel HDA | Non-destructive PCI controller detection, BAR diagnostics, synthetic regression | CORB/RIRB, codec verbs, DMA descriptors, IRQ, PCM stream, звук |
| Intel UHD 600 | PCI ID `8086:3185` detection, bootloader-framebuffer handoff, diagnostics `gpu` | Modesetting, GTT/GEM, display-pipe control, 3D, DRM/i915 compatibility |
| Батарея | Явное состояние `telemetry unavailable`; UI не рисует выдуманный процент | ACPI AML `_BST`/`_BIX` evaluator, generic EC driver, реальный процент на произвольном ноутбуке |
| Wi‑Fi | PCI class discovery; separate controller/driver/connected/signal fields | MAC driver, firmware loader, scan, association, WPA, data plane |
| Bluetooth | Separate USB-host, controller, HCI-driver, enabled and connected fields | USB enumeration, HCI transport, pairing, RFCOMM, BLE |
| Tray Exp | Семантические battery/Wi‑Fi/Bluetooth glyphs; зелёный только для подтверждённого connection state | Изображение готового подключения, когда отсутствует реальный драйвер |

> **Ключевое правило интерфейса:** жёлтый означает обнаруженное устройство или подготовленный драйвер без подключения; зелёный зарезервирован для подтверждённого рабочего соединения; серый означает unavailable/unknown.

## MP3 и аудиовывод

Модуль `src/mp3.c` самостоятельно реализует ограниченный анализ Layer III frame headers. Он требует три совместимые frame headers в первых 128 KiB, распознаёт MPEG-1, MPEG-2 и MPEG-2.5 Layer III, sample rate, bitrate, channels, VBR variation и оценку длительности по проверенной части потока. Команда `mp3info` выводит эти данные, не пытаясь открыть поток как текст или изображение.

MPEG audio состоит из последовательности frame blocks, а metadata может быть расположена вне MPEG frames.[1] У validated header есть sync, version, layer, bitrate, sample-rate, padding и channel-mode fields.[2] Это делает inspection полезным, но само по себе не создаёт PCM samples.

Intel HD Audio architecture требует controller command/response rings, codec verbs, DMA engines, cyclic PCM buffers, interrupt handling и mixer policy.[3] ATMKoala сейчас только определяет HDA controller безопасным чтением PCI configuration-space. Поэтому **MP3 playback не реализован**: система честно сообщает, что player и PCM sink отсутствуют.

## Intel UHD Graphics 600

Intel указывает PCI ID `0x3185` как **Intel UHD Graphics 600**, Gen9 Gemini Lake.[4] ATMKoala выделяет его из PCI scan, сохраняет observed BAR metadata и, только когда bootloader уже выдал валидный 24/32-bpp framebuffer, связывает его с `g_i915` как inherited framebuffer handoff.

Этот путь не включает устройство через PCI command register, не пишет GPU MMIO, не трогает power wells или display pipes. Следовательно, безопасно говорить о **UHD 600 detection + firmware framebuffer**, но нельзя говорить о нативном драйвере видеокарты, аппаратном ускорении или смене режима экрана.

## Батарея и радиоустройства

Control Method Battery в ACPI обычно требует выполнения `_BST` для динамического состояния и `_BIX`/`_BIF` для статических характеристик.[5] В ATMKoala пока нет ACPI table + AML object evaluator. Старый OEM-specific raw EC guess отключён: обращение к случайным EC registers может зависнуть на несовместимой firmware. Поэтому tray выводит `--` и gray icon, пока не появится подтверждённый battery provider.

Wi‑Fi controller может быть обнаружен по PCI network-controller class, но association и RSSI требуют MAC driver и firmware. Bluetooth обычно требует USB enumeration плюс HCI transport. Exp теперь показывает эти уровни раздельно; controller detection не становится false `connected`.

## Regression evidence

Финальный QEMU harness требует следующие additional serial markers:

| Marker | Проверяемый инвариант |
|---|---|
| `[mp3] parser-ok` | Valid Layer III synthetic frames parse; broken sequence fails. |
| `[hda] detect-ok` | HDA PCI controller discovery preserves `pcm_output_ready=0`. |
| `[uhd600] detect-ok` | Exact UHD 600 PCI detection + handoff metadata; no modeset/3D flag. |
| `[vbe] fastpath-ok` | Clipped 32-bpp VBE fill optimization preserves pixels. |
| `[hardware] status-ok` | Controller-only Wi‑Fi/USB and unavailable battery do not become false connected telemetry. |

## Следующие обязательные инженерные работы

Полезный MP3 playback потребует сначала full HDA transport path и PCM sink, затем independently implemented/appropriately licensed Layer III decoder. Реальная батарея потребует bounded ACPI discovery plus AML evaluation of battery methods. Wi‑Fi и Bluetooth требуют конкретных transport drivers и firmware/provenance policy. Эти блоки намеренно не скрыты косметическими состояниями.

## References

[1] [RFC 3003 — The audio/mpeg Media Type](https://datatracker.ietf.org/doc/html/rfc3003)

[2] [MP3' Tech — MPEG Audio frame header](http://www.mp3-tech.org/programmer/frame_header.html)

[3] [Microsoft — Intel's HD Audio Architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/intel-s-hd-audio-architecture)

[4] [Intel — Legacy GPUs: `3185` / UHD Graphics 600 / Gemini Lake](https://dgpu-docs.intel.com/overview/supported-hardware/legacy-gpus.html)

[5] [UEFI Forum — ACPI Control Method Battery objects](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/10_Power_Source_and_Power_Meter_Devices/Power_Source_and_Power_Meter_Devices.html)
