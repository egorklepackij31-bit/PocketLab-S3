# IncognitoX - Тестирование

## 1. Хост-тесты (чистая логика, без железа)

Файлы: `test/host_test.cpp`, `src/lib/nmea.cpp`, `src/lib/pulse.cpp`.

Сборка (mingw g++):

```bash
g++ -std=c++17 -O2 -I src test/host_test.cpp src/lib/nmea.cpp src/lib/pulse.cpp -o test/host_test.exe
```

Запуск:

```bash
test\host_test.exe
```

Покрытие:
- NMEA 0183: парсинг GGA/RMC/GSV, контрольная сумма, координаты, скорость/course,
  дата, южное полушарие/западная долгота.
- OOK: EV1527-подобный вектор (24 бита, 300/900 мкс), пауза между кадрами, мусор,
  ведущая пауза.

## 2. Проверка компиляции (реальная прошивка)

```bash
pio run -e incognitox
pio run -e incognitox -t size
pio run -e incognitox_psram
```

Результат (2026-11-25, финальная сборка после правок переполнений текста):

| Env           | RAM   | Flash |
|---------------|-------|-------|
| `incognitox`      | 26.1% (85652 / 327680) | 51.1% (1606520 / 3145728) |
| `incognitox_psram`| - | - |

`incognitox_psram` требует внешнюю PSRAM-плату (W25Q32/16MB); в текущем тесте собирался только `incognitox`.

## 3. Pin-ф validator

```bash
python tools/check_pins.py
```

Проверяет пересечения назначений периферии и кнопок. Цель: 0 ошибок.

## 4. On-device self-test

`Screens -> System -> Self-test`: проверка дисплея, I2C/SPI/DDS, SD, UART, бата, звука.

## 5. Текущий статус прошивки

- Сборка `incognitox`: **SUCCESS** (RAM 26.1%, Flash 51.1%).
- Первая прошивка загружена на ESP32-S3 (IncognitoX hardware).
- Текст: сканер `scan_text.py` (временная папка `C:\Users\egork\AppData\Local\Temp\opencode\scan_text.py`)
  после правок переполнений: **total 0** (переполнений нет).
- Финальная загрузка (2026-11-25) не удалась: порт COM14 не определяется,
  ESP32-S3 сейчас не виден как COM-устройство. Необходимо:
  1. Проверить USB-соединение с платой (переподключить кабель/порт).
  2. Задать заново `--upload-port COM14` при повторной загрузке.
  3. Запустить `pio run -e incognitox -t upload` и проверить `SUCCESS`.
