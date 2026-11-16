; SlopStudio installer for Windows (Inno Setup 6).
;
; Built by scripts/package_windows_installer.py, which passes the staged
; portable package, the version and the branding paths on the command line:
;   ISCC /DAppVersion=0.1.0-alpha.1 /DVersionInfo=0.1.0.0 /DSourceDir=... /DArtDir=...
;        /DIconFile=... /DOutputDir=... /DOutputBaseFilename=... slopstudio.iss
;
; The AppId never changes: it is how Windows recognises upgrades and uninstalls.

#ifndef AppVersion
  #error Define AppVersion (/DAppVersion=...)
#endif
#ifndef VersionInfo
  #define VersionInfo "0.0.0.0"
#endif
#ifndef SourceDir
  #error Define SourceDir, the staged portable package
#endif
#ifndef ArtDir
  #error Define ArtDir, the folder holding the wizard BMPs and notices
#endif
#ifndef IconFile
  #error Define IconFile (assets/branding/icons/slopstudio.ico)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "SlopStudio-" + AppVersion + "-windows-x64-setup"
#endif

[Setup]
AppId={{CFD89BA5-E0D6-4F29-B906-94E2033E6E00}
AppName=SlopStudio
AppVersion={#AppVersion}
AppVerName=SlopStudio {#AppVersion}
AppPublisher=DarkMatter Productions
AppPublisherURL=https://github.com/themuffinator/SlopStudio
AppSupportURL=https://github.com/themuffinator/SlopStudio/issues
AppUpdatesURL=https://github.com/themuffinator/SlopStudio/releases
AppCopyright=Copyright (C) themuffinator and SlopStudio contributors
VersionInfoVersion={#VersionInfo}
VersionInfoProductName=SlopStudio
VersionInfoProductTextVersion={#AppVersion}
VersionInfoDescription=SlopStudio Setup
; Per-user by default (no administrator rights); the dialog offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\SlopStudio
DefaultGroupName=SlopStudio
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\bin\slopstudio.exe
UninstallDisplayName=SlopStudio
WizardStyle=modern
WizardImageFile={#ArtDir}\windows-wizard-100.bmp,{#ArtDir}\windows-wizard-200.bmp
WizardSmallImageFile={#ArtDir}\windows-wizard-small-100.bmp,{#ArtDir}\windows-wizard-small-200.bmp
LicenseFile={#SourceDir}\licenses\slopstudio\LICENSE
InfoBeforeFile={#ArtDir}\before-install.txt
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
SetupLogging=yes

[Languages]
; Inno Setup's official translations that overlap the studio's interface languages.
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"

[CustomMessages]
DocumentationShortcut=SlopStudio Documentation

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\SlopStudio"; Filename: "{app}\bin\slopstudio.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:DocumentationShortcut}"; Filename: "{app}\docs\html\index.html"
Name: "{autodesktop}\SlopStudio"; Filename: "{app}\bin\slopstudio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\slopstudio.exe"; Description: "{cm:LaunchProgram,SlopStudio}"; Flags: nowait postinstall skipifsilent
