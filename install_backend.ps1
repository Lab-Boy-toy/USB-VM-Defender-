param(
    [Parameter(Mandatory=$true)]
    [string]$AppDir,

    [Parameter(Mandatory=$true)]
    [string]$DataDir,

    [Parameter(Mandatory=$true)]
    [string]$OVAPath,

    [Parameter(Mandatory=$true)]
    [string]$VBoxInstaller
)

$ErrorActionPreference = "Stop"

$VMName = "USB-Sandbox"
$VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$KeyDir = Join-Path $DataDir "Keys"
$PrivateKey = Join-Path $KeyDir "usb_sandbox_key"
$KnownHosts = Join-Path $DataDir "known_hosts"
$SSHPort = 2222
$SSHUser = "vboxuser"

function Write-Step {
    param([string]$Message)
    Write-Host "[USB VM Defender] $Message"
}

function Ensure-Directory {
    param([string]$Path)

    if (!(Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Install-VirtualBox {
    if (Test-Path $VBoxManage) {
        Write-Step "VirtualBox already installed."
        return
    }

    if (!(Test-Path $VBoxInstaller)) {
        throw "VirtualBox installer was not bundled with setup."
    }

    Write-Step "Installing VirtualBox."

    $process = Start-Process `
        -FilePath $VBoxInstaller `
        -ArgumentList "--silent" `
        -Wait `
        -PassThru

    if ($process.ExitCode -ne 0) {
        # Some VirtualBox releases use MSI-style switches through the wrapper.
        Write-Step "First silent install attempt returned $($process.ExitCode). Trying MSI parameters."

        $process = Start-Process `
            -FilePath $VBoxInstaller `
            -ArgumentList "-msiparams ADDLOCAL=VBoxApplication,VBoxUSB" `
            -Wait `
            -PassThru
    }

    if (!(Test-Path $VBoxManage)) {
        throw "VirtualBox installation did not produce VBoxManage.exe."
    }
}

function Ensure-OpenSSH {
    $ssh = Get-Command ssh.exe -ErrorAction SilentlyContinue

    if ($null -ne $ssh) {
        Write-Step "Windows OpenSSH Client is already available."
        return
    }

    Write-Step "Installing Windows OpenSSH Client."

    try {
        Add-WindowsCapability `
            -Online `
            -Name "OpenSSH.Client~~~~0.0.1.0" `
            | Out-Null
    }
    catch {
        throw "Could not install Windows OpenSSH Client. $($_.Exception.Message)"
    }

    $ssh = Get-Command ssh.exe -ErrorAction SilentlyContinue

    if ($null -eq $ssh) {
        throw "Windows OpenSSH Client is still unavailable."
    }
}

function Secure-PrivateKey {
    if (!(Test-Path $PrivateKey)) {
        throw "SSH private key is missing: $PrivateKey"
    }

    Write-Step "Locking down SSH private key permissions."

    & icacls $PrivateKey /inheritance:r | Out-Null
    & icacls $PrivateKey /grant:r "SYSTEM:F" | Out-Null
    & icacls $PrivateKey /grant:r "Administrators:F" | Out-Null
    & icacls $PrivateKey /grant:r "$($env:USERNAME):R" | Out-Null

    if ($LASTEXITCODE -ne 0) {
        throw "Could not secure SSH private key permissions."
    }
}

function VM-Exists {
    $output = & $VBoxManage list vms 2>$null

    foreach ($line in $output) {
        if ($line -match '"USB-Sandbox"') {
            return $true
        }
    }

    return $false
}

function VM-Running {
    $output = & $VBoxManage list runningvms 2>$null

    foreach ($line in $output) {
        if ($line -match '"USB-Sandbox"') {
            return $true
        }
    }

    return $false
}

function Stop-VM {
    if (VM-Running) {
        Write-Step "Stopping existing USB sandbox."
        & $VBoxManage controlvm $VMName poweroff 2>$null | Out-Null
        Start-Sleep -Seconds 2
    }
}

function Import-VM {
    if (VM-Exists) {
        Write-Step "USB-Sandbox already exists. Leaving the existing VM in place."
        return
    }

    if (!(Test-Path $OVAPath)) {
        throw "USB sandbox appliance is missing: $OVAPath"
    }

    Write-Step "Importing USB-Sandbox appliance."

    & $VBoxManage import $OVAPath `
        --vsys 0 `
        --vmname $VMName

    if ($LASTEXITCODE -ne 0) {
        throw "VirtualBox failed to import USB-Sandbox.ova."
    }
}

function Configure-VM {
    Write-Step "Configuring USB sandbox networking."

    Stop-VM

    # Remove an old SSH rule if it already exists.
    & $VBoxManage modifyvm $VMName `
        --natpf1 delete "USBDefenderSSH" `
        2>$null | Out-Null

    # Ensure NIC 1 uses NAT.
    & $VBoxManage modifyvm $VMName `
        --nic1 nat

    if ($LASTEXITCODE -ne 0) {
        throw "Could not configure VM NAT networking."
    }

    # Host 127.0.0.1:2222 -> Guest port 22.
    & $VBoxManage modifyvm $VMName `
        --natpf1 "USBDefenderSSH,tcp,127.0.0.1,2222,,22"

    if ($LASTEXITCODE -ne 0) {
        throw "Could not configure SSH port forwarding."
    }

    # Keep USB controller support enabled.
    & $VBoxManage modifyvm $VMName --usb on 2>$null | Out-Null
    & $VBoxManage modifyvm $VMName --usbehci on 2>$null | Out-Null
    & $VBoxManage modifyvm $VMName --usbxhci on 2>$null | Out-Null
}

function Start-VM {
    if (VM-Running) {
        Write-Step "USB sandbox is already running."
        return
    }

    Write-Step "Starting USB sandbox."

    & $VBoxManage startvm $VMName --type headless | Out-Null

    if ($LASTEXITCODE -ne 0) {
        throw "Could not start USB-Sandbox."
    }
}

function Test-SSH {
    Write-Step "Waiting for sandbox SSH."

    $sshExe = (Get-Command ssh.exe).Source

    for ($attempt = 1; $attempt -le 30; $attempt++) {
        $arguments = @(
            "-i", $PrivateKey,
            "-o", "BatchMode=yes",
            "-o", "IdentitiesOnly=yes",
            "-o", "StrictHostKeyChecking=accept-new",
            "-o", "UserKnownHostsFile=$KnownHosts",
            "-o", "ConnectTimeout=3",
            "-p", "$SSHPort",
            "$SSHUser@127.0.0.1",
            "printf USB_DEFENDER_READY"
        )

        $result = & $sshExe @arguments 2>$null

        if ($LASTEXITCODE -eq 0 -and $result -match "USB_DEFENDER_READY") {
            Write-Step "Sandbox SSH authentication succeeded."
            return
        }

        Start-Sleep -Seconds 2
    }

    throw @"
Could not authenticate to USB-Sandbox over SSH.

The matching PUBLIC key for:
$PrivateKey

must already be present in:
 /home/vboxuser/.ssh/authorized_keys

inside USB-Sandbox.ova.
"@
}

function Verify-Guest {
    Write-Step "Checking required sandbox programs."

    $sshExe = (Get-Command ssh.exe).Source

    $arguments = @(
        "-i", $PrivateKey,
        "-o", "BatchMode=yes",
        "-o", "IdentitiesOnly=yes",
        "-o", "StrictHostKeyChecking=accept-new",
        "-o", "UserKnownHostsFile=$KnownHosts",
        "-p", "$SSHPort",
        "$SSHUser@127.0.0.1",
        "command -v clamscan >/dev/null && command -v lsblk >/dev/null && test -d /mnt/usb"
    )

    & $sshExe @arguments | Out-Null

    if ($LASTEXITCODE -ne 0) {
        throw "Sandbox is missing ClamAV, lsblk, or /mnt/usb."
    }

    Write-Step "Sandbox dependency check passed."
}

function Start-Defender {
    $PythonEngine = Join-Path $AppDir "USBDefender-Python.exe"
    $CDetector = Join-Path $AppDir "BACKGROUND(C++).exe"

    if (!(Test-Path $PythonEngine)) {
        throw "Python security engine is missing: $PythonEngine"
    }

    if (!(Test-Path $CDetector)) {
        throw "C++ detector is missing: $CDetector"
    }

    Write-Step "Starting USB VM Defender."

    Start-Process `
        -FilePath $PythonEngine `
        -WorkingDirectory $AppDir
}

Write-Step "Beginning installation configuration."

Ensure-Directory $DataDir
Ensure-Directory $KeyDir

Install-VirtualBox
Ensure-OpenSSH
Secure-PrivateKey
Import-VM
Configure-VM
Start-VM
Test-SSH
Verify-Guest
Start-Defender

Write-Step "USB VM Defender setup completed successfully."
