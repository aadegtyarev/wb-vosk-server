# wb-vosk-server

Проект **wb-vosk-server** представляет собой серверное приложение, предназначенное для работы с моделью распознавания речи Vosk. Vosk — это легковесная и высокоэффективная библиотека для распознавания речи, которая поддерживает множество языков и может быть использована как на сервере, так и на мобильных устройствах.

Основные возможности:
- Распознавание речи в реальном времени.
- Поддержка множества языков.
- Интеграция с различными приложениями через API.

Есть скетч прошивки для M5AtomEcho, которая по кнопке отправляет аудио на сервер для распознавания.

Для более подробной информации о настройке и использовании, обратитесь к документации Vosk или к файлам конфигурации в проекте.

## Использование

### Прошивка
Используется M5AtomEcho.

Для компиляции прошивки у вас должна быть установлена Arduino, я использую cli версию.

Компиляция:
```sh
./esp/M5AtomEcho/m5stack_build.sh
```

Компиляция и прошивка:
```sh
./esp/M5AtomEcho/m5stack_build.sh -u --port /dev/ttyUSB0
```

Посмотреть вывод в терминал: 
```sh
minicom -D /dev/ttyUSB0 -b 115200
```

### Сервер

```sh
pip install -r ./server/requirements.txt
```

Запустите сервер:
```sh
python ./server/wb-vosk-server.py
```

# Настройка среды для компиляции скетчей Arduino в VSCode

### 1. Установи `arduino-cli`
```sh
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```
Проверь установку:
```sh
arduino-cli version
```

### 2. Добавь поддержку плат (например, M5Stack)
```sh
arduino-cli config add board_manager.additional_urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32
```

### 3. Установи VSCode и расширение Arduino
- Установи **VSCode** (или **VS Codium**):
  ```sh
  sudo apt install codium
  ```
- Установи **расширение Arduino** из магазина расширений.

### 4. Создай проект и настрой компиляцию
```sh
mkdir my_project && cd my_project
arduino-cli sketch new my_sketch
```

### 5. Компиляция и прошивка из терминала
Узнай порт платы:
```sh
arduino-cli board list
```
Компиляция:
```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack-atom my_sketch
```
Загрузка прошивки:
```sh
arduino-cli upload -p /dev/ttyUSB0 --fqbn m5stack:esp32:m5stack-atom my_sketch
```

Теперь можно писать и компилировать скетчи прямо в **VSCode**.

