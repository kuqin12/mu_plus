/** @file
  This file include all platform action which can be customized by IBV/OEM.

Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "BdsPlatform.h"

static EFI_BOOT_MODE                 mBootMode;
static EFI_DEVICE_PATH_PROTOCOL      **mPlatformConnectSequence;
static USB_CLASS_FORMAT_DEVICE_PATH  mUsbClassKeyboardDevicePath = {
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_USB_CLASS_DP,
      {
        (UINT8)(sizeof (USB_CLASS_DEVICE_PATH)),
        (UINT8)((sizeof (USB_CLASS_DEVICE_PATH)) >> 8)
      }
    },
    0xffff,           // VendorId
    0xffff,           // ProductId
    CLASS_HID,        // DeviceClass
    SUBCLASS_BOOT,    // DeviceSubClass
    PROTOCOL_KEYBOARD // DeviceProtocol
  },
  gEndEntire
};

VOID
ExitPmAuth (
  VOID
  )
{
  EFI_HANDLE  Handle;
  EFI_STATUS  Status;

  PERF_FUNCTION_BEGIN (); // MS_CHANGE

  DEBUG ((DEBUG_INFO, "ExitPmAuth ()- Start\n"));

  //
  // Since PI1.2.1, we need signal EndOfDxe as ExitPmAuth
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  DEBUG ((DEBUG_INFO, "All EndOfDxe callbacks have returned successfully\n"));

  //
  // NOTE: We need install DxeSmmReadyToLock directly here because many boot script is added via ExitPmAuth/EndOfDxe callback.
  // If we install them at same callback, these boot script will be rejected because BootScript Driver runs first to lock them done.
  // So we separate them to be 2 different events, ExitPmAuth is last chance to let platform add boot script. DxeSmmReadyToLock will
  // make boot script save driver lock down the interface.
  //
  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEfiDxeSmmReadyToLockProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);
  DEBUG ((DEBUG_INFO, "ExitPmAuth ()- End\n"));

  PERF_FUNCTION_END (); // MS_CHANGE
}

VOID
ConnectRootBridge (
  BOOLEAN  Recursive
  )
{
  UINTN       RootBridgeHandleCount;
  EFI_HANDLE  *RootBridgeHandleBuffer;
  UINTN       RootBridgeIndex;

  PERF_FUNCTION_BEGIN (); // MS_CHANGE

  RootBridgeHandleCount = 0;
  gBS->LocateHandleBuffer (
         ByProtocol,
         &gEfiPciRootBridgeIoProtocolGuid,
         NULL,
         &RootBridgeHandleCount,
         &RootBridgeHandleBuffer
         );
  for (RootBridgeIndex = 0; RootBridgeIndex < RootBridgeHandleCount; RootBridgeIndex++) {
    gBS->ConnectController (RootBridgeHandleBuffer[RootBridgeIndex], NULL, NULL, Recursive);
  }

  PERF_FUNCTION_END (); // MS_CHANGE
}

BOOLEAN
IsGopDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  while (!IsDevicePathEndType (DevicePath)) {
    if ((DevicePathType (DevicePath) == ACPI_DEVICE_PATH) &&
        (DevicePathSubType (DevicePath) == ACPI_ADR_DP))
    {
      return TRUE;
    }

    DevicePath = NextDevicePathNode (DevicePath);
  }

  return FALSE;
}

/**
  Remove all GOP device path instance from DevicePath and add the GOP to the DevicePath.
**/
EFI_DEVICE_PATH_PROTOCOL *
UpdateGopDevicePath (
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  EFI_DEVICE_PATH_PROTOCOL  *Gop
  )
{
  UINTN                     Size;
  UINTN                     GopSize;
  EFI_DEVICE_PATH_PROTOCOL  *Temp;
  EFI_DEVICE_PATH_PROTOCOL  *Return;
  EFI_DEVICE_PATH_PROTOCOL  *Instance;
  BOOLEAN                   Exist;

  Exist   = FALSE;
  Return  = NULL;
  GopSize = GetDevicePathSize (Gop);
  do {
    Instance = GetNextDevicePathInstance (&DevicePath, &Size);
    if (Instance == NULL) {
      break;
    }

    if (!IsGopDevicePath (Instance) ||
        ((Size == GopSize) && (CompareMem (Instance, Gop, GopSize) == 0))
        )
    {
      if ((Size == GopSize) && (CompareMem (Instance, Gop, GopSize) == 0)) {
        Exist = TRUE;
      }

      Temp   = Return;
      Return = AppendDevicePathInstance (Return, Instance);
      if (Temp != NULL) {
        FreePool (Temp);
      }
    }

    FreePool (Instance);
  } while (DevicePath != NULL);

  if (!Exist) {
    // NOTE: Return MAY be NULL, and is proper if it is NULL
    Temp   = Return;
    Return = AppendDevicePathInstance (Return, Gop);
    if (Temp != NULL) {
      FreePool (Temp);
    }
  }

  return Return;
}

/**
  Platform Bds init. Include the platform firmware vendor, revision
  and so crc check.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_DEVICE_PATH_PROTOCOL   *TempDevicePath;
  EFI_DEVICE_PATH_PROTOCOL   *ConsoleOut;
  EFI_DEVICE_PATH_PROTOCOL   *Temp;
  EFI_HANDLE                 Handle;
  BDS_CONSOLE_CONNECT_ENTRY  *PlatformConsoles;

  mBootMode = GetBootModeHob ();  // BeforeConsole has to be called before AfterConsole.

  //
  // Append Usb Keyboard short form DevicePath into "ConIn"
  //
  EfiBootManagerUpdateConsoleVariable (
    ConIn,
    (EFI_DEVICE_PATH_PROTOCOL *)&mUsbClassKeyboardDevicePath,
    NULL
    );

  //
  // Connect Root Bridge to make PCI BAR resource allocated and all PciIo created
  //
  ConnectRootBridge (FALSE);

  TempDevicePath = NULL;
  Handle         = DeviceBootManagerBeforeConsole (&TempDevicePath, &PlatformConsoles);

  //
  // Update ConOut variable according to the Console Handle
  //
  ConsoleOut = NULL;
  GetEfiGlobalVariable2 (L"ConOut", (VOID **)&ConsoleOut, NULL);

  if (Handle != NULL) {
    if (TempDevicePath != NULL) {
      Temp       = ConsoleOut;
      ConsoleOut = UpdateGopDevicePath (ConsoleOut, TempDevicePath);
      if (Temp != NULL) {
        FreePool (Temp);
      }

      FreePool (TempDevicePath);
      if (ConsoleOut != NULL) {
        Status = gRT->SetVariable (
                        L"ConOut",
                        &gEfiGlobalVariableGuid,
                        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                        GetDevicePathSize (ConsoleOut),
                        ConsoleOut
                        );
        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "%a: Error setting ConOut. Code = %r\n", __FUNCTION__, Status));
        }
      }
    }
  }

  if (ConsoleOut != NULL) {
    FreePool (ConsoleOut);
  }

  if (PlatformConsoles != NULL) {
    while (PlatformConsoles->DevicePath != NULL) {
      //
      // Update the console variable with the connect type
      //
      if ((PlatformConsoles->ConnectType & CONSOLE_IN) == CONSOLE_IN) {
        EfiBootManagerUpdateConsoleVariable (ConIn, PlatformConsoles->DevicePath, NULL);
      }

      if ((PlatformConsoles->ConnectType & CONSOLE_OUT) == CONSOLE_OUT) {
        EfiBootManagerUpdateConsoleVariable (ConOut, PlatformConsoles->DevicePath, NULL);
      }

      if ((PlatformConsoles->ConnectType & STD_ERROR) == STD_ERROR) {
        EfiBootManagerUpdateConsoleVariable (ErrOut, PlatformConsoles->DevicePath, NULL);
      }

      PlatformConsoles++;
    }
  }

  //
  // Exit PM auth before Legacy OPROM run.
  //

  ExitPmAuth ();

  //
  // Dispatch the deferred 3rd party images.
  //
  EfiBootManagerDispatchDeferredImages ();
}

VOID
ConnectSequence (
  VOID
  )
{
  EFI_HANDLE                DeviceHandle;
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  **PlatformConnectSequence;

  PERF_FUNCTION_BEGIN (); // MS_CHANGE

  //
  // Here we can get the customized platform connect sequence
  // Notes: we can connect with new variable which record the
  // last time boots connect device path sequence
  //
  PlatformConnectSequence = mPlatformConnectSequence;
  if (PlatformConnectSequence != NULL) {
    while (*PlatformConnectSequence != NULL) {
      //
      // Build the platform boot option
      //
      Status = EfiBootManagerConnectDevicePath (*PlatformConnectSequence, &DeviceHandle);
      if (!EFI_ERROR (Status)) {
        gBS->ConnectController (DeviceHandle, NULL, NULL, TRUE);
      }

      PlatformConnectSequence++;
    }
  }

  //
  // Dispatch again since Switchable Graphics driver depends on PCI_IO protocol
  //
  gDS->Dispatch ();

  PERF_FUNCTION_END (); // MS_CHANGE
}

STATIC
EFI_STATUS
SetMorControl (
  VOID
  )
{
  UINT8       MorControl;
  UINTN       VariableSize;
  EFI_STATUS  Status;

  VariableSize = sizeof (MorControl);
  MorControl   = 1;

  Status = gRT->SetVariable (
                  MEMORY_OVERWRITE_REQUEST_VARIABLE_NAME,
                  &gEfiMemoryOverwriteControlDataGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  VariableSize,
                  &MorControl
                  );

  return Status;
}
