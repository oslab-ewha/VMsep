# VMsep (VM-separated commit on jbd2/ext4)

- Nested ext4 filesystems on both guest and host machine can incur excessively frequent journal commits.
- VMsep can dramatically reduce write traffic by managing modified file blocks from each guest as a separate list and splitting the running transaction list into two sub-transactions.
- Filebench and IOzone benchmarks show that VMsep improves the I/O throughput by 19.5% on average and up to 64.2% over existing systems.

## Build

### Notes
- Build is tested on Windows 10 x64 and the projects are configured for this target by default.
- x86/x64 platforms should be supported. However, we don't have an x86 setup for testing at the moment.
- For Windows 7 users, change usbip\_stub and usbip\_vhci projects' Target OS version to Windows 7.
  - Right-click on the project > Properties > Driver Settings > Target OS version > Windows 7

### Build Tools
- Visual Studio 2019 Community(v142)
  - Build with VS 2017(v141) is also possible if platform toolset in setting is configured to v141
- Windows SDK 10.0.18362.0(recommended)
  - VS 2019(v142): requires &gt;= 10.0.18xxx
  - VS 2017(v141): requires &gt;= 10.0.17xxx
- Windows Driver Kit Windows 10, version 1903 (10.0.18362)
  - WDK 10.0.17134(1803), 10.0.17763(1809), 10.0.18346 are also tested

### Build Process
- Open usbip_win.sln
- If VS 2017 is used, SDK version for userspace projects(usbip, usbip_common, usbipd, stubctl) should be adjusted.
- Set certificate driver signing for usbip\_stub and usbip\_vhci projects.
  - Right-click on the project > Properties > Driver Signing > Test Certificate
  - Browse to driver/usbip\_test.pfx
- Build solution or desired project.

- All output files are created under {Debug,Release}/{x64,x86} folder

## Install

### Windows USB/IP server

- Prepare a linux machine as a USB/IP client (or windows usbip-win vhci client)  
  - Tested on Ubuntu 16.04
  - Kernel 4.15.0-29 (USB/IP kernel module crash was observed on some other version)
  - `# modprobe vhci-hcd`
- Install USB/IP test certificate
  - Install `driver/usbip_test.pfx` (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) and
    2. "Trusted Publishers" in "Local Computer" (not current user)
- Enable test signing
  - `> bcdedit.exe /set TESTSIGNING ON`
  - reboot the system to apply
- Copy `usbip.exe`, `usbipd.exe`, `usb.ids`, `usbip_stub.sys`, `usbip_stub.inx` into a folder in target machine
  - You can find `usbip.exe`, `usbipd.exe`, `usbip_stub.sys` in output folder after build or on [release](https://github.com/cezanne/usbip-win/releases) page.
  - `userspace/usb.ids`
  - `driver/stub/usbip_stub.inx`
- Find USB device id
  - You can get device id from usbip listing
    - `> usbip.exe list -l`
  - Bus id is always 1. So output from `usbip.exe` listing is shown as:
```
usbip.exe list -l
 - busid 1-59 (045e:00cb)
   Microsoft Corp. : Basic Optical Mouse v2.0 (045e:00cb)
 - busid 1-30 (80ee:0021)
   VirtualBox : USB Tablet (80ee:0021)
```
- Bind USB device to usbip stub
  - This command replaces an existing function driver with usbip stub driver
    - This should be executed using administrator privilege
    - `usbip_stub.inx` and `usbip_stub.sys` files should be in the same folder as `usbip.exe`
  - `> usbip.exe bind -b 1-59`
- Run `usbipd.exe`
  - `> usbipd.exe -d -4`
	- TCP port `3240` should be allowed by firewall
- Attach USB/IP device on linux machine
  - `# usbip attach -r <usbip server ip> -b 1-59`

### Windows USB/IP client

- Prepare a linux machine as a USB/IP server (or windows usbip-win stub server)  
  - tested on Ubuntu 16.04 (Kernerl 4.15.0-29)
  - `# modprobe usbip-host`
  - You can use virtual [usbip-vstub](https://github.com/cezanne/usbip-vstub) as a stub server
- Run usbipd on a USB/IP server (Linux)
  - `# usbipd -4 -d`
- Install USB/IP test certificate
  - Install `driver/usbip_test.pfx` (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) and
    2. "Trusted Publishers" in "Local Computer" (not current user)
- Enable test signing
  - `> bcdedit.exe /set TESTSIGNING ON`
  - reboot the system to apply
- Copy vhci driver files into a folder in target machine
  - Currently, there are 2 versions for a vhci driver: vhci(old) and vhci(ude)
  - vhci(ude) is newly developed to fully support USB applications using MS UDE framework.
  - If you're testing vhci(ude), copy `usbip.exe`, `usbip_vhci_udf.sys`, `usbip_vhci_udf.inf`, `usbip_vhci_udf.cat` into a folder in target machine
  - If you're testing vhci(old), copy `usbip.exe`, `usbip_vhci.sys`, `usbip_vhci.inf`, `usbip_root.inf`, `usbip_vhci.cat` into a folder in target machine
  - You can find all files in output folder after build or on [release](https://github.com/cezanne/usbip-win/releases) page.
- Install USB/IP VHCI driver
  - You can install using usbip.exe or manually
  - Using usbip.exe install command
     - Run PowerShell or CMD as an Administrator
     - if using vhci(ude), `PS> usbip.exe install_ude`
     - if using vhci(old), `PS> usbip.exe install`
  - Install manually(new ude vhci)
     - Run PowerShell or CMD as an Administrator
     - `PS> pnputil /add-driver usbip_vhci_ude.inf`
     - Start Device manager
     - Choose "Add Legacy Hardware" from the "Action" menu.
     - Select "Install the hardware that I manually select from the list".
     - Click "Next".
     - Click "Have Disk", click "Browse", choose the copied folder, and click "OK".
     - Click on the "usbip-win VHCI(ude)", and then click "Next".
     - Click Finish at "Completing the Add/Remove Hardware Wizard".	 
  - Install manually(old vhci)
     - Run PowerShell or CMD as an Administrator
     - `PS> pnputil /add-driver usbip_vhci.inf`
     - Start Device manager
     - Choose "Add Legacy Hardware" from the "Action" menu.
     - Select "Install the hardware that I manually select from the list".
     - Click "Next".
     - Click "Have Disk", click "Browse", choose the copied folder, and click "OK".
     - Click on the "USB/IP VHCI Root", and then click "Next".
     - Click Finish at "Completing the Add/Remove Hardware Wizard".
- Attach a remote USB device
  - if using vhci(ude), `> usbip.exe attach_ude -r <usbip server ip> -b 2-2`
  - if using vhci(old), `> usbip.exe attach -r <usbip server ip> -b 2-2`
- Uninstall driver
  - if using vhci(ude), `PS> usbip.exe uninstall_ude`
  - if using vhci(old),`PS> usbip.exe uninstall`
- Disable test signing
  - `> bcdedit.exe /set TESTSIGNING OFF`
  - reboot the system to apply

### Reporting Bug
- usbip-win is not yet ready for production use. We could find problems with more detailed logs.

#### How to get windows kernel log
- Set registry key to enable a debug filter
  - usbip-win uses [DbgPrintEx API for kernel logging](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/reading-and-filtering-debugging-messages).
  - save following as .reg and run or manually insert a registry key
  - restart is required
```
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter]
"IHVDRIVER"=dword:ffffffff
```
- Run a debugging log viewer program before you test
  - [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview) is a good tool to view the logs
  
- If your testing machine suffer from BSOD (blue screen on death), you should get it via remote debugging.
  - WinDbg on virtual machines would be good to get logs

#### How to get usbip forwarder log
- usbip-win transmits usbip packets via a userland forwarder.
  - forwarder log is the best to look into usbip packet internals. 
- edit `usbip_forward.c` to define `DEBUG_PDU` at the head of the file
- compile `usbip.exe` or `usbipd.exe`
- `debug_pdu.log` is created at the path where an executable runs.  

#### How to get linux kernel log
- Sometimes linux kernel log is required

```
\# dmesg --follow | tee kernel_log.txt
```

<hr>
<sub>This project was supported by Basic Science Research Program through the National Research Foundation of Korea(NRF) funded by the Ministry of Education(2016R1A6A3A11930295).</sub>
