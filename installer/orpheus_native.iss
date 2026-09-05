; Inno Setup script for the Orpheus Native SAPI5 voices.
;
; Installs the complete Dolphin Orpheus 2.10 engine - all 25 languages, all 48
; voices and every data file the synthesiser needs - the 32-bit and 64-bit
; SAPI5 interfaces, the engine host and the configuration utility.
;
; The wizard uses only standard pages, which are screen-reader accessible, and
; SetupLogging writes a detailed log that is copied into the application
; folder at the end of the install.
;
; ---------------------------------------------------------------------------
; A note on getting back out again.
;
; SAPI 5's voice list is one shared, machine-wide registry key. An uninstall
; that leaves a broken entry there does not just leave a mess of its own: it
; hands every other speech engine on the machine a voice that cannot be
; created, and clients that remember their voice by token path (NVDA does) can
; then fail to start SAPI5 at all. So the registration is removed three
; independent ways, and none of them can take the others down with it:
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
#define MyAppVersion "1.0.0"
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
DefaultDirName={autopf}\OrpheusNativeSAPI
DefaultGroupName=Orpheus Native
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=OrpheusNativeSAPI_Setup
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
UninstallDisplayIcon={app}\OrpheusNativeConfig.exe
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\OrpheusNativeSAPI.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 32bit
Source: "{#SourceDir}\x64\OrpheusNativeSAPI.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode
Source: "{#SourceDir}\orpheus-native-host.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\OrpheusNativeConfig.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\orpheus\*"; DestDir: "{app}\orpheus"; Excludes: "orpheus.sys"; Flags: ignoreversion recursesubdirs createallsubdirs
; The engine opens orpheus.sys for reading AND writing every time it starts -
; it stamps its own directory into the file - and refuses to initialise if it
; cannot. Under Program Files that write fails for a standard user, so every
; voice returns E_FAIL. This one 804-byte file therefore gets modify rights
; for Users; nothing else in the tree does, so the engine DLLs stay
; unwritable. The engine overwrites the contents at every startup with the
; path Setup installed to, so nothing an unprivileged writer puts there
; survives to be acted on.
Source: "{#SourceDir}\orpheus\orpheus.sys"; DestDir: "{app}\orpheus"; Flags: ignoreversion; Permissions: users-modify

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
; The setup log, and the file the engine rewrites with its own location.
;
; %APPDATA%\OrpheusNativeSAPI - the per-voice settings and the debug logs - is
; deliberately NOT listed here. Setup runs elevated, so {userappdata} resolves
; to the administrator's profile rather than the profile that actually holds
; those settings: the line would miss the real files and delete somebody
; else's. Per-user settings surviving an uninstall is normal; deleting the
; wrong user's data is not. The README says where they are.
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\orpheus\orpheus.sys"
Type: dirifempty; Name: "{app}\orpheus"
Type: dirifempty; Name: "{app}"

[Code]

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

// Preserve the setup log for debugging: copy it into the application folder.
procedure CurStepChanged(CurStep: TSetupStep);
var
  LogDir: String;
begin
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
