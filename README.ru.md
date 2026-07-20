[English](README.md) | **Русский**

# SmartMotion Keychain

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.1-E7352C?style=flat-square&logo=espressif)](https://github.com/espressif/esp-idf)
[![Target](https://img.shields.io/badge/target-ESP32--C3-111111?style=flat-square&logo=espressif)](https://www.espressif.com/en/products/socs/esp32-c3)
[![Firmware](https://img.shields.io/badge/firmware-C-00599C?style=flat-square&logo=c)](main/main.c)
[![Android](https://img.shields.io/badge/Android-8.0%2B-3DDC84?style=flat-square&logo=android&logoColor=white)](android_time_sync)
[![BLE](https://img.shields.io/badge/BLE-on--demand-0082FC?style=flat-square&logo=bluetooth&logoColor=white)](docs/ARCHITECTURE.md)
[![Status](https://img.shields.io/badge/status-hardware%20prototype-F59E0B?style=flat-square)](docs/HARDWARE.md)

SmartMotion Keychain — маленький интерактивный брелок на ESP32-C3 с
OLED-дисплеем и датчиком движения MPU-6050. Он реагирует на наклон, засыпает
при бездействии, синхронизирует время с Android-приложением по BLE и запускает
мини-игру Breakout с управлением движением.

Идея проекта похожа на карманный Tamagotchi: это небольшой физический объект,
который ощущает движение, оживает в руках, засыпает в покое и объединяет
embedded-прошивку, электронику и мобильное приложение.

| | |
|---|---|
| <img src="docs/assets/demo.gif" alt="Демонстрация SmartMotion Keychain" width="360"> | <img src="docs/assets/ascii-magic.png" alt="SmartMotion Keychain — ASCII-эффект частиц" width="360"> |

| | |
|---|---|
| **Статус** | Рабочий аппаратный прототип |
| **Прошивка** | ESP-IDF 6.0.1 / ESP32-C3 |
| **Приложение** | Android 8.0+ |
| **Дисплей** | OLED 0.96", 128x64 |

## Зачем я сделала этот проект

Проект начался как идея личного устройства: брелока, который ощущается не
просто инструментом, а маленьким цифровым объектом со своим поведением. На
одном связанном прототипе я объединила архитектуру embedded-прошивки, датчик
движения, BLE, энергосбережение, Android-интеграцию и раннюю продуктовую
разработку.

## Главное

- Анимация частиц `FLUID`, реагирующая на наклон.
- Режимы `SLEEP` и Light Sleep при бездействии.
- Аппаратное пробуждение через `INT` MPU-6050.
- Тройное встряхивание для открытия короткого BLE-окна.
- Синхронизация времени с Android-телефоном.
- Экран времени, даты, шагов и статуса синхронизации.
- Breakout со случайными уровнями и управлением наклоном.
- Модульная архитектура ESP-IDF вместо монолитного `main.c`.
- BLE включается только по необходимости для экономии батареи.

## Как ведёт себя брелок

| Режим | Поведение |
|---|---|
| `FLUID` | Частицы перемещаются в зависимости от наклона |
| `SLEEP` | Через 30 секунд включается тусклая медленная анимация |
| Light Sleep | Ещё через 30 секунд OLED выключается, MPU-6050 остаётся источником пробуждения |
| `TIME` | Показывает время, дату, шаги и статус синхронизации |
| `GAME` | Локально запускает Breakout с управлением акселерометром |

Тройное встряхивание открывает BLE на 60 секунд. В остальное время брелок не
рекламируется, поэтому радиомодуль не расходует энергию постоянно.

## Оборудование

- ESP32-C3 Super Mini.
- OLED 0.96", 128x64, SSD1306-совместимый контроллер.
- Акселерометр/гироскоп MPU-6050.
- Одноэлементный LiPo-аккумулятор 3.7 В.
- Защищённое зарядное устройство и стабилизированный преобразователь.

OLED и MPU-6050 используют общую I2C-шину 200 кГц:

| Сигнал | ESP32-C3 |
|---|---:|
| SDA | `GPIO5` |
| SCL | `GPIO6` |
| MPU-6050 INT | `GPIO4` |

Ожидаемые устройства:

```text
0x3C  OLED
0x68  MPU-6050
```

Полная схема подключения, перевод OLED в I2C, питание и измерение тока:
[docs/HARDWARE.md](docs/HARDWARE.md).

## Архитектура

```text
Android-приложение
    |
    | BLE GATT: время / GAME:START
    v
phone_sync (NimBLE)
    |
    v
автомат состояний
    +-- FLUID -> flip_animation
    +-- SLEEP -> idle_animation
    +-- TIME  -> time_animation + device_clock + step_counter
    +-- GAME  -> breakout_game
    |
    +-- mpu6050 -> motion_detector -> wake / shake / tilt
    +-- oled_display -> framebuffer SSD1306 -> общая I2C
```

### Компоненты прошивки

| Компонент | Ответственность |
|---|---|
| `board_config` | GPIO, адреса и параметры шин |
| `i2c_bus` | Общая шина и проверка устройств при загрузке |
| `mpu6050` | Работа с датчиком и low-power wake |
| `motion_detector` | Движение, неподвижность и встряхивания |
| `motion_state` | Переходы `FLUID/SLEEP/TIME/GAME` |
| `oled_display` | Framebuffer и передача SSD1306 |
| `flip_animation` | Активная анимация частиц |
| `idle_animation` | Энергосберегающая idle-анимация |
| `time_animation` | Время, дата и шаги |
| `breakout_game` | Физика, столкновения и генерация уровней |
| `phone_sync` | BLE GATT по запросу |
| `android_time_sync` | Приложение-компаньон |

Полная схема выполнения, BLE-протокол и архитектурные ограничения:
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Галерея

| Демонстрация движения | Концепт-рендер | ASCII-эффект частиц |
|---|---|---|
| <img src="docs/assets/demo.gif" alt="Демонстрация SmartMotion Keychain" width="240"> | <img src="docs/assets/prototype.webp" alt="Концепт-рендер SmartMotion Keychain" width="240"> | <img src="docs/assets/ascii-magic.png" alt="ASCII-эффект частиц SmartMotion Keychain" width="240"> |

Сейчас материалы показывают интерактивную концепцию и направление дизайна
корпуса. Фотографии собранной электроники будут добавлены после интеграции
корпуса.

## Быстрый старт

### Требования

- ESP-IDF 6.0.1 с toolchain для ESP32-C3.
- Python, CMake и Ninja из ESP-IDF Tools.
- Android Studio, JDK 17 и Android SDK 35 для приложения.

### Сборка и прошивка

```powershell
git clone https://github.com/Godcomplexx/Keychain_motion.git
cd Keychain_motion

. $env:IDF_PATH\export.ps1
idf.py set-target esp32c3
idf.py build
idf.py -p COM13 flash monitor
```

Замените `COM13` на порт платы. Выход из монитора: `Ctrl+]`.

Исправная загрузка показывает:

```text
i2c_bus: Found I2C device at address 0x3C
i2c_bus: Found I2C device at address 0x68
i2c_bus: I2C scan complete: 2 device(s) found
```

## Android-приложение

Откройте `android_time_sync` в Android Studio либо выполните:

```powershell
cd android_time_sync
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleDebug
```

APK создаётся здесь:

```text
android_time_sync/app/build/outputs/apk/debug/app-debug.apk
```

### Auto Sync

1. Выдайте разрешения Bluetooth и уведомлений.
2. Нажмите `Start auto sync`.
3. Три раза встряхните брелок.
4. Приложение найдёт `KeychainSync`, передаст время и отключится.

Фоновая синхронизация использует низкоэнергетический BLE scan. Явные ручные
команды используют более быстрый balanced-режим.

### Breakout

1. Нажмите `Start Breakout`.
2. Три раза встряхните брелок.
3. Телефон отправит `GAME:START`.
4. BLE выключится, а игра продолжит работать локально.
5. Наклоняйте брелок влево и вправо для движения платформы.

В игре три жизни, случайные уровни и постепенно растущая скорость. Общего
лимита активной игры нет; выход происходит после пяти минут без управления.

## Энергосбережение

Прошивка уменьшает средний ток без удаления интерактивных режимов:

- через 30 секунд неподвижности FLUID переходит в SLEEP;
- ещё через 30 секунд OLED выключается;
- MPU-6050 переходит в low-power motion detection;
- движение на `INT` пробуждает ESP32-C3;
- BLE включается только после тройного встряхивания;
- после команды и во время Breakout BLE полностью выключен.

Для шести часов от аккумулятора 350 мА·ч:

```text
максимальный средний ток = 350 мА·ч / 6 ч = 58.3 мА
```

Реальную автономность необходимо измерить на собранном устройстве. USB
Serial/JTAG блокирует Light Sleep, поэтому измерения выполняются без USB.

## Проверка

```powershell
idf.py build

cd android_time_sync
.\gradlew.bat lintDebug assembleDebug
```

Аппаратный checklist:

- I2C-скан находит `0x3C` и `0x68`;
- FLUID и Breakout реагируют на наклон;
- бездействие включает SLEEP и Light Sleep;
- движение MPU-6050 пробуждает контроллер;
- Auto Sync обновляет время;
- после команды BLE выключается.

## Документация

- [Архитектура и устройство ПО](docs/ARCHITECTURE.md)
- [Оборудование, подключение, питание и диагностика](docs/HARDWARE.md)

## Планы и ограничения

- Измерить ток во всех режимах и опубликовать реальную автономность.
- Откалибровать подсчёт шагов для разных положений брелока.
- Добавить host-тесты автомата состояний и игровой логики.
- Добавить воспроизводимые аппаратные проверки OLED, MPU-6050 и BLE.
- Подготовить release-подпись Android-приложения для распространения.
