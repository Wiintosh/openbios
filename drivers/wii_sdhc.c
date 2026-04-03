/*
 *   OpenBIOS Wii and Wii U sdhc driver
 *
 *   (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "kernel/kernel.h"
#include "libc/byteorder.h"
#include "libc/vsprintf.h"

#include <libopenbios/ofmem.h>
#include "drivers/drivers.h"
#include "wii_sdhc.h"
#include "timer.h"

#ifdef CONFIG_DEBUG_WII_SDHC
#define SDHC_DPRINTF(fmt, args...) \
do { printk("SDHC - %s: " fmt, __func__ , ##args); } while (0)
#else
#define SDHC_DPRINTF(fmt, args...) do { } while (0)
#endif

extern void flush_dcache_range(char *start, char *stop);
extern void invalidate_dcache_range(char *start, char *stop);

DECLARE_UNNAMED_NODE( ob_wii_sdhc, 0, sizeof(sdhc_device_t*) );

static inline uint16_t sdhc_calc_power(uint8_t exp) {
  uint16_t value = 1;
  for (int i = 0; i < exp; i++) {
    value *= 2;
  }
  return value;
}

//
// Read/write registers.
//
static inline uint32_t sdhc_read32(sdhc_device_t *sdhc, uint32_t offset) {
  return in_be32((volatile unsigned*)(sdhc->mmio_base + offset));
}
static inline uint16_t sdhc_read16(sdhc_device_t *sdhc, uint32_t offset) {
  // Can't handle non 32-bit reads.
  return sdhc_read32(sdhc, offset & -4) >> (8 * (offset & 2));
}
static inline uint8_t sdhc_read8(sdhc_device_t *sdhc, uint32_t offset) {
  // Can't handle non 32-bit reads.
  return sdhc_read32(sdhc, offset & -4) >> (8 * (offset & 3));
}
static inline void sdhc_write32(sdhc_device_t *sdhc, uint32_t offset, uint32_t data) {
  out_be32((volatile unsigned*)(sdhc->mmio_base + offset), data);
}
static inline void sdhc_write16(sdhc_device_t *sdhc, uint32_t offset, uint16_t data) {
  // Can't handle non 32-bit writes.
  uint32_t shift = 8 * (offset & 2);
  offset &= -4;

  uint32_t tmp = sdhc_read32(sdhc, offset);
  tmp = (data << shift) | (tmp & ~(0xFFFF << shift));
  sdhc_write32(sdhc, offset, tmp);
}
static inline void sdhc_write8(sdhc_device_t *sdhc, uint32_t offset, uint8_t data) {
  // Can't handle non 32-bit writes.
  uint32_t shift = 8 * (offset & 3);
  offset &= -4;

  uint32_t tmp = sdhc_read32(sdhc, offset);
  tmp = (data << shift) | (tmp & ~(0xFF << shift));
  sdhc_write32(sdhc, offset, tmp);
}

static inline int sdhc_is_sdcard(sdhc_device_t *sdhc) {
  return sdhc->card_type != kSDCardTypeMMC;
}

//
// Get controller version.
//
static inline uint8_t sdhc_get_version(sdhc_device_t *sdhc) {
  return (uint8_t) (sdhc_read16(sdhc, kSDHCRegHostControllerVersion) & kSDHCRegHostControllerVersionMask);
}

//
// Get controller present state.
//
static inline uint32_t sdhc_get_present_state(sdhc_device_t *sdhc) {
  return sdhc_read32(sdhc, kSDHCRegPresentState);
}

//
// Check if card is present.
//
static inline int sdhc_is_card_present(sdhc_device_t *sdhc) {
  return (sdhc_get_present_state(sdhc) & kSDHCRegPresentStateCardInserted) != 0;
}

//
// Check if card is write protected.
//
static inline int shdc_is_card_write_protected(sdhc_device_t *sdhc) {
  return (sdhc_get_present_state(sdhc) & kSDHCRegPresentStateCardWriteable) == 0;
}

//
// Reset the controller.
//
static int sdhc_reset(sdhc_device_t *sdhc, uint8_t bits) {
  uint32_t timeout;

  SDHC_DPRINTF("Resetting host controller with bits 0x%X\n", bits);
  sdhc_write8(sdhc, kSDHCRegSoftwareReset, bits);

  //
  // Wait for bits to clear.
  //
  for (timeout = kSDHCResetTimeoutMS; timeout > 0; timeout--) {
    if ((sdhc_read8(sdhc, kSDHCRegSoftwareReset) & bits) == 0) {
      break;
    }
    mdelay(1);
  }

  if (timeout == 0) {
    SDHC_DPRINTF("Timed out waiting for reset bits to clear: 0x%X\n", sdhc_read8(sdhc, kSDHCRegSoftwareReset));
    return 1;
  }

  SDHC_DPRINTF("Host controller is now reset\n");
  return 0;
}

//
// Initializes the controller.
//
static int sdhc_init(sdhc_device_t *sdhc) {
  SDHC_DPRINTF("SD host controller version: 0x%X, ps: 0x%X\n", sdhc_get_version(sdhc), sdhc_get_present_state(sdhc));

  //
  // Reset controller completely.
  //
  int result = sdhc_reset(sdhc, kSDHCRegSoftwareResetAll);
  if (result) {
    return result;
  }

  //
  // Set controller parameters.
  //
  sdhc_write8(sdhc, kSDHCRegTimeoutControl, 0xE);

  //
  // Enable all interrupt statuses, but do not signal for any as interrupts are not enabled.
  //
  sdhc_write16(sdhc, kSDHCRegNormalIntStatusEnable, -1);
  sdhc_write16(sdhc, kSDHCRegErrorIntStatusEnable, -1);
  sdhc_write16(sdhc, kSDHCRegNormalIntSignalEnable, 0);
  sdhc_write16(sdhc, kSDHCRegErrorIntSignalEnable, 0);
  return 0;
}

//
// Sets the controller clock rate.
//
static int sdhc_set_clock(sdhc_device_t *sdhc, uint32_t speedHz) {
  uint32_t timeout;

  //
  // Clear existing clock register.
  //
  sdhc_write16(sdhc, kSDHCRegClockControl, 0);
  if (speedHz == 0) {
    return 0;
  }

  //
  // Get base clock speed.
  //
  uint32_t hcCaps    = sdhc_read32(sdhc, kSDHCRegCapabilities);
  uint32_t baseClock = sdhc_get_version(sdhc) >= kSDHCVersion3_00 ?
    (hcCaps & kSDHCRegCapabilitiesBaseClockMaskVer3) >> kSDHCRegCapabilitiesBaseClockShift :
    (hcCaps & kSDHCRegCapabilitiesBaseClockMaskVer1) >> kSDHCRegCapabilitiesBaseClockShift;
  baseClock *= MHz;
  SDHC_DPRINTF("Caps: 0x%X\n", hcCaps);
  SDHC_DPRINTF("Base clock is %u MHz\n", baseClock / MHz);

  //
  // Calculate clock divisor.
  //
  uint32_t clockDiv;
  for (clockDiv = 1; (baseClock / clockDiv) > speedHz; clockDiv <<= 1);
  SDHC_DPRINTF("Clock will be set to %u %s using divisor %u\n",
                speedHz >= MHz ? (baseClock / clockDiv) / MHz : (baseClock / clockDiv) / kHz,
                speedHz >= MHz ? "MHz" : "kHz", clockDiv);

  //
  // Set clock divisor and enable internal clock.
  //
  uint16_t newClockDiv = ((clockDiv << kSDHCRegClockControlFreqSelectLowShift) & kSDHCRegClockControlFreqSelectLowMask)
    | ((clockDiv >> kSDHCRegClockControlFreqSelectHighRhShift) & kSDHCRegClockControlFreqSelectHighMask);
  sdhc_write16(sdhc, kSDHCRegClockControl, sdhc_read16(sdhc, kSDHCRegClockControl) | newClockDiv | kSDHCRegClockControlIntClockEnable);

  //
  // Wait for clock to be stable.
  //
  for (timeout = kSDHCClockTimeoutMS; timeout > 0; timeout--) {
    if ((sdhc_read16(sdhc, kSDHCRegClockControl) & kSDHCRegClockControlIntClockStable) != 0) {
      break;
    }
    mdelay(1);
  }

  if (timeout == 0) {
    SDHC_DPRINTF("Timed out waiting for clock to become stable\n");
    return 1;
  }
  SDHC_DPRINTF("Clock is now stable\n");

  //
  // Enable clock to card.
  //
  sdhc_write16(sdhc, kSDHCRegClockControl, sdhc_read16(sdhc, kSDHCRegClockControl) | kSDHCRegClockControlSDClockEnable);
  SDHC_DPRINTF("Clock control register is now 0x%X\n", sdhc_read16(sdhc, kSDHCRegClockControl));
  mdelay(100);

  return 0;
}

//
// Sets the card voltage used by the controller.
//
static void sdhc_set_power(sdhc_device_t *sdhc, int enabled) {
  //
  // Clear power register.
  //
  sdhc_write16(sdhc, kSDHCRegPowerControl, 0);
  if (!enabled) {
    return;
  }

  //
  // Get highest supported card voltage and enable it.
  //
  uint32_t hcCaps       = sdhc_read32(sdhc, kSDHCRegCapabilities);
  uint16_t powerControl = sdhc_read16(sdhc, kSDHCRegPowerControl);
  if (hcCaps & kSDHCRegCapabilitiesVoltage3_3Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_3_3;
    SDHC_DPRINTF("Card voltage: 3.3V\n");
  } else if (hcCaps & kSDHCRegCapabilitiesVoltage3_0Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_3_0;
    SDHC_DPRINTF("Card voltage: 3.0V\n");
  } else if (hcCaps & kSDHCRegCapabilitiesVoltage1_8Supported) {
    powerControl |= kSDHCRegPowerControlVDD1_1_8;
    SDHC_DPRINTF("Card voltage: 1.8V\n");
  }
  sdhc_write16(sdhc, kSDHCRegPowerControl, powerControl);

  //
  // Turn power on to card.
  //
  sdhc_write16(sdhc, kSDHCRegPowerControl, sdhc_read16(sdhc, kSDHCRegPowerControl) | kSDHCRegPowerControlVDD1On);
  SDHC_DPRINTF("Card power control register is now 0x%X\n", sdhc_read16(sdhc, kSDHCRegPowerControl));
  mdelay(100);
}

//
// Sets the controller bus width bits.
//
static void sdhc_set_bus_width(sdhc_device_t *sdhc, sdhc_bus_width_t busWidth) {
  uint16_t hcControl = sdhc_read16(sdhc, kSDHCRegHostControl1) & ~kSDHCRegHostControl1DataWidthMask;
  if (busWidth == kSDBusWidth4) {
    hcControl |= kSDHCRegHostControl1DataWidth4Bit;
    SDHC_DPRINTF("Setting controller bus width to 4-bit mode\n");
  } else if (busWidth == kSDBusWidth8) {
    hcControl |= kSDHCRegHostControl1DataWidth8Bit;
    SDHC_DPRINTF("Setting controller bus width to 8-bit mode\n");
  } else {
    SDHC_DPRINTF("Setting controller bus width to 1-bit mode\n");
  }
  sdhc_write16(sdhc, kSDHCRegHostControl1, hcControl);
}

static int sdhc_command(sdhc_device_t *sdhc, uint8_t commandIndex, uint8_t responseType, uint32_t argument,
                        void *buffer, uint16_t blockCount, int bufferRead, sd_cmd_response_t *outResponse) {
  uint32_t  timeout;
  uint16_t  commandValue;
  uint16_t  transferMode;
  uint32_t  intStatus;

  //WIIDBGLOG("Command: 0x%X, rspType: 0x%X, arg: 0x%X", commandIndex, responseType, argument);

  //
  // Wait for controller to be ready.
  //
  for (timeout = kSDHCCommandTimeoutMS; timeout > 0; timeout--) {
    if ((sdhc_get_present_state(sdhc) & (kSDHCRegPresentStateCmdInhibit | kSDHCRegPresentStateDatInhibit)) == 0) {
      break;
    }
    mdelay(1);
  }

  if (timeout == 0) {
    SDHC_DPRINTF("Timed out waiting for command inhibit\n");
    return 1;
  }

  //
  // Clear interrupt status.
  //
  sdhc_write32(sdhc, kSDHCRegNormalIntStatus, -1);

  // Build out command register.
  commandValue  = (commandIndex << kSDHCRegCommandIndexShift) & kSDHCRegCommandIndexMask;
  commandValue |= (responseType & kSDHCResponseTypeMask);

  //
  // Configure DMA if there's a buffer.
  //
  if (buffer != NULL) {
    //
    // Copy to temp buffer.
    //
    if (!bufferRead) {
      memcpy(sdhc->buffer, buffer, blockCount * kSDBlockSize);
      flush_dcache_range((char*)sdhc->buffer, (char*)sdhc->buffer + (blockCount * kSDBlockSize));
    }

    commandValue   |= kSDHCRegCommandDataPresent;
    transferMode    = kSDHCRegTransferModeDMAEnable | kSDHCRegTransferModeBlockCountEnable | kSDHCRegTransferModeMultipleBlock | kSDHCRegTransferModeAutoCMD12;
    if (bufferRead) {
      transferMode |= kSDHCRegTransferModeDataTransferRead;
    }

    sdhc_write32(sdhc, kSDHCRegSDMA, virt_to_phys(sdhc->buffer));
    sdhc_write16(sdhc, kSDHCRegBlockSize, kSDBlockSize | kSDHCRegBlockSizeDMA512K);
    sdhc_write16(sdhc, kSDHCRegBlockCount, blockCount);
  } else {
    transferMode = 0;
    sdhc_write32(sdhc, kSDHCRegBlockSize, 0);
  }

  //
  // Write data for command.
  // Command must be written together with transfer mode as both are 16-bit registers.
  //
  sdhc_write32(sdhc, kSDHCRegArgument, argument);
  sdhc_write32(sdhc, kSDHCRegTransferMode, transferMode | (commandValue << 16));

  //
  // Wait for command to complete.
  //
  for (timeout = kSDHCCommandTimeoutMS * 2; timeout > 0; timeout--) {
    if ((sdhc_read32(sdhc, kSDHCRegNormalIntStatus) & kSDHCRegNormalIntStatusCommandComplete) != 0) {
      sdhc_write32(sdhc, kSDHCRegNormalIntStatus, kSDHCRegNormalIntStatusCommandComplete);
      break;
    }
    mdelay(1);
  }

  if (timeout == 0) {
    SDHC_DPRINTF("Timed out waiting for command to complete\n");
    return 1;
  }

  if (outResponse != NULL) {
    outResponse->data[3] = sdhc_read32(sdhc, kSDHCRegResponse0);
    outResponse->data[2] = sdhc_read32(sdhc, kSDHCRegResponse1);
    outResponse->data[1] = sdhc_read32(sdhc, kSDHCRegResponse2);
    outResponse->data[0] = sdhc_read32(sdhc, kSDHCRegResponse3);
  }

  //
  // If no data, command is done.
  //
  if (buffer == NULL) {
    return 0;
  }

  while (1) {
    //
    // Wait for transfer to complete or a DMA interrupt to occur.
    //
    for (timeout = (kSDHCCommandTimeoutMS * 10); timeout > 0; timeout--) {
      intStatus = sdhc_read32(sdhc, kSDHCRegNormalIntStatus);
      if ((intStatus & (kSDHCRegNormalIntStatusTransferComplete | kSDHCRegNormalIntStatusDMAInterrupt)) != 0) {
        sdhc_write32(sdhc, kSDHCRegNormalIntStatus, intStatus);
        break;
      }
      mdelay(1);
    }

    if (timeout == 0) {
      SDHC_DPRINTF("Timed out waiting for data to complete\n");
      return 1;
    }

    //
    // If the transfer has completed, nothing else to do.
    //
    if (intStatus & kSDHCRegNormalIntStatusTransferComplete) {
      break;
    }

    //
    // Rewrite the SDMA register to continue the transfer.
    //
    sdhc_write32(sdhc, kSDHCRegSDMA, sdhc_read32(sdhc, kSDHCRegSDMA));
  }

  //
  // Invalidate the buffer.
  //
  if (bufferRead) {
    invalidate_dcache_range((char*)sdhc->buffer, (char*)sdhc->buffer + (blockCount * kSDBlockSize));
    memcpy(buffer, sdhc->buffer, blockCount * kSDBlockSize);
  }

  return 0;
}

//
// Sends an SD app command.
//
static int sdhc_app_command(sdhc_device_t *sdhc, uint8_t commandIndex, uint8_t responseType, uint32_t argument,
                            void *buffer, uint16_t blockCount, int bufferRead, sd_cmd_response_t *outResponse) {
  sd_cmd_response_t   appResponse;
  int                 result;

  result = sdhc_command(sdhc, kSDCommandAppCommand, kSDHCResponseTypeR1, (sdhc->card_addr << kSDRelativeAddressShift), NULL, 0, 0, &appResponse);
  if (result) {
    return result;
  }

  return sdhc_command(sdhc, commandIndex, responseType, argument, buffer, blockCount, bufferRead, outResponse);
}

//
// Resets the card.
//
static int sdhc_reset_card(sdhc_device_t *sdhc) {
  sd_cmd_response_t sdResponse;
  sd_cmd_response_t cidResponse;
  int              result;

  //
  // Send card to IDLE state.
  //
  result = sdhc_command(sdhc, kSDCommandGoIdleState, kSDHCResponseTypeR0, 0, NULL, 0, 0, NULL);
  if (result) {
    return result;
  }
  SDHC_DPRINTF("Card has been reset and should be in IDLE status\n");

  //
  // Issue SEND_IF_COND to card.
  // If no response, this is either is SD 1.0 or MMC.
  //
  result = sdhc_command(sdhc, kSDCommandSendIfCond, kSDHCResponseTypeR7, 0x1AA, NULL, 0, 0, &sdResponse);
  if (result) {
    SDHC_DPRINTF("Card did not respond to SEND_IF_COND, not an SD 2.00 card\n");
    sdhc->card_type = kSDCardTypeSD_Legacy;// TODO have timeout status
  }

  //
  // Issue SD card initialization command.
  //
  SDHC_DPRINTF("Initializing %s card\n", sdhc->card_type == kSDCardTypeSD_Legacy ? "MMC or legacy SD" : "SD 2.00");
  for (int i = 0; i < 20; i++) {
    result = sdhc_app_command(sdhc, kSDAppCommandSendOpCond, kSDHCResponseTypeR3, kSDOCRInitValue, NULL, 0, 0, &sdResponse);

    //
    // No response indicates an MMC card.
    //
    if (result && sdhc->card_type == kSDCardTypeSD_Legacy) {
      SDHC_DPRINTF("Card did not respond to SEND_OP_COND, not an SD card\n");
      sdhc->card_type = kSDCardTypeMMC;
      break;
    } else if (result) {
      return result;
    }

    if (sdResponse.r1 & kSDOCRCardBusy) {
      break;
    }

    //
    // Spec indicates to wait 1sec between attempts.
    //
    mdelay(1000);
  }

  //
  // Check if SD card has started.
  //
  //
  // If card is still not ready, abort.
  //
  if (!(sdResponse.r1 & kSDOCRCardBusy)) {
    SDHC_DPRINTF("Timed out initializing card\n");
    return 1;
  }
  sdhc->is_card_high_capacity = sdResponse.r1 & kSDOCRCCSHighCapacity;

  SDHC_DPRINTF("Got SD card, OCR: 0x%X\n", sdResponse.r1);

  //
  // Get CID from card.
  //
  result = sdhc_command(sdhc, kSDCommandAllSendCID, kSDHCResponseTypeR2, 0, NULL, 0, 0, &cidResponse);
  if (result) {
    return result;
  }

  if (sdhc_is_sdcard(sdhc)) {
    //
    // Ask card to send address.
    //
    result = sdhc_command(sdhc, kSDCommandSendRelativeAddress, kSDHCResponseTypeR6, 0, NULL, 0, 0, &sdResponse);
    if (result) {
      return result;
    }
    sdhc->card_addr = sdResponse.r1 >> kSDRelativeAddressShift;
  }

  SDHC_DPRINTF("Card @ 0x%X has CID of 0x%08X%08X%08X%08X\n", sdhc->card_addr,
    cidResponse.data[0], cidResponse.data[1], cidResponse.data[2], cidResponse.data[3]);

  memcpy(&sdhc->cid, cidResponse.data, sizeof (sdhc->cid));
  return 0;
}

static int sdhc_read_csd(sdhc_device_t *sdhc) {
  sd_cmd_response_t *csdResponse;
  int               result;

  csdResponse = (sd_cmd_response_t*)&sdhc->csd;
  result = sdhc_command(sdhc, kSDCommandSendCSD, kSDHCResponseTypeR2, sdhc->card_addr << kSDRelativeAddressShift, NULL, 0, 0, csdResponse);
  if (result) {
    return result;
  }
  SDHC_DPRINTF("CSD: 0x%08X%08X%08X%08X\n", csdResponse->data[0], csdResponse->data[1], csdResponse->data[2], csdResponse->data[3]);

  //
  // Calculate card size.
  //
  if (sdhc_is_sdcard(sdhc)) {
    SDHC_DPRINTF("CSD struct version: 0x%X\n", sdhc->csd.sd1.csdStructure);
    SDHC_DPRINTF("CSD supported classes: 0x%X\n", sdhc->csd.sd1.ccc);
    SDHC_DPRINTF("CSD max clock rate: 0x%X\n", sdhc->csd.sd1.tranSpeed);

    //
    // Calculate SD block size in bytes and blocks.
    //
    uint64_t cardBlockBytes;
    if (sdhc->csd.sd1.csdStructure == kSDCSDVersion1_0) {
      cardBlockBytes = ((sdhc->csd.sd1.cSize + 1) * sdhc_calc_power(sdhc->csd.sd1.cSizeMultiplier + 2)) * sdhc_calc_power(sdhc->csd.sd1.readBLLength);
    } else if (sdhc->csd.sd1.csdStructure == kSDCSDVersion2_0) {
      cardBlockBytes = ((uint64_t)sdhc->csd.sd2.cSize + 1) * (512 * kByte);
    } else {
      SDHC_DPRINTF("Unsupported SD card\n");
      return 1;
    }
    sdhc->block_count = (uint32_t)(cardBlockBytes / kSDBlockSize);
    SDHC_DPRINTF("Block count: %u, high capacity: %u\n", sdhc->block_count, sdhc->is_card_high_capacity);

  } else { // TODO: handle MMC cards.
    //
    // Calculate MMC block size in bytes and blocks.
    //
    /*UInt32 cardBlockBytes = ((_cardCSD.mmc.cSize + 1) * calcPower(_cardCSD.mmc.cSizeMultiplier + 2)) * calcPower(_cardCSD.mmc.readBLLength);
    _cardBlockCount = cardBlockBytes / kSDBlockSize;
    WIIDBGLOG("CSD struct version: 0x%X, spec version: 0x%X", _cardCSD.mmc.csdStructure, _cardCSD.mmc.specVersion);
    WIIDBGLOG("CSD CSIZE: 0x%X, CCC: 0x%X", _cardCSD.mmc.cSize, _cardCSD.mmc.ccc);
    WIIDBGLOG("CSD max clock rate: 0x%X", _cardCSD.mmc.tranSpeed);
    WIIDBGLOG("Block count: %u (%llu bytes), high capacity: %u", _cardBlockCount, cardBlockBytes, _isCardHighCapacity);*/

    //
    // Calculate max clock speed for standard mode.
    //
    /*if (_cardCSD.mmc.tranSpeed == kMMCTranSpeed20MHz) {
      _mmcMaxStandardClock = kSDANormalSpeedClock20MHz;
    } else if (_cardCSD.mmc.tranSpeed == kMMCTranSpeed26MHz) {
      _mmcMaxStandardClock = kSDANormalSpeedClock26MHz;
    } else {
      _mmcMaxStandardClock = kSDANormalSpeedClock20MHz; // TODO:
    }*/
   // WIIDBGLOG("MMC maximum clock speed is %u Hz", _mmcMaxStandardClock);
  }

  return 0;
}

//
// Selects or deselects the card.
//
static int sdhc_select_deselect(sdhc_device_t *sdhc, int select) {
  return sdhc_command(sdhc, kSDCommandSelectDeselectCard, kSDHCResponseTypeR1b, select ? (sdhc->card_addr << kSDRelativeAddressShift) : 0, NULL, 0, 0, NULL);
}

//
// Sets the card's bus width.
//
static int sdhc_set_card_bus_width(sdhc_device_t *sdhc, sdhc_bus_width_t busWidth) {
  uint16_t  val;
  int       result;

  if (sdhc_is_sdcard(sdhc)) {
    if (busWidth == kSDBusWidth4) {
      val = kSDBusWidth4Bit;
      SDHC_DPRINTF("Setting card bus width to 4-bit mode\n");
    } else {
      val = kSDBusWidth1Bit;
      SDHC_DPRINTF("Setting card bus width to 1-bit mode\n");
    }

    result = sdhc_app_command(sdhc, kSDAppCommandSetBusWidth, kSDHCResponseTypeR1, val, NULL, 0, 0, NULL);
  } else {
    // TODO MMC
    result = 1;
  }

  if (result) {
    return result;
  }

  //
  // Controller needs to match.
  //
  sdhc_set_bus_width(sdhc, busWidth);
  return 0;
}

//
// Sets the card's block length.
//
static int sdhc_set_block_length(sdhc_device_t *sdhc, uint16_t block_size) {
  int result;

  result = sdhc_command(sdhc, kSDCommandSetBlockLength, kSDHCResponseTypeR1, block_size, NULL, 0, 0, NULL);
  if (!result) {
    sdhc->block_size = block_size;
  }
  return result;
}

//
// OpenBIOS interface functions.
//

//
// OF: Get max transfer size.
//
static void ob_wii_sdhc_max_transfer(int *idx) {
    SDHC_DPRINTF("max_transfer %x\n", SDHC_BUFFER_SIZE);
    PUSH(SDHC_BUFFER_SIZE);
}

//
// OF: Read blocks.
//
static void ob_wii_sdhc_read_blocks(int *idx) {
    cell n = POP(), cnt=n;
    ucell blk = POP();
    unsigned char *dest = (unsigned char *)cell2pointer(POP());
    sdhc_device_t *sdhc = *(sdhc_device_t **)idx;

    SDHC_DPRINTF("%lx block=%ld n=%ld\n",
                (unsigned long)dest, (unsigned long)blk, (long)n);

    while (n) {
        int len = n;
        if (len > (SDHC_BUFFER_SIZE / kSDBlockSize))
            len = SDHC_BUFFER_SIZE / kSDBlockSize;

        if (sdhc_command(sdhc, kSDCommandReadMultipleBlock, kSDHCResponseTypeR1, blk, dest, len, 1, NULL)) {
            SDHC_DPRINTF("error\n");
            RET(0);
        }

        dest += len * sdhc->block_size;
        n -= len;
        blk += len;
    }

    PUSH(cnt);
}

//
// OF: Gets the block size.
//
static void ob_wii_sdhc_block_size(int *idx) {
    sdhc_device_t *sdhc = *(sdhc_device_t **)idx;
    SDHC_DPRINTF("ob_wii_sdhc_block_size: block size %x\n", sdhc->block_size);
    PUSH(sdhc->block_size);
}

static void ob_wii_sdhc_open(int *idx)
{
    int ret=1;
    phandle_t ph;
    sdhc_device_t *sdhc;

    PUSH(find_ih_method("sdhc", my_self()));
    fword("execute");
    sdhc = cell2pointer(POP());
    *(sdhc_device_t **)idx = sdhc;

    selfword("open-deblocker");

    /* interpose disk-label */
    ph = find_dev("/packages/disk-label");
    fword("my-args");
    PUSH_ph( ph );
    fword("interpose");

    RET ( -ret );
}

//
// OF: Close device.
//
static void ob_wii_sdhc_close(sdhc_device_t *sdhc) {
    selfword("close-deblocker");
}

//
// OF: DMA allocate
//
static void ob_wii_sdhc_dma_alloc(int *idx) {
    call_parent_method("dma-alloc");
}

static void ob_wii_sdhc_dma_free(int *idx) {
    call_parent_method("dma-free");
}

static void ob_wii_sdhc_dma_map_in(int *idx) {
    call_parent_method("dma-map-in");
}

static void ob_wii_sdhc_dma_map_out(int *idx) {
    call_parent_method("dma-map-out");
}

static void ob_wii_sdhc_dma_sync(int *idx) {
    call_parent_method("dma-sync");
}

NODE_METHODS(ob_wii_sdhc) = {
    { "open",		ob_wii_sdhc_open		},
    { "close",		ob_wii_sdhc_close		},
    { "read-blocks",	ob_wii_sdhc_read_blocks	    },
    { "block-size",		ob_wii_sdhc_block_size	    },
    { "max-transfer",	ob_wii_sdhc_max_transfer	},
    { "dma-alloc",		ob_wii_sdhc_dma_alloc	    },
    { "dma-free",		ob_wii_sdhc_dma_free		},
    { "dma-map-in",		ob_wii_sdhc_dma_map_in	    },
    { "dma-map-out",	ob_wii_sdhc_dma_map_out	    },
    { "dma-sync",		ob_wii_sdhc_dma_sync		},
};

static void set_hd_alias(const char *path)
{
    phandle_t aliases;

    aliases = find_dev("/aliases");

    if (get_property(aliases, "hd", NULL))
        return;

    set_property(aliases, "hd", path, strlen(path) + 1);
    set_property(aliases, "disk", path, strlen(path) + 1);
}

int ob_wii_shdc_init(const char *path, unsigned long mmio_base) {
    phandle_t dnode;
    sdhc_device_t *sdhc;

    sdhc = malloc(sizeof (*sdhc));
    ofmem_posix_memalign((void **)&sdhc->buffer, SDHC_BUFFER_SIZE, SDHC_BUFFER_SIZE);

    sdhc->mmio_base = mmio_base;

    //
    // Initialize the SDHC.
    //
    if (sdhc_init(sdhc)) {
        SDHC_DPRINTF("Failed to init SDHC");
        return 1;
    }

    //
    // Check if card is present.
    //
    if (!sdhc_is_card_present(sdhc)) {
        SDHC_DPRINTF("No card is currently inserted");
        return 1;
    }

    if (sdhc_set_clock(sdhc, kSDHCInitSpeedClock400kHz)) {
        return 1;
    }
    sdhc_set_power(sdhc, 1);

    if (sdhc_reset_card(sdhc)) {
        return 1;
    }

    if (sdhc_read_csd(sdhc)) {
        return 1;
    }

    if (sdhc_set_clock(sdhc, kSDHCNormalSpeedClock25MHz)) {
        return 1;
    }

    if (sdhc_select_deselect(sdhc, 1)) {
        return 1;
    }

    if (sdhc_set_card_bus_width(sdhc, kSDBusWidth4)) {
        return 1;
    }

    if (sdhc_set_block_length(sdhc, kSDBlockSize)) {
        return 1;
    }

    mdelay(1000);

    //
    // Create the disk device.
    //
    fword("new-device");
    dnode = get_cur_dev();
    set_int_property(dnode, "reg", 0);

    push_str("disk");
    fword("device-name");

    push_str("block");
    fword("device-type");

    PUSH(pointer2cell(sdhc));
    feval("value sdhc");

    BIND_NODE_METHODS(dnode, ob_wii_sdhc);
    fword("is-deblocker");

    fword("finish-device");

    set_hd_alias(get_path_from_ph(dnode));

    return 0;
}
