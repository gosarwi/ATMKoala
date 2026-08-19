# Архитектурные основания: AWM и ATPK

## Нативный оконный сервер вместо X11

Официальная спецификация X11 определяет полноценный двоичный request/reply/event protocol, соединение client/server, большую модель ресурсов (windows, pixmaps, graphics contexts, fonts, atoms, properties) и десятки запросов. Даже базовая реализация требует transport, object namespace, event delivery, focus/grab semantics и совместимости с форматом X11 сообщений.

ATMKoala создаёт **AWM (ATM Window Manager/Server)**: нативный локальный API поверх VBE, Exp, keyboard и mouse. Он будет предоставлять surface, windows, focus, Z-order, pointer/key events, damage/redraw и базовые drawing primitives. Это X-подобный server layer по архитектурной роли, но **не X11/Xlib/XCB server и не Linux binary compatibility layer**.

## Debian-подобные архивы вместо `.deb` compatibility

Формат Debian `.deb` является `ar` archive с строго упорядоченными `debian-binary`, `control.tar.*` и `data.tar.*`. Он предполагает обработку Unix paths, dpkg control fields, допустимые maintainer scripts и родной Linux userspace.

ATMKoala создаёт **ATPK**: собственный archive format с идеей разделения metadata и payload, но с нативными limits и защитами. Рекомендуемый layout:

```text
package.atpk (tar.zst container)
├── ATMKOALA/control       # Package, Version, Architecture, Depends, Description
├── ATMKOALA/manifest      # путь, размер, checksum
├── ATMKOALA/hooks         # только декларативные post-install actions; никаких shell scripts
└── data/
    ├── apps/<package>/
    ├── share/icons/
    ├── share/fonts/
    └── share/doc/
```

Installer обязан отклонять absolute paths, `..`, дубликаты путей, слишком большие entries и несовпадающие checksums. Установка должна разворачиваться в staging area с manifest journal и rollback до фиксации реестра. `Architecture` ограничивается `atmkoala-x86_64` либо `all`; ELF/Linux executables не принимаются как installable native apps.

## Источники

1. X Window System Protocol: https://xorg.freedesktop.org/releases/X11R7.7/doc/xproto/x11protocol.html
2. X New Developer’s Guide: https://www.x.org/guide/concepts/
3. Debian Policy: Control files: https://www.debian.org/doc/debian-policy/ch-controlfields.html
4. Debian Administrator’s Handbook, binary package structure: https://debian-handbook.info/browse/stable/packaging-system.html
5. deb(5): https://man7.org/linux/man-pages/man5/deb.5.html
