[English](#english) | [Русский](#русский)

# SmartMotion Keychain Architecture

## English

### System Scope

SmartMotion Keychain consists of two deliverables:

1. ESP32-C3 firmware built with ESP-IDF 6.0.1.
2. An Android 8.0+ companion application that sends time and game commands
   over Bluetooth Low Energy.

The firmware owns all real-time behavior. The phone is not required for normal
animation, motion wake-up, step counting, or gameplay.

### Runtime Overview

```text
Android application
    |
    | BLE GATT write
    v
phone_sync (NimBLE host task)
    |
    | protected pending update
    v
main loop
    +-- motion_state
    +-- device_clock / step_counter
    +-- FLUID -> flip_animation
    +-- SLEEP -> idle_animation
    +-- TIME  -> time_animation
    +-- GAME  -> breakout_game
    |
    +-- mpu6050 -> motion_detector
    +-- oled_display -> SSD1306 framebuffer -> I2C
```

### State Machine

| State | Entry | Behavior | Exit |
|---|---|---|---|
| `FLUID` | Boot, motion wake, game end | Tilt-controlled particle animation | 30 seconds without movement |
| `SLEEP` | FLUID inactivity | Dim, low-frame-rate idle animation | Motion or another 30 seconds |
| Light Sleep | Extended SLEEP inactivity | OLED and BLE off; MPU motion interrupt active | MPU-6050 `INT` |
| `TIME` | Valid phone datetime received | Time, date, steps, sync indicator | 60-second timeout |
| `GAME` | `GAME:START` received | Local Breakout physics; BLE off | 3 lost lives, 5 minutes without input, or triple shake |

A triple shake is a transport-enabling gesture, not a direct TIME or GAME
transition. It starts a bounded 60-second advertising window. The final state
depends on the payload written by the phone.

### BLE Protocol

| Field | Value |
|---|---|
| Device name | `KeychainSync` |
| Service UUID | `11223344-5566-7788-9a49-315b10371342` |
| Characteristic UUID | `11223344-5566-7788-9a49-315b10371343` |
| Operations | Read, Write |

Accepted writes:

```text
YYYY-MM-DD HH:MM:SS
GAME:START
```

Command lifecycle:

1. The motion detector recognizes three shake peaks within 2.5 seconds.
2. Firmware initializes NimBLE and advertises for up to 60 seconds.
3. Android connects and writes one payload.
4. The GATT callback stores the update behind a FreeRTOS critical section.
5. The main loop consumes the update.
6. Firmware terminates the connection and deinitializes NimBLE.

The disconnect after an acknowledged write is intentional.

### Execution and Concurrency

- Application state, rendering, MPU reads, and game physics run in the ESP-IDF
  main task.
- NimBLE runs its own host task only while BLE is needed.
- Datetime and command handoff uses a `portMUX_TYPE` critical section.
- OLED rendering uses one private 1024-byte framebuffer.
- No heap allocation occurs in the Breakout frame loop.
- Display and MPU-6050 transactions share one I2C bus.

### Component Boundaries

| Component | Owns | Must not own |
|---|---|---|
| `board_config` | Pins, addresses, bus settings | Runtime behavior |
| `i2c_bus` | Bus lifetime and scanning | Sensor/display policy |
| `mpu6050` | Registers and raw acceleration | Application states |
| `motion_detector` | Movement, stillness, shake recognition | Rendering |
| `motion_state` | Allowed state transitions | Hardware access |
| `oled_display` | Framebuffer and SSD1306 transfer | Animation physics |
| Animation components | Frame generation | BLE and power policy |
| `breakout_game` | Game state, collision, levels | BLE lifecycle |
| `phone_sync` | GATT and pending phone updates | UI state transitions |
| `main` | Integration and lifecycle policy | Low-level register logic |

### Android Application

The Android application uses:

- `MainActivity` for permission handling and manual commands;
- `KeychainBleSync` for scan, GATT discovery, and characteristic writes;
- `KeychainSyncService` as a `connectedDevice` foreground service;
- `BootReceiver` to restore an enabled Auto Sync service after reboot;
- `SyncPreferences` for persistent Auto Sync state.

Background time sync uses `SCAN_MODE_LOW_POWER`. Explicit time and game
commands use `SCAN_MODE_BALANCED`.

### Design Invariants

- BLE remains off unless a triple shake opens a synchronization window.
- GAME can start only after a phone command.
- Light Sleep is not entered while USB Serial/JTAG is connected.
- A display transmission error must not terminate BLE/time synchronization.
- GPIO assignments are changed only in `board_config.h`.
- Hardware addresses must be confirmed by the startup I2C scan.

### Change Checklist

Before merging firmware changes:

```powershell
idf.py build
```

Before merging Android changes:

```powershell
cd android_time_sync
.\gradlew.bat assembleDebug
```

Hardware regression checks:

- startup scan reports `0x3C` and `0x68`;
- motion controls FLUID and Breakout;
- inactivity reaches SLEEP and then Light Sleep;
- motion interrupt wakes the controller;
- time and `GAME:START` writes are acknowledged;
- BLE powers down after each completed command.

---

## Русский

### Состав системы

SmartMotion Keychain состоит из двух частей:

1. Прошивка ESP32-C3 на ESP-IDF 6.0.1.
2. Android-приложение для Android 8.0+, передающее время и игровые команды
   через Bluetooth Low Energy.

Вся логика реального времени работает на ESP32-C3. Телефон не нужен для
обычной анимации, пробуждения, подсчёта шагов и уже запущенной игры.

### Схема выполнения

```text
Android-приложение
    |
    | BLE GATT write
    v
phone_sync (задача NimBLE)
    |
    | защищённое ожидающее обновление
    v
главный цикл
    +-- motion_state
    +-- device_clock / step_counter
    +-- FLUID -> flip_animation
    +-- SLEEP -> idle_animation
    +-- TIME  -> time_animation
    +-- GAME  -> breakout_game
    |
    +-- mpu6050 -> motion_detector
    +-- oled_display -> framebuffer SSD1306 -> I2C
```

### Автомат состояний

| Состояние | Вход | Поведение | Выход |
|---|---|---|---|
| `FLUID` | Загрузка, пробуждение, конец игры | Анимация частиц от наклона | 30 секунд без движения |
| `SLEEP` | Неподвижность во FLUID | Тусклая медленная анимация | Движение или ещё 30 секунд |
| Light Sleep | Длительная неподвижность | OLED и BLE выключены, активен motion interrupt | `INT` MPU-6050 |
| `TIME` | Получено корректное время | Время, дата, шаги и статус | Тайм-аут 60 секунд |
| `GAME` | Получено `GAME:START` | Локальная физика Breakout, BLE выключен | 3 жизни, 5 минут без управления или тройное встряхивание |

Тройное встряхивание не открывает TIME или GAME напрямую. Оно включает
60-секундное BLE-окно, а следующее состояние определяется payload телефона.

### BLE-протокол

| Поле | Значение |
|---|---|
| Имя устройства | `KeychainSync` |
| Service UUID | `11223344-5566-7788-9a49-315b10371342` |
| Characteristic UUID | `11223344-5566-7788-9a49-315b10371343` |
| Операции | Read, Write |

Поддерживаемые записи:

```text
YYYY-MM-DD HH:MM:SS
GAME:START
```

Жизненный цикл команды:

1. Детектор фиксирует три пика встряхивания за 2.5 секунды.
2. Прошивка запускает NimBLE и рекламируется до 60 секунд.
3. Android подключается и записывает один payload.
4. GATT callback сохраняет данные под FreeRTOS critical section.
5. Главный цикл забирает обновление.
6. Прошивка разрывает соединение и деинициализирует NimBLE.

Отключение после подтверждённой записи является штатным.

### Выполнение и конкурентность

- Состояния, отрисовка, MPU-6050 и игровая физика работают в main task.
- Отдельная задача NimBLE существует только при включённом BLE.
- Передача времени и команд защищена `portMUX_TYPE`.
- OLED использует один приватный framebuffer размером 1024 байта.
- Игровой цикл не выделяет динамическую память.
- OLED и MPU-6050 совместно используют одну шину I2C.

### Границы компонентов

| Компонент | Ответственность |
|---|---|
| `board_config` | GPIO, адреса и параметры шин |
| `i2c_bus` | Жизненный цикл I2C и сканирование |
| `mpu6050` | Регистры и сырые данные ускорения |
| `motion_detector` | Движение, неподвижность и встряхивания |
| `motion_state` | Допустимые переходы состояний |
| `oled_display` | Framebuffer и передача SSD1306 |
| Компоненты анимации | Формирование кадров |
| `breakout_game` | Физика, столкновения и уровни |
| `phone_sync` | GATT и ожидающие обновления телефона |
| `main` | Интеграция и политика жизненного цикла |

### Android-приложение

- `MainActivity` управляет разрешениями и ручными командами.
- `KeychainBleSync` выполняет scan, GATT discovery и запись.
- `KeychainSyncService` является foreground service типа `connectedDevice`.
- `BootReceiver` восстанавливает включённый Auto Sync после перезагрузки.
- `SyncPreferences` сохраняет состояние Auto Sync.

Фоновая синхронизация использует `SCAN_MODE_LOW_POWER`, явные команды —
`SCAN_MODE_BALANCED`.

### Инварианты

- BLE выключен, пока тройное встряхивание не откроет окно.
- GAME запускается только командой телефона.
- При подключённом USB Serial/JTAG Light Sleep не включается.
- Ошибка передачи OLED не должна останавливать BLE-синхронизацию.
- GPIO изменяются только через `board_config.h`.
- Адреса оборудования подтверждаются стартовым I2C-сканером.

### Проверка изменений

```powershell
idf.py build

cd android_time_sync
.\gradlew.bat assembleDebug
```

Аппаратная регрессия:

- I2C-скан показывает `0x3C` и `0x68`;
- наклон управляет FLUID и Breakout;
- неподвижность включает SLEEP и Light Sleep;
- `INT` MPU-6050 пробуждает контроллер;
- время и `GAME:START` принимаются;
- BLE выключается после команды.
