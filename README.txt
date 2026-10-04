USB VM DEFENDER
===============

USB VM Defender is a Windows USB security system that intercepts removable storage devices, sends unknown devices into an isolated VirtualBox Ubuntu VM, scans them with ClamAV, and only releases clean devices back to Windows.


SETUP REQUIREMENTS
==================

USB VM Defender requires three setup steps before it will work.


1. GENERATE SSH KEYS
--------------------

Open Command Prompt and run:

mkdir "%USERPROFILE%\.ssh"
ssh-keygen -t ed25519 -f "%USERPROFILE%\.ssh\usb_sandbox_key"

When prompted for a passphrase, leave it blank.

Press Enter twice.

This creates:

%USERPROFILE%\.ssh\usb_sandbox_key
%USERPROFILE%\.ssh\usb_sandbox_key.pub

The key must not have a password because USB VM Defender uses it for automatic SSH communication with the sandbox VM.


2. INSTALL VIRTUALBOX AND IMPORT THE VM
---------------------------------------

Run the provided VirtualBox installer.

After VirtualBox is installed, import the provided USB VM Defender virtual machine.

The VM must be named exactly:

USB-Sandbox

The VM is already configured for USB VM Defender.

No additional configuration inside the VM is required after importing it.


3. INSTALL USB VM DEFENDER
--------------------------

Run the provided USB VM Defender installer.

The installer installs the Windows-side USB Defender components.

After installation, USB VM Defender is ready to run.


HOW USB VM DEFENDER WORKS
=========================

When a USB storage device is connected:

1. The Windows C++ detection layer detects the USB device.

2. Device information is sent to the USB Defender Python security engine.

3. USB Defender checks the local device whitelist.

4. If the device is already trusted, normal processing can continue.

5. If the storage device is unknown, VirtualBox captures the USB device.

6. The USB device is attached to the USB-Sandbox VM.

7. Ubuntu mounts the USB storage device as read-only.

8. ClamAV scans the contents of the drive.

9. The USB device is unmounted from Ubuntu.

10. If the scan is clean, the USB device is released back to Windows.

11. The clean device is added to the local whitelist.

12. If malware is detected or the scan fails, the device is denied instead of being released.


MAIN COMPONENTS
===============

Windows
|
|-- USB Defender Python Security Engine
|
|-- C++ USB Detection Layer
|
|-- SQLite Device Whitelist
|
`-- VirtualBox
     |
     `-- USB-Sandbox
          |
          |-- Ubuntu
          |
          |-- ClamAV
          |
          `-- Read-Only USB Scanning


IMPORTANT REQUIREMENTS
======================

Do not add a passphrase to the SSH key.

The imported VirtualBox VM must be named exactly:

USB-Sandbox

The supplied VM is already configured and does not require manual configuration after it is imported.

VirtualBox must be installed before USB VM Defender can use the sandbox.

The USB Defender installer must be run after the VM environment is installed.


DEVELOPMENT BUILD
=================

The development build displays console output showing the USB detection and scanning process.

A normal successful scan should look roughly like this:

USB device detected
Device information received
USB captured by VM
USB mounted read-only
ClamAV scan started
ClamAV scan passed
USB unmounted
USB released back to Windows
Device added to whitelist
USB detector restarted

The C++ USB detector is automatically restarted after every device-processing cycle.


SUCCESSFUL INSTALLATION ORDER
=============================

1. Generate SSH keys with no passphrase
2. Install VirtualBox
3. Import USB-Sandbox VM
4. Run USB VM Defender installer
5. Run USB VM Defender

Once these steps are complete, the development build should be able to detect, capture, scan, release, and whitelist USB storage devices.