// Числовые идентификаторы форм и контролов модуля.
//
// Формат требует именно чисел: SWELL сопоставляет контролы диалога с кодом по
// ним, а не по именам.

#pragma once

// Именно макросы, а не enum: этот заголовок включается в forms.rc, который
// читает не компилятор C++, а генератор таблицы диалога.
// NOLINTBEGIN(modernize-macro-to-enum,cppcoreguidelines-macro-usage)

// --- Окно тренажёра ------------------------------------------------------------

#define IDD_TRAINER 100

#define IDC_MODE_LABEL 1000
#define IDC_MODE 1001
#define IDC_TOLERANCE_LABEL 1002
#define IDC_TOLERANCE 1003
#define IDC_CHANNEL_LABEL 1004
#define IDC_CHANNEL 1005
#define IDC_SILENCE_LABEL 1006
#define IDC_SILENCE 1007
#define IDC_STATUS 1008

// NOLINTEND(modernize-macro-to-enum,cppcoreguidelines-macro-usage)
