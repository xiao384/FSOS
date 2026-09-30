// power.h - ACPI system power controls
#ifndef FSOS_POWER_H
#define FSOS_POWER_H

// Return 0 when an ACPI/known-hypervisor operation was issued, nonzero when unavailable.
int poweroff_system(void);
int reboot_system(void);

#endif
