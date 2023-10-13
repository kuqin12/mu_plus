/** @file
  This module produce main entry for BDS phase - BdsEntry.
  When this module was dispatched by DxeCore, gEfiBdsArchProtocolGuid will be installed
  which contains interface of BdsEntry.
  After DxeCore finish DXE phase, gEfiBdsArchProtocolGuid->BdsEntry will be invoked
  to enter BDS phase.

Copyright (c) 2004 - 2019, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016-2019 Hewlett Packard Enterprise Development LP<BR>
(C) Copyright 2015 Hewlett-Packard Development Company, L.P.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/


#include <Uefi.h>
#include <Guid/GlobalVariable.h>
#include <Guid/ConnectConInEvent.h>
#include <Guid/StatusCodeDataTypeVariable.h>
#include <Guid/EventGroup.h>

#include <Protocol/Bds.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/VariableLock.h>
#include <Protocol/DeferredImageLoad.h>

#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ReportStatusCodeLib.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/PerformanceLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>

#include <Library/UefiBootManagerLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/DeviceBootManagerLib.h>

#define SET_BOOT_OPTION_SUPPORT_KEY_COUNT(a, c)  { \
      (a) = ((a) & ~EFI_BOOT_OPTION_SUPPORT_COUNT) | (((c) << LowBitSet32 (EFI_BOOT_OPTION_SUPPORT_COUNT)) & EFI_BOOT_OPTION_SUPPORT_COUNT); \
      }

typedef enum {
  BdsCheckOsIndication,
  BdsPriorityBoot,
  BdsBootNext,
  BdsBootNormalPrepare,
  BdsBootNormal,
  BdsNormalEnd,
  BdsBootMenu,
  BdsBootCannotBoot,
  BdsBootMaxNum
} BDS_BOOT_STATE;

/**

  Service routine for BdsInstance->Entry(). Devices are connected, the
  consoles are initialized, and the boot options are tried.

  @param This            Protocol Instance structure.

**/
VOID
EFIAPI
BdsEntry (
  IN  EFI_BDS_ARCH_PROTOCOL  *This
  );

///
/// BDS arch protocol instance initial value.
///
EFI_BDS_ARCH_PROTOCOL  gBds = {
  BdsEntry
};

//
// gConnectConInEvent - Event which is signaled when ConIn connection is required
//
EFI_EVENT  gConnectConInEvent = NULL;

///
/// The read-only variables defined in UEFI Spec.
///
CHAR16  *mReadOnlyVariables[] = {
  EFI_PLATFORM_LANG_CODES_VARIABLE_NAME,
  EFI_LANG_CODES_VARIABLE_NAME,
  EFI_BOOT_OPTION_SUPPORT_VARIABLE_NAME,
  EFI_HW_ERR_REC_SUPPORT_VARIABLE_NAME,
  EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME
};

CHAR16  *mBdsLoadOptionName[] = {
  L"Driver",
  L"SysPrep",
  L"Boot",
  L"PlatformRecovery"
};

/**
  Event to Connect ConIn.

  @param  Event                 Event whose notification function is being invoked.
  @param  Context               Pointer to the notification function's context,
                                which is implementation-dependent.

**/
VOID
EFIAPI
BdsDxeOnConnectConInCallBack (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  // Inform platform of duties to perform before connecting consoles.   // MSCHANGE
  PlatformBootManagerOnDemandConInConnect ();                           // MSCHANGE

  //
  // When Osloader call ReadKeyStroke to signal this event
  // no driver dependency is assumed existing. So use a non-dispatch version
  //
  Status = EfiBootManagerConnectConsoleVariable (ConIn);
  if (EFI_ERROR (Status)) {
    //
    // Should not enter this case, if enter, the keyboard will not work.
    // May need platfrom policy to connect keyboard.
    //
    DEBUG ((DEBUG_WARN, "[Bds] Connect ConIn failed - %r!!!\n", Status));
  }
}

/**
  Notify function for event group EFI_EVENT_GROUP_READY_TO_BOOT. This is used to
  check whether there is remaining deferred load images.

  @param[in]  Event   The Event that is being processed.
  @param[in]  Context The Event Context.

**/
VOID
EFIAPI
CheckDeferredLoadImageOnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                        Status;
  EFI_DEFERRED_IMAGE_LOAD_PROTOCOL  *DeferredImage;
  UINTN                             HandleCount;
  EFI_HANDLE                        *Handles;
  UINTN                             Index;
  UINTN                             ImageIndex;
  EFI_DEVICE_PATH_PROTOCOL          *ImageDevicePath;
  VOID                              *Image;
  UINTN                             ImageSize;
  BOOLEAN                           BootOption;
  CHAR16                            *DevicePathStr;

  //
  // Find all the deferred image load protocols.
  //
  HandleCount = 0;
  Handles     = NULL;
  Status      = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gEfiDeferredImageLoadProtocolGuid,
                       NULL,
                       &HandleCount,
                       &Handles
                       );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDeferredImageLoadProtocolGuid, (VOID **)&DeferredImage);
    if (EFI_ERROR (Status)) {
      continue;
    }

    for (ImageIndex = 0; ; ImageIndex++) {
      //
      // Load all the deferred images in this protocol instance.
      //
      Status = DeferredImage->GetImageInfo (
                                DeferredImage,
                                ImageIndex,
                                &ImageDevicePath,
                                (VOID **)&Image,
                                &ImageSize,
                                &BootOption
                                );
      if (EFI_ERROR (Status)) {
        break;
      }

      DevicePathStr = ConvertDevicePathToText (ImageDevicePath, FALSE, FALSE);
      DEBUG ((DEBUG_LOAD, "[Bds] Image was deferred but not loaded: %s.\n", DevicePathStr));
      if (DevicePathStr != NULL) {
        FreePool (DevicePathStr);
      }
    }
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }
}

/**

  Install Boot Device Selection Protocol

  @param ImageHandle     The image handle.
  @param SystemTable     The system table.

  @retval  EFI_SUCEESS  BDS has finished initializing.
                        Return the dispatcher and recall BDS.Entry
  @retval  Other        Return status from AllocatePool() or gBS->InstallProtocolInterface

**/
EFI_STATUS
EFIAPI
BdsInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  //
  // Install protocol interface
  //
  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiBdsArchProtocolGuid,
                  &gBds,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG_CODE (
    EFI_EVENT   Event;
    //
    // Register notify function to check deferred images on ReadyToBoot Event.
    //
    Status = gBS->CreateEventEx (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    CheckDeferredLoadImageOnReadyToBoot,
                    NULL,
                    &gEfiEventReadyToBootGuid,
                    &Event
                    );
    ASSERT_EFI_ERROR (Status);
    );
  return Status;
}

/**
  The function will load and start every Driver####, SysPrep#### or PlatformRecovery####.

  @param  LoadOptions        Load option array.
  @param  LoadOptionCount    Load option count.
**/
VOID
ProcessLoadOptions (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *LoadOptions,
  IN UINTN                         LoadOptionCount
  )
{
  EFI_STATUS                         Status;
  UINTN                              Index;
  BOOLEAN                            ReconnectAll;
  EFI_BOOT_MANAGER_LOAD_OPTION_TYPE  LoadOptionType;

  ReconnectAll   = FALSE;
  LoadOptionType = LoadOptionTypeMax;

  //
  // Process the driver option
  //
  for (Index = 0; Index < LoadOptionCount; Index++) {
    //
    // All the load options in the array should be of the same type.
    //
    if (Index == 0) {
      LoadOptionType = LoadOptions[Index].OptionType;
    }

    ASSERT (LoadOptionType == LoadOptions[Index].OptionType);
    ASSERT (LoadOptionType != LoadOptionTypeBoot);

    Status = EfiBootManagerProcessLoadOption (&LoadOptions[Index]);

    //
    // Status indicates whether the load option is loaded and executed
    // LoadOptions[Index].Status is what the load option returns
    //
    if (!EFI_ERROR (Status)) {
      //
      // Stop processing if any PlatformRecovery#### returns success.
      //
      if ((LoadOptions[Index].Status == EFI_SUCCESS) &&
          (LoadOptionType == LoadOptionTypePlatformRecovery))
      {
        break;
      }

      //
      // Only set ReconnectAll flag when the load option executes successfully.
      //
      if (!EFI_ERROR (LoadOptions[Index].Status) &&
          ((LoadOptions[Index].Attributes & LOAD_OPTION_FORCE_RECONNECT) != 0))
      {
        ReconnectAll = TRUE;
      }
    }
  }

  //
  // If a driver load option is marked as LOAD_OPTION_FORCE_RECONNECT,
  // then all of the EFI drivers in the system will be disconnected and
  // reconnected after the last driver load option is processed.
  //
  if (ReconnectAll && (LoadOptionType == LoadOptionTypeDriver)) {
    EfiBootManagerDisconnectAll ();
    EfiBootManagerConnectAll ();
  }
}

/**

  Validate input console variable data.

  If found the device path is not a valid device path, remove the variable.

  @param VariableName             Input console variable name.

**/
VOID
BdsFormalizeConsoleVariable (
  IN  CHAR16  *VariableName
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  UINTN                     VariableSize;
  EFI_STATUS                Status;

  GetEfiGlobalVariable2 (VariableName, (VOID **)&DevicePath, &VariableSize);
  if ((DevicePath != NULL) && !IsDevicePathValid (DevicePath, VariableSize)) {
    Status = gRT->SetVariable (
                    VariableName,
                    &gEfiGlobalVariableGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                    0,
                    NULL
                    );
    //
    // Deleting variable with current variable implementation shouldn't fail.
    //
    ASSERT_EFI_ERROR (Status);
  }

  if (DevicePath != NULL) {
    FreePool (DevicePath);
  }
}

/**
  Formalize OsIndication related variables.

  For OsIndicationsSupported, Create a BS/RT/UINT64 variable to report caps
  Delete OsIndications variable if it is not NV/BS/RT UINT64.

  Item 3 is used to solve case when OS corrupts OsIndications. Here simply delete this NV variable.

  Create a boot option for BootManagerMenu if it hasn't been created yet

**/
VOID
BdsFormalizeOSIndicationVariable (
  VOID
  )
{
  EFI_STATUS                    Status;
  UINT64                        OsIndicationSupport;
  UINT64                        OsIndication;
  UINTN                         DataSize;
  UINT32                        Attributes;
  EFI_BOOT_MANAGER_LOAD_OPTION  BootManagerMenu;

  //
  // OS indicater support variable
  //
  Status = EfiBootManagerGetBootManagerMenu (&BootManagerMenu);
  if (Status != EFI_NOT_FOUND) {
    OsIndicationSupport = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    EfiBootManagerFreeLoadOption (&BootManagerMenu);
  } else {
    OsIndicationSupport = 0;
  }

  if (PcdGetBool (PcdPlatformRecoverySupport)) {
    OsIndicationSupport |= EFI_OS_INDICATIONS_START_PLATFORM_RECOVERY;
  }

  if (PcdGetBool (PcdCapsuleOnDiskSupport)) {
    OsIndicationSupport |= EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
  }

  Status = gRT->SetVariable (
                  EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (UINT64),
                  &OsIndicationSupport
                  );
  //
  // Platform needs to make sure setting volatile variable before calling 3rd party code shouldn't fail.
  //
  ASSERT_EFI_ERROR (Status);

  //
  // If OsIndications is invalid, remove it.
  // Invalid case
  //   1. Data size != UINT64
  //   2. OsIndication value inconsistence
  //   3. OsIndication attribute inconsistence
  //
  OsIndication = 0;
  Attributes   = 0;
  DataSize     = sizeof (UINT64);
  Status       = gRT->GetVariable (
                        EFI_OS_INDICATIONS_VARIABLE_NAME,
                        &gEfiGlobalVariableGuid,
                        &Attributes,
                        &DataSize,
                        &OsIndication
                        );
  if (Status == EFI_NOT_FOUND) {
    return;
  }

  if ((DataSize != sizeof (OsIndication)) ||
      ((OsIndication & ~OsIndicationSupport) != 0) ||
      (Attributes != (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE))
      )
  {
    DEBUG ((DEBUG_ERROR, "[Bds] Unformalized OsIndications variable exists. Delete it\n"));
    Status = gRT->SetVariable (
                    EFI_OS_INDICATIONS_VARIABLE_NAME,
                    &gEfiGlobalVariableGuid,
                    0,
                    0,
                    NULL
                    );
    //
    // Deleting variable with current variable implementation shouldn't fail.
    //
    ASSERT_EFI_ERROR (Status);
  }
}

/**

  Validate variables.

**/
VOID
BdsFormalizeEfiGlobalVariable (
  VOID
  )
{
  //
  // Validate Console variable.
  //
  BdsFormalizeConsoleVariable (EFI_CON_IN_VARIABLE_NAME);
  BdsFormalizeConsoleVariable (EFI_CON_OUT_VARIABLE_NAME);
  BdsFormalizeConsoleVariable (EFI_ERR_OUT_VARIABLE_NAME);

  //
  // Validate OSIndication related variable.
  //
  BdsFormalizeOSIndicationVariable ();
}

/**
  Set the HwErrRecSupport variable contains a binary UINT16 that supplies the
  level of support for Hardware Error Record Persistence that is implemented
  by the platform.

**/
VOID
InitializeHwErrRecSupport (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      HardwareErrorRecordLevel;

  HardwareErrorRecordLevel = PcdGet16 (PcdHardwareErrorRecordLevel);

  if (HardwareErrorRecordLevel != 0) {
    //
    // If level value equal 0, no need set to 0 to variable area because UEFI specification
    // define same behavior between no value or 0 value for L"HwErrRecSupport".
    //
    Status = gRT->SetVariable (
                    L"HwErrRecSupport",
                    &gEfiGlobalVariableGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                    sizeof (UINT16),
                    &HardwareErrorRecordLevel
                    );
    ASSERT_EFI_ERROR (Status);
  }
}

/**

  Service routine for BdsInstance->Entry(). Devices are connected, the
  consoles are initialized, and the boot options are tried.

  @param This             Protocol Instance structure.

**/
VOID
EFIAPI
BdsEntry (
  IN EFI_BDS_ARCH_PROTOCOL  *This
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *LoadOptions;
  UINTN                         LoadOptionCount;
  CHAR16                        *FirmwareVendor;
  UINT64                        OsIndication;
  UINTN                         DataSize;
  EFI_STATUS                    Status;
  UINT32                        BootOptionSupport;
  EDKII_VARIABLE_LOCK_PROTOCOL  *VariableLock;
  UINTN                         Index;
  EFI_BOOT_MANAGER_LOAD_OPTION  LoadOption;
  UINT16                        *BootNext;
  CHAR16                        BootNextVariableName[sizeof ("Boot####")];
  EFI_BOOT_MANAGER_LOAD_OPTION  BootManagerMenu;
  BOOLEAN                       BootFwUi;
  BOOLEAN                       PlatformRecovery;
  BOOLEAN                       BootSuccess;
  EFI_STATUS                    BootManagerMenuStatus;
  BDS_BOOT_STATE                BdsState;
  EFI_BOOT_MANAGER_LOAD_OPTION  BootOption;

  Status          = EFI_SUCCESS;
  BootSuccess     = FALSE;

  //
  // Insert the performance probe
  //
  PERF_CROSSMODULE_END ("DXE");
  PERF_CROSSMODULE_BEGIN ("BDS");
  DEBUG ((DEBUG_INFO, "[Bds] Entry...\n"));
  DeviceBootManagerBdsEntry ();

  //
  // Fill in FirmwareVendor and FirmwareRevision from PCDs
  //
  FirmwareVendor      = (CHAR16 *)PcdGetPtr (PcdFirmwareVendor);
  gST->FirmwareVendor = AllocateRuntimeCopyPool (StrSize (FirmwareVendor), FirmwareVendor);
  ASSERT (gST->FirmwareVendor != NULL);
  gST->FirmwareRevision = PcdGet32 (PcdFirmwareRevision);

  //
  // Fixup Tasble CRC after we updated Firmware Vendor and Revision
  //
  gST->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 ((VOID *)gST, sizeof (EFI_SYSTEM_TABLE), &gST->Hdr.CRC32);

  //
  // Validate Variable.
  //
  BdsFormalizeEfiGlobalVariable ();

  //
  // Mark the read-only variables if the Variable Lock protocol exists
  //
  Status = gBS->LocateProtocol (&gEdkiiVariableLockProtocolGuid, NULL, (VOID **)&VariableLock);
  DEBUG ((EFI_D_INFO, "[BdsDxe] Locate Variable Lock protocol - %r\n", Status));
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < ARRAY_SIZE (mReadOnlyVariables); Index++) {
      Status = VariableLock->RequestToLock (VariableLock, mReadOnlyVariables[Index], &gEfiGlobalVariableGuid);
      ASSERT_EFI_ERROR (Status);
    }
  }

  InitializeHwErrRecSupport ();

  //
  // Initialize L"BootOptionSupport" EFI global variable.
  //
  BootOptionSupport = EFI_BOOT_OPTION_SUPPORT_APP | EFI_BOOT_OPTION_SUPPORT_SYSPREP | EFI_BOOT_OPTION_SUPPORT_KEY;
  SET_BOOT_OPTION_SUPPORT_KEY_COUNT (BootOptionSupport, 3);

  Status = gRT->SetVariable (
                  EFI_BOOT_OPTION_SUPPORT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (BootOptionSupport),
                  &BootOptionSupport
                  );
  //
  // Platform needs to make sure setting volatile variable before calling 3rd party code shouldn't fail.
  //
  ASSERT_EFI_ERROR (Status);

  //
  // Cache the "BootNext" NV variable before calling any PlatformBootManagerLib APIs
  // This could avoid the "BootNext" set by PlatformBootManagerLib be consumed in this boot.
  //
  GetEfiGlobalVariable2 (EFI_BOOT_NEXT_VARIABLE_NAME, (VOID **)&BootNext, &DataSize);
  if (DataSize != sizeof (UINT16)) {
    if (BootNext != NULL) {
      FreePool (BootNext);
    }

    BootNext = NULL;
  }

  //
  // Report Status Code to indicate connecting drivers will happen
  //
  REPORT_STATUS_CODE (
    EFI_PROGRESS_CODE,
    (EFI_SOFTWARE_DXE_BS_DRIVER | EFI_SW_DXE_BS_PC_BEGIN_CONNECTING_DRIVERS)
    );

  //
  // Initialize ConnectConIn event before calling platform code.
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  BdsDxeOnConnectConInCallBack,
                  NULL,
                  &gConnectConInEventGuid,
                  &gConnectConInEvent
                  );
  if (EFI_ERROR (Status)) {
    gConnectConInEvent = NULL;
  }

  //
  // Do the platform init, can be customized by OEM/IBV
  // Possible things that can be done in PlatformBootManagerBeforeConsole:
  // > Update console variable: 1. include hot-plug devices; 2. Clear ConIn and add SOL for AMT
  // > Register new Driver#### or Boot####
  // > Register new Key####: e.g.: F12
  // > Signal ReadyToLock event
  // > Authentication action: 1. connect Auth devices; 2. Identify auto logon user.
  //
  PERF_INMODULE_BEGIN ("PlatformBootManagerBeforeConsole");
  PlatformBootManagerBeforeConsole ();
  PERF_INMODULE_END ("PlatformBootManagerBeforeConsole");

  //
  // Execute Driver Options
  //
  LoadOptions = EfiBootManagerGetLoadOptions (&LoadOptionCount, LoadOptionTypeDriver);
  if ((LoadOptionCount != 0) && (LoadOptions != NULL)) {
    ProcessLoadOptions (LoadOptions, LoadOptionCount);
    EfiBootManagerFreeLoadOptions (LoadOptions, LoadOptionCount);
  }

  //
  // Connect consoles
  //
  PERF_INMODULE_BEGIN ("EfiBootManagerConnectAllDefaultConsoles");
  EfiBootManagerConnectConsoleVariable (ConOut);
  EfiBootManagerConnectConsoleVariable (ErrOut);

  PERF_INMODULE_END ("EfiBootManagerConnectAllDefaultConsoles");

  //
  // Do the platform specific action after the console is ready
  // Possible things that can be done in PlatformBootManagerAfterConsole:
  // > Console post action:
  //   > Dynamically switch output mode from 100x31 to 80x25 for certain senarino
  //   > Signal console ready platform customized event
  // > Run diagnostics like memory testing
  // > Connect certain devices
  // > Dispatch aditional option roms
  // > Special boot: e.g.: USB boot, enter UI
  //
  PERF_INMODULE_BEGIN ("PlatformBootManagerAfterConsole");
  PlatformBootManagerAfterConsole ();
  PERF_INMODULE_END ("PlatformBootManagerAfterConsole");

  //
  // If any component set PcdTestKeyUsed to TRUE because use of a test key
  // was detected, then display a warning message on the debug log and the console
  //
  if (PcdGetBool (PcdTestKeyUsed)) {
    DEBUG ((DEBUG_ERROR, "**********************************\n"));
    DEBUG ((DEBUG_ERROR, "**  WARNING: Test Key is used.  **\n"));
    DEBUG ((DEBUG_ERROR, "**********************************\n"));
    Print (L"**  WARNING: Test Key is used.  **\n");
  }

  //
  // Boot to Boot Manager Menu when EFI_OS_INDICATIONS_BOOT_TO_FW_UI is set.
  //
  DataSize = sizeof (UINT64);
  Status   = gRT->GetVariable (
                    EFI_OS_INDICATIONS_VARIABLE_NAME,
                    &gEfiGlobalVariableGuid,
                    NULL,
                    &DataSize,
                    &OsIndication
                    );
  if (EFI_ERROR (Status)) {
    OsIndication = 0;
  }

  DEBUG_CODE_BEGIN ();
  EFI_BOOT_MANAGER_LOAD_OPTION_TYPE  LoadOptionType;

  DEBUG ((DEBUG_INFO, "[Bds]OsIndication: %016x\n", OsIndication));
  DEBUG ((DEBUG_INFO, "[Bds]=============Begin Load Options Dumping ...=============\n"));
  for (LoadOptionType = 0; LoadOptionType < LoadOptionTypeMax; LoadOptionType++) {
    DEBUG ((
      DEBUG_INFO,
      "  %s Options:\n",
      mBdsLoadOptionName[LoadOptionType]
      ));
    LoadOptions = EfiBootManagerGetLoadOptions (&LoadOptionCount, LoadOptionType);
    if ((LoadOptionCount != 0) && (LoadOptions != NULL)) {
      for (Index = 0; Index < LoadOptionCount; Index++) {
        DEBUG ((
          DEBUG_INFO,
          "    %s%04x: %s \t\t 0x%04x\n",
          mBdsLoadOptionName[LoadOptionType],
          LoadOptions[Index].OptionNumber,
          LoadOptions[Index].Description,
          LoadOptions[Index].Attributes
          ));
      }
    }

    EfiBootManagerFreeLoadOptions (LoadOptions, LoadOptionCount);
  }

  DEBUG ((DEBUG_INFO, "[Bds]=============End Load Options Dumping=============\n"));
  DEBUG_CODE_END ();

  //
  // BootManagerMenu doesn't contain the correct information when return status is EFI_NOT_FOUND.
  //
  BootManagerMenuStatus = EfiBootManagerGetBootManagerMenu (&BootManagerMenu);

  // Entering the BDS state machine...
  BdsState = BdsCheckOsIndication;
  while (TRUE) {
    switch (BdsState) {
      case BdsCheckOsIndication:
        BootFwUi         = (BOOLEAN)((OsIndication & EFI_OS_INDICATIONS_BOOT_TO_FW_UI) != 0);
        PlatformRecovery = (BOOLEAN)((OsIndication & EFI_OS_INDICATIONS_START_PLATFORM_RECOVERY) != 0);

        //
        // Clear EFI_OS_INDICATIONS_BOOT_TO_FW_UI to acknowledge OS
        //
        if (BootFwUi || PlatformRecovery) {
          OsIndication &= ~((UINT64)(EFI_OS_INDICATIONS_BOOT_TO_FW_UI | EFI_OS_INDICATIONS_START_PLATFORM_RECOVERY));
          Status        = gRT->SetVariable (
                                EFI_OS_INDICATIONS_VARIABLE_NAME,
                                &gEfiGlobalVariableGuid,
                                EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                                sizeof (UINT64),
                                &OsIndication
                                );
          //
          // Changing the content without increasing its size with current variable implementation shouldn't fail.
          //
          ASSERT_EFI_ERROR (Status);
        }

        if (BootFwUi) {
          //
          // Follow generic rule, Call BdsDxeOnConnectConInCallBack to connect ConIn before enter UI
          //
          BdsDxeOnConnectConInCallBack (NULL, NULL);

          BdsState = BdsBootMenu;
        } else if (PlatformRecovery) {
          BdsState = BdsBootCannotBoot;
        } else {
          BdsState = BdsPriorityBoot;
        }
        break;
      case BdsPriorityBoot:
        //
        // Execute SysPrep####
        //
        LoadOptions = EfiBootManagerGetLoadOptions (&LoadOptionCount, LoadOptionTypeSysPrep);
        if ((LoadOptionCount != 0) && (LoadOptions != NULL)) {
          ProcessLoadOptions (LoadOptions, LoadOptionCount);
          EfiBootManagerFreeLoadOptions (LoadOptions, LoadOptionCount);
        }

        Status = DeviceBootManagerPriorityBoot (&BootOption);

        //
        // Exit if nothing to process
        //
        if (EFI_NOT_FOUND == Status) {
          DEBUG ((DEBUG_INFO, "No Priority Boot option selected.\n"));
        } else if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "[Bds] Other errors detected, and unable to boot. Code=%r\n", Status));
        } else {
          // Attempt the priority boot option.
          EfiBootManagerBoot (&BootOption);
          Status = BootOption.Status;
          EfiBootManagerFreeLoadOption (&BootOption);

          //
          // If the priority boot option returns with a status of EFI_SUCCESS, and platform firmware supports boot manager
          // menu the boot manager will stop processing boot options here and present a boot manager menu to the user.
          //
          if (Status == EFI_SUCCESS) {
            BdsState = BdsBootMenu;
            break;
          }
        }

        // Otherwise, we process the boot next
        BdsState = BdsBootNext;
        break;
      case BdsBootNext:
        //
        // Delete "BootNext" NV variable before transferring control to it to prevent loops.
        //
        Status = gRT->SetVariable (
                        EFI_BOOT_NEXT_VARIABLE_NAME,
                        &gEfiGlobalVariableGuid,
                        0,
                        0,
                        NULL
                        );
        //
        // Deleting NV variable shouldn't fail unless it doesn't exist.
        //
        ASSERT (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND);

        //
        // Boot to "BootNext"
        //
        UnicodeSPrint (BootNextVariableName, sizeof (BootNextVariableName), L"Boot%04x", *BootNext);
        Status = EfiBootManagerVariableToLoadOption (BootNextVariableName, &LoadOption);
        if (!EFI_ERROR (Status)) {
          EfiBootManagerBoot (&LoadOption);
          DeviceBootManagerProcessBootCompletion (&LoadOption);        // MSCHANGE 00076 - record boot status
          EfiBootManagerFreeLoadOption (&LoadOption);
          if ((LoadOption.Status == EFI_SUCCESS) &&
              (BootManagerMenuStatus != EFI_NOT_FOUND) &&
              (LoadOption.OptionNumber != BootManagerMenu.OptionNumber))
          {
            //
            // Boot to Boot Manager Menu upon EFI_SUCCESS
            // Exception: Do not boot again when the BootNext points to Boot Manager Menu.
            //
            BdsState = BdsBootMenu;
          }
        }
        break;
      case BdsBootNormalPrepare:
        LoadOptions = EfiBootManagerGetLoadOptions (&LoadOptionCount, LoadOptionTypeBoot);
        if ((LoadOptionCount != 0) && (LoadOptions != NULL)) {
          BdsState = BdsBootNormal;
        } else if (!PcdGetBool (PcdSupportInfiniteBootRetries)) {
          BdsState = BdsBootCannotBoot;
        } else {
          // Just stay in this state...?
        }
        break;
      case BdsBootNormal:
        REPORT_STATUS_CODE (EFI_PROGRESS_CODE, (EFI_SOFTWARE_DXE_BS_DRIVER | EFI_SW_DXE_BS_PC_ATTEMPT_BOOT_ORDER_EVENT));

        //
        // According to EFI Specification, if a load option is not marked
        // as LOAD_OPTION_ACTIVE, the boot manager will not automatically
        // load the option.
        //
        if ((LoadOptions[Index].Attributes & LOAD_OPTION_ACTIVE) == 0) {
          // Do nothing
        }

        //
        // Boot#### load options with LOAD_OPTION_CATEGORY_APP are executables which are not
        // part of the normal boot processing. Boot options with reserved category values will be
        // ignored by the boot manager.
        //
        else if ((LoadOptions[Index].Attributes & LOAD_OPTION_CATEGORY) != LOAD_OPTION_CATEGORY_BOOT) {
          // Do nothing
        }

        //
        // All the driver options should have been processed since
        // now boot will be performed.
        //
        else {
          EfiBootManagerBoot (&LoadOptions[Index]);

          DeviceBootManagerProcessBootCompletion (&LoadOptions[Index]);

          // MU_CHANGE [BEGIN] - Support infinite boot retries
          //  Changes for PcdSupportInfiniteBootRetries are meant to minimize upkeep in mu repos.
          //   If/when upstreaming this change, refactoring calling loop in BdsEntry() would be
          //   better location.
          if (!PcdGetBool (PcdSupportInfiniteBootRetries) && (LoadOptions[Index].Status == EFI_SUCCESS)) {
            BdsState = BdsBootMenu;
            break;
          }
        }

        // Normal break out, increment the index and let the state machine handle the transition.
        Index ++;
        if (Index >= LoadOptionCount) {
          BdsState = BdsNormalEnd;
        } else {
          // Keep looping the reset of the boot options
        }
        break;
      case BdsNormalEnd:
          EfiBootManagerFreeLoadOptions (LoadOptions, LoadOptionCount);
          if (PcdGetBool (PcdSupportInfiniteBootRetries)) {
            BdsState = BdsBootNormalPrepare;
          }
          break;
      case BdsBootMenu:
        if (BootManagerMenuStatus != EFI_NOT_FOUND) {

          //
          // Directly enter the setup page.
          //
          EfiBootManagerBoot (&BootManagerMenu);
        }

        // If we ever get here, we failed to boot. Just let that case handle the error.
        BdsState = BdsBootCannotBoot;
        break;
      case BdsBootCannotBoot:
        // Fall through
      default:
        if (BootManagerMenuStatus != EFI_NOT_FOUND) {
          EfiBootManagerFreeLoadOption (&BootManagerMenu);
        }

        DEBUG ((DEBUG_ERROR, "[Bds] Unable to boot!\n"));
        DeviceBootManagerUnableToBoot ();

        CpuDeadLoop ();
    }
  }

  // Never get here
}
