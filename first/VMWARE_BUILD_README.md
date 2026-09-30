# FSOS VMware clean build

The repository now refuses to package a VMware image when `user/sidebar.c` exists but the built `kernel.exe` does not export `sidebar_draw`. This prevents a stale-kernel VM from being mistaken for a current GUI build.

On Windows with the required MinGW/NASM/Python/qemu-img tools:

```powershell
cd first
.\tools\build_vmware_clean.ps1
```

The resulting clean UEFI VMware files are written to `first\output\Auto`.

Important: the checked-in `output` binaries in this development environment are not guaranteed to reflect the latest source changes. Rebuilding on the target machine is therefore required before using the VMware image as a GUI acceptance build.
