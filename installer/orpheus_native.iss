; Inno Setup script for the Orpheus Native SAPI5 voices.
;
; Installs the Dolphin Orpheus 2.10 engine and a SAPI 5 interface for it, in
; both 32-bit and 64-bit form. The wizard lets you choose which of the 25
; languages are installed, and for each one whether its second voice comes too.
;
; The choice is recorded in {app}\voices.ini, which the SAPI interfaces and the
; configuration utility read, so the Windows voice list only ever offers voices
; whose data files are actually present.
;
; The wizard uses only standard pages, which are screen-reader accessible, and
; SetupLogging writes a detailed log that is copied into the application folder
; at the end of the install.
;
; ---------------------------------------------------------------------------
; A note on getting back out again.
;
; SAPI 5's voice list is one shared, machine-wide registry key. An uninstall
; that leaves a broken entry there does not merely leave a mess of its own: it
; hands every other speech engine on the machine a voice that cannot be
; created, and clients that remember their voice by token path (NVDA does) can
; then fail to start SAPI5 at all. So the registration is removed three
; independent ways, and none can take the others down with it:
;
;   1. DllUnregisterServer, through the regserver flags below.
;   2. [Registry] entries flagged "uninsdeletekey dontcreatekey", so Setup
;      records the deletion without ever creating the keys itself - these work
;      even if regsvr32 never ran.
;   3. A CurUninstallStepChanged sweep that deletes the keys directly in both
;      the 32-bit and 64-bit registry views.
;
; No parent key is ever touched: only our own TokenEnums subkey and our own
; two CLSIDs.

#define MyAppName "Orpheus Native SAPI5"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "Orpheus Native SAPI5 Project"
; The CLSIDs are written out literally below rather than through macros: a
; brace starts a constant in [Registry] and must be doubled there, while a
; Pascal string in [Code] takes a single brace, so one macro cannot serve both.
;   engine CLSID     {81dfbb60-64b5-49e6-a900-8abd937d5463}
;   enumerator CLSID {e7077968-c442-45ac-bf26-9c5b3648be5a}
#ifndef SourceDir
  #define SourceDir "..\output"
#endif

[Setup]
AppId={{2740a33f-2ce6-430a-8dd4-95eacc15ff56}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\OrpheusNativeSAPI
DefaultGroupName=Orpheus Native
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=OrpheusNativeSAPI_Setup_{#MyAppVersion}
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
UninstallDisplayIcon={app}\OrpheusNativeConfig.exe
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full - all 25 languages and all 48 voices"
Name: "english"; Description: "English only - US and UK English, four voices"
Name: "custom"; Description: "Custom - choose languages and voices"; Flags: iscustom

[Components]
Name: "core"; Description: "Program files and the Orpheus engine (required)"; Types: full english custom; Flags: fixed
Name: "core\x64"; Description: "64-bit SAPI5 interface"; Types: full english custom; Check: Is64BitInstallMode
Name: "lang"; Description: "Languages and voices"; Types: full english custom; Flags: fixed
Name: "lang\c00001"; Description: "US English (Synthetic Dave)"; Types: full english
Name: "lang\c00001\v2"; Description: "Second voice: Synthetic Andy"; Types: full english
Name: "lang\c00030"; Description: "Greek (Synthetic Dave)"; Types: full
Name: "lang\c00030\v2"; Description: "Second voice: Synthetic Andy"; Types: full
Name: "lang\c00031"; Description: "Dutch (Jan)"; Types: full
Name: "lang\c00031\v2"; Description: "Second voice: Hendrick"; Types: full
Name: "lang\c00033"; Description: "French (Jean)"; Types: full
Name: "lang\c00033\v2"; Description: "Second voice: Pierre"; Types: full
Name: "lang\c00034"; Description: "Castilian Spanish (David)"; Types: full
Name: "lang\c00034\v2"; Description: "Second voice: Andrés"; Types: full
Name: "lang\c00036"; Description: "Hungarian (Istvan)"; Types: full
Name: "lang\c00036\v2"; Description: "Second voice: Marcus"; Types: full
Name: "lang\c00038"; Description: "Croatian (Stjepan)"; Types: full
Name: "lang\c00038\v2"; Description: "Second voice: Marija"; Types: full
Name: "lang\c00039"; Description: "Italian (Davide)"; Types: full
Name: "lang\c00039\v2"; Description: "Second voice: Andrea"; Types: full
Name: "lang\c00040"; Description: "Romanian (David)"; Types: full
Name: "lang\c00040\v2"; Description: "Second voice: Andrei"; Types: full
Name: "lang\c00042"; Description: "Czech (Honza)"; Types: full
Name: "lang\c00042\v2"; Description: "Second voice: Katka"; Types: full
Name: "lang\c00044"; Description: "UK English (Synthetic Dave)"; Types: full english
Name: "lang\c00044\v2"; Description: "Second voice: Synthetic Andy"; Types: full english
Name: "lang\c00045"; Description: "Danish (Thomas)"; Types: full
Name: "lang\c00045\v2"; Description: "Second voice: Lasse"; Types: full
Name: "lang\c00046"; Description: "Swedish (Tomas)"; Types: full
Name: "lang\c00046\v2"; Description: "Second voice: Lasse"; Types: full
Name: "lang\c00047"; Description: "Norwegian (Knut)"; Types: full
Name: "lang\c00047\v2"; Description: "Second voice: Andreas"; Types: full
Name: "lang\c00048"; Description: "Polish (Synthetic Dave)"; Types: full
Name: "lang\c00048\v2"; Description: "Second voice: Synthetic Andy"; Types: full
Name: "lang\c00049"; Description: "German (Klaus)"; Types: full
Name: "lang\c00049\v2"; Description: "Second voice: Andreas"; Types: full
Name: "lang\c00052"; Description: "Latin American Spanish (David)"; Types: full
Name: "lang\c00052\v2"; Description: "Second voice: Andrés"; Types: full
Name: "lang\c00055"; Description: "Brazilian Portuguese (João)"; Types: full
Name: "lang\c00055\v2"; Description: "Second voice: Isabel"; Types: full
Name: "lang\c00060"; Description: "Malay (David)"; Types: full
Name: "lang\c00060\v2"; Description: "Second voice: Anne"; Types: full
Name: "lang\c00086"; Description: "Chinese Putonghua (Dave)"; Types: full
Name: "lang\c00351"; Description: "Portuguese (João)"; Types: full
Name: "lang\c00351\v2"; Description: "Second voice: Isabel"; Types: full
Name: "lang\c00358"; Description: "Finnish (Dave)"; Types: full
Name: "lang\c00358\v2"; Description: "Second voice: Andy"; Types: full
Name: "lang\c00370"; Description: "Lithuanian (Jonas)"; Types: full
Name: "lang\c00370\v2"; Description: "Second voice: Petras"; Types: full
Name: "lang\c10044"; Description: "Welsh (David)"; Types: full
Name: "lang\c10044\v2"; Description: "Second voice: Megan"; Types: full
Name: "lang\c10086"; Description: "Cantonese (John)"; Types: full

[Files]
; --- program files and the engine itself ---
Source: "{#SourceDir}\OrpheusNativeSAPI.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 32bit; Components: core
Source: "{#SourceDir}\x64\OrpheusNativeSAPI.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode; Components: core\x64
Source: "{#SourceDir}\orpheus-native-host.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "{#SourceDir}\OrpheusNativeConfig.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: core

; Engine binaries and shared data. The Language\ folders are selected
; separately below; everything here is needed whatever is chosen.
Source: "{#SourceDir}\orpheus\*"; DestDir: "{app}\orpheus"; Excludes: "orpheus.sys,Language\*"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core

; The engine opens orpheus.sys for reading AND writing every time it starts -
; it stamps its own directory into the file - and refuses to initialise if it
; cannot. Under Program Files that write fails for a standard user, so every
; voice returns E_FAIL. This one 804-byte file therefore gets modify rights
; for Users; nothing else in the tree does, so the engine DLLs stay
; unwritable. The engine overwrites the contents at every startup with the
; path Setup installed to, so nothing an unprivileged writer puts there
; survives to be acted on.
Source: "{#SourceDir}\orpheus\orpheus.sys"; DestDir: "{app}\orpheus"; Flags: ignoreversion; Permissions: users-modify; Components: core

Source: "{#SourceDir}\orpheus\Language\00001\*"; DestDir: "{app}\orpheus\Language\00001"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00001
Source: "{#SourceDir}\orpheus\Language\00001\synth2.vcx"; DestDir: "{app}\orpheus\Language\00001"; Flags: ignoreversion; Components: lang\c00001\v2
Source: "{#SourceDir}\orpheus\Language\00030\*"; DestDir: "{app}\orpheus\Language\00030"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00030
Source: "{#SourceDir}\orpheus\Language\00030\synth2.vcx"; DestDir: "{app}\orpheus\Language\00030"; Flags: ignoreversion; Components: lang\c00030\v2
Source: "{#SourceDir}\orpheus\Language\00031\*"; DestDir: "{app}\orpheus\Language\00031"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00031
Source: "{#SourceDir}\orpheus\Language\00031\synth2.vcx"; DestDir: "{app}\orpheus\Language\00031"; Flags: ignoreversion; Components: lang\c00031\v2
Source: "{#SourceDir}\orpheus\Language\00033\*"; DestDir: "{app}\orpheus\Language\00033"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00033
Source: "{#SourceDir}\orpheus\Language\00033\synth2.vcx"; DestDir: "{app}\orpheus\Language\00033"; Flags: ignoreversion; Components: lang\c00033\v2
Source: "{#SourceDir}\orpheus\Language\00034\*"; DestDir: "{app}\orpheus\Language\00034"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00034
Source: "{#SourceDir}\orpheus\Language\00034\synth2.vcx"; DestDir: "{app}\orpheus\Language\00034"; Flags: ignoreversion; Components: lang\c00034\v2
Source: "{#SourceDir}\orpheus\Language\00036\*"; DestDir: "{app}\orpheus\Language\00036"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00036
Source: "{#SourceDir}\orpheus\Language\00036\synth2.vcx"; DestDir: "{app}\orpheus\Language\00036"; Flags: ignoreversion; Components: lang\c00036\v2
Source: "{#SourceDir}\orpheus\Language\00038\*"; DestDir: "{app}\orpheus\Language\00038"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00038
Source: "{#SourceDir}\orpheus\Language\00038\synth2.vcx"; DestDir: "{app}\orpheus\Language\00038"; Flags: ignoreversion; Components: lang\c00038\v2
Source: "{#SourceDir}\orpheus\Language\00039\*"; DestDir: "{app}\orpheus\Language\00039"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00039
Source: "{#SourceDir}\orpheus\Language\00039\synth2.vcx"; DestDir: "{app}\orpheus\Language\00039"; Flags: ignoreversion; Components: lang\c00039\v2
Source: "{#SourceDir}\orpheus\Language\00040\*"; DestDir: "{app}\orpheus\Language\00040"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00040
Source: "{#SourceDir}\orpheus\Language\00040\synth2.vcx"; DestDir: "{app}\orpheus\Language\00040"; Flags: ignoreversion; Components: lang\c00040\v2
Source: "{#SourceDir}\orpheus\Language\00042\*"; DestDir: "{app}\orpheus\Language\00042"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00042
Source: "{#SourceDir}\orpheus\Language\00042\synth2.vcx"; DestDir: "{app}\orpheus\Language\00042"; Flags: ignoreversion; Components: lang\c00042\v2
Source: "{#SourceDir}\orpheus\Language\00044\*"; DestDir: "{app}\orpheus\Language\00044"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00044
Source: "{#SourceDir}\orpheus\Language\00044\synth2.vcx"; DestDir: "{app}\orpheus\Language\00044"; Flags: ignoreversion; Components: lang\c00044\v2
Source: "{#SourceDir}\orpheus\Language\00045\*"; DestDir: "{app}\orpheus\Language\00045"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00045
Source: "{#SourceDir}\orpheus\Language\00045\synth2.vcx"; DestDir: "{app}\orpheus\Language\00045"; Flags: ignoreversion; Components: lang\c00045\v2
Source: "{#SourceDir}\orpheus\Language\00046\*"; DestDir: "{app}\orpheus\Language\00046"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00046
Source: "{#SourceDir}\orpheus\Language\00046\synth2.vcx"; DestDir: "{app}\orpheus\Language\00046"; Flags: ignoreversion; Components: lang\c00046\v2
Source: "{#SourceDir}\orpheus\Language\00047\*"; DestDir: "{app}\orpheus\Language\00047"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00047
Source: "{#SourceDir}\orpheus\Language\00047\synth2.vcx"; DestDir: "{app}\orpheus\Language\00047"; Flags: ignoreversion; Components: lang\c00047\v2
Source: "{#SourceDir}\orpheus\Language\00048\*"; DestDir: "{app}\orpheus\Language\00048"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00048
Source: "{#SourceDir}\orpheus\Language\00048\synth2.vcx"; DestDir: "{app}\orpheus\Language\00048"; Flags: ignoreversion; Components: lang\c00048\v2
Source: "{#SourceDir}\orpheus\Language\00049\*"; DestDir: "{app}\orpheus\Language\00049"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00049
Source: "{#SourceDir}\orpheus\Language\00049\synth2.vcx"; DestDir: "{app}\orpheus\Language\00049"; Flags: ignoreversion; Components: lang\c00049\v2
Source: "{#SourceDir}\orpheus\Language\00052\*"; DestDir: "{app}\orpheus\Language\00052"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00052
Source: "{#SourceDir}\orpheus\Language\00052\synth2.vcx"; DestDir: "{app}\orpheus\Language\00052"; Flags: ignoreversion; Components: lang\c00052\v2
Source: "{#SourceDir}\orpheus\Language\00055\*"; DestDir: "{app}\orpheus\Language\00055"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00055
Source: "{#SourceDir}\orpheus\Language\00055\synth2.vcx"; DestDir: "{app}\orpheus\Language\00055"; Flags: ignoreversion; Components: lang\c00055\v2
Source: "{#SourceDir}\orpheus\Language\00060\*"; DestDir: "{app}\orpheus\Language\00060"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00060
Source: "{#SourceDir}\orpheus\Language\00060\synth2.vcx"; DestDir: "{app}\orpheus\Language\00060"; Flags: ignoreversion; Components: lang\c00060\v2
Source: "{#SourceDir}\orpheus\Language\00086\*"; DestDir: "{app}\orpheus\Language\00086"; Flags: ignoreversion; Components: lang\c00086
Source: "{#SourceDir}\orpheus\Language\00351\*"; DestDir: "{app}\orpheus\Language\00351"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00351
Source: "{#SourceDir}\orpheus\Language\00351\synth2.vcx"; DestDir: "{app}\orpheus\Language\00351"; Flags: ignoreversion; Components: lang\c00351\v2
Source: "{#SourceDir}\orpheus\Language\00358\*"; DestDir: "{app}\orpheus\Language\00358"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00358
Source: "{#SourceDir}\orpheus\Language\00358\synth2.vcx"; DestDir: "{app}\orpheus\Language\00358"; Flags: ignoreversion; Components: lang\c00358\v2
Source: "{#SourceDir}\orpheus\Language\00370\*"; DestDir: "{app}\orpheus\Language\00370"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c00370
Source: "{#SourceDir}\orpheus\Language\00370\synth2.vcx"; DestDir: "{app}\orpheus\Language\00370"; Flags: ignoreversion; Components: lang\c00370\v2
Source: "{#SourceDir}\orpheus\Language\10044\*"; DestDir: "{app}\orpheus\Language\10044"; Excludes: "synth2.vcx"; Flags: ignoreversion; Components: lang\c10044
Source: "{#SourceDir}\orpheus\Language\10044\synth2.vcx"; DestDir: "{app}\orpheus\Language\10044"; Flags: ignoreversion; Components: lang\c10044\v2
Source: "{#SourceDir}\orpheus\Language\10086\*"; DestDir: "{app}\orpheus\Language\10086"; Flags: ignoreversion; Components: lang\c10086

[Icons]
Name: "{group}\Orpheus Native Configuration"; Filename: "{app}\OrpheusNativeConfig.exe"
Name: "{group}\Uninstall Orpheus Native SAPI5"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Orpheus Native Configuration"; Filename: "{app}\OrpheusNativeConfig.exe"

; Mechanism 2: recorded for deletion, never created here. dontcreatekey means
; Setup writes nothing at install time but still removes these on uninstall,
; so the cleanup does not depend on regsvr32 having succeeded.
[Registry]
Root: HKLM32; Subkey: "SOFTWARE\Microsoft\Speech\Voices\TokenEnums\OrpheusNative"; Flags: uninsdeletekey dontcreatekey
Root: HKLM32; Subkey: "SOFTWARE\Classes\CLSID\{{81dfbb60-64b5-49e6-a900-8abd937d5463}"; Flags: uninsdeletekey dontcreatekey
Root: HKLM32; Subkey: "SOFTWARE\Classes\CLSID\{{e7077968-c442-45ac-bf26-9c5b3648be5a}"; Flags: uninsdeletekey dontcreatekey
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Speech\Voices\TokenEnums\OrpheusNative"; Flags: uninsdeletekey dontcreatekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{{81dfbb60-64b5-49e6-a900-8abd937d5463}"; Flags: uninsdeletekey dontcreatekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{{e7077968-c442-45ac-bf26-9c5b3648be5a}"; Flags: uninsdeletekey dontcreatekey; Check: Is64BitInstallMode

[Run]
Filename: "{app}\OrpheusNativeConfig.exe"; Description: "Open the Orpheus Native configuration utility"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
; The setup log, the manifest Setup writes, and the file the engine rewrites
; with its own location.
;
; %APPDATA%\OrpheusNativeSAPI - the per-voice settings and the debug logs - is
; deliberately NOT listed here. Setup runs elevated, so {userappdata} resolves
; to the administrator's profile rather than the profile that actually holds
; those settings: the line would miss the real files and delete somebody
; else's. Per-user settings surviving an uninstall is normal; deleting the
; wrong user's data is not. The README says where they are.
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\voices.ini"
Type: files; Name: "{app}\orpheus\orpheus.sys"
Type: filesandordirs; Name: "{app}\orpheus\Language"
Type: dirifempty; Name: "{app}\orpheus"
Type: dirifempty; Name: "{app}"

[Code]
const
  LangCount = 25;

var
  LangCountry: array[0..LangCount - 1] of String;
  LangVoices: array[0..LangCount - 1] of Integer;

procedure InitLanguageTable;
begin
  LangCountry[0] := '00001'; LangVoices[0] := 2;  // US English
  LangCountry[1] := '00030'; LangVoices[1] := 2;  // Greek
  LangCountry[2] := '00031'; LangVoices[2] := 2;  // Dutch
  LangCountry[3] := '00033'; LangVoices[3] := 2;  // French
  LangCountry[4] := '00034'; LangVoices[4] := 2;  // Castilian Spanish
  LangCountry[5] := '00036'; LangVoices[5] := 2;  // Hungarian
  LangCountry[6] := '00038'; LangVoices[6] := 2;  // Croatian
  LangCountry[7] := '00039'; LangVoices[7] := 2;  // Italian
  LangCountry[8] := '00040'; LangVoices[8] := 2;  // Romanian
  LangCountry[9] := '00042'; LangVoices[9] := 2;  // Czech
  LangCountry[10] := '00044'; LangVoices[10] := 2;  // UK English
  LangCountry[11] := '00045'; LangVoices[11] := 2;  // Danish
  LangCountry[12] := '00046'; LangVoices[12] := 2;  // Swedish
  LangCountry[13] := '00047'; LangVoices[13] := 2;  // Norwegian
  LangCountry[14] := '00048'; LangVoices[14] := 2;  // Polish
  LangCountry[15] := '00049'; LangVoices[15] := 2;  // German
  LangCountry[16] := '00052'; LangVoices[16] := 2;  // Latin American Spanish
  LangCountry[17] := '00055'; LangVoices[17] := 2;  // Brazilian Portuguese
  LangCountry[18] := '00060'; LangVoices[18] := 2;  // Malay
  LangCountry[19] := '00086'; LangVoices[19] := 1;  // Chinese Putonghua
  LangCountry[20] := '00351'; LangVoices[20] := 2;  // Portuguese
  LangCountry[21] := '00358'; LangVoices[21] := 2;  // Finnish
  LangCountry[22] := '00370'; LangVoices[22] := 2;  // Lithuanian
  LangCountry[23] := '10044'; LangVoices[23] := 2;  // Welsh
  LangCountry[24] := '10086'; LangVoices[24] := 1;  // Cantonese
end;

function InitializeSetup(): Boolean;
begin
  InitLanguageTable;
  Result := True;
end;

function IsLangSelected(Index: Integer): Boolean;
begin
  Result := WizardIsComponentSelected('lang\c' + LangCountry[Index]);
end;

// How many voice slots this language will have on disk: 1 unless it has a
// second voice and that second voice was selected too.
function SelectedSlots(Index: Integer): Integer;
begin
  Result := 1;
  if LangVoices[Index] > 1 then
    if WizardIsComponentSelected('lang\c' + LangCountry[Index] + '\v2') then
      Result := 2;
end;

function SelectedLanguageCount(): Integer;
var
  i: Integer;
begin
  Result := 0;
  for i := 0 to LangCount - 1 do
    if IsLangSelected(i) then
      Result := Result + 1;
end;

// Nothing usable would be installed with no language selected, so ask for a
// correction rather than producing a silent, voiceless installation.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    if SelectedLanguageCount = 0 then
    begin
      MsgBox('Please select at least one language, otherwise there will be no voices to speak with.',
             mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// Record the choice for the SAPI interfaces and the configuration utility.
// A language that is absent from this file was not installed.
procedure WriteVoiceManifest;
var
  Path: String;
  i: Integer;
begin
  Path := ExpandConstant('{app}\voices.ini');
  DeleteFile(Path);
  for i := 0 to LangCount - 1 do
    if IsLangSelected(i) then
      SetIniInt('languages', LangCountry[i], SelectedSlots(i), Path);
end;

procedure StopEngineHosts;
var
  ResultCode: Integer;
begin
  // The host is stateless and SAPI clients respawn it on demand, so stopping
  // it is always safe. Without this, an upgrade cannot replace the engine
  // files while a screen reader is running.
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im orpheus-native-host.exe', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  StopEngineHosts;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LogDir: String;
begin
  if CurStep = ssPostInstall then
    WriteVoiceManifest;

  // Preserve the setup log for debugging: copy it into the application folder.
  if CurStep = ssDone then
  begin
    LogDir := ExpandConstant('{app}\logs');
    if not DirExists(LogDir) then
      CreateDir(LogDir);
    CopyFile(ExpandConstant('{log}'), LogDir + '\install.log', False);
  end;
end;

// Mechanism 3: remove our own registration directly, in both registry views.
// RegDeleteKeyIncludingSubkeys is used rather than the plain variant because
// a CLSID key always has an InProcServer32 child and the plain call refuses
// any key that still has subkeys - a mistake that has shipped before.
procedure RemoveOurRegistration(RootKey: Integer);
var
  DefaultToken: String;
begin
  RegDeleteKeyIncludingSubkeys(RootKey,
    'SOFTWARE\Microsoft\Speech\Voices\TokenEnums\OrpheusNative');
  RegDeleteKeyIncludingSubkeys(RootKey,
    'SOFTWARE\Classes\CLSID\{81dfbb60-64b5-49e6-a900-8abd937d5463}');
  RegDeleteKeyIncludingSubkeys(RootKey,
    'SOFTWARE\Classes\CLSID\{e7077968-c442-45ac-bf26-9c5b3648be5a}');

  // A default-voice pointer aimed at one of our tokens would outlive us and
  // point SAPI at a voice that no longer exists.
  if RegQueryStringValue(RootKey, 'SOFTWARE\Microsoft\Speech\Voices',
                         'DefaultTokenId', DefaultToken) then
  begin
    if Pos('tokenenums\orpheusnative', Lowercase(DefaultToken)) > 0 then
      RegDeleteValue(RootKey, 'SOFTWARE\Microsoft\Speech\Voices', 'DefaultTokenId');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopEngineHosts;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOurRegistration(HKLM32);
    if IsWin64 then
      RemoveOurRegistration(HKLM64);
    RemoveOurRegistration(HKCU32);
    if IsWin64 then
      RemoveOurRegistration(HKCU64);
  end;
end;
