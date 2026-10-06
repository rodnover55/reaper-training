; Установщик reaper-training для Windows (openspec add-installers, design D2).
;
; Собирается целью installer из installer/CMakeLists.txt:
;
;   iscc /DAppVersion=<версия> /DSourceFile=<reaper_training.dll> reaper-training.iss
;
; {app} здесь — папка ресурсов REAPER, а не папка программы: модуль ложится в
; {app}\UserPlugins. Всё ставится с правами пользователя, без UAC.
;
; Файл в UTF-8 с BOM: без BOM компилятор читает его в кодировке системы, и
; русские сообщения ломаются.

#ifndef AppVersion
  #error Не задана версия: iscc /DAppVersion=<версия>
#endif
#ifndef SourceFile
  #error Не задан модуль: iscc /DSourceFile=<путь к reaper_training.dll>
#endif

[Setup]
; Один AppId на все версии: новая версия обновляет ту же запись в списке
; программ, а журнал удаления дописывается — удаление убирает модуль из всех
; папок, куда он ставился.
AppId={{58AA3CC5-EA40-4963-9857-3BB641C998BE}
AppName=reaper-training
AppVersion={#AppVersion}
AppPublisher=Sergei Melnikov
AppPublisherURL=https://github.com/rodnover55/reaper-training
AppSupportURL=https://github.com/rodnover55/reaper-training/issues
AppUpdatesURL=https://github.com/rodnover55/reaper-training/releases
VersionInfoVersion={#AppVersion}
UninstallDisplayName=reaper-training
PrivilegesRequired=lowest
; Windows на ARM тоже: REAPER для ARM64EC загружает модули x64.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Папка ищется заново при каждом запуске, а не берётся из прошлой установки:
; REAPER могли переставить. Страница выбора папки видна, только когда REAPER
; не найден (ShouldSkipPage).
DefaultDirName={code:DefaultResourceDir}
UsePreviousAppDir=no
DisableDirPage=no
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
; Деинсталлятор — не в папке REAPER, а в %LOCALAPPDATA%\Programs.
UninstallFilesDir={userpf}\reaper-training
; Запущенный REAPER проверяется по окну (PrepareToInstall). Менеджер
; перезапуска Windows увидел бы только REAPER, который держит модуль прошлой
; версии, и при первой установке промолчал бы.
CloseApplications=no
RestartApplications=no
WizardStyle=modern
Compression=lzma2
SolidCompression=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "{#SourceFile}"; DestDir: "{app}\UserPlugins"; Flags: ignoreversion

[CustomMessages]
en.ReaperNotFound=REAPER 7 (64-bit) was not found on this computer. Install REAPER and run it once, then run this installer again.
en.AskPortable=If you use a portable REAPER, click Yes and choose its folder, the one with reaper.ini.
en.ReaperNot64Bit=REAPER in %1 is 32-bit, and reaper-training needs 64-bit REAPER.
en.ReaperTooOld=REAPER in %1 is version %2, and reaper-training needs REAPER 7 or newer.
en.NotResourceDir=%1 is not a REAPER folder: there is no reaper.ini in it.
en.CloseReaper=REAPER is running. Close REAPER and click Retry.
en.ReaperStillRunning=REAPER is running. Close it and run the installer again.
en.SelectPortableDir=Choose the folder of your portable REAPER, the one with reaper.exe and reaper.ini. The extension goes to its UserPlugins folder.
en.Finished=reaper-training is installed in %1.%n%nStart REAPER: the action list (Actions → Show action list) has the action "reaper-training: Show/hide timing trainer".
ru.ReaperNotFound=REAPER 7 (64-битный) на этом компьютере не найден. Поставьте REAPER и запустите его хотя бы раз, затем запустите установщик снова.
ru.AskPortable=Если REAPER портативный, нажмите «Да» и укажите его папку — ту, где лежит reaper.ini.
ru.ReaperNot64Bit=REAPER в %1 — 32-битный, а reaper-training нужен 64-битный REAPER.
ru.ReaperTooOld=REAPER в %1 — версии %2, а reaper-training нужен REAPER 7 или новее.
ru.NotResourceDir=%1 — не папка REAPER: в ней нет reaper.ini.
ru.CloseReaper=REAPER запущен. Закройте REAPER и нажмите «Повтор».
ru.ReaperStillRunning=REAPER запущен. Закройте его и запустите установщик снова.
ru.SelectPortableDir=Укажите папку портативного REAPER — ту, где лежат reaper.exe и reaper.ini. Расширение ляжет в её папку UserPlugins.
ru.Finished=reaper-training установлен в %1.%n%nЗапустите REAPER: в списке действий (Actions → Show action list) появится действие «reaper-training: Show/hide timing trainer».

[Code]
const
  { Машина в заголовке PE: x64 и ARM64X. В процесс REAPER с любой из них
    загружается модуль x64. }
  MachineAmd64 = $8664;
  MachineArm64 = $AA64;
  ReaperWindowClass = 'REAPERwnd';

var
  { Папка ресурсов найденного REAPER; пустая строка — REAPER не найден. }
  DetectedDir: String;

{ Возвращает машину из заголовка PE файла FileName: $8664 у x64, $14C у x86.
  Возвращает 0, если файл не читается или это не PE. }
function ImageMachine(const FileName: String): Integer;
var
  Stream: TFileStream;
  Header: AnsiString;
  PeOffset: Integer;
begin
  Result := 0;
  try
    Stream := TFileStream.Create(FileName, fmOpenRead or fmShareDenyNone);
    try
      SetLength(Header, 64);
      Stream.ReadBuffer(Header, 64);
      if Copy(Header, 1, 2) = 'MZ' then
      begin
        { Смещение заголовка PE — 4 байта со смещения $3C. }
        PeOffset := Ord(Header[61]) or (Ord(Header[62]) shl 8) or
          (Ord(Header[63]) shl 16) or (Ord(Header[64]) shl 24);
        Stream.Position := PeOffset;
        SetLength(Header, 6);
        Stream.ReadBuffer(Header, 6);
        if Copy(Header, 1, 4) = 'PE'#0#0 then
          Result := Ord(Header[5]) or (Ord(Header[6]) shl 8);
      end;
    finally
      Stream.Free;
    end;
  except
    Result := 0;
  end;
end;

{ Возвращает старшую цифру версии файла FileName из его сведений о версии или
  -1, если сведений нет. }
function FileMajorVersion(const FileName: String): Integer;
var
  VersionMS, VersionLS: Cardinal;
begin
  if GetVersionNumbers(FileName, VersionMS, VersionLS) then
    Result := VersionMS shr 16
  else
    Result := -1;
end;

{ Возвращает причину, по которой REAPER из папки Dir не загрузит модуль, —
  REAPER 32-битный или версии ниже 7, — или пустую строку, если загрузит.
  Машину и версию, которые не читаются из reaper.exe, считает подходящими. }
function ReaperProblem(const Dir: String): String;
var
  Exe: String;
  Machine, Major: Integer;
begin
  Result := '';
  Exe := AddBackslash(Dir) + 'reaper.exe';
  Machine := ImageMachine(Exe);
  Major := FileMajorVersion(Exe);
  if (Machine <> 0) and (Machine <> MachineAmd64) and (Machine <> MachineArm64) then
    Result := FmtMessage(CustomMessage('ReaperNot64Bit'), [Dir])
  else if (Major >= 0) and (Major < 7) then
    Result := FmtMessage(CustomMessage('ReaperTooOld'), [Dir, IntToStr(Major)]);
end;

{ Добавляет в Dirs папку программы REAPER Path, если в ней есть reaper.exe и
  её ещё нет в списке. Path может быть и путём к самому reaper.exe, в
  кавычках и с косой чертой в конце. }
procedure AddReaperDir(Dirs: TStringList; Path: String);
begin
  Path := RemoveBackslashUnlessRoot(RemoveQuotes(Trim(Path)));
  if CompareText(ExtractFileName(Path), 'reaper.exe') = 0 then
    Path := ExtractFileDir(Path);
  if (Path <> '') and FileExists(AddBackslash(Path) + 'reaper.exe') and
    (Dirs.IndexOf(Path) < 0) then
    Dirs.Add(Path);
end;

{ Добавляет в Dirs папку программы REAPER из значения по умолчанию ключа
  SOFTWARE\REAPER корня RootKey, если такое значение есть. }
procedure AddRegisteredReaperDir(Dirs: TStringList; RootKey: Integer);
var
  Path: String;
begin
  if RegQueryStringValue(RootKey, 'SOFTWARE\REAPER', '', Path) then
    AddReaperDir(Dirs, Path);
end;

{ Ищет REAPER, который загрузит модуль, и возвращает его папку ресурсов: папку
  программы, если рядом с reaper.exe лежит reaper.ini, иначе
  %APPDATA%\REAPER. Если программы REAPER не нашлось, но в %APPDATA%\REAPER
  есть reaper.ini, возвращает эту папку.

  Возвращает пустую строку, если подходящего REAPER нет. Problem тогда —
  причина, по которой найденный REAPER не подходит, или пустая строка, если
  REAPER не найден вовсе. }
function DetectResourceDir(var Problem: String): String;
var
  Dirs: TStringList;
  I: Integer;
  Reason, StandardDir: String;
begin
  Result := '';
  Problem := '';
  StandardDir := ExpandConstant('{userappdata}\REAPER');
  Dirs := TStringList.Create;
  try
    { Установщик REAPER пишет папку программы в SOFTWARE\REAPER; в какое
      представление реестра — зависит от разрядности, поэтому читаются все. }
    AddRegisteredReaperDir(Dirs, HKLM64);
    AddRegisteredReaperDir(Dirs, HKCU64);
    AddRegisteredReaperDir(Dirs, HKLM32);
    AddRegisteredReaperDir(Dirs, HKCU32);
    AddReaperDir(Dirs, ExpandConstant('{commonpf64}\REAPER (x64)'));
    AddReaperDir(Dirs, ExpandConstant('{commonpf64}\REAPER'));
    AddReaperDir(Dirs, ExpandConstant('{commonpf32}\REAPER'));

    for I := 0 to Dirs.Count - 1 do
      if Result = '' then
      begin
        Reason := ReaperProblem(Dirs[I]);
        if Reason <> '' then
        begin
          if Problem = '' then
            Problem := Reason;
        end
        else if FileExists(AddBackslash(Dirs[I]) + 'reaper.ini') then
          Result := Dirs[I]
        else
          Result := StandardDir;
      end;
  finally
    Dirs.Free;
  end;

  if Result <> '' then
    Problem := ''
  else if (Problem = '') and FileExists(AddBackslash(StandardDir) + 'reaper.ini') then
    Result := StandardDir;
end;

{ Возвращает папку, которую мастер предлагает для установки: папку ресурсов
  найденного REAPER или %APPDATA%\REAPER, если REAPER не найден. Param не
  используется. }
function DefaultResourceDir(Param: String): String;
begin
  if DetectedDir <> '' then
    Result := DetectedDir
  else
    Result := ExpandConstant('{userappdata}\REAPER');
end;

{ Возвращает причину, по которой в папку Dir ставить нельзя, или пустую
  строку. Ставить можно в папку ресурсов найденного REAPER — даже если REAPER
  ещё не запускался и её нет — и в любую папку с reaper.ini, где reaper.exe,
  если он есть, загрузит модуль. }
function ResourceDirProblem(Dir: String): String;
begin
  Result := '';
  Dir := RemoveBackslashUnlessRoot(Dir);
  if (DetectedDir = '') or (CompareText(Dir, DetectedDir) <> 0) then
  begin
    if not FileExists(AddBackslash(Dir) + 'reaper.ini') then
      Result := FmtMessage(CustomMessage('NotResourceDir'), [Dir])
    else if FileExists(AddBackslash(Dir) + 'reaper.exe') then
      Result := ReaperProblem(Dir);
  end;
end;

{ Проверяет, запущен ли REAPER у текущего пользователя, — по главному окну. }
function ReaperRunning(): Boolean;
begin
  Result := FindWindowByClassName(ReaperWindowClass) <> 0;
end;

{ Просит закрыть REAPER, пока он запущен. Возвращает True, когда REAPER
  закрыт, и False, если пользователь отказался. В тихом режиме с
  /SUPPRESSMSGBOXES ответ — отказ. }
function WaitForReaperClosed(): Boolean;
begin
  Result := True;
  while Result and ReaperRunning() do
    Result := SuppressibleMsgBox(CustomMessage('CloseReaper'), mbError,
      MB_RETRYCANCEL, IDCANCEL) = IDRETRY;
end;

function InitializeSetup(): Boolean;
var
  Problem: String;
begin
  DetectedDir := DetectResourceDir(Problem);
  if DetectedDir <> '' then
    Result := True
  { Папку задали ключом /DIR — её проверит PrepareToInstall. }
  else if ExpandConstant('{param:DIR}') <> '' then
    Result := True
  else
  begin
    if Problem = '' then
      Problem := CustomMessage('ReaperNotFound');
    Result := SuppressibleMsgBox(Problem + #13#10#13#10 + CustomMessage('AskPortable'),
      mbConfirmation, MB_YESNO, IDNO) = IDYES;
  end;
end;

procedure InitializeWizard();
begin
  WizardForm.SelectDirLabel.Caption := CustomMessage('SelectPortableDir');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpSelectDir) and (DetectedDir <> '');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Problem := ResourceDirProblem(WizardDirValue);
    if Problem <> '' then
    begin
      SuppressibleMsgBox(Problem, mbError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := ResourceDirProblem(WizardDirValue);
  if (Result = '') and not WaitForReaperClosed() then
    Result := CustomMessage('ReaperStillRunning');
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Plugins: String;
begin
  { Строка, которая начинается с «[», в [Code] читается как заголовок
    раздела, поэтому массив аргументов не переносится на новую строку. }
  if CurPageID = wpFinished then
  begin
    Plugins := AddBackslash(WizardDirValue) + 'UserPlugins';
    WizardForm.FinishedLabel.Caption := FmtMessage(CustomMessage('Finished'), [Plugins]);
    WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForReaperClosed();
end;
