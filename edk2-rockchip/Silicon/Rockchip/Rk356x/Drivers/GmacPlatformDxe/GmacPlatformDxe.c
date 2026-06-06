/** @file
 *
 *  RK3588 GMAC initializer
 *
 *  Copyright (c) 2021-2022, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseCryptLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/NetLib.h>
#include <Library/OtpLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DwcEqosPlatformDevice.h>
#include <IndustryStandard/Rk356x.h>

#include "EthernetPhy.h"

/* GMAC registers */
#define  GMAC_MAC_MDIO_ADDRESS              0x0200
#define   GMAC_MAC_MDIO_ADDRESS_PA_SHIFT    21
#define   GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT   16
#define   GMAC_MAC_MDIO_ADDRESS_CR_SHIFT    8
#define   GMAC_MAC_MDIO_ADDRESS_CR_100_150  (1U << GMAC_MAC_MDIO_ADDRESS_CR_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT   2
#define   GMAC_MAC_MDIO_ADDRESS_GOC_READ    (3U << GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GOC_WRITE   (1U << GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GB          BIT0
#define  GMAC_MAC_MDIO_DATA                 0x0204

/* MII registers */
#define MII_PHYIDR1  0x02
#define MII_PHYIDR2  0x03

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          VendorDP;
  UINT8                       ControllerId;
  MAC_ADDR_DEVICE_PATH        MacAddrDP;
  EFI_DEVICE_PATH_PROTOCOL    End;
} GMAC_DEVICE_PATH;
#pragma pack ()

typedef struct {
  UINT32                               Signature;
  UINT8                                Id;
  EFI_PHYSICAL_ADDRESS                 BaseAddress;

  BOOLEAN                              Supported;
#if 0 // NOTYET
  UINT8                                TxDelay;
  UINT8                                RxDelay;
#endif

  DWC_EQOS_PLATFORM_DEVICE_PROTOCOL    EqosPlatform;
  GMAC_DEVICE_PATH                     DevicePath;
} GMAC_DEVICE;

#define GMAC_DEVICE_SIGNATURE  SIGNATURE_32 ('G', 'M', 'a', 'C')

#define GMAC_DEVICE_FROM_EQOS_PLATFORM(a) \
  CR (a, GMAC_DEVICE, EqosPlatform, GMAC_DEVICE_SIGNATURE)

#define GMAC_DEVICE_INIT(_Id, _BaseAddress)                     \
  {                                                             \
    .Signature    = GMAC_DEVICE_SIGNATURE,                      \
    .Id           = _Id,                                        \
    .BaseAddress  = _BaseAddress,                               \
    .Supported    = FixedPcdGet8(PcdMac##_Id##Status) == 0xF,   \
  }

STATIC GMAC_DEVICE_PATH  mGmacDevicePathTemplate = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(OFFSET_OF (GMAC_DEVICE_PATH, MacAddrDP)),
        (UINT8)((OFFSET_OF (GMAC_DEVICE_PATH, MacAddrDP)) >> 8)
      }
    },
    EFI_CALLER_ID_GUID
  },
  0,
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_MAC_ADDR_DP,
      {
        (UINT8)(sizeof (MAC_ADDR_DEVICE_PATH)),
        (UINT8)((sizeof (MAC_ADDR_DEVICE_PATH)) >> 8)
      }
    },
    {
      { 0 }
    },
    NET_IFTYPE_ETHERNET
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      sizeof (EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};

STATIC GMAC_DEVICE  mGmacDevices[] = {
  GMAC_DEVICE_INIT (0, GMAC0_BASE),
  GMAC_DEVICE_INIT (1, GMAC1_BASE),
};

#if 0
//
// XXX: It may be tempting to move all this PHY code to the MAC driver,
// however it is too platform specific and would make the driver less
// generic, as it does not currently need to touch any MII registers in
// order to work -- assuming the PHY has been previously initialized or
// is already usable in its default state.
//
// The proper way to fix this would be to have an MDIO bus/device protocol
// and separate PHY device drivers, including support for additional platform
// data.
//
// It is also important to leave the PHY configured for ACPI OSes that don't
// support initializing it (ESXi, Windows), regardless of whether UEFI uses
// the network interface (see how the MAC address is programmed in this case).
//
STATIC ETHERNET_PHY_INIT  mPhyInitList[] = {
  RealtekPhyInit,
  MotorcommPhyInit
};
#endif

VOID
PhyRead (
  IN  EFI_PHYSICAL_ADDRESS  GmacBase,
  IN  UINT8                 Phy,
  IN  UINT16                Reg,
  OUT UINT16                *Value
  )
{
  UINT32  Addr;
  UINTN   Retry;

  Addr = GMAC_MAC_MDIO_ADDRESS_CR_100_150 |
         (Phy << GMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
         (Reg << GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT) |
         GMAC_MAC_MDIO_ADDRESS_GOC_READ |
         GMAC_MAC_MDIO_ADDRESS_GB;
  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_ADDRESS, Addr);

  MicroSecondDelay (10000);

  for (Retry = 1000; Retry > 0; Retry--) {
    Addr = MmioRead32 (GmacBase + GMAC_MAC_MDIO_ADDRESS);
    if ((Addr & GMAC_MAC_MDIO_ADDRESS_GB) == 0) {
      *Value = MmioRead32 (GmacBase + GMAC_MAC_MDIO_DATA) & 0xFFFFu;
      break;
    }

    MicroSecondDelay (10);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "MDIO: PHY read timeout!\n"));
    *Value = 0xFFFFU;
    ASSERT (FALSE);
  }
}

VOID
PhyWrite (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT8                 Phy,
  IN UINT16                Reg,
  IN UINT16                Value
  )
{
  UINT32  Addr;
  UINTN   Retry;

  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_DATA, Value);

  Addr = GMAC_MAC_MDIO_ADDRESS_CR_100_150 |
         (Phy << GMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
         (Reg << GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT) |
         GMAC_MAC_MDIO_ADDRESS_GOC_WRITE |
         GMAC_MAC_MDIO_ADDRESS_GB;
  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_ADDRESS, Addr);

  MicroSecondDelay (10000);

  for (Retry = 1000; Retry > 0; Retry--) {
    Addr = MmioRead32 (GmacBase + GMAC_MAC_MDIO_ADDRESS);
    if ((Addr & GMAC_MAC_MDIO_ADDRESS_GB) == 0) {
      break;
    }

    MicroSecondDelay (10);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "MDIO: PHY write timeout!\n"));
    ASSERT (FALSE);
  }
}

#if 0 // NOTYET
STATIC
VOID
EFIAPI
PhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase
  )
{
  EFI_STATUS  Status;
  UINT16      PhyIdReg;
  UINT32      PhyId;
  UINT32      Index;

  PhyRead (GmacBase, 0, MII_PHYIDR1, &PhyIdReg);
  PhyId = PhyIdReg << 16;
  PhyRead (GmacBase, 0, MII_PHYIDR2, &PhyIdReg);
  PhyId |= PhyIdReg;

  for (Index = 0; Index < ARRAY_SIZE (mPhyInitList); Index++) {
    Status = mPhyInitList[Index](GmacBase, PhyId);
    if (Status == EFI_UNSUPPORTED) {
      continue;
    } else if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: mPhyInitList[%d]() failed: %r\n", __func__, Index, Status));
    }

    return;
  }

  DEBUG ((DEBUG_ERROR, "%a: Unknown PHY ID %08X\n", __func__, PhyId));
}

STATIC
VOID
GmacSetToRgmii (
  IN UINT32  Id,
  IN UINT8   TxDelay,
  IN UINT8   RxDelay
  )
{
  // Currently done in BoardInitDxe
}

STATIC
VOID
GmacSetToRmii (
  IN UINT32  Id
  )
{
  // Currently done in BoardInitDxe
}

STATIC
VOID
GmacSetClockSelectFromIo (
  IN UINT32   Id,
  IN BOOLEAN  Enable
  )
{
  // Currently done in BoardInitDxe
}

STATIC
EFI_STATUS
EFIAPI
GmacSetTxClockSpeed (
  IN UINT32   Id,
  IN BOOLEAN  RgmiiMode,
  IN UINT32   Speed
  )
{
  // Currently done in BoardInitDxe
  return EFI_SUCCESS;
}
#endif

STATIC
VOID
EFIAPI
GmacPlatformGetConfig (
  IN DWC_EQOS_PLATFORM_DEVICE_PROTOCOL  *This,
  IN DWC_EQOS_CONFIG                    *Config
  )
{
  Config->CsrClockRate  = 125000000;
  Config->AxiBusWidth   = EqosAxiBusWidth64;
  Config->AxiFixedBurst = FALSE;
  Config->AxiMixedBurst = TRUE;
  Config->AxiWrOsrLmt   = 4;
  Config->AxiRdOsrLmt   = 8;
  Config->AxiBlen       = EqosAxiBlen16 | EqosAxiBlen8 | EqosAxiBlen4;
}

STATIC
EFI_STATUS
EFIAPI
GmacPlatformSetInterfaceSpeed (
  IN DWC_EQOS_PLATFORM_DEVICE_PROTOCOL  *This,
  IN UINT32                             Speed
  )
{
#if 0 // NOTYET
  GMAC_DEVICE  *Gmac = GMAC_DEVICE_FROM_EQOS_PLATFORM (This);

  return GmacSetTxClockSpeed (Gmac->Id, TRUE, Speed);
#else
  return EFI_SUCCESS;
#endif
}

STATIC
VOID
GmacGetOtpMacAddress (
  OUT EFI_MAC_ADDRESS  *MacAddress
  )
{
  UINT8  OtpData[32];
  UINT8  Hash[SHA256_DIGEST_SIZE];

  /* Generate MAC addresses from the first 32 bytes in the OTP */
  OtpRead (0x00, sizeof (OtpData), OtpData);
  Sha256HashAll (OtpData, sizeof (OtpData), Hash);

  /* Clear multicast bit, set locally administered bit. */
  Hash[0] &= 0xFE;
  Hash[0] |= 0x02;

  /* ... and for compatibility with old drivers (see https://github.com/jaredmcneill/quartz64_uefi/pull/68) */
  Hash[3] &= 0xFE;
  Hash[3] |= 0x02;

  ZeroMem (MacAddress, sizeof (EFI_MAC_ADDRESS));
  CopyMem (MacAddress, Hash, NET_ETHER_ADDR_LEN);
}

EFI_STATUS
EFIAPI
GmacPlatformDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS       Status;
  EFI_MAC_ADDRESS  MacAddress;
  UINT32           Index;
  GMAC_DEVICE      *Gmac;
  EFI_HANDLE       Handle;
  UINTN            Count = 0;

  GmacGetOtpMacAddress (&MacAddress);

  for (Index = 0; Index < ARRAY_SIZE (mGmacDevices); Index++) {
    Gmac = &mGmacDevices[Index];
    if (!Gmac->Supported) {
      continue;
    }

  #if 0 // NOTYET
    /* Configure pins */
    GmacIomux (Gmac->Id);

    /* Setup clocks and delays */
    GmacSetClockSelectFromIo (Gmac->Id, FALSE);

    GmacSetToRgmii (Gmac->Id, Gmac->TxDelay, Gmac->RxDelay);
  
    /* Reset PHY */
    GmacIoPhyReset (Gmac->Id, TRUE);
    MicroSecondDelay (20000);
    GmacIoPhyReset (Gmac->Id, FALSE);
    MicroSecondDelay (200000);

    PhyInit (Gmac->BaseAddress);
#endif

    Gmac->EqosPlatform.BaseAddress       = Gmac->BaseAddress;
    Gmac->EqosPlatform.GetConfig         = GmacPlatformGetConfig;
    Gmac->EqosPlatform.SetInterfaceSpeed = GmacPlatformSetInterfaceSpeed;

    /* Last octet is even for the first EQOS instance and odd for the second. */
    if (Count == 0) {
      Gmac->EqosPlatform.MacAddress.Addr[5] &= ~1;
    } else {
      ASSERT (Count == 1);
      Gmac->EqosPlatform.MacAddress.Addr[5] |= 1;
    }
    Count++;

    CopyMem (&Gmac->EqosPlatform.MacAddress, &MacAddress, NET_ETHER_ADDR_LEN);

    DEBUG ((
      DEBUG_INFO,
      "%a: GMAC%u MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
      __func__,
      Gmac->Id,
      Gmac->EqosPlatform.MacAddress.Addr[0],
      Gmac->EqosPlatform.MacAddress.Addr[1],
      Gmac->EqosPlatform.MacAddress.Addr[2],
      Gmac->EqosPlatform.MacAddress.Addr[3],
      Gmac->EqosPlatform.MacAddress.Addr[4],
      Gmac->EqosPlatform.MacAddress.Addr[5]
      ));

    CopyMem (&Gmac->DevicePath, &mGmacDevicePathTemplate, sizeof (GMAC_DEVICE_PATH));
    CopyMem (&Gmac->DevicePath.MacAddrDP.MacAddress, &Gmac->EqosPlatform.MacAddress, NET_ETHER_ADDR_LEN);
    Gmac->DevicePath.ControllerId = Gmac->Id;

    Handle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gDwcEqosPlatformDeviceProtocolGuid,
                    &Gmac->EqosPlatform,
                    &gEfiDevicePathProtocolGuid,
                    &Gmac->DevicePath,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to install GMAC%u EQOS device. Status=%r",
        __func__,
        Gmac->Id,
        Status
        ));
    }
  }

  return EFI_SUCCESS;
}
