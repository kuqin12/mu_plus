/** @file
  Header file for BDS Platform specific code

Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _BDS_PLATFORM_H
#define _BDS_PLATFORM_H

/**
  Platform Bds init. Include the platform firmware vendor, revision
  and so crc check.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  );

/**
  The function will execute with as the platform policy, current policy
  is driven by boot mode. IBV/OEM can customize this code for their specific
  policy action.

  @param DriverOptionList - The header of the driver option link list
  @param BootOptionList   - The header of the boot option link list
  @param ProcessCapsules  - A pointer to ProcessCapsules()
  @param BaseMemoryTest   - A pointer to BaseMemoryTest()
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  );

extern EFI_BOOT_MODE                 mBootMode;
extern EFI_DEVICE_PATH_PROTOCOL      **mPlatformConnectSequence;

#endif
