# ADHD-limiter

ADHD-limiter — небольшой таймер для разделения времени работы и отдыха и переключения внимания.

## Возможности

- Оконное приложение Windows с системными кнопками свернуть, развернуть и закрыть.
- Системный фон окна и масштабируемая разметка.
- Зеленый таймер отдыха слева и красный таймер работы справа.
- Кнопка-стрелка «Пуск» внизу; после завершения периода она становится кнопкой подтверждения перехода.
- Системный звуковой сигнал при завершении периода.
- Редактирование минут в паузе и перед запуском.
- Значения по умолчанию: работа — 20 минут, отдых — 20 минут.

## Скачать

Готовые архивы доступны в разделе [Releases](https://github.com/MaksimStryapunin/ADHD-limiter/releases):

- Windows: `ADHD-limiter-Windows-x64.zip`
- Linux: `ADHD-limiter-Linux-x64.tar.gz`
- macOS: `ADHD-limiter-macOS-x64.tar.gz`

В текущей beta полноценный GUI реализован для Windows. Linux и macOS собираются CI как кроссплатформенная заготовка до добавления GUI-бэкенда.

## Запуск

### Windows

Распакуйте `ADHD-limiter-Windows-x64.zip` и запустите `ADHD-limiter.exe`.

### Linux

```bash
tar -xzf ADHD-limiter-Linux-x64.tar.gz
cd ADHD-limiter
chmod +x ADHD-limiter
./ADHD-limiter
```

### macOS

Распакуйте архив и запустите бинарник `ADHD-limiter`. При блокировке Gatekeeper можно выполнить `xattr -dr com.apple.quarantine ADHD-limiter`.

## Сборка из исходников

Требования: CMake 3.16+, компилятор с C++17, Visual Studio 2019 или новее на Windows, GCC/Clang на Linux/macOS.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

На Windows исполняемый файл находится в `build/Release/ADHD-limiter.exe`, на Unix-подобных системах — в `build/ADHD-limiter`.

## Ограничения beta

- Нативный GUI пока поддерживается только Windows.
- На Linux и macOS релизный бинарник является проверочной консольной заготовкой.
- Настройки сохраняются только в рамках текущего запуска приложения.
