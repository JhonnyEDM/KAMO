; KAMO installer (Inno Setup 6). Built by tools/kamo_package.ps1:
;   ISCC /DAppVersion=0.3.0 /DSourceDir=<package folder> /DOutputDir=<dir> kamo-setup.iss
; Installs for the current user (no administrator rights), so KAMO can update itself. The
; player picks their ROM once, on the installer's ROM page; it is checked against the
; supported dump and remembered in rom.cfg. Updates run this installer silently.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "KAMO"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#define RomSha1 "274b102b6d940f46861a92b4e65f89a51815c12c"

[Setup]
AppId={{6E3C2B7A-4F0D-4C1B-9A57-6B1E9C3D2A10}
AppName=KAMO
AppVersion={#AppVersion}
AppVerName=KAMO {#AppVersion}
AppPublisher=KAMO
AppPublisherURL=https://github.com/JhonnyEDM/KAMO
AppSupportURL=https://github.com/JhonnyEDM/KAMO/issues
AppUpdatesURL=https://github.com/JhonnyEDM/KAMO/releases
DefaultDirName={localappdata}\Programs\KAMO
DefaultGroupName=KAMO
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=KAMO-Setup-{#AppVersion}
SetupIconFile=..\..\assets\icon\kamo.ico
UninstallDisplayIcon={app}\KAMO.exe
UninstallDisplayName=KAMO
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.RomTitle=Tu copia del juego
es.RomSubtitle=KAMO no incluye el juego. Elige tu ROM de Kirby & The Amazing Mirror (USA).
es.RomText=Elige el archivo .gba de tu propia copia de Kirby & The Amazing Mirror (USA). Solo se hace una vez: KAMO lo recordará.
es.RomLabel=Archivo de la ROM (.gba):
es.RomMissing=Elige el archivo .gba de tu ROM para continuar.
es.RomWrong=Ese archivo no es Kirby & The Amazing Mirror (USA). KAMO solo funciona con esa versión.
es.PlayNow=Jugar ahora
es.DesktopIcon=Crear un acceso directo en el escritorio
en.RomTitle=Your copy of the game
en.RomSubtitle=KAMO does not include the game. Choose your Kirby & The Amazing Mirror (USA) ROM.
en.RomText=Choose the .gba file of your own copy of Kirby & The Amazing Mirror (USA). You only do this once: KAMO will remember it.
en.RomLabel=ROM file (.gba):
en.RomMissing=Choose your ROM's .gba file to continue.
en.RomWrong=That file is not Kirby & The Amazing Mirror (USA). KAMO only works with that version.
en.PlayNow=Play now
en.DesktopIcon=Create a desktop shortcut

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\KAMO"; Filename: "{app}\KAMO.exe"
Name: "{autodesktop}\KAMO"; Filename: "{app}\KAMO.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\KAMO.exe"; Description: "{cm:PlayNow}"; Flags: nowait postinstall skipifsilent
; After a silent update, open KAMO again.
Filename: "{app}\KAMO.exe"; Flags: nowait; Check: WizardSilent

[UninstallDelete]
Type: files; Name: "{app}\rom.cfg"
Type: files; Name: "{app}\kamo.ini"
Type: dirifempty; Name: "{app}"

[Code]
var
  RomPage: TInputFileWizardPage;

function RomIsValid(const Path: String): Boolean;
begin
  Result := (Path <> '') and FileExists(Path) and
            (CompareText(GetSHA1OfFile(Path), '{#RomSha1}') = 0);
end;

function SavedRom(): String;
var
  Lines: TArrayOfString;
begin
  Result := '';
  if LoadStringsFromFile(ExpandConstant('{app}\rom.cfg'), Lines) and (GetArrayLength(Lines) > 0) then
    Result := Trim(Lines[0]);
end;

procedure InitializeWizard();
begin
  RomPage := CreateInputFilePage(wpSelectTasks, CustomMessage('RomTitle'), CustomMessage('RomSubtitle'),
                                 CustomMessage('RomText'));
  RomPage.Add(CustomMessage('RomLabel'), 'GBA ROM (*.gba)|*.gba|*.*|*.*', '.gba');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // An update keeps the ROM chosen before.
  Result := (PageID = RomPage.ID) and RomIsValid(SavedRom());
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = RomPage.ID) and not WizardSilent() then begin
    if RomPage.Values[0] = '' then begin
      MsgBox(CustomMessage('RomMissing'), mbError, MB_OK);
      Result := False;
    end else if not RomIsValid(RomPage.Values[0]) then begin
      MsgBox(CustomMessage('RomWrong'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Lines: TArrayOfString;
begin
  if (CurStep = ssPostInstall) and RomIsValid(RomPage.Values[0]) then begin
    SetArrayLength(Lines, 1);
    Lines[0] := RomPage.Values[0];
    SaveStringsToUTF8File(ExpandConstant('{app}\rom.cfg'), Lines, False);
  end;
end;
