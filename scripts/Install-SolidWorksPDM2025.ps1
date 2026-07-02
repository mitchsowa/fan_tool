#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs the SOLIDWORKS PDM 2025 client on a Nyle client PC and points it at the
    on-prem archive server (NyleSystems-pdm) so the vault works over the LAN without VPN.

.DESCRIPTION
    Run this on a client PC that is on the Nyle network (in-building wired/Wi-Fi, or any
    network that routes to NyleSystems-pdm). Because the client talks directly to the
    on-prem archive server, no VPN / OpenVPN / AWS VPN Client connection is needed.

    Steps performed:
        1. Elevation check + transcript logging.
        2. Confirms the PC can actually reach the PDM archive server on the LAN
           (TCP 3030). If it can't, VPN really would be required and the script stops.
        3. Installs the SOLIDWORKS PDM 2025 client from SolidWorksSetup.exe
           (skipped if a PDM client is already installed).
        4. Applies the 2025 SP05 hotfix, if the hotfix .exe is present.
        5. Writes the "no-VPN" registry keys that register the PDMVault database against
           NyleSystems-pdm (this is the content of "Client Reg Update.reg", applied
           directly so no separate .reg file is required), and clears the stale
           per-user ConisioAdmin key.
        6. Attaches the PDMVault view so it appears in File Explorer.

    These installers/config come from the shared PDM 2025 setup folder:
        SolidWorksSetup.exe
        HotFix_HF-1463236_BR10000413928_2025SP05.exe
        Client Reg Update.reg   (its contents are baked into step 5 below)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Install-SolidWorksPDM2025.ps1

.EXAMPLE
    # Point at a copy of the setup files on a share, and attach the view silently:
    .\Install-SolidWorksPDM2025.ps1 -SourceDir '\\NyleSystems-pdm\Software\PDM2025' -CvsFile '\\NyleSystems-pdm\Software\PDM2025\PDMVault.cvs'

.NOTES
    Values below come from the internal SolidWorks PDM 2025 setup page:
    https://sites.google.com/nyle.com/solidworks-pdm-2025/home
#>

[CmdletBinding()]
param(
    # --- Environment (from Nyle "Client Reg Update.reg" / setup page) ----------
    # Archive + database server hostname on the LAN. This is what makes VPN unnecessary.
    [string]$DbServer   = 'NyleSystems-pdm',
    [string]$ServerLoc  = 'Nylesystems-pdm',
    [string]$VaultName  = 'PDMVault',

    # Folder containing SolidWorksSetup.exe and the hotfix. Defaults to this script's
    # own folder (drop the files next to it), but can be a share.
    [string]$SourceDir  = $PSScriptRoot,

    [string]$SetupExe   = 'SolidWorksSetup.exe',
    [string]$HotfixExe  = 'HotFix_HF-1463236_BR10000413928_2025SP05.exe',

    # Optional pre-exported view-settings file for a fully silent vault-view attach.
    # Export once from a working machine (PDM Administration > export view) and pass here.
    [string]$CvsFile    = '',

    [string]$VaultViewRoot = 'C:\',

    # Skip the SOLIDWORKS installer and only apply the registry pointer + attach the view.
    [switch]$ConfigureOnly,

    [int]$ArchivePort = 3030,   # PDM archive server
    [int]$SqlPort     = 1433,   # SQL database (optional check)

    [string]$LogPath = "$env:ProgramData\Nyle\Install-SolidWorksPDM2025.log"
)

$ErrorActionPreference = 'Stop'

# --- Logging ------------------------------------------------------------------
$logDir = Split-Path -Parent $LogPath
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }
Start-Transcript -Path $LogPath -Append | Out-Null

function Write-Step { param([string]$m) Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Write-Info { param([string]$m) Write-Host "    $m" -ForegroundColor Gray }
function Write-Ok   { param([string]$m) Write-Host "[OK]  $m" -ForegroundColor Green }
function Write-Warn2{ param([string]$m) Write-Host "[!]   $m" -ForegroundColor Yellow }

function Test-Reachable {
    param([string]$ComputerName, [int]$Port)
    try {
        if (Get-Command Test-NetConnection -ErrorAction SilentlyContinue) {
            return (Test-NetConnection -ComputerName $ComputerName -Port $Port `
                        -InformationLevel Quiet -WarningAction SilentlyContinue)
        }
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect($ComputerName, $Port, $null, $null)
        $ok  = $iar.AsyncWaitHandle.WaitOne(5000, $false)
        if ($ok -and $client.Connected) { $client.EndConnect($iar); $client.Close(); return $true }
        $client.Close(); return $false
    } catch { return $false }
}

function Get-ViewSetupPath {
    @(
        "$env:ProgramFiles\SOLIDWORKS PDM\ViewSetup.exe",
        "${env:ProgramFiles(x86)}\SOLIDWORKS PDM\ViewSetup.exe",
        "$env:ProgramFiles\SOLIDWORKS Corp\SOLIDWORKS PDM\ViewSetup.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}

try {
    Write-Step "SOLIDWORKS PDM 2025 client setup — starting"
    Write-Info "Machine: $env:COMPUTERNAME   User: $env:USERNAME"
    Write-Info "Vault:   $VaultName   Server: $DbServer"
    Write-Info "Log:     $LogPath"

    # --- 1. Network reachability (this is what replaces the VPN) --------------
    Write-Step "Checking LAN access to the PDM archive server"
    Write-Info "Testing $DbServer on TCP $ArchivePort ..."
    if (-not (Test-Reachable -ComputerName $DbServer -Port $ArchivePort)) {
        throw "Cannot reach archive server '$DbServer' on port $ArchivePort. This PC must be on " +
              "the Nyle LAN (or a network that routes to $DbServer). If you are off-site you would " +
              "still need VPN. Once you are on the network, re-run this script."
    }
    Write-Ok "Archive server '$DbServer' is reachable — no VPN required."
    if (Test-Reachable -ComputerName $DbServer -Port $SqlPort) {
        Write-Ok "SQL ($DbServer`:$SqlPort) is reachable."
    } else {
        Write-Warn2 "SQL port $SqlPort not directly reachable (often fine — the archive server proxies it)."
    }

    # --- 2. Install the PDM client -------------------------------------------
    $viewSetup = Get-ViewSetupPath
    $alreadyInstalled = [bool]$viewSetup

    if ($ConfigureOnly) {
        Write-Step "Skipping installation (-ConfigureOnly)"
    }
    elseif ($alreadyInstalled) {
        Write-Step "SOLIDWORKS PDM client already installed — skipping installer"
        Write-Info "Found: $viewSetup"
    }
    else {
        Write-Step "Installing the SOLIDWORKS PDM 2025 client"
        $setupPath = Join-Path $SourceDir $SetupExe
        if (-not (Test-Path $setupPath)) {
            throw "Installer not found: $setupPath`nPlace '$SetupExe' in '$SourceDir' " +
                  "(or pass -SourceDir), or use -ConfigureOnly if PDM is already installed."
        }
        Write-Info "Launching $SetupExe (SOLIDWORKS Installation Manager)."
        Write-Info "In the manager, choose to install the 'SOLIDWORKS PDM Client' (2025), product"
        Write-Info "type Standard/Professional per your license, then finish. This window will"
        Write-Info "resume automatically once the installer closes."
        $p = Start-Process -FilePath $setupPath -Wait -PassThru
        if ($p.ExitCode -notin 0, 3010) {
            throw "SolidWorksSetup.exe exited with code $($p.ExitCode). Check the SOLIDWORKS " +
                  "Installation Manager logs under %TEMP%."
        }
        Write-Ok "SOLIDWORKS PDM client installer finished."

        # --- 3. Hotfix -------------------------------------------------------
        $hotfixPath = Join-Path $SourceDir $HotfixExe
        if (Test-Path $hotfixPath) {
            Write-Step "Applying 2025 SP05 hotfix"
            Write-Info "Running $HotfixExe ..."
            $hp = Start-Process -FilePath $hotfixPath -Wait -PassThru
            if ($hp.ExitCode -notin 0, 3010) {
                Write-Warn2 "Hotfix exited with code $($hp.ExitCode). Review it manually if PDM misbehaves."
            } else {
                Write-Ok "Hotfix applied."
            }
        } else {
            Write-Warn2 "Hotfix '$HotfixExe' not found in $SourceDir — skipping (install SP05 later if needed)."
        }

        $viewSetup = Get-ViewSetupPath
    }

    # --- 4. Apply the no-VPN registry pointer (Client Reg Update.reg) ---------
    # Registers the PDMVault database against the on-prem server so the client/ViewSetup
    # find it directly on the LAN. Written to both native and Wow6432Node paths.
    Write-Step "Registering vault '$VaultName' against '$DbServer' (no-VPN pointer)"
    $vaultKeys = @(
        "HKLM:\SOFTWARE\SolidWorks\Applications\PDMWorks Enterprise\Databases\$VaultName",
        "HKLM:\SOFTWARE\Wow6432Node\SolidWorks\Applications\PDMWorks Enterprise\Databases\$VaultName"
    )
    foreach ($key in $vaultKeys) {
        if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
        New-ItemProperty -Path $key -Name 'DbServer'  -Value $DbServer  -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $key -Name 'ServerLoc' -Value $ServerLoc -PropertyType String -Force | Out-Null
        Write-Info "Set DbServer/ServerLoc under $key"
    }
    # Clear the stale per-user admin connection key (the leading '-' in the .reg deletes it).
    $conisioAdmin = 'HKCU:\Software\Solidworks\Applications\PDMWorks Enterprise\ConisioAdmin'
    if (Test-Path $conisioAdmin) {
        Remove-Item -Path $conisioAdmin -Recurse -Force
        Write-Info "Removed stale ConisioAdmin key."
    }
    Write-Ok "Registry pointer applied."

    # --- 5. Attach the vault view --------------------------------------------
    Write-Step "Attaching the '$VaultName' vault view"
    if (-not $viewSetup) {
        Write-Warn2 "ViewSetup.exe not found. If you just ran the installer, reboot and re-run with " +
                    "-ConfigureOnly, or attach the view from the Start menu (View Setup)."
    }
    elseif ($CvsFile) {
        if (-not (Test-Path $CvsFile)) { throw "View settings file not found: $CvsFile" }
        Write-Info "Silent attach from $CvsFile"
        $vp = Start-Process -FilePath $viewSetup -ArgumentList "`"$CvsFile`"", '/s' -Wait -PassThru
        if ($vp.ExitCode -ne 0) { throw "ViewSetup.exe (silent) failed with exit code $($vp.ExitCode)." }
        Write-Ok "Vault view attached silently."
    }
    else {
        Write-Info "Launching the View Setup wizard. In it:"
        Write-Info "   1. 'Attach to an existing archive server'."
        Write-Info "   2. Add archive server:  $DbServer"
        Write-Info "   3. Select the vault:    $VaultName"
        Write-Info "   4. View location:       $VaultViewRoot"
        Start-Process -FilePath $viewSetup -Wait
        Write-Ok "View Setup wizard closed."
    }

    Write-Step "Done"
    Write-Ok "SOLIDWORKS PDM 2025 client is set up on $env:COMPUTERNAME."
    Write-Info "Open File Explorer, find the '$VaultName' vault, and log in with your PDM credentials."
    Write-Info "No VPN needed while this PC is on the Nyle network."
}
catch {
    Write-Host "`n[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "        Log: $LogPath" -ForegroundColor Red
    exit 1
}
finally {
    Stop-Transcript | Out-Null
}
