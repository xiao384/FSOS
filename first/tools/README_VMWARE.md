# VMware acceptance build

Do not use stale `output/*.vmdk` files as GUI acceptance images.

On Windows, from `first`:

```powershell
.\tools\build_vmware_clean.ps1
```

The script performs a clean x86-64 build with MicroPython and then runs the UEFI/VMware packer.
The packer now verifies that `kernel.exe` exports `sidebar_draw` whenever `user/sidebar.c` exists, so an old kernel cannot silently become the test VM.

Open:

```text
first\output\Auto\FSOS_UEFI.vmx
```

The VM uses UEFI, IDE disk, no USB/xHCI, serial logging, and 4 GiB RAM.
