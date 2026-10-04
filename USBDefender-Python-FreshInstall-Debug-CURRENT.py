# General idea make the Python security side that handles USBs after the C layer detects them
# program starts and makes sure the VirtualBox USB sandbox is running
# listen on port 5050 for device information sent from the C detector
# when a device is received check the local whitelist database first
# if the exact device information is already whitelisted then ignore it
# if the device is new check if it is a storage device
# wait until the Ubuntu VirtualBox sandbox is running before doing anything with the USB
# pull the VID PID and serial number out of the information sent by C
# reject the event if the physical USB information cannot be identified
# remove any old VirtualBox USB filters so nothing gets automatically grabbed by accident
# create a temporary VirtualBox filter for the exact USB using its VID PID and serial
# wait for VirtualBox to capture the USB and Ubuntu to see it
# once Ubuntu has the USB remove the capture filter so VirtualBox cannot grab it again later
# mount the USB inside Ubuntu as read only no execute no suid and no device files
# run ClamAV against everything on the USB
# unmount the USB from Ubuntu when the scan finishes
# if ClamAV finds malware keep the USB away from Windows
# send the string DENY back to the C program on port 5000
# if ClamAV has an error also treat the device as unsafe and send DENY
# if the scan is clean find the USB address currently attached to the VM
# detach the USB from VirtualBox so Windows can detect and mount it normally
# remove any leftover automatic USB filters again
# add the clean device information to the whitelist database
# then go back to listening for another USB
# final flow start program ->
# start sandbox if needed ->
# listen for C device event ->
# check whitelist ->
# identify storage USB ->
# capture USB in VirtualBox ->
# mount read only ->
# scan with ClamAV ->
# if infected send DENY to port 5000 ->
# if clean detach from VM and return to Windows ->
# whitelist clean device ->
# continue listening


import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import time
from pathlib import Path


# Fresh-install paths
# The installer should create:
# C:\ProgramData\USB VM Defender\
# C:\ProgramData\USB VM Defender\Keys\
# and place the private SSH key at:
# C:\ProgramData\USB VM Defender\Keys\usb_sandbox_key

PROGRAM_DATA = Path(
    os.environ.get(
        "PROGRAMDATA",
        r"C:\ProgramData"
    )
)

APP_DATA = PROGRAM_DATA / "USB VM Defender"
KEY_DIR = APP_DATA / "Keys"
DB = APP_DATA / "usb_security.db"
SSH_KEY = KEY_DIR / "usb_sandbox_key"
KNOWN_HOSTS = APP_DATA / "known_hosts"

VM = "USB-Sandbox"

# Find the folder the program is actually installed in.
# When compiled with PyInstaller sys.executable points to the EXE.
# When running as a normal .py file use the Python file folder instead.
if getattr(sys, "frozen", False):
    APP_DIR = Path(sys.executable).resolve().parent
else:
    APP_DIR = Path(__file__).resolve().parent

# The C++ detector is installed beside the Python security engine.
C_EXE = APP_DIR / "BACKGROUND(C++).exe"

# Try the standard VirtualBox install location first.
PROGRAM_FILES = Path(
    os.environ.get(
        "ProgramFiles",
        r"C:\Program Files"
    )
)

VBOX = PROGRAM_FILES / "Oracle" / "VirtualBox" / "VBoxManage.exe"

# Find Windows OpenSSH even if ssh.exe is not on PATH.
SSH = shutil.which("ssh")

if SSH is None:
    SYSTEM_ROOT = Path(
        os.environ.get(
            "SystemRoot",
            r"C:\Windows"
        )
    )

    SSH_FALLBACK = SYSTEM_ROOT / "System32" / "OpenSSH" / "ssh.exe"

    if SSH_FALLBACK.exists():
        SSH = str(SSH_FALLBACK)


def startup_check():

    try:
        APP_DATA.mkdir(
            parents=True,
            exist_ok=True
        )

        KEY_DIR.mkdir(
            parents=True,
            exist_ok=True
        )

    except PermissionError:
        print(
            "USB Defender could not create its ProgramData folder.\n"
            "The installer should create this folder and give the application permission to use it."
        )
        sys.exit(1)

    if VBOX.exists() is False:
        print(
            "USB Defender could not find VirtualBox.\n"
            f"Expected VBoxManage at:\n{VBOX}"
        )
        sys.exit(1)

    if SSH is None:
        print(
            "USB Defender could not find the Windows OpenSSH client.\n"
            "Install the Windows OpenSSH Client feature before running USB Defender."
        )
        sys.exit(1)

    if SSH_KEY.exists() is False:
        print(
            "USB Defender could not find its SSH key.\n"
            f"Expected key at:\n{SSH_KEY}"
        )
        sys.exit(1)

    if C_EXE.exists() is False:
        print(
            "USB Defender could not find the C++ detector.\n"
            f"Expected detector at:\n{C_EXE}"
        )
        sys.exit(1)


def start_c_detector():

    print("Starting C++ USB detector.")

    return subprocess.Popen(
        [str(C_EXE)],
        cwd=str(APP_DIR)
    )


def stop_c_detector(c_process):

    if c_process is None:
        return

    if c_process.poll() is not None:
        return

    print("Stopping C++ USB detector.")

    c_process.terminate()

    try:
        c_process.wait(
            timeout=3
        )

    except subprocess.TimeoutExpired:
        c_process.kill()

        c_process.wait()


def ssh_command(remote_command):

    return [
        SSH,
        "-i",
        str(SSH_KEY),
        "-o",
        "BatchMode=yes",
        "-o",
        "IdentitiesOnly=yes",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-o",
        f"UserKnownHostsFile={KNOWN_HOSTS}",
        "-p",
        "2222",
        "vboxuser@127.0.0.1",
        remote_command
    ]


def send_to_c(message):

    client = socket.socket(
        socket.AF_INET,
        socket.SOCK_STREAM
    )

    try:
        client.connect(
            (
                "127.0.0.1",
                5000
            )
        )

        client.sendall(
            message.encode()
        )

    finally:
        client.close()


def add_device(INFO):

    connection = sqlite3.connect(
        str(DB)
    )

    cursor = connection.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS whitelist (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            info TEXT UNIQUE
        )
    """)

    cursor.execute(
        "INSERT OR IGNORE INTO whitelist (info) VALUES (?)",
        (INFO,)
    )

    connection.commit()
    connection.close()

    print("Device added to whitelist.")


def check_device(INFO):

    connection = sqlite3.connect(
        str(DB)
    )

    cursor = connection.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS whitelist (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            info TEXT UNIQUE
        )
    """)

    cursor.execute(
        "SELECT info FROM whitelist WHERE info = ?",
        (INFO,)
    )

    match = cursor.fetchone()

    connection.close()

    if match is not None:
        return True

    return False


def remove_auto_usb_filters():

    result = subprocess.run(
        [
            str(VBOX),
            "showvminfo",
            VM
        ],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print("Could not check VirtualBox USB filters.")
        print(result.stderr)
        return False

    lines = result.stdout.splitlines()

    filter_indexes = []

    current_index = None
    current_active = False

    # Find active USB filters
    for line in lines:

        line = line.strip()

        if line.startswith("Index:"):

            # Save previous filter if it was active
            if current_index is not None:
                if current_active is True:
                    filter_indexes.append(
                        current_index
                    )

            current_index = line.split(
                ":",
                1
            )[1].strip()

            current_active = False

        if line.startswith("Active:"):

            active_value = line.split(
                ":",
                1
            )[1].strip().lower()

            if active_value == "yes":
                current_active = True

    # Check final filter
    if current_index is not None:
        if current_active is True:
            filter_indexes.append(
                current_index
            )

    # Nothing is auto-capturing
    if len(filter_indexes) == 0:
        print("No automatic USB capture filters found.")
        return True

    print("Automatic USB filters found:")
    print(filter_indexes)

    # Remove highest indexes first so indexes do not shift underneath us
    filter_indexes.sort(
        key=int,
        reverse=True
    )

    for index in filter_indexes:

        print(
            "Permanently removing USB filter:",
            index
        )

        subprocess.run(
            [
                str(VBOX),
                "usbfilter",
                "remove",
                index,
                "--target",
                VM
            ],
            capture_output=True,
            text=True
        )

    return True


def mount_and_scan_usb(INFO):

    VID = None
    PID = None
    SERIAL = None

    # Get USB information from C
    for line in INFO.splitlines():

        line = line.strip()

        if line.startswith("VID:"):
            VID = line.split(
                ":",
                1
            )[1].strip()

        if line.startswith("PID:"):
            PID = line.split(
                ":",
                1
            )[1].strip()

        if line.startswith("Instance ID:"):
            instance_id = line.split(
                ":",
                1
            )[1].strip()

            SERIAL = instance_id.split("\\")[-1]

    print("USB INFORMATION")
    print("VID:", VID)
    print("PID:", PID)
    print("Serial:", SERIAL)

    # Reject bad / VirtualBox-generated device events
    if not VID or not PID or not SERIAL:
        print("Could not identify physical USB.")
        remove_auto_usb_filters()
        return False

    # --------------------------------
    # Create capture filter
    # --------------------------------

    print("Creating VirtualBox USB capture filter.")

    # Remove old filter if it exists
    subprocess.run(
        [
            str(VBOX),
            "usbfilter",
            "remove",
            "0",
            "--target",
            VM
        ],
        capture_output=True,
        text=True
    )

    filter_result = subprocess.run(
        [
            str(VBOX),
            "usbfilter",
            "add",
            "0",
            "--target",
            VM,
            "--name",
            "USB_Defence_Scan",
            "--active",
            "yes",
            "--vendorid",
            VID,
            "--productid",
            PID,
            "--serialnumber",
            SERIAL
        ],
        capture_output=True,
        text=True
    )

    if filter_result.returncode != 0:
        print("Could not create USB filter.")
        print(filter_result.stderr)
        return False

    print("USB capture filter created.")
    print("Waiting for USB to enter VM.")

    # --------------------------------
    # Wait for Ubuntu to see USB
    # --------------------------------

    usb_found = False

    for attempt in range(10):

        check = subprocess.run(
            ssh_command(
                "lsblk -nr -o NAME,RM,TYPE"
            ),
            capture_output=True,
            text=True
        )

        print(check.stdout)

        if "sdb" in check.stdout:
            usb_found = True
            break

        time.sleep(2)

    if usb_found is False:
        print("USB was not captured by VM.")

        subprocess.run(
            [
                str(VBOX),
                "usbfilter",
                "remove",
                "0",
                "--target",
                VM
            ],
            capture_output=True,
            text=True
        )

        return False

    print("USB captured by VM.")

    # --------------------------------
    # REMOVE CAPTURE FILTER NOW
    # --------------------------------
    #
    # The USB is already attached to the VM.
    # We no longer need the filter.
    #
    # Removing it here prevents VirtualBox
    # from automatically grabbing the USB
    # again after we detach it later.
    # --------------------------------

    remove_filter = subprocess.run(
        [
            str(VBOX),
            "usbfilter",
            "remove",
            "0",
            "--target",
            VM
        ],
        capture_output=True,
        text=True
    )

    if remove_filter.returncode != 0:
        print("Failed to remove USB capture filter.")
        print(remove_filter.stderr)
        return False

    print("USB capture filter removed.")
    print(
        "VirtualBox will not automatically recapture the USB."
    )

    # --------------------------------
    # Mount read-only
    # --------------------------------

    mount = subprocess.run(
        ssh_command(
            "sudo -n /usr/bin/mount "
            "-o ro,noexec,nosuid,nodev "
            "/dev/sdb1 /mnt/usb"
        ),
        capture_output=True,
        text=True
    )

    if mount.returncode != 0:
        print("USB failed to mount.")
        print(mount.stderr)
        return False

    print("USB mounted read-only.")
    print("Starting ClamAV scan.")

    # --------------------------------
    # ClamAV
    # --------------------------------

    result = subprocess.run(
        ssh_command(
            "clamscan -r /mnt/usb"
        ),
        capture_output=True,
        text=True
    )

    print(result.stdout)

    if result.stderr:
        print(result.stderr)

    # --------------------------------
    # Unmount from Ubuntu
    # --------------------------------

    unmount = subprocess.run(
        ssh_command(
            "sudo -n /usr/bin/umount /mnt/usb"
        ),
        capture_output=True,
        text=True
    )

    if unmount.returncode != 0:
        print("USB failed to unmount.")
        print(unmount.stderr)
        return False

    print("USB unmounted from Ubuntu.")

    # --------------------------------
    # Malware / scan error
    # --------------------------------

    if result.returncode == 1:
        print("MALWARE DETECTED")
        print(
            "USB WILL NOT BE RELEASED TO WINDOWS."
        )
        return False

    if result.returncode != 0:
        print("CLAMAV ERROR")
        print(
            "USB WILL NOT BE RELEASED TO WINDOWS."
        )
        return False

    print("USB PASSED CLAMAV.")

    # --------------------------------
    # Find USB attached to the VM
    # --------------------------------

    vm_info = subprocess.run(
        [
            str(VBOX),
            "showvminfo",
            VM
        ],
        capture_output=True,
        text=True
    )

    usb_address = None
    found_serial = False

    for line in vm_info.stdout.splitlines():

        line = line.strip()

        if SERIAL in line:
            found_serial = True

        if found_serial is True:
            if line.startswith("Address:"):
                usb_address = line.split(
                    ":",
                    1
                )[1].strip()

                break

    # --------------------------------
    # Release USB to Windows
    # --------------------------------

    if usb_address is None:
        print(
            "Could not find captured USB address."
        )

        print(
            "USB cannot safely be returned to Windows."
        )

        return False

    print(
        "Captured USB address:",
        usb_address
    )

    print(
        "Releasing USB back to Windows."
    )

    detach = subprocess.run(
        [
            str(VBOX),
            "controlvm",
            VM,
            "usbdetach",
            usb_address
        ],
        capture_output=True,
        text=True
    )

    if detach.returncode != 0:
        print(
            "VirtualBox failed to release USB."
        )

        print(
            detach.stderr
        )

        return False

    print("USB released from VM.")
    print(
        "Windows should now detect and mount the drive."
    )

    # Give Windows a moment to enumerate it
    time.sleep(2)

    return True


def start_vm():

    subprocess.run(
        [
            str(VBOX),
            "startvm",
            VM,
            "--type",
            "headless"
        ]
    )


def vm_running():

    result = subprocess.run(
        [
            str(VBOX),
            "list",
            "runningvms"
        ],
        capture_output=True,
        text=True
    )

    if VM in result.stdout:
        return True

    return False


startup_check()


if vm_running() is False:
    start_vm()


server = socket.socket(
    socket.AF_INET,
    socket.SOCK_STREAM
)

server.setsockopt(
    socket.SOL_SOCKET,
    socket.SO_EXCLUSIVEADDRUSE,
    1
)

server.bind(
    (
        "127.0.0.1",
        5050
    )
)

server.listen(1)

print("Listening on 5050...")


c_process = None


while True:

    # The current C++ detector only reliably handles one detection cycle.
    # Make sure the previous copy is gone before starting a fresh one.
    try:
        stop_c_detector(
            c_process
        )

    except Exception as Error:
        print(
            f"Could not close previous C++ detector: {Error}"
        )

    c_process = None

    try:
        c_process = start_c_detector()

    except Exception as Error:
        print(
            f"Could not start C++ detector: {Error}"
        )

        time.sleep(2)
        continue

    connection = None

    try:
        connection, address = server.accept()

        print(
            "CONNECTED:",
            address
        )

        data = connection.recv(
            4096
        )

        if not data:
            continue

        print(
            "RAW:",
            data
        )

        INFO = data.decode(
            "utf-8"
        )

        print(
            "TEXT:",
            INFO
        )

        if check_device(INFO) is True:
            continue

        if 'Storage' in INFO or 'Driver' in INFO:

            while vm_running() is False:
                print("Waiting for VM...")
                time.sleep(2)

            print("VM is running.")

            clean = mount_and_scan_usb(
                INFO
            )

            if clean is True:
                print("ALLOW")
                remove_auto_usb_filters()
                add_device(INFO)

            if clean is False:
                send_to_c(
                    'DENY'
                )

    except Exception as Error:
        print(
            f"USB Defender encountered an error: {Error}"
        )

    finally:
        if connection is not None:
            connection.close()

        try:
            stop_c_detector(
                c_process
            )

        except Exception as Error:
            print(
                f"Could not close C++ detector: {Error}"
            )

        c_process = None


# IT WORKS !!!!!!
# updated the program so the important file paths do not depend on where the EXE is launched from
# the whitelist database now lives in C ProgramData USB VM Defender instead of the current working folder
# the SSH private key also has one fixed install location under ProgramData Keys
# added a known hosts file under ProgramData so SSH does not depend on the logged in users normal SSH folder
# added an SSH helper so every Ubuntu command uses the same port user key and host checking settings
# BatchMode prevents SSH from hanging forever waiting for a password on a clean computer
# IdentitiesOnly makes SSH use the key installed specifically for USB Defender
# StrictHostKeyChecking accept-new lets the fresh install trust the new sandbox host on first connection
# added startup checks so missing VirtualBox SSH or the application key fails immediately instead of breaking halfway through a scan
# VirtualBox is now located using the Windows Program Files environment variable instead of assuming the launch folder
# Windows OpenSSH is found from PATH first and then from the standard System32 OpenSSH folder
# the database folder and key folder are created if they do not already exist
# changed the socket handling so incoming C connections always close even when the device is already whitelisted
# also made the C response socket close safely even if the connection or send fails
# kept the same basic security flow as the working version
# C detects the device and sends the information to Python on port 5050
# Python checks the whitelist and captures unknown storage devices into USB-Sandbox
# Ubuntu mounts the drive read only and ClamAV scans it
# clean devices are detached from VirtualBox and added to the whitelist
# bad devices or failed scans send DENY back to C on port 5000
# installer still needs to import the USB-Sandbox OVA set up NAT port 2222 to guest port 22
# and place usb_sandbox_key in C ProgramData USB VM Defender Keys before this program is started
# fresh install update also manages the C++ detector process directly
# BACKGROUND(C++).exe is expected beside the compiled Python EXE
# each watcher loop kills the previous tracked detector if it is somehow still alive
# then launches a fresh C++ detector before waiting for the next device event
# after that one event is handled the detector is closed in finally
# the next loop starts a completely fresh C++ detector
# only the detector process started by this Python program is terminated
# it does not use taskkill by image name so it will not kill unrelated processes

# fresh install update also manages the C++ detector process directly
# BACKGROUND(C++).exe is expected beside the compiled Python EXE
# each watcher loop closes the previous tracked detector if it is somehow still alive
# then launches a fresh C++ detector before waiting for the next device event
# after that one event is handled the detector is closed in finally
# the next loop starts a completely fresh C++ detector
# only the detector process started by this Python program is terminated
# it does not use taskkill by image name so it will not kill unrelated processes
