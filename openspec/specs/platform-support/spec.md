# platform-support Specification

## Purpose

На каких системах работает расширение тренажёра, как пользователь его ставит и
как собираются файлы для раздачи.

## Requirements

### Requirement: Linux x86_64

Расширение SHALL работать в REAPER 7 на Linux x86_64 с glibc 2.35 и новее.

#### Scenario: Ubuntu 22.04

- **WHEN** файл расширения для Linux положен в `UserPlugins` REAPER на
  Ubuntu 22.04
- **THEN** REAPER загружает расширение

### Requirement: macOS 12 и новее, Intel и Apple Silicon одним файлом

Расширение SHALL работать в REAPER 7 на macOS 12 и новее, на Mac с процессором
Intel и с Apple Silicon. Для обоих процессоров SHALL быть один файл.

#### Scenario: MacBook Pro Early 2015

- **WHEN** файл расширения для macOS положен в `UserPlugins` REAPER на MacBook Pro
  Early 2015 с macOS 12
- **THEN** REAPER загружает расширение, ноты измеряются

### Requirement: Установка файлом в UserPlugins

Расширение SHALL ставиться одним файлом в папку `UserPlugins` папки ресурсов
REAPER. При загрузке оно SHALL писать в консоль REAPER строку с версией и
временем сборки.

#### Scenario: Первый запуск после установки

- **WHEN** пользователь положил файл и запустил REAPER
- **THEN** в консоли REAPER строка с версией расширения и временем сборки

#### Scenario: Файл скачан на macOS

- **WHEN** файл скачан из интернета на macOS
- **THEN** инструкция по установке говорит, как снять с него пометку карантина,
  без которой macOS не даёт REAPER его загрузить

### Requirement: Файлы для раздачи собирает CI

Каждый push в репозиторий SHALL собирать и проверять файлы расширения для Linux
и macOS и SHALL выкладывать их в артефакты прогона. Тег версии SHALL
публиковать релиз с этими файлами и их контрольными суммами. Файл для macOS
SHALL объявлять минимальную версию системы 12.0.

#### Scenario: Push в ветку

- **WHEN** в любую ветку пришёл push
- **THEN** в артефактах прогона лежат файлы для Linux и macOS, тесты прошли

#### Scenario: Минимальная версия macOS

- **WHEN** CI собрал файл для macOS
- **THEN** проверка в CI подтверждает, что файл объявляет минимальную версию
  macOS 12.0 для обоих процессоров
