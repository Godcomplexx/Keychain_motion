[English](#english) | [Русский](#русский)

# SmartMotion Keychain Hardware

## English

### Supported Prototype

| Module | Active configuration |
|---|---|
| Controller | ESP32-C3 Super Mini |
| Display | 0.96-inch 128x64 SSD1306-compatible OLED in I2C mode |
| Motion sensor | MPU-6050 at address `0x68` |
| Shared bus | I2C port 0, 200 kHz |
| Battery | Protected single-cell 3.7 V LiPo |

`components/board_config/include/board_config.h` is the source of truth. Update
it before changing this document.

### Wiring

| Module pin | ESP32-C3 | Notes |
|---|---|---|
| OLED `VDD/VCC` | `3V3` | Display power |
| OLED `GND` | `GND` | Common ground |
| OLED `SDA` | `GPIO5` | Shared I2C data |
| OLED `SCK/SCL` | `GPIO6` | Shared I2C clock |
| MPU-6050 `VCC` | `3V3` | Sensor module power |
| MPU-6050 `GND` | `GND` | Common ground |
| MPU-6050 `SDA` | `GPIO5` | Shared I2C data |
| MPU-6050 `SCL` | `GPIO6` | Shared I2C clock |
| MPU-6050 `AD0` | `GND` | Selects address `0x68` |
| MPU-6050 `INT` | `GPIO4` | ESP32-C3 Light Sleep wake-up |
| MPU-6050 `XDA/XCL` | not connected | Auxiliary bus is unused |

I2C allows the display and sensor to share SDA/SCL because their addresses are
different.

### OLED I2C Configuration

The active seven-pin `0.96 OLED V2.3` board is physically strapped for I2C.
Its silkscreen lists `IIC: R1,R4,R6,R7,R8`.

Required configuration:

1. Move the zero-ohm link from `R3` to `R1`.
2. Bridge `R8`; a short soldered wire is a valid jumper.
3. Confirm `R4`, `R6`, and `R7` are populated.
4. Connect `SCK` to `GPIO6` and `SDA` to `GPIO5`.
5. Leave `DC` and `CS` disconnected.

The expected display address is `0x3C`.

### MPU-6050 Modes

Active mode:

- accelerometer range: +/-2 g;
- application scale: 256 counts/g;
- sample rate: 100 Hz;
- gyroscope axes disabled.

Sleep wake mode:

- 5 Hz low-power accelerometer cycle;
- gyroscope and temperature sensor disabled;
- motion threshold approximately 80 mg;
- latched motion interrupt on `INT`;
- ESP32-C3 GPIO wake-up on `GPIO4` high level.

The driver validates `WHO_AM_I == 0x68` before normal operation.

### Power Path

Use a protected single-cell LiPo and a charger designed for 4.2 V termination.
The exact connection to the ESP32-C3 depends on the converter output:

| Measured converter output | ESP32-C3 input |
|---|---|
| Regulated 5 V | `5V/VBUS` |
| Regulated 3.3 V | `3V3` |

Never connect a raw LiPo cell directly to `3V3`; a fully charged cell reaches
4.2 V. Do not feed both `5V/VBUS` and `3V3` at the same time.

Before first power-up:

1. Disconnect the ESP32-C3.
2. Measure converter polarity and output voltage.
3. Confirm common ground.
4. Check for a short between power and ground.
5. Connect the controller only after the voltage is verified.

### Current Measurement

For a 350 mAh battery and a six-hour target:

```text
maximum average current = 350 mAh / 6 h = 58.3 mA
```

Measure current in series with the battery in each state:

| State | What to record |
|---|---|
| `FLUID` | Average active animation current |
| `SLEEP` | Visible idle current |
| Light Sleep | OLED-off baseline and converter quiescent current |
| BLE advertising | Average and peak current |
| `GAME` | Active gameplay current |

USB must be disconnected because USB Serial/JTAG suppresses Light Sleep.
Converter losses, board LEDs, and regulator quiescent current can dominate the
sleep measurement.

### Startup Validation

A healthy boot reports:

```text
i2c_bus: Found I2C device at address 0x3C
i2c_bus: Found I2C device at address 0x68
i2c_bus: I2C scan complete: 2 device(s) found
```

If neither device appears, check power, common ground, and swapped SDA/SCL.
If only one appears, inspect that module and its address/configuration links.

### Hardware Change Checklist

- Update `board_config.h`.
- Check GPIO conflicts at compile time.
- Run the I2C scanner before enabling a driver.
- Build and flash the firmware.
- Verify boot logs and visible behavior.
- Measure active and sleep current again.

---

## Русский

### Поддерживаемый прототип

| Модуль | Активная конфигурация |
|---|---|
| Контроллер | ESP32-C3 Super Mini |
| Дисплей | OLED 0.96", 128x64, SSD1306-совместимый, режим I2C |
| Датчик движения | MPU-6050 по адресу `0x68` |
| Общая шина | I2C port 0, 200 кГц |
| Аккумулятор | Защищённый одноэлементный LiPo 3.7 В |

Источник истины — `components/board_config/include/board_config.h`. Сначала
изменяйте его, затем документацию.

### Подключение

| Контакт модуля | ESP32-C3 | Примечание |
|---|---|---|
| OLED `VDD/VCC` | `3V3` | Питание дисплея |
| OLED `GND` | `GND` | Общая земля |
| OLED `SDA` | `GPIO5` | Общая линия данных I2C |
| OLED `SCK/SCL` | `GPIO6` | Общая тактовая линия I2C |
| MPU-6050 `VCC` | `3V3` | Питание модуля |
| MPU-6050 `GND` | `GND` | Общая земля |
| MPU-6050 `SDA` | `GPIO5` | Общая линия данных I2C |
| MPU-6050 `SCL` | `GPIO6` | Общая тактовая линия I2C |
| MPU-6050 `AD0` | `GND` | Выбирает адрес `0x68` |
| MPU-6050 `INT` | `GPIO4` | Пробуждение ESP32-C3 |
| MPU-6050 `XDA/XCL` | не подключены | Вспомогательная шина не используется |

OLED и MPU-6050 могут совместно использовать SDA/SCL, поскольку имеют разные
адреса.

### Настройка OLED для I2C

Активная семиконтактная плата `0.96 OLED V2.3` должна быть физически настроена
на I2C. На шелкографии указано `IIC: R1,R4,R6,R7,R8`.

1. Перенесите нулевой резистор с `R3` на `R1`.
2. Замкните `R8`; короткий припаянный провод является перемычкой.
3. Убедитесь, что `R4`, `R6` и `R7` установлены.
4. Подключите `SCK` к `GPIO6`, `SDA` к `GPIO5`.
5. Оставьте `DC` и `CS` неподключёнными.

Ожидаемый адрес дисплея — `0x3C`.

### Режимы MPU-6050

Активный режим:

- акселерометр +/-2 g;
- масштаб приложения 256 отсчётов/g;
- частота выборки 100 Гц;
- оси гироскопа выключены.

Режим пробуждения:

- low-power цикл акселерометра 5 Гц;
- гироскоп и датчик температуры выключены;
- порог движения около 80 mg;
- защёлкнутое прерывание на `INT`;
- пробуждение ESP32-C3 высоким уровнем на `GPIO4`.

Драйвер проверяет `WHO_AM_I == 0x68`.

### Питание

Используйте защищённый одноэлементный LiPo и зарядное устройство с окончанием
заряда при 4.2 В. Подключение зависит от измеренного выхода преобразователя:

| Выход преобразователя | Вход ESP32-C3 |
|---|---|
| Стабилизированные 5 В | `5V/VBUS` |
| Стабилизированные 3.3 В | `3V3` |

Не подключайте LiPo напрямую к `3V3`: полностью заряженная ячейка выдаёт
4.2 В. Не подавайте питание одновременно на `5V/VBUS` и `3V3`.

Перед первым включением:

1. Отключите ESP32-C3.
2. Измерьте полярность и выход преобразователя.
3. Проверьте общую землю.
4. Проверьте отсутствие короткого замыкания питания на землю.
5. Подключайте контроллер только после проверки напряжения.

### Измерение тока

Для аккумулятора 350 мА·ч и цели 6 часов:

```text
максимальный средний ток = 350 мА·ч / 6 ч = 58.3 мА
```

Измерьте ток последовательно с аккумулятором:

| Состояние | Что записать |
|---|---|
| `FLUID` | Средний ток активной анимации |
| `SLEEP` | Ток видимой idle-анимации |
| Light Sleep | Базовый ток с выключенным OLED |
| BLE advertising | Средний и пиковый ток |
| `GAME` | Ток активной игры |

USB должен быть отключён, поскольку USB Serial/JTAG блокирует Light Sleep.
Потери преобразователя, светодиоды платы и ток покоя стабилизатора могут
доминировать в режиме сна.

### Проверка при загрузке

Исправное устройство выводит:

```text
i2c_bus: Found I2C device at address 0x3C
i2c_bus: Found I2C device at address 0x68
i2c_bus: I2C scan complete: 2 device(s) found
```

Если нет обоих устройств, проверьте питание, землю и SDA/SCL. Если найдено
только одно, проверяйте соответствующий модуль и его аппаратную конфигурацию.

### Проверка после изменения оборудования

- Обновите `board_config.h`.
- Проверьте конфликты GPIO.
- Запустите I2C-сканер до драйверов.
- Соберите и прошейте проект.
- Проверьте загрузочный лог и отображение.
- Повторно измерьте ток в активном режиме и во сне.
