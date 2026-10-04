USB VM DEFENDER - INSTALLER BUILD LAYOUT

Place these files/folders together before compiling the Inno Setup script:

USB-VM-Defender/
|
|-- App/
|   |-- USBDefender-Python.exe
|   `-- BACKGROUND(C++).exe
|
|-- VM/
|   `-- USB-Sandbox.ova
|
|-- Keys/
|   |-- usb_sandbox_key
|   `-- usb_sandbox_key.pub      (build/reference only, not required by installer)
|
|-- Dependencies/
|   `-- VirtualBox-Win.exe
|
`-- Installer/
    |-- USB_VM_Defender_Setup.iss
    `-- install_backend.ps1


IMPORTANT KEY BOOTSTRAP STEP

The installer copies:
    Keys\usb_sandbox_key

to:
    C:\ProgramData\USB VM Defender\Keys\usb_sandbox_key

The matching PUBLIC key must already exist inside the VM at:
    /home/vboxuser/.ssh/authorized_keys

BEFORE exporting USB-Sandbox.ova.

The installer cannot securely invent a brand-new key on the end user's computer
and put its public half into an otherwise inaccessible Ubuntu guest without
some pre-existing guest authentication mechanism.


VM REQUIREMENTS BEFORE EXPORTING THE OVA

Ubuntu guest must contain:
  - user: vboxuser
  - OpenSSH server enabled
  - ClamAV installed
  - lsblk available
  - /mnt/usb directory created
  - the public key in /home/vboxuser/.ssh/authorized_keys
  - sudoers rules allowing vboxuser to run these without a password:

      /usr/bin/mount
      /usr/bin/umount

A narrow sudoers rule is preferable to full passwordless sudo.


HOST SETTINGS CREATED BY THE INSTALLER

VM name:
    USB-Sandbox

Network:
    NAT

Port forward:
    127.0.0.1:2222 -> guest port 22

Application:
    C:\Program Files\USB VM Defender\

Data:
    C:\ProgramData\USB VM Defender\

Private SSH key:
    C:\ProgramData\USB VM Defender\Keys\usb_sandbox_key


BUILDING THE INSTALLER

1. Install Inno Setup on the build computer.
2. Open Installer\USB_VM_Defender_Setup.iss.
3. Compile it.
4. The generated installer appears under:
       Installer\Output\USB_VM_Defender_Setup.exe

TEST THE GENERATED INSTALLER ON A CLEAN WINDOWS MACHINE OR VM.

Do not treat a successful install on the development laptop as proof of a
fresh-machine deployment. Development computers have a suspicious habit of
already containing every missing dependency.
