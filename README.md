# CalculatorOS

## 📸 Скриншоты

![Главный экран](screenshots/1.png)
![help](screenshots/2.png)
![ls /dev](screenshots/3.png)

**Bare Metal Calculator** — операционная система-калькулятор, работающая напрямую на железе (без Linux/Windows).  
Поддерживает BIOS и UEFI (через CSM).

## ✨ Возможности

### ➗ Арифметика

- Сложение (`+`), вычитание (`-`), умножение (`*`), деление (`/`), остаток (`%`)
- Степень (`^`), квадратный корень (`sqrt`)
- Скобки и унарный минус

### 📊 Функции

| Команда | Описание | Пример |
|---------|----------|--------|
| `rch x n` | Округление до n знаков | `rch 3.14159 2` → `3.14` |
| `ceil x` / `floor x` | Округление вверх/вниз | `ceil 3.14` → `4.00` |
| `abs x` | Абсолютное значение | `abs -5` → `5.00` |
| `pct x total` | Процент от числа | `pct 20 200` → `10.00%` |
| `vat x [rate]` | НДС (по умолч. 20%) | `vat 100` → `20.00` |
| `sum / avg / min / max` | Множественные аргументы | `sum 10 20 30` → `60.00` |

### 🔧 Переменные

- `var name = value` – создать переменную (int/double/string)
- `echo name` – вывести значение
- `type name` – показать тип
- `list_vars` – список всех переменных
- `delete name` – удалить переменную

### 🎨 Интерфейс

| Команда | Описание |
|---------|----------|
| `bg-color <color>` | Установить цвет фона |
| `char-color <color>` | Установить цвет текста |
| `cursor-color <color>` | Установить цвет курсора |
| `clear` / `cls` | Очистить экран |

**Доступные цвета:** black, blue, green, cyan, red, magenta, brown, lightgray, darkgray, lightblue, lightgreen, lightcyan, lightred, lightmagenta, yellow, white

### 🖥️ Графическая подсистема – VESA

- **VESA BIOS Extensions (VBE) 2.0** – современный графический режим без VGA
- **Линейный фреймбуфер (LFB)** – прямой доступ к видеопамяти
- **Разрешение:** 1024×768 пикселей, 32 бита на пиксель (True Color)
- **Двойная буферизация (Double Buffering)** – плавный рендеринг без мерцания
- **Кастомный шрифт** – 8×8 пикселей, загрузка из BIOS
- **Аппаратный курсор мыши** – PS/2 мышь поверх фреймбуфера
- **3D-куб** – вращающийся куб на фоне (можно отключить командой `cube off`)

### 💾 Дисковая подсистема

#### AHCI (DMA) – SATA

Полноценная поддержка SATA через AHCI (Advanced Host Controller Interface):
- `/dev/sda`, `/dev/sdb`, `/dev/sdc`, `/dev/sdd` – SATA диски (до 4 устройств)
- `/dev/sda1`, `/dev/sda2`, `/dev/sda3`, `/dev/sda4` – разделы MBR на SATA
- **DMA (Direct Memory Access)** – данные передаются без участия процессора
- **PRDT (Physical Region Descriptor Table)** – поддержка scatter/gather
- **32 командных слота** – очередь команд
- **Высокая скорость** – до 300 МБ/с (SATA2)
- Реализован строго по спецификации AHCI 1.3.1

### 🐚 Файловая система – **MFS**

Дисковая файловая система с поддержкой папок, файлов, разделов MBR и VFS.

#### Работа с файлами и папками

| Команда | Описание | Пример |
|---------|----------|--------|
| `new <file>` | Создать пустой файл | `new test.txt` |
| `newdir <dir>` | Создать папку | `newdir myfolder` |
| `write <file> <text>` | Записать текст в файл | `write test.txt Hello` |
| `cat <file>` | Прочитать файл | `cat test.txt` |
| `rm <file>` | Удалить файл | `rm test.txt` |
| `rmdir <dir>` | Удалить пустую папку | `rmdir myfolder` |
| `cp <src> <dst>` | Копировать файл | `cp test.txt copy.txt` |
| `mv <src> <dst>` | Переместить файл | `mv test.txt moved.txt` |
| `ls [path]` | Список файлов/папок | `ls /myfolder` |
| `cd <path>` | Сменить текущую папку | `cd myfolder` |
| `pwd` | Показать текущий путь | `pwd` |

#### Ссылки

| Команда | Описание | Пример |
|---------|----------|--------|
| `hlink <target> <link>` | Создать жёсткую ссылку | `hlink /file.txt /link.txt` |
| `unlink <link>` | Удалить жёсткую ссылку | `unlink /link.txt` |
| `symlink <target> <link>` | Создать символическую ссылку | `symlink /home/docs /docs` |
| `readlink <link>` | Показать цель ссылки | `readlink /docs` |

**Особенности MFS 2.0:**
- **Жёсткие ссылки** – несколько имён файла с общим inode
- **Счётчик ссылок (link_count)** – файл удаляется только когда счётчик достигает 0

#### Управление разделами MBR

| Команда | Описание | Пример |
|---------|----------|--------|
| `showpart` | Показать таблицу разделов | `showpart` |
| `mkpart /dev/hda <num> <size>` | Создать раздел (размер в MB) | `mkpart /dev/hda 2 100` |
| `delpart /dev/hda<num>` | Удалить раздел | `delpart /dev/hda2` |
| `format <device>` | Отформатировать раздел в MFS | `format /dev/sda1` |

#### Монтирование и блочные устройства

| Команда | Описание | Пример |
|---------|----------|--------|
| `mount <device> <path>` | Смонтировать раздел в папку | `mount /dev/sda1 /mnt` |
| `umount <path>` | Отмонтировать раздел | `umount /mnt` |
| `ls /dev` | Список устройств | `ls /dev` |

**Блочные устройства:**
- AHCI: `/dev/sda`, `/dev/sdb`, `/dev/sdc`, `/dev/sdd`
- AHCI разделы: `/dev/sda1`–`/dev/sda4`

## 🔧 Низкоуровневая архитектура

### ACPI (Advanced Configuration and Power Interface)
- Поиск RSDP в EBDA и области BIOS (0xE0000–0x100000)
- Парсинг RSDT, проверка контрольных сумм всех таблиц
- Поддержка таблиц: MADT (APIC), MCFG (PCIe), HPET, FACP (FADT)
- Reset Register для аппаратной перезагрузки
- S5 состояние (Soft Off) для выключения питания

### HPET (High Precision Event Timer)
- Таймер высокой точности, замена устаревшего PIT
- Частота до 14.31818 МГц (зависит от чипсета)
- Режим one-shot с автоматическим перезапуском
- Системный таймер 60 Гц

### APIC (Advanced Programmable Interrupt Controller)
- Local APIC (LAPIC) – MMIO по адресу из MSR 0x1B
- IOAPIC – обработка прерываний от устройств
- Полная замена устаревшего PIC

### PCI Express
- Доступ к конфигурационному пространству через MCFG из ACPI
- MMIO по адресу из таблицы MCFG
- Fallback на legacy PCI при отсутствии MCFG
- Поддержка PCIe extended конфигурации (4096 байт на устройство)

### ⚙️ JIT – ассемблер на лету

Прямое выполнение x86 инструкций (исключения обрабатываются штатно).

```asm
asm mov eax, 42
asm mov ebx, 0
asm div ebx            ; → Divide Error (#DE)
asm int 3              ; → Breakpoint (#BP)
asm ud2                ; → Invalid Opcode (#UD)
asm xor ax, ax
asm mov ds, ax         ; → General Protection Fault (#GP)
asm mov cr0, eax       ; → меняет режим процессора
asm lidt [0]           ; → убивает IDT
```

## ⌨️ Все команды

`vat`, `pct`, `rch`, `ceil`, `floor`, `abs`, `sum`, `avg`, `min`, `max`, `sqrt`
`var`, `echo`, `type`, `list_vars`, `delete`
`new`, `write`, `cat`, `rm`, `newdir`, `rmdir`, `ls`, `cd`, `pwd`, `cp`, `mv`
`mount`, `umount`, `mkpart`, `delpart`, `format`, `showpart`
`symlink`, `readlink`
`bg-color`, `char-color`, `cursor-color`, `clear`, `cls`
`asm`, `time`, `help`, `exit`, `hlink`, `unlink`

## 🛠️ Сборка и запуск

```bash
git clone https://github.com/madmenmadmen/calculatoros.git
cd calculatoros
make
make setup-disk
make run
```

![License](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Platform](https://img.shields.io/badge/Platform-BIOS%20%7C%20UEFI%20(CSM)-blue)
![C](https://img.shields.io/badge/C-95%25-brightgreen)
[![Python](https://img.shields.io/badge/Python-1.7%25-yellow.svg)](https://www.python.org/)
[![Linker Script](https://img.shields.io/badge/Linker%20Script-0.1%25-blue.svg)]()