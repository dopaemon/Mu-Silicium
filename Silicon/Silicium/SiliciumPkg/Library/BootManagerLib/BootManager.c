/**
  Copyright (C) DuoWoA authors. All rights reserved.
  Copyright (C) Microsoft Corporation. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/AcpiTableUpdateLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/SecureBootHandlerLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MsBootOptionsLib.h>
#include <Library/BootGraphicsLib.h>
#include <Library/BootGraphics.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootManagerLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Uefi/UefiGpt.h>

STATIC EFI_GUID mSmemProtocolGuid   = { 0xF4E5C7D0, 0xD239, 0x47CB, { 0xAA, 0xCD, 0x7F, 0x66, 0xEF, 0x76, 0x32, 0x38 } };
STATIC EFI_GUID mDalSysProtocolGuid = { 0xAE37B942, 0x457F, 0x4C91, { 0xA1, 0x96, 0xD9, 0x66, 0x9F, 0xD3, 0x47, 0xA3 } };
STATIC EFI_GUID mHwioProtocolGuid   = { 0x9E61DE7C, 0x9927, 0x4F28, { 0x9F, 0x3D, 0x32, 0x07, 0x05, 0xD8, 0x18, 0x01 } };

#include <Configuration/BootDevices.h>

#include "BootManager.h"
#include "FontData.h"

//
// Screen Geometry & Layout Coordinates (Xiaomi 12S: 1080 x 2400)
//
#define SCREEN_W            1080
#define SCREEN_H            2400

#define CARD_X              60
#define CARD_W              960
#define CARD_H              125
#define CARD_GAP            20
#define CARD_START_Y        300

#define DOCK_X              60
#define DOCK_W              960
#define DOCK_Y              1200
#define DOCK_H              430

//
// Theme Colors (32-bit BGRX: { Blue, Green, Red, Reserved })
//
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_BG          = { 0x14, 0x0E, 0x0B, 0x00 }; // #0B0E14 Deep Midnight
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_ACCENT_UB   = { 0x16, 0x54, 0xE9, 0x00 }; // #E95416 Ubuntu Yaru Orange
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_ACCENT_MAC  = { 0xF9, 0xA8, 0x00, 0x00 }; // #00A8F9 macOS Azure Cyan
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_CARD_BG     = { 0x22, 0x18, 0x13, 0x00 }; // #131822 Sleek Card Background
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_CARD_BORDER = { 0x36, 0x27, 0x1F, 0x00 }; // #1F2736 Card Border
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_SEL_BG      = { 0x56, 0x33, 0x18, 0x00 }; // #183356 Selected Card Ocean
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_SEL_BORDER  = { 0xFF, 0xC4, 0x00, 0x00 }; // #00C4FF Vibrant Cyan Glow
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_WHITE       = { 0xFF, 0xFF, 0xFF, 0x00 }; // #FFFFFF Pure White
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_SILVER      = { 0xE2, 0xD5, 0xCB, 0x00 }; // #CBD5E2 Soft Silver
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_MUTED       = { 0x8B, 0x74, 0x64, 0x00 }; // #64748B Slate Gray
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_GREEN       = { 0x22, 0xC5, 0x5E, 0x00 }; // #5EC522 Emerald Green
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_RED         = { 0x44, 0x44, 0xEF, 0x00 }; // #EF4444 Crimson Red
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_AMBER       = { 0x08, 0xB3, 0xF5, 0x00 }; // #F5B308 Warm Amber
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_DOCK_BG     = { 0x1C, 0x15, 0x10, 0x00 }; // #10151C Dock Container
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL COLOR_DOCK_BORDER = { 0x3E, 0x2E, 0x24, 0x00 }; // #242E3E Dock Border

//
// Menu Item Structure
//
typedef struct {
  CONST CHAR8                   *Badge;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  BadgeColor;
  CONST CHAR8                   *Title;
  CONST CHAR8                   *Subtitle;
} MENU_ITEM_INFO;

STATIC CONST MENU_ITEM_INFO mMenuItems[] = {
  { "AND", { 0x22, 0xC5, 0x5E, 0x00 }, "LineageOS Android (Slot B)",      "Primary Mobile System  *  Safe & Operational" },
  { "RCV", { 0x44, 0x44, 0xEF, 0x00 }, "Reboot to Recovery Mode",         "Qualcomm Cookie 0x77665502  *  TWRP / Recovery" },
  { "LNX", { 0xF9, 0xA8, 0x00, 0x00 }, "Linux / UEFI Shell",              "Interactive UEFI Boot Manager & Shell" },
  { "FST", { 0x06, 0xB6, 0xD4, 0x00 }, "Reboot to Fastboot Mode",         "Qualcomm Cookie 0x77665500  *  Bootloader" },
  { "RBT", { 0x08, 0xB3, 0xF5, 0x00 }, "Reboot Device (Cold Reset)",       "Qualcomm System Reset  *  Re-execute active slot" },
  { "OFF", { 0xA8, 0x55, 0xF7, 0x00 }, "Power Off (Shutdown)",             "Safely power down all hardware subsystems" }
};
#define NUM_MENU_ITEMS (ARRAY_SIZE (mMenuItems))

//
// Global Variables & Buffers
//
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGopProtocol    = NULL;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL *mCardBltBuffer  = NULL;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL *mLineBltBuffer  = NULL;

VOID
EFIAPI
PlatformBootManagerBeforeConsole ()
{
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);
  EfiBootManagerDispatchDeferredImages ();
  MsBootOptionsLibRegisterDefaultBootOptions ();

  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&KeypadDevicePath,  NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&UsbKeyboardDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath, NULL);

  DeviceBootManagerBeforeConsole (NULL, NULL);
}

VOID
PostGopSetup (OUT EFI_GRAPHICS_OUTPUT_PROTOCOL **GopProtocol)
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *LocalGopProtocol;

  if (GopProtocol == NULL) {
    return;
  }

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID *)&LocalGopProtocol);
  if (EFI_ERROR (Status) && Status != EFI_UNSUPPORTED) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate GOP Protocol from Handle! Status = %r\n", __FUNCTION__, Status));
    return;
  }

  if (Status == EFI_UNSUPPORTED) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID *)&LocalGopProtocol);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "%a: Failed to Locate GOP Protocol! Status = %r\n", __FUNCTION__, Status));
      return;
    }

    Status = gBS->InstallProtocolInterface (&gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, EFI_NATIVE_INTERFACE, LocalGopProtocol);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "%a: Failed to Install GOP Protocol onto ConsoleOutHandle! Status = %r\n", __FUNCTION__, Status));
    }
  }

  *GopProtocol = LocalGopProtocol;
}

VOID
EFIAPI
PlatformBootManagerAfterConsole ()
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GopProtocol = NULL;

  PostGopSetup (&GopProtocol);

  // Display Boot Logo cleanly
  Status = DisplayBootGraphic (BG_SYSTEM_LOGO);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Display Boot Logo! Status = %r\n", __FUNCTION__, Status));
  }

  // Register Key Callback
  RegisterKeyCallback ((EFI_DEVICE_PATH_PROTOCOL *)&KeypadDevicePath);

  DeviceBootManagerAfterConsole ();
}

VOID
EFIAPI
PlatformBootManagerOnDemandConInConnect ()
{
  DeviceBootManagerOnDemandConInConnect ();
}

VOID
EFIAPI
PlatformBootManagerProcessBootCompletion (OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
  return;
}

//
// Graphics Blt Primitives (100% crash-safe through FrameBufferBltLib)
//
STATIC
VOID
BltFillRect (
  IN UINTN                          X,
  IN UINTN                          Y,
  IN UINTN                          Width,
  IN UINTN                          Height,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if (mGopProtocol == NULL || Width == 0 || Height == 0) {
    return;
  }
  if (X >= SCREEN_W || Y >= SCREEN_H) {
    return;
  }
  if (X + Width > SCREEN_W) {
    Width = SCREEN_W - X;
  }
  if (Y + Height > SCREEN_H) {
    Height = SCREEN_H - Y;
  }

  mGopProtocol->Blt (
                  mGopProtocol,
                  &Color,
                  EfiBltVideoFill,
                  0,
                  0,
                  X,
                  Y,
                  Width,
                  Height,
                  0
                  );
}

STATIC
VOID
BltDrawBorder (
  IN UINTN                          X,
  IN UINTN                          Y,
  IN UINTN                          Width,
  IN UINTN                          Height,
  IN UINTN                          Thickness,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  BltFillRect (X, Y, Width, Thickness, Color);
  BltFillRect (X, Y + Height - Thickness, Width, Thickness, Color);
  BltFillRect (X, Y, Thickness, Height, Color);
  BltFillRect (X + Width - Thickness, Y, Thickness, Height, Color);
}

STATIC
VOID
BltDrawString (
  IN UINTN                          X,
  IN UINTN                          Y,
  IN CONST CHAR8                    *Str,
  IN UINTN                          MaxChars,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  FgColor,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  BgColor,
  IN UINTN                          Scale
  )
{
  if (mGopProtocol == NULL || mLineBltBuffer == NULL || Str == NULL || Scale == 0) {
    return;
  }

  UINTN StrLen = AsciiStrLen (Str);
  if (StrLen > MaxChars) {
    StrLen = MaxChars;
  }
  if (StrLen == 0) {
    return;
  }

  UINTN LineW = StrLen * FONT_GLYPH_WIDTH * Scale;
  UINTN LineH = FONT_GLYPH_HEIGHT * Scale;

  if (LineW > SCREEN_W) {
    LineW = SCREEN_W;
  }
  if (LineH > 100) {
    LineH = 100;
  }
  if (X + LineW > SCREEN_W) {
    LineW = SCREEN_W - X;
  }
  if (Y + LineH > SCREEN_H) {
    LineH = SCREEN_H - Y;
  }

  // 1. Fill background in local RAM buffer
  for (UINTN r = 0; r < LineH; r++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Row = &mLineBltBuffer[r * LineW];
    for (UINTN c = 0; c < LineW; c++) {
      Row[c] = BgColor;
    }
  }

  // 2. Render characters in local RAM buffer
  for (UINTN idx = 0; idx < StrLen; idx++) {
    CHAR8 Char = Str[idx];
    if (Char < 0x20 || Char > 0x7E) {
      Char = '?';
    }
    CONST UINT8 *Glyph = mFontGlyphs[(UINTN)(Char - 0x20)];
    UINTN CharX = idx * FONT_GLYPH_WIDTH * Scale;

    for (UINTN r = 0; r < FONT_GLYPH_HEIGHT; r++) {
      UINT8 RowByte = Glyph[r];
      for (UINTN c = 0; c < FONT_GLYPH_WIDTH; c++) {
        if (RowByte & (0x80 >> c)) {
          for (UINTN dy = 0; dy < Scale; dy++) {
            UINTN Py = r * Scale + dy;
            if (Py >= LineH) break;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Line = &mLineBltBuffer[Py * LineW + CharX + c * Scale];
            for (UINTN dx = 0; dx < Scale; dx++) {
              if (CharX + c * Scale + dx >= LineW) break;
              Line[dx] = FgColor;
            }
          }
        }
      }
    }
  }

  // 3. Blt to video in a single hardware call
  mGopProtocol->Blt (
                  mGopProtocol,
                  mLineBltBuffer,
                  EfiBltBufferToVideo,
                  0,
                  0,
                  X,
                  Y,
                  LineW,
                  LineH,
                  LineW * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                  );
}

STATIC
VOID
BltDrawStringCentered (
  IN UINTN                          Y,
  IN CONST CHAR8                    *Str,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  FgColor,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  BgColor,
  IN UINTN                          Scale
  )
{
  if (Str == NULL) return;
  UINTN StrLen     = AsciiStrLen (Str);
  UINTN TotalWidth = StrLen * FONT_GLYPH_WIDTH * Scale;
  UINTN X          = (SCREEN_W > TotalWidth) ? (SCREEN_W - TotalWidth) / 2 : 0;
  BltDrawString (X, Y, Str, StrLen, FgColor, BgColor, Scale);
}

//
// SPMI Arbiter v7 Register Reader
//
STATIC
BOOLEAN
SpmiReadRegister (
  IN  UINT32 Channel,
  IN  UINT8  RegOffset,
  OUT UINT8  *Value
  )
{
  volatile UINT32 *ChBase = (volatile UINT32 *)(0x0C440000ULL + 0x20ULL * Channel);
  UINT32 Cmd = (1U << 27) | ((UINT32)(RegOffset & 0xFF) << 4);
  UINT32 Status;

  ChBase[0] = Cmd;

  for (UINTN i = 0; i < 2000; i++) {
    Status = ChBase[2];
    if (Status & 1) { // STATUS_DONE
      *Value = (UINT8)(ChBase[6] & 0xFF);
      return TRUE;
    }
    if (Status & 0xE) { // FAILURE, DENIED, or DROPPED
      return FALSE;
    }
    gBS->Stall (5);
  }

  return FALSE;
}

//
// Menu Card Rendering (Composed in RAM and Blt-ed in a single call)
//
STATIC
VOID
DrawMenuCard (
  IN UINTN   Index,
  IN BOOLEAN Selected
  )
{
  if (mGopProtocol == NULL || mCardBltBuffer == NULL || Index >= NUM_MENU_ITEMS) {
    return;
  }

  UINTN CardY = CARD_START_Y + Index * (CARD_H + CARD_GAP);

  EFI_GRAPHICS_OUTPUT_BLT_PIXEL CardBg     = Selected ? COLOR_SEL_BG     : COLOR_CARD_BG;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL CardBorder = Selected ? COLOR_SEL_BORDER : COLOR_CARD_BORDER;
  UINTN BorderThickness                    = Selected ? 4 : 2;

  // 1. Fill Card Background in RAM buffer
  for (UINTN r = 0; r < CARD_H; r++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Row = &mCardBltBuffer[r * CARD_W];
    for (UINTN c = 0; c < CARD_W; c++) {
      Row[c] = CardBg;
    }
  }

  // 2. Draw Card Border
  for (UINTN t = 0; t < BorderThickness; t++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopRow = &mCardBltBuffer[t * CARD_W];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BotRow = &mCardBltBuffer[(CARD_H - 1 - t) * CARD_W];
    for (UINTN c = 0; c < CARD_W; c++) {
      TopRow[c] = CardBorder;
      BotRow[c] = CardBorder;
    }
  }
  for (UINTN r = 0; r < CARD_H; r++) {
    for (UINTN t = 0; t < BorderThickness; t++) {
      mCardBltBuffer[r * CARD_W + t]                = CardBorder;
      mCardBltBuffer[r * CARD_W + (CARD_W - 1 - t)] = CardBorder;
    }
  }

  // 3. Draw Badge Box on the left
  UINTN BadgeX = 24;
  UINTN BadgeY = 22;
  UINTN BadgeW = 88;
  UINTN BadgeH = 80;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL BadgeBg = Selected ? (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0x30, 0x1E, 0x0E, 0x00 } : (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0x12, 0x0E, 0x0A, 0x00 };

  for (UINTN r = 0; r < BadgeH; r++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Row = &mCardBltBuffer[(BadgeY + r) * CARD_W + BadgeX];
    for (UINTN c = 0; c < BadgeW; c++) {
      Row[c] = BadgeBg;
    }
  }
  for (UINTN c = 0; c < BadgeW; c++) {
    mCardBltBuffer[BadgeY * CARD_W + BadgeX + c]                = mMenuItems[Index].BadgeColor;
    mCardBltBuffer[(BadgeY + BadgeH - 1) * CARD_W + BadgeX + c] = mMenuItems[Index].BadgeColor;
  }
  for (UINTN r = 0; r < BadgeH; r++) {
    mCardBltBuffer[(BadgeY + r) * CARD_W + BadgeX]              = mMenuItems[Index].BadgeColor;
    mCardBltBuffer[(BadgeY + r) * CARD_W + BadgeX + BadgeW - 1] = mMenuItems[Index].BadgeColor;
  }

  // Draw Badge Letters (Scale 2, centered inside Badge box)
  CONST CHAR8 *BadgeStr = mMenuItems[Index].Badge;
  UINTN BStrLen = AsciiStrLen (BadgeStr);
  UINTN BTextX = BadgeX + (BadgeW - BStrLen * FONT_GLYPH_WIDTH * 2) / 2;
  UINTN BTextY = BadgeY + (BadgeH - FONT_GLYPH_HEIGHT * 2) / 2;

  for (UINTN idx = 0; idx < BStrLen; idx++) {
    CHAR8 Char = BadgeStr[idx];
    if (Char < 0x20 || Char > 0x7E) Char = '?';
    CONST UINT8 *Glyph = mFontGlyphs[(UINTN)(Char - 0x20)];
    UINTN CharX = BTextX + idx * FONT_GLYPH_WIDTH * 2;

    for (UINTN r = 0; r < FONT_GLYPH_HEIGHT; r++) {
      UINT8 RowByte = Glyph[r];
      for (UINTN c = 0; c < FONT_GLYPH_WIDTH; c++) {
        if (RowByte & (0x80 >> c)) {
          for (UINTN dy = 0; dy < 2; dy++) {
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Line = &mCardBltBuffer[(BTextY + r * 2 + dy) * CARD_W + CharX + c * 2];
            Line[0] = mMenuItems[Index].BadgeColor;
            Line[1] = mMenuItems[Index].BadgeColor;
          }
        }
      }
    }
  }

  // 4. Draw Title (Scale 2 = 16x38 px, Bold White or Soft Silver)
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL TitleColor = Selected ? COLOR_WHITE : COLOR_SILVER;
  CONST CHAR8 *TitleStr = mMenuItems[Index].Title;
  UINTN TStrLen = AsciiStrLen (TitleStr);
  UINTN TitleX  = 136;
  UINTN TitleY  = 26;

  for (UINTN idx = 0; idx < TStrLen; idx++) {
    CHAR8 Char = TitleStr[idx];
    if (Char < 0x20 || Char > 0x7E) Char = '?';
    CONST UINT8 *Glyph = mFontGlyphs[(UINTN)(Char - 0x20)];
    UINTN CharX = TitleX + idx * FONT_GLYPH_WIDTH * 2;

    for (UINTN r = 0; r < FONT_GLYPH_HEIGHT; r++) {
      UINT8 RowByte = Glyph[r];
      for (UINTN c = 0; c < FONT_GLYPH_WIDTH; c++) {
        if (RowByte & (0x80 >> c)) {
          for (UINTN dy = 0; dy < 2; dy++) {
            UINTN Py = TitleY + r * 2 + dy;
            if (Py >= CARD_H) break;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Line = &mCardBltBuffer[Py * CARD_W + CharX + c * 2];
            Line[0] = TitleColor;
            Line[1] = TitleColor;
          }
        }
      }
    }
  }

  // 5. Draw Subtitle (Scale 1 = 8x19 px)
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL SubColor = Selected ? (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0xFF, 0xF1, 0xE2, 0x00 } : COLOR_MUTED;
  CONST CHAR8 *SubStr = mMenuItems[Index].Subtitle;
  UINTN SStrLen = AsciiStrLen (SubStr);
  UINTN SubX    = 136;
  UINTN SubY    = 76;

  for (UINTN idx = 0; idx < SStrLen; idx++) {
    CHAR8 Char = SubStr[idx];
    if (Char < 0x20 || Char > 0x7E) Char = '?';
    CONST UINT8 *Glyph = mFontGlyphs[(UINTN)(Char - 0x20)];
    UINTN CharX = SubX + idx * FONT_GLYPH_WIDTH;

    for (UINTN r = 0; r < FONT_GLYPH_HEIGHT; r++) {
      UINT8 RowByte = Glyph[r];
      for (UINTN c = 0; c < FONT_GLYPH_WIDTH; c++) {
        if (RowByte & (0x80 >> c)) {
          UINTN Py = SubY + r;
          if (Py >= CARD_H) break;
          mCardBltBuffer[Py * CARD_W + CharX + c] = SubColor;
        }
      }
    }
  }

  // 6. Draw Right Selection Pill
  if (Selected) {
    CONST CHAR8 *SelTag = "[BOOT >]";
    UINTN TagLen = AsciiStrLen (SelTag);
    UINTN TagX   = CARD_W - 160;
    UINTN TagY   = 44;

    for (UINTN idx = 0; idx < TagLen; idx++) {
      CHAR8 Char = SelTag[idx];
      if (Char < 0x20 || Char > 0x7E) Char = '?';
      CONST UINT8 *Glyph = mFontGlyphs[(UINTN)(Char - 0x20)];
      UINTN CharX = TagX + idx * FONT_GLYPH_WIDTH * 2;

      for (UINTN r = 0; r < FONT_GLYPH_HEIGHT; r++) {
        UINT8 RowByte = Glyph[r];
        for (UINTN c = 0; c < FONT_GLYPH_WIDTH; c++) {
          if (RowByte & (0x80 >> c)) {
            for (UINTN dy = 0; dy < 2; dy++) {
              UINTN Py = TagY + r * 2 + dy;
              if (Py >= CARD_H) break;
              EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Line = &mCardBltBuffer[Py * CARD_W + CharX + c * 2];
              Line[0] = COLOR_SEL_BORDER;
              Line[1] = COLOR_SEL_BORDER;
            }
          }
        }
      }
    }
  }

  // 7. Blt entire card to screen in a single hardware call
  mGopProtocol->Blt (
                  mGopProtocol,
                  mCardBltBuffer,
                  EfiBltBufferToVideo,
                  0,
                  0,
                  CARD_X,
                  CardY,
                  CARD_W,
                  CARD_H,
                  CARD_W * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                  );
}

STATIC
VOID
DrawHardwareKeyStatus (
  IN BOOLEAN VolUpPressed,
  IN BOOLEAN VolDownPressed,
  IN BOOLEAN PowerPressed,
  IN UINT8   PonVal,
  IN UINT8   GpioVal
  )
{
  CHAR8 Line1[80];
  CHAR8 Line2[80];

  AsciiSPrint (
    Line1,
    sizeof (Line1),
    "Keys : VolUp:[%a]  VolDown:[%a]  Power:[%a]",
    VolUpPressed ? "HELD" : "--- ",
    VolDownPressed ? "HELD" : "--- ",
    PowerPressed ? "HELD" : "--- "
    );

  AsciiSPrint (
    Line2,
    sizeof (Line2),
    "SPMI : Ch407(PON)=0x%02X   Ch434(GPIO6)=0x%02X",
    PonVal,
    GpioVal
    );

  BltDrawString (
    DOCK_X + 36,
    DOCK_Y + 290,
    Line1,
    70,
    (VolUpPressed || VolDownPressed || PowerPressed) ? COLOR_GREEN : COLOR_MUTED,
    COLOR_DOCK_BG,
    2
    );

  BltDrawString (
    DOCK_X + 36,
    DOCK_Y + 350,
    Line2,
    70,
    COLOR_MUTED,
    COLOR_DOCK_BG,
    2
    );
}

STATIC
VOID
DrawFullUI (IN UINTN SelectedIndex)
{
  // 1. Wipe entire screen with Deep Midnight Background (erases boot logo completely!)
  BltFillRect (0, 0, SCREEN_W, SCREEN_H, COLOR_BG);

  // 2. Top Accent Bar (Ubuntu Orange)
  BltFillRect (0, 0, SCREEN_W, 10, COLOR_ACCENT_UB);

  // 3. Top Pill Badge (macOS Azure)
  UINTN BadgeX = (SCREEN_W - 360) / 2;
  BltFillRect (BadgeX, 48, 360, 46, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0x36, 0x24, 0x18, 0x00 });
  BltDrawBorder (BadgeX, 48, 360, 46, 2, COLOR_ACCENT_MAC);
  BltDrawStringCentered (62, "MAYFLY BOOT PICKER", COLOR_ACCENT_MAC, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0x36, 0x24, 0x18, 0x00 }, 2);

  // 4. Header Titles
  BltDrawStringCentered (115, "Xiaomi 12S", COLOR_WHITE, COLOR_BG, 4);
  BltDrawStringCentered (205, "Snapdragon 8+ Gen 1  |  Mu-Silicium UEFI", COLOR_SILVER, COLOR_BG, 2);

  // Header separator line
  BltFillRect (CARD_X, 260, CARD_W, 2, COLOR_CARD_BORDER);

  // 5. Render All 6 Cards
  for (UINTN i = 0; i < NUM_MENU_ITEMS; i++) {
    DrawMenuCard (i, (i == SelectedIndex));
  }

  // 6. Dock Container (Ubuntu / macOS Control Center aesthetic)
  BltFillRect (DOCK_X, DOCK_Y, DOCK_W, DOCK_H, COLOR_DOCK_BG);
  BltDrawBorder (DOCK_X, DOCK_Y, DOCK_W, DOCK_H, 2, COLOR_DOCK_BORDER);

  BltDrawString (DOCK_X + 36, DOCK_Y + 24, "[ NAVIGATION & SYSTEM CONTROLS ]", 35, COLOR_ACCENT_MAC, COLOR_DOCK_BG, 2);
  BltDrawString (DOCK_X + 36, DOCK_Y + 74,  "  Volume Up   [^]  : Move Selection Up", 40, COLOR_WHITE, COLOR_DOCK_BG, 2);
  BltDrawString (DOCK_X + 36, DOCK_Y + 124, "  Volume Down [v]  : Move Selection Down", 42, COLOR_WHITE, COLOR_DOCK_BG, 2);
  BltDrawString (DOCK_X + 36, DOCK_Y + 174, "  Power Key   [*]  : Boot / Execute Selected", 46, COLOR_AMBER, COLOR_DOCK_BG, 2);
  BltDrawString (DOCK_X + 36, DOCK_Y + 224, "  USB Keyboard     : Up / Down / Enter / 1-6", 46, COLOR_MUTED, COLOR_DOCK_BG, 2);

  // Inner Dock Divider
  BltFillRect (DOCK_X + 36, DOCK_Y + 270, DOCK_W - 72, 2, COLOR_DOCK_BORDER);

  // 7. Footer
  BltDrawStringCentered (1700, "Protected Slot: B (LineageOS Operational)", COLOR_GREEN, COLOR_BG, 2);
  BltDrawStringCentered (1750, "Blob-free Mu-Silicium UEFI  *  hakios-mayfly", COLOR_MUTED, COLOR_BG, 2);

  // Storage partition recognition status & hardware diagnostic
  VOID *SmemPtr = NULL;
  VOID *DalPtr  = NULL;
  VOID *HwioPtr = NULL;
  EFI_STATUS SmemStat = gBS->LocateProtocol (&mSmemProtocolGuid, NULL, &SmemPtr);
  EFI_STATUS DalStat  = gBS->LocateProtocol (&mDalSysProtocolGuid, NULL, &DalPtr);
  EFI_STATUS HwioStat = gBS->LocateProtocol (&mHwioProtocolGuid, NULL, &HwioPtr);

  // Connect any BlockIo handles to PartitionDxe
  UINTN       BlockIoCount = 0;
  EFI_HANDLE *BlockHandles = NULL;
  gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &BlockIoCount, &BlockHandles);
  if (BlockHandles != NULL) {
    for (UINTN b = 0; b < BlockIoCount; b++) {
      gBS->ConnectController (BlockHandles[b], NULL, NULL, TRUE);
    }
    FreePool (BlockHandles);
  }

  EFI_STATUS  Status;
  UINTN       PartitionCount = 0;
  EFI_HANDLE *PartHandles    = NULL;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &PartitionCount, &PartHandles);
  if (PartHandles != NULL) {
    FreePool (PartHandles);
  }

  // Refresh BlockIo count
  BlockHandles = NULL;
  BlockIoCount = 0;
  gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &BlockIoCount, &BlockHandles);
  if (BlockHandles != NULL) {
    FreePool (BlockHandles);
  }

  if (!EFI_ERROR (Status) && PartitionCount > 0) {
    CHAR8 StorageStatus[80];
    AsciiSPrint (StorageStatus, sizeof (StorageStatus), "Storage: UFS Initialized (%d Partitions Found)", PartitionCount);
    BltDrawStringCentered (1800, StorageStatus, COLOR_GREEN, COLOR_BG, 2);
  } else {
    CHAR8 DiagStr[100];
    AsciiSPrint (
      DiagStr,
      sizeof (DiagStr),
      "Diag: SMEM:%a DAL:%a HWIO:%a Blk:%d Part:%d",
      !EFI_ERROR (SmemStat) ? "OK" : "--",
      !EFI_ERROR (DalStat) ? "OK" : "--",
      !EFI_ERROR (HwioStat) ? "OK" : "--",
      BlockIoCount,
      PartitionCount
      );
    BltDrawStringCentered (1800, DiagStr, COLOR_AMBER, COLOR_BG, 2);
  }
}

#define BOOT_CTRL_MAGIC   0x42414342 /* "BACB" */

STATIC
EFI_STATUS
FindPartitionByName (
  IN  CHAR16                   *TargetName,
  OUT EFI_BLOCK_IO_PROTOCOL   **OutBlockIo,
  OUT EFI_PARTITION_ENTRY      *OutGptEntry
  )
{
  EFI_STATUS                  Status;
  UINTN                       HandleCount = 0;
  EFI_HANDLE                 *HandleBuffer = NULL;
  UINTN                       Index;
  EFI_PARTITION_INFO_PROTOCOL *PartInfo;
  EFI_BLOCK_IO_PROTOCOL       *BlockIo;

  if (OutBlockIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *OutBlockIo = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPartitionInfoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status) || HandleCount == 0) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiPartitionInfoProtocolGuid,
                    (VOID **)&PartInfo
                    );
    if (EFI_ERROR (Status) || PartInfo == NULL) {
      continue;
    }

    if (PartInfo->Type == PARTITION_TYPE_GPT) {
      if (StrCmp (PartInfo->Info.Gpt.PartitionName, TargetName) == 0) {
        Status = gBS->HandleProtocol (
                        HandleBuffer[Index],
                        &gEfiBlockIoProtocolGuid,
                        (VOID **)&BlockIo
                        );
        if (!EFI_ERROR (Status)) {
          *OutBlockIo = BlockIo;
          if (OutGptEntry != NULL) {
            CopyMem (OutGptEntry, &PartInfo->Info.Gpt, sizeof (EFI_PARTITION_ENTRY));
          }
          FreePool (HandleBuffer);
          return EFI_SUCCESS;
        }
      }
    }
  }

  FreePool (HandleBuffer);
  return EFI_NOT_FOUND;
}

STATIC
BOOLEAN
SwitchSlotInMisc (VOID)
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_PARTITION_ENTRY    GptEntry;
  UINT8                 *Buffer = NULL;
  UINT32                 BlockSize;
  UINTN                  BufferSize;
  UINTN                  Offset = 0x800; // 2048 bytes in misc
  UINT32                 Magic;
  UINT32                 NewCrc32 = 0;

  Status = FindPartitionByName (L"misc", &BlockIo, &GptEntry);
  if (EFI_ERROR (Status) || BlockIo == NULL) {
    return FALSE;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0) {
    BlockSize = 4096;
  }

  // Read at least 4KB to cover offset 0x800 (2048) and the 32-byte struct
  BufferSize = ((4096 + BlockSize - 1) / BlockSize) * BlockSize;
  Buffer     = AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    return FALSE;
  }

  Status = BlockIo->ReadBlocks (
                      BlockIo,
                      BlockIo->Media->MediaId,
                      0,
                      BufferSize,
                      Buffer
                      );
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return FALSE;
  }

  // Check magic at offset 0x800 + 4 (struct bootloader_control: slot_suffix[4] then magic[4])
  Magic = *(UINT32 *)(Buffer + Offset + 4);
  if (Magic != BOOT_CTRL_MAGIC) {
    // Try offset 0 + 4 in case BCB starts at offset 0
    if (*(UINT32 *)(Buffer + 4) == BOOT_CTRL_MAGIC) {
      Offset = 0;
    } else {
      FreePool (Buffer);
      return FALSE;
    }
  }

  UINT8 *Ctrl = Buffer + Offset;
  // Ctrl layout:
  // Offset 0: slot_suffix[4] -> set to "_b\0\0"
  Ctrl[0] = '_';
  Ctrl[1] = 'b';
  Ctrl[2] = '\0';
  Ctrl[3] = '\0';

  // Offset 4: magic (0x42414342) - preserved
  // Offset 8: version (1) - preserved
  // Offset 9..11: flags/reserved - preserved
  // Offset 12..15: slot_info[0..3]
  // slot_info[0] (Slot A): lower priority to 14
  Ctrl[12] = (Ctrl[12] & 0xF0) | 0x0E;

  // slot_info[1] (Slot B): priority=15, tries=7, successful_boot=1 -> 0xFF
  Ctrl[13] = 0xFF;

  // CRC32 is calculated over the 28 bytes preceding crc32_le (offset 0 to 27)
  Status = gBS->CalculateCrc32 (Ctrl, 28, &NewCrc32);
  if (!EFI_ERROR (Status)) {
    *(UINT32 *)(Ctrl + 28) = NewCrc32;
  }

  // Write updated blocks back to misc partition
  Status = BlockIo->WriteBlocks (
                      BlockIo,
                      BlockIo->Media->MediaId,
                      0,
                      BufferSize,
                      Buffer
                      );

  BlockIo->FlushBlocks (BlockIo);
  FreePool (Buffer);

  return !EFI_ERROR (Status);
}

STATIC
VOID
ShowActionMessage (
  IN CONST CHAR8                   *Title,
  IN CONST CHAR8                   *Subtitle,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  AccentColor
  )
{
  UINTN PopX = 100;
  UINTN PopY = 850;
  UINTN PopW = 880;
  UINTN PopH = 240;

  BltFillRect (PopX, PopY, PopW, PopH, COLOR_CARD_BG);
  BltDrawBorder (PopX, PopY, PopW, PopH, 4, AccentColor);
  BltDrawStringCentered (PopY + 50, Title, COLOR_WHITE, COLOR_CARD_BG, 3);
  BltDrawStringCentered (PopY + 130, Subtitle, AccentColor, COLOR_CARD_BG, 2);
}

STATIC
VOID
ExecuteMenuAction (IN UINTN Index)
{
  EFI_STATUS Status;

  switch (Index) {
  case 0: // Boot Android LineageOS (Slot B)
    ShowActionMessage ("Checking Storage...", "Locating Slot B / misc partition", COLOR_ACCENT_MAC);
    gBS->Stall (600000);

    if (SwitchSlotInMisc ()) {
      ShowActionMessage ("Slot B Activated!", "misc partition updated! Cold Rebooting...", COLOR_GREEN);
      *(volatile UINT32 *)0x146AA000ULL = 0;
      gBS->Stall (1500000);
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    } else {
      ShowActionMessage ("Slot Switch via Recovery", "Rebooting to Recovery to switch slot", COLOR_AMBER);
      *(volatile UINT32 *)0x146AA000ULL = 0x77665502; // Qualcomm Recovery cookie
      gBS->Stall (1500000);
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    }
    break;

  case 1: // Reboot to Recovery
    ShowActionMessage ("Rebooting...", "Android Recovery Mode (0x77665502)", COLOR_RED);
    *(volatile UINT32 *)0x146AA000ULL = 0x77665502; // Qualcomm Recovery cookie
    gBS->Stall (1200000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    break;

  case 2: // Boot Linux (UEFI Shell / Boot Manager)
    ShowActionMessage ("Launching...", "UEFI Boot Manager / Shell", COLOR_ACCENT_UB);
    gBS->Stall (1000000);
    {
      EFI_BOOT_MANAGER_LOAD_OPTION LoadOption;
      Status = MsBootOptionsLibGetBootManagerMenu (&LoadOption, NULL);
      if (!EFI_ERROR (Status)) {
        EfiBootManagerBoot (&LoadOption);
        EfiBootManagerFreeLoadOption (&LoadOption);
      }
    }
    break;

  case 3: // Reboot to Fastboot Mode
    ShowActionMessage ("Rebooting...", "Fastboot Bootloader Mode (0x77665500)", COLOR_ACCENT_MAC);
    *(volatile UINT32 *)0x146AA000ULL = 0x77665500; // Qualcomm Fastboot cookie
    gBS->Stall (1200000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    break;

  case 4: // Reboot Device (Cold Reset)
    ShowActionMessage ("Restarting...", "System Cold Reset", COLOR_AMBER);
    *(volatile UINT32 *)0x146AA000ULL = 0;
    gBS->Stall (1200000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    break;

  case 5: // Power Off (Shutdown)
    ShowActionMessage ("Shutting Down...", "Safe Hardware Power Off", (EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ 0xA8, 0x55, 0xF7, 0x00 });
    gBS->Stall (1000000);
    gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    break;

  default:
    break;
  }
}

VOID
RunDualBootMenu ()
{
  UINTN   SelectedIndex  = 0;
  BOOLEAN VolUpLive      = FALSE;
  BOOLEAN VolDownLive    = FALSE;
  BOOLEAN PowerLive      = FALSE;
  BOOLEAN PrevVolUp      = FALSE;
  BOOLEAN PrevVolDown    = FALSE;
  BOOLEAN PrevPower      = FALSE;
  UINT8   LastPonVal     = 0x10;
  UINT8   LastGpioVal    = 0x01;

  // Initialize GOP Protocol
  if (mGopProtocol == NULL) {
    PostGopSetup (&mGopProtocol);
  }

  // Connect Consoles & All Controllers
  EfiBootManagerConnectConsoleVariable (ConIn);
  EfiBootManagerConnectConsoleVariable (ConOut);
  EfiBootManagerConnectAll ();

  // Allocate reusable Blt buffers in system RAM
  mCardBltBuffer = AllocatePool (CARD_W * CARD_H * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  mLineBltBuffer = AllocatePool (SCREEN_W * 100 * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  // Render Full UI
  DrawFullUI (SelectedIndex);
  DrawHardwareKeyStatus (FALSE, FALSE, FALSE, LastPonVal, LastGpioVal);

  while (TRUE) {
    UINTN   OldIndex       = SelectedIndex;
    BOOLEAN NeedRedrawCard = FALSE;
    BOOLEAN VolUpPressed   = FALSE;
    BOOLEAN VolDownPressed = FALSE;
    BOOLEAN PowerPressed   = FALSE;
    EFI_INPUT_KEY Key;

    // 1. USB Keyboard Input
    if (gST->ConIn != NULL) {
      EFI_STATUS Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status)) {
        if (Key.ScanCode == SCAN_UP || Key.UnicodeChar == L'w' || Key.UnicodeChar == L'W') {
          SelectedIndex  = (SelectedIndex + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
          NeedRedrawCard = TRUE;
        } else if (Key.ScanCode == SCAN_DOWN || Key.UnicodeChar == L's' || Key.UnicodeChar == L'S') {
          SelectedIndex  = (SelectedIndex + 1) % NUM_MENU_ITEMS;
          NeedRedrawCard = TRUE;
        } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.UnicodeChar == L' ') {
          ExecuteMenuAction (SelectedIndex);
        } else if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= (CHAR16)(L'0' + NUM_MENU_ITEMS)) {
          SelectedIndex = (UINTN)(Key.UnicodeChar - L'1');
          ExecuteMenuAction (SelectedIndex);
        }
      }
    }

    // 2. Hardware SPMI Buttons
    // Channel 407: PMK8350 PON (0x80 = Power, 0x40 = Vol Down)
    // Channel 434: PM8350 GPIO6 (active-low bit 0 == 0 is Vol Up)
    UINT8 PonVal  = 0x10;
    UINT8 GpioVal = 0x01;

    if (SpmiReadRegister (407, 0x10, &PonVal)) {
      LastPonVal = PonVal;
      if (PonVal & 0x80) {
        PowerPressed = TRUE;
      }
      if (PonVal & 0x40) {
        VolDownPressed = TRUE;
      }
    }

    if (SpmiReadRegister (434, 0x10, &GpioVal)) {
      LastGpioVal = GpioVal;
      if ((GpioVal & 0x01) == 0) {
        VolUpPressed = TRUE;
      }
    }

    // Arm "Live" state only after key is observed released once
    if (!VolUpPressed) {
      VolUpLive = TRUE;
    }
    if (!VolDownPressed) {
      VolDownLive = TRUE;
    }
    if (!PowerPressed) {
      PowerLive = TRUE;
    }

    // Trigger on Rising Edge
    if (VolUpLive && VolUpPressed && !PrevVolUp) {
      SelectedIndex  = (SelectedIndex + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
      NeedRedrawCard = TRUE;
    }
    if (VolDownLive && VolDownPressed && !PrevVolDown) {
      SelectedIndex  = (SelectedIndex + 1) % NUM_MENU_ITEMS;
      NeedRedrawCard = TRUE;
    }
    if (PowerLive && PowerPressed && !PrevPower) {
      ExecuteMenuAction (SelectedIndex);
    }

    // Flicker-free Partial Updates:
    // Update menu cards only if selection changed
    if (NeedRedrawCard && OldIndex != SelectedIndex) {
      DrawMenuCard (OldIndex, FALSE);
      DrawMenuCard (SelectedIndex, TRUE);
    }

    // Update hardware status line only if key states changed
    if (VolUpPressed != PrevVolUp || VolDownPressed != PrevVolDown || PowerPressed != PrevPower) {
      DrawHardwareKeyStatus (VolUpPressed, VolDownPressed, PowerPressed, LastPonVal, LastGpioVal);
    }

    PrevVolUp   = VolUpPressed;
    PrevVolDown = VolDownPressed;
    PrevPower   = PowerPressed;

    gBS->Stall (25000); // 25ms polling interval (40 Hz responsiveness)
  }
}

VOID
EFIAPI
PlatformBootManagerPriorityBoot (IN OUT UINT16 **BootNext)
{
  UnregisterKeyCallback ();
  RunDualBootMenu ();
}

VOID
EFIAPI
PlatformBootManagerWaitCallback (IN UINT16 TimeoutRemain)
{
}

VOID
EFIAPI
PlatformBootManagerUnableToBoot ()
{
  DeviceBootManagerUnableToBoot ();
  RunDualBootMenu ();
}

VOID
EFIAPI
PlatformBootManagerBdsEntry ()
{
  EFI_STATUS Status;

  Status = SetupSecureBoot ();
  ASSERT_EFI_ERROR (Status);

  UpdateAcpiTables ();
  DeviceBootManagerBdsEntry ();
}
