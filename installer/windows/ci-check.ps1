# Проверка установщика для Windows на чистой машине с настоящим REAPER
# (openspec add-installers, design D6). Идёт в CI на раннере Windows.
#
# Установщик запускается тихо (/VERYSILENT /SUPPRESSMSGBOXES): вопросы
# получают ответ по умолчанию, то есть «нет» и «отмена».
#
# Использование: ci-check.ps1 <установщик .exe> <reaper_training.dll> `
#                  <reaper*_x64-install.exe>
#
# Меняет систему: ставит REAPER, создаёт папки в C:\. Запускать только на
# одноразовой машине.

param(
    [Parameter(Mandatory)] [string] $Setup,
    [Parameter(Mandatory)] [string] $Module,
    [Parameter(Mandatory)] [string] $ReaperSetup
)

$ErrorActionPreference = 'Stop'

$Setup = (Resolve-Path $Setup).Path
$Module = (Resolve-Path $Module).Path
$resources = Join-Path $env:APPDATA 'REAPER'
$plugin = Join-Path $resources 'UserPlugins\reaper_training.dll'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{58AA3CC5-EA40-4963-9857-3BB641C998BE}_is1'
$logs = Join-Path $env:RUNNER_TEMP 'installer-logs'
New-Item -ItemType Directory -Force $logs | Out-Null

function Step([string] $Title) {
    Write-Host ''
    Write-Host "=== $Title"
}

function Fail([string] $Message) {
    Write-Host "::error::$Message"
    exit 1
}

# Запускает установщик тихо с ключами $Extra и возвращает код выхода. Журнал
# установщика — в $logs\<Name>.log, он же печатается.
function Invoke-Setup([string] $Name, [string[]] $Extra = @()) {
    $log = Join-Path $logs "$Name.log"
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"") + $Extra
    $process = Start-Process -FilePath $Setup -ArgumentList $arguments -Wait -PassThru
    if (Test-Path $log) {
        Get-Content $log | Write-Host
    }
    Write-Host "Код выхода: $($process.ExitCode)"
    return $process.ExitCode
}

function Test-SameFile([string] $Path) {
    (Test-Path $Path) -and
        (Get-FileHash $Path).Hash -eq (Get-FileHash $Module).Hash
}

$reaperDir = Join-Path $env:ProgramFiles 'REAPER (x64)'
if (Test-Path (Join-Path $reaperDir 'reaper.exe')) { Fail 'На раннере уже есть REAPER' }
if (Test-Path $resources) { Fail 'На раннере уже есть папка ресурсов REAPER' }

Step 'Без REAPER установщик отказывается'
if ((Invoke-Setup 'no-reaper') -eq 0) { Fail 'Установщик без REAPER отработал успешно' }
if (Test-Path $plugin) { Fail 'Без REAPER модуль поставлен' }

Step 'Портативный 32-битный REAPER — отказ'
# 32-битный reaper.exe изображает любая 32-битная программа из SysWOW64.
$portable32 = 'C:\reaper-portable-x86'
New-Item -ItemType Directory -Force $portable32 | Out-Null
Copy-Item (Join-Path $env:SystemRoot 'SysWOW64\cmd.exe') (Join-Path $portable32 'reaper.exe')
New-Item -ItemType File -Force (Join-Path $portable32 'reaper.ini') | Out-Null
if ((Invoke-Setup 'reaper-x86' @("/DIR=`"$portable32`"")) -eq 0) {
    Fail 'Установщик поставил модуль в 32-битный REAPER'
}
if (Test-Path (Join-Path $portable32 'UserPlugins')) { Fail 'В 32-битный REAPER модуль поставлен' }

Step 'REAPER ставится своим установщиком'
Start-Process -FilePath $ReaperSetup -ArgumentList '/S' -Wait
Get-Process reaper -ErrorAction SilentlyContinue | Stop-Process -Force
if (-not (Test-Path (Join-Path $reaperDir 'reaper.exe'))) { Fail "REAPER не поставился в $reaperDir" }
Write-Host 'Что REAPER записал в реестр:'
foreach ($key in 'HKLM:\SOFTWARE\REAPER', 'HKLM:\SOFTWARE\WOW6432Node\REAPER',
                 'HKCU:\Software\REAPER',
                 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\REAPER',
                 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\REAPER') {
    if (Test-Path $key) {
        Write-Host "[$key]"
        Get-ItemProperty $key | Format-List | Out-String | Write-Host
    }
}

Step 'Установка'
if ((Invoke-Setup 'install') -ne 0) { Fail 'Установка не удалась' }
if (-not (Test-SameFile $plugin)) { Fail 'Поставленный модуль не совпадает с собранным' }
$log = Get-Content (Join-Path $logs 'install.log') -Raw
if ($log -notmatch 'Administrative install mode: No') { Fail 'Установка шла в режиме администратора' }
if ($log -notmatch 'Install mode root key: HKEY_CURRENT_USER') { Fail 'Установка писала не в HKCU' }
if (-not (Test-Path $uninstallKey)) { Fail 'Нет записи в списке программ (HKCU)' }
$uninstaller = (Get-ItemProperty $uninstallKey).UninstallString.Trim('"')
Write-Host "Деинсталлятор: $uninstaller"
if ($uninstaller.StartsWith($resources, [StringComparison]::OrdinalIgnoreCase)) {
    Fail 'Деинсталлятор лежит в папке ресурсов REAPER'
}

Step 'Повторная установка'
if ((Invoke-Setup 'reinstall') -ne 0) { Fail 'Повторная установка не удалась' }
$files = @(Get-ChildItem (Join-Path $resources 'UserPlugins'))
if ($files.Count -ne 1) { Fail "В UserPlugins не один файл: $($files.Name -join ', ')" }

Step 'Портативный REAPER через /DIR'
$portable = 'C:\reaper-portable'
New-Item -ItemType Directory -Force $portable | Out-Null
Copy-Item (Join-Path $reaperDir 'reaper.exe') $portable
New-Item -ItemType File -Force (Join-Path $portable 'reaper.ini') | Out-Null
if ((Invoke-Setup 'portable' @("/DIR=`"$portable`"")) -ne 0) { Fail 'Установка в портативный REAPER не удалась' }
if (-not (Test-SameFile (Join-Path $portable 'UserPlugins\reaper_training.dll'))) {
    Fail 'В портативный REAPER модуль не поставлен'
}

Step 'Папка без reaper.ini через /DIR — отказ'
$notReaper = 'C:\not-reaper'
New-Item -ItemType Directory -Force $notReaper | Out-Null
if ((Invoke-Setup 'not-reaper' @("/DIR=`"$notReaper`"")) -eq 0) { Fail 'Установщик поставил модуль в папку без reaper.ini' }
if (Test-Path (Join-Path $notReaper 'UserPlugins')) { Fail 'В папку без reaper.ini модуль поставлен' }

Step 'Удаление'
New-Item -ItemType File -Force (Join-Path $resources 'reaper.ini') | Out-Null
# Деинсталлятор Inno Setup перезапускает себя из временной папки и сразу
# выходит, поэтому результат ждётся по файлам.
Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
$portablePlugin = Join-Path $portable 'UserPlugins\reaper_training.dll'
for ($i = 0; $i -lt 60 -and ((Test-Path $plugin) -or (Test-Path $portablePlugin) -or (Test-Path $uninstallKey)); $i++) {
    Start-Sleep -Seconds 1
}
if (Test-Path $plugin) { Fail 'После удаления модуль на месте' }
if (Test-Path $portablePlugin) { Fail 'После удаления модуль в портативном REAPER на месте' }
if (Test-Path $uninstallKey) { Fail 'После удаления запись в списке программ на месте' }
if (-not (Test-Path (Join-Path $resources 'reaper.ini'))) { Fail 'Удаление тронуло reaper.ini' }
if (-not (Test-Path (Join-Path $portable 'reaper.ini'))) { Fail 'Удаление тронуло reaper.ini портативного REAPER' }

Write-Host ''
Write-Host 'Установщик для Windows прошёл все проверки'
