# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: Для входа MIDI нет порога тишины и калибровки`
- TO: `### Requirement: Для входа MIDI нет порога тишины`

## MODIFIED Requirements

### Requirement: Для входа MIDI нет порога тишины

Пока в Input выбран вход MIDI, поля Silence SHALL не быть: в строке
калибровки SHALL оставаться одна кнопка «Calibrate…», которая начинает
калибровку задержки MIDI (`latency-calibration`). Порог тишины при этом SHALL
оставаться прежним.

#### Scenario: Выбор входа MIDI

- **WHEN** порог тишины −60 dBFS, и пользователь выбирает в Input вход MIDI
- **THEN** поля Silence нет, в строке калибровки одна кнопка «Calibrate…»

#### Scenario: Возврат к звуковому входу

- **WHEN** выбран вход MIDI, и пользователь выбирает в Input канал 1
- **THEN** строка с Silence и «Calibrate…» снова видна, порог тишины
  −60 dBFS
