#ifndef AppVersion
  #error Pass AppVersion to ISCC with --define=AppVersion=<version>.
#endif
#ifndef StageDir
  #error Pass StageDir to ISCC with --define=StageDir=<absolute stage directory>.
#endif

[Setup]
AppId=Outmode.Rendepth
AppName=Rendepth
AppVersion={#AppVersion}
AppPublisher=Outmode
DefaultDirName={autopf}\Rendepth
DefaultGroupName=Rendepth
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
LicenseFile={#StageDir}\libexec\rendepth\Legal\RENDEPTH_APP_LICENSE
SetupIconFile=..\..\Assets\Rendepth.ico
UninstallDisplayIcon={app}\Binary\Rendepth.exe
OutputBaseFilename=Rendepth-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardImageFile=Artwork\wizard-portrait.png
WizardSmallImageFile=
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\libexec\rendepth\Binary\*"; DestDir: "{app}\Binary"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\libexec\rendepth\Assets\*"; DestDir: "{app}\Assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\libexec\rendepth\Shaders\*"; DestDir: "{app}\Shaders"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\libexec\rendepth\Runtimes\*"; DestDir: "{app}\Runtimes"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\libexec\rendepth\Legal\*"; DestDir: "{app}\Legal"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
; Register as an Open with choice without taking over the user's default apps.
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\Rendepth.exe"; ValueType: string; ValueData: "{app}\Binary\Rendepth.exe"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Rendepth"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\shell\open\command"; ValueType: string; ValueData: """{app}\Binary\Rendepth.exe"" ""%1"""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jpe"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jfif"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jif"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jps"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".pns"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".tga"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".dib"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".tiff"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".ico"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".cur"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".qoi"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".avif"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".avifs"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".jxl"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mp4"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".m4v"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mov"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mkv"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".webm"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".avi"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".wmv"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mpeg"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mpg"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".vob"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".ifo"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".iso"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".m2ts"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".ts"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".gif"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".aac"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".flac"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".m4a"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".oga"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".ogg"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".opus"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Applications\Rendepth.exe\SupportedTypes"; ValueType: string; ValueName: ".wav"; ValueData: ""

[Icons]
Name: "{autoprograms}\Rendepth"; Filename: "{app}\Binary\Rendepth.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Rendepth"; Filename: "{app}\Binary\Rendepth.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Binary\Rendepth.exe"; Description: "Launch Rendepth"; Flags: nowait postinstall skipifsilent
