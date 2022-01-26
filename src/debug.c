#if DEBUGGING

#include "global.h"
#include "list_menu.h"
#include "main.h"
#include "menu.h"
#include "script.h"
#include "sound.h"
#include "strings.h"
#include "task.h"
#include "constants/songs.h"
#include "data.h"
#include "map_name_popup.h"
#include "overworld.h"
#include "m4a.h"
#include "palette.h"
#include "malloc.h"
#include "gpu_regs.h"
#include "field_weather.h"
#include "scanline_effect.h"
#include "constants/rgb.h"
#include "string_util.h"
#include "bg.h"
#include "script_pokemon_util.h"

#include "decompress.h"
#include "pokemon_icon.h"
#include "battle.h"
#include "trainer_pokemon_sprites.h"

#include "item.h"
#include "item_icon.h"
#include "constants/items.h"

#define DEBUG_MAIN_MENU_HEIGHT 7
#define DEBUG_MAIN_MENU_WIDTH 11
#define DEBUG_MON_X 16
#define DEBUG_MON_Y 16
#define DEBUG_MON_BACK_X 84
#define DEBUG_MON_BACK_Y 16
#define DEBUG_ICON_X 148
#define DEBUG_ICON_Y 16
#define DEBUG_MON_SHINY 0
#define DEBUG_MON_NORMAL 9
#define DEBUG_ITEM_X 24
#define DEBUG_ITEM_Y 64

static const u16 sBgColor[] = {RGB_WHITE};

void Debug_ShowMainMenu(void);
static void Debug_DestroyMainMenu(u8);
static void DebugAction_Cancel(u8);
static void DebugAction_Pokemon(u8);
static void DebugAction_Item(u8);
static void DebugTask_HandleMainMenuInput(u8);

void Handle_Input_Debug_Pokemon(u8);
void Handle_Input_Debug_Item(u8);
void Exit_Debug_Pokemon(u8);
void Exit_Debug_Item(u8);
void CB2_Debug_Pokemon(void);
void CB2_Debug_Runner(void);
void CB2_Debug_Item(void);
void ResetBGs_Debug_Menu(u16);

enum {
    DEBUG_MENU_ITEM_POKEMON,
    DEBUG_MENU_ITEM_ITEM,
    DEBUG_MENU_ITEM_CANCEL
};

static const u8 gDebugText_Pokemon[] = _("Pokemon");
static const u8 gDebugText_Item[] = _("Item");
static const u8 gDebugText_Cancel[] = _("Cancel");

static const struct ListMenuItem sDebugMenuItems[] =
{
    [DEBUG_MENU_ITEM_POKEMON] = {gDebugText_Pokemon, DEBUG_MENU_ITEM_POKEMON},
    [DEBUG_MENU_ITEM_ITEM] = {gDebugText_Item, DEBUG_MENU_ITEM_ITEM},
    [DEBUG_MENU_ITEM_CANCEL] = {gDebugText_Cancel, DEBUG_MENU_ITEM_CANCEL}
};

static void (*const sDebugMenuActions[])(u8) =
{
    [DEBUG_MENU_ITEM_POKEMON] = DebugAction_Pokemon,
    [DEBUG_MENU_ITEM_ITEM] = DebugAction_Item,
    [DEBUG_MENU_ITEM_CANCEL] = DebugAction_Cancel
};

static const struct WindowTemplate sDebugMenuWindowTemplate =
{
    .bg = 0,
    .tilemapLeft = 1,
    .tilemapTop = 1,
    .width = DEBUG_MAIN_MENU_WIDTH,
    .height = 2 * DEBUG_MAIN_MENU_HEIGHT,
    .paletteNum = 15,
    .baseBlock = 1,
};

static const struct ListMenuTemplate sDebugMenuListTemplate =
{
    .items = sDebugMenuItems,
    .moveCursorFunc = ListMenuDefaultCursorMoveFunc,
    .totalItems = ARRAY_COUNT(sDebugMenuItems),
    .maxShowed = DEBUG_MAIN_MENU_HEIGHT,
    .windowId = 0,
    .header_X = 0,
    .item_X = 8,
    .cursor_X = 0,
    .upText_Y = 1,
    .cursorPal = 2,
    .fillValue = 1,
    .cursorShadowPal = 3,
    .lettersSpacing = 1,
    .itemVerticalPadding = 0,
    .scrollMultiple = LIST_NO_MULTIPLE_SCROLL,
    .fontId = 1,
    .cursorKind = 0
};

struct PokemonDebugMenu
{
    u16 currentmonId;
    u8 currentmonWindowId;
    u8 InstructionsWindowId;
    u8 frontspriteId;
    u8 backspriteId;
    u8 iconspriteId;
    u8 isshiny;
};

struct ItemDebugMenu
{
    u16 currentitemId;
    u8 currentitemWindowId;
    u8 InstructionsWindowId;
    u8 itemspriteId;
};

static const struct WindowTemplate sCurrentTitleTemplate =
{
    .bg = 0,
    .tilemapLeft =1,
    .tilemapTop = 0,
    .width = 14,
    .height = 2,
    .paletteNum = 0xF,
    .baseBlock = 0x200
};

static const struct WindowTemplate sDebugPokemonInstructionsTemplate =
{
    .bg = 0,
    .tilemapLeft =1,
    .tilemapTop = 203,
    .width = 14,
    .height = 8,
    .paletteNum = 0xF,
    .baseBlock = 0x300
};

static struct PokemonDebugMenu *GetStructPtr(u8 taskId)
{
    u8 *taskDataPtr = (u8*)(&gTasks[taskId].data[0]);

    return (struct PokemonDebugMenu*)(T1_READ_PTR(taskDataPtr));
}

static struct ItemDebugMenu *GetStructPtr2(u8 taskId)
{
    u8 *taskDataPtr = (u8*)(&gTasks[taskId].data[0]);

    return (struct ItemDebugMenu*)(T1_READ_PTR(taskDataPtr));
}

static void PadString(const u8 *src, u8 *dst)
{
    u32 i;

    for (i = 0; i < 17 && src[i] != EOS; i++)
        dst[i] = src[i];

    for (; i < 17; i++)
        dst[i] = CHAR_SPACE;

    dst[i] = EOS;
}


static void PrintOnCurrentMonWindow(u8 windowId, u16 monId)
{
    u8 text[POKEMON_NAME_LENGTH + 10];

    text[0] = CHAR_0 + monId / 100;
    text[1] = CHAR_0 + (monId % 100) / 10;
    text[2] = CHAR_0 + (monId % 100) % 10;
    text[3] = CHAR_SPACE;
    text[4] = CHAR_HYPHEN;
    text[5] = CHAR_SPACE;

    StringCopy(&text[6], gSpeciesNames[monId]);

    FillWindowPixelBuffer(windowId, 0x11);
    AddTextPrinterParameterized(windowId, 1, text, 0, 0, 0, NULL);
    CopyWindowToVram(windowId, 3);
}

static void PrintOnCurrentItemWindow(u8 windowId, u16 itemId)
{
    u8 text[ITEM_NAME_LENGTH + 10];

    text[0] = CHAR_0 + itemId / 100;
    text[1] = CHAR_0 + (itemId % 100) / 10;
    text[2] = CHAR_0 + (itemId % 100) % 10;
    text[3] = CHAR_SPACE;
    text[4] = CHAR_HYPHEN;
    text[5] = CHAR_SPACE;

    PadString(ItemId_GetName(itemId), &text[6]);

    FillWindowPixelBuffer(windowId, 0x11);
    AddTextPrinterParameterized(windowId, 1, text, 0, 0, 0, NULL);
    CopyWindowToVram(windowId, 3);
}

static void PrintInstructionsOnWindow(u8 windowId)
{
    u8 text[] = _("A - Play Cry\nSelect - Shiny\nStart - Give Pokemon\nR - Animate$");

    FillWindowPixelBuffer(windowId, 0x11);
    AddTextPrinterParameterized(windowId, 1, text, 0, 0, 0, NULL);
    CopyWindowToVram(windowId, 3);
}

static void PrintInstructionsOnItemWindow(u8 windowId)
{
    u8 text[] = _("Start - Give Item$");

    FillWindowPixelBuffer(windowId, 0x11);
    AddTextPrinterParameterized(windowId, 1, text, 0, 0, 0, NULL);
    CopyWindowToVram(windowId, 3);
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void SetStructPtr(u8 taskId, void *ptr)
{
    u32 structPtr = (u32)(ptr);
    u8 *taskDataPtr = (u8*)(&gTasks[taskId].data[0]);

    taskDataPtr[0] = structPtr >> 0;
    taskDataPtr[1] = structPtr >> 8;
    taskDataPtr[2] = structPtr >> 16;
    taskDataPtr[3] = structPtr >> 24;
}

void Debug_ShowMainMenu(void) {
    struct ListMenuTemplate menuTemplate;
    u8 windowId;
    u8 menuTaskId;
    u8 inputTaskId;

    // create window
    HideMapNamePopUpWindow();
    LoadMessageBoxAndBorderGfx();
    windowId = AddWindow(&sDebugMenuWindowTemplate);
    DrawStdWindowFrame(windowId, FALSE);

    // create list menu
    menuTemplate = sDebugMenuListTemplate;
    menuTemplate.windowId = windowId;
    menuTaskId = ListMenuInit(&menuTemplate, 0, 0);

    // draw everything
    CopyWindowToVram(windowId, 3);

    // create input handler task
    inputTaskId = CreateTask(DebugTask_HandleMainMenuInput, 3);
    gTasks[inputTaskId].data[0] = menuTaskId;
    gTasks[inputTaskId].data[1] = windowId;
}

static void Debug_DestroyMainMenu(u8 taskId)
{
    DestroyListMenuTask(gTasks[taskId].data[0], NULL, NULL);
    ClearStdWindowAndFrame(gTasks[taskId].data[1], TRUE);
    RemoveWindow(gTasks[taskId].data[1]);
    DestroyTask(taskId);
    EnableBothScriptContexts();
}

static void DebugTask_HandleMainMenuInput(u8 taskId)
{
    void (*func)(u8);
    u32 input = ListMenu_ProcessInput(gTasks[taskId].data[0]);

    if (gMain.newKeys & A_BUTTON)
    {
        PlaySE(SE_SELECT);
        if ((func = sDebugMenuActions[input]) != NULL)
            func(taskId);
    }
    else if (gMain.newKeys & B_BUTTON)
    {
        PlaySE(SE_SELECT);
        Debug_DestroyMainMenu(taskId);
    }
}

static void DebugAction_Pokemon(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        PlayRainStoppingSoundEffect();
        CleanupOverworldWindowsAndTilemaps();
        SetMainCallback2(CB2_Debug_Pokemon);
    }
}

static void DebugAction_Item(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        PlayRainStoppingSoundEffect();
        CleanupOverworldWindowsAndTilemaps();
        SetMainCallback2(CB2_Debug_Item);
    }
}

static void DebugAction_Cancel(u8 taskId)
{
    Debug_DestroyMainMenu(taskId);
}

void CB2_Debug_Pokemon(void)
{
    u8 taskId;
    const struct CompressedSpritePalette *palette;
    struct PokemonDebugMenu *data;

    switch (gMain.state)
    {
        case 0:
        default:
            SetVBlankCallback(NULL);
            FreeMonSpritesGfx();
            ResetBGs_Debug_Menu(0);
            DmaFillLarge16(3, 0, (u8 *)VRAM, VRAM_SIZE, 0x1000)
            DmaClear32(3, OAM, OAM_SIZE);
            DmaClear16(3, PLTT, PLTT_SIZE);
            gMain.state = 1;
            break;
        case 1:
            ScanlineEffect_Stop();
            ResetTasks();
            ResetSpriteData();
            ResetPaletteFade();
            FreeAllSpritePalettes();
            gReservedSpritePaletteCount = 8;
            ResetAllPicSprites();
            gMain.state++;
            break;
        case 2:
            AllocateMonSpritesGfx();

            LoadPalette(sBgColor, 0, 2);
            LoadMonIconPalettes();
            //LoadPalette(GetOverworldTextboxPalettePtr(), 0xf0, 16);

            SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
            ShowBg(0);
            ShowBg(1);

            //input task handler
            taskId = CreateTask(Handle_Input_Debug_Pokemon, 0);

            data = AllocZeroed(sizeof(struct PokemonDebugMenu));
            SetStructPtr(taskId, data);

            data->currentmonId = 1;
            data->currentmonWindowId = AddWindow(&sCurrentTitleTemplate);
            PutWindowTilemap(data->currentmonWindowId);
            PrintOnCurrentMonWindow(data->currentmonWindowId, data->currentmonId);

            data->InstructionsWindowId = AddWindow(&sDebugPokemonInstructionsTemplate);
            PutWindowTilemap(data->InstructionsWindowId);
            PrintInstructionsOnWindow(data->InstructionsWindowId);

            HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
            data->isshiny = DEBUG_MON_NORMAL;
            palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
            LoadCompressedSpritePalette(palette);
            SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
            gMultiuseSpriteTemplate.paletteTag = palette->tag;
            data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
            gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
            gSprites[data->frontspriteId].oam.priority = 0;

            HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
            palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
            LoadCompressedSpritePalette(palette);
            SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
            gMultiuseSpriteTemplate.paletteTag = palette->tag;
            data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
            gSprites[data->backspriteId].callback = SpriteCallbackDummy;
            gSprites[data->backspriteId].oam.priority = 0;

            //Icon Sprite
            data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
            gSprites[data->iconspriteId].oam.priority = 0;

            gMain.state++;
            break;
        case 3:
            EnableInterrupts(1);
            SetVBlankCallback(VBlankCB);
            SetMainCallback2(CB2_Debug_Runner);
            m4aMPlayVolumeControl(&gMPlayInfo_BGM, 0xFFFF, 0x80);
            break;
    }
}

void CB2_Debug_Runner(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

void CB2_Debug_Item(void)
{
    u8 taskId;
    struct ItemDebugMenu *data;

    switch (gMain.state)
    {
        case 0:
        default:
            SetVBlankCallback(NULL);
            ResetBGs_Debug_Menu(0);
            DmaFillLarge16(3, 0, (u8 *)VRAM, VRAM_SIZE, 0x1000)
            DmaClear32(3, OAM, OAM_SIZE);
            DmaClear16(3, PLTT, PLTT_SIZE);
            gMain.state = 1;
            break;
        case 1:
            ScanlineEffect_Stop();
            ResetTasks();
            ResetSpriteData();
            ResetPaletteFade();
            FreeAllSpritePalettes();
            gReservedSpritePaletteCount = 8;
            ResetAllPicSprites();
            gMain.state++;
            break;
        case 2:
            LoadPalette(sBgColor, 0, 2);

            SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
            ShowBg(0);
            ShowBg(1);

            //input task handler
            taskId = CreateTask(Handle_Input_Debug_Item, 0);

            data = AllocZeroed(sizeof(struct ItemDebugMenu));
            SetStructPtr(taskId, data);

            data->currentitemId = 1;
            data->currentitemWindowId = AddWindow(&sCurrentTitleTemplate);
            PutWindowTilemap(data->currentitemWindowId);
            PrintOnCurrentItemWindow(data->currentitemWindowId, data->currentitemId);

            data->itemspriteId = AddItemIconSprite(102, 102, data->currentitemId);
            gSprites[data->itemspriteId].x2 = DEBUG_ITEM_X;
            gSprites[data->itemspriteId].y2 = DEBUG_ITEM_Y;

            data->InstructionsWindowId = AddWindow(&sDebugPokemonInstructionsTemplate);
            PutWindowTilemap(data->InstructionsWindowId);
            PrintInstructionsOnItemWindow(data->InstructionsWindowId);

            gMain.state++;
            break;
        case 3:
            EnableInterrupts(1);
            SetVBlankCallback(VBlankCB);
            SetMainCallback2(CB2_Debug_Runner);
            m4aMPlayVolumeControl(&gMPlayInfo_BGM, 0xFFFF, 0x80);
            break;
    }
}

void ResetBGs_Debug_Menu(u16 a)
{
    if (!(a & DISPCNT_BG0_ON))
    {
        ClearGpuRegBits(0, DISPCNT_BG0_ON);
        SetGpuReg(REG_OFFSET_BG0CNT, 0);
        SetGpuReg(REG_OFFSET_BG0HOFS, 0);
        SetGpuReg(REG_OFFSET_BG0VOFS, 0);
    }
    if (!(a & DISPCNT_BG1_ON))
    {
        ClearGpuRegBits(0, DISPCNT_BG1_ON);
        SetGpuReg(REG_OFFSET_BG1CNT, 0);
        SetGpuReg(REG_OFFSET_BG1HOFS, 0);
        SetGpuReg(REG_OFFSET_BG1VOFS, 0);
    }
    if (!(a & DISPCNT_BG2_ON))
    {
        ClearGpuRegBits(0, DISPCNT_BG2_ON);
        SetGpuReg(REG_OFFSET_BG2CNT, 0);
        SetGpuReg(REG_OFFSET_BG2HOFS, 0);
        SetGpuReg(REG_OFFSET_BG2VOFS, 0);
    }
    if (!(a & DISPCNT_BG3_ON))
    {
        ClearGpuRegBits(0, DISPCNT_BG3_ON);
        SetGpuReg(REG_OFFSET_BG3CNT, 0);
        SetGpuReg(REG_OFFSET_BG3HOFS, 0);
        SetGpuReg(REG_OFFSET_BG3VOFS, 0);
    }
    if (!(a & DISPCNT_OBJ_ON))
    {
        ClearGpuRegBits(0, DISPCNT_OBJ_ON);
        ResetSpriteData();
        FreeAllSpritePalettes();
        gReservedSpritePaletteCount = 8;
    }
}

void Handle_Input_Debug_Pokemon(u8 taskId)
{

    struct PokemonDebugMenu *data = GetStructPtr(taskId);
    const struct CompressedSpritePalette *palette;
    struct Sprite *Frontsprite = &gSprites[data->frontspriteId];

    if ((gMain.newKeys & A_BUTTON))
    {
        PlayCryInternal(data->currentmonId, 0, 120, 10, 0);
    }
    else if (gMain.newKeys & START_BUTTON)
    {
        ScriptGiveMon(data->currentmonId, 30, 0, 0, 0, 0);
        PlaySE(SE_SUCCESS);
    }
    else if (gMain.newKeys & SELECT_BUTTON)
    {


        if( data->isshiny == 9)
        {
            data->isshiny = DEBUG_MON_SHINY;
            PlaySE(SE_SHINY);
        }
        else
        {
            data->isshiny = DEBUG_MON_NORMAL;
        }

        DestroySprite(&gSprites[data->frontspriteId]);
        DestroySprite(&gSprites[data->backspriteId]);
        DestroySprite(&gSprites[data->iconspriteId]);

        FreeMonSpritesGfx();
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetAllPicSprites();
        AllocateMonSpritesGfx();

        FreeAllSpritePalettes();
        FreeMonIconPalettes();

        LoadMonIconPalettes();

        HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
        gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
        gSprites[data->frontspriteId].oam.priority = 0;
        
        HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
        gSprites[data->backspriteId].callback = SpriteCallbackDummy;
        gSprites[data->backspriteId].oam.priority = 0;

        //Icon Sprite
        data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
        gSprites[data->iconspriteId].oam.priority = 0;

    }
    else if (gMain.newKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Exit_Debug_Pokemon;
        PlaySE(SE_PC_OFF);
    }
    else if (gMain.newKeys & DPAD_DOWN) // || gMain.heldKeys & DPAD_DOWN)
    {
        data->currentmonId++;
        if (data->currentmonId > (NUM_SPECIES - 1))
        {
            data->currentmonId = NUM_SPECIES - 1;
        }
        PrintOnCurrentMonWindow(data->currentmonWindowId, data->currentmonId);

        DestroySprite(&gSprites[data->frontspriteId]);
        DestroySprite(&gSprites[data->backspriteId]);
        DestroySprite(&gSprites[data->iconspriteId]);

        FreeMonSpritesGfx();
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetAllPicSprites();
        AllocateMonSpritesGfx();

        FreeAllSpritePalettes();
        FreeMonIconPalettes();

        LoadMonIconPalettes();

        HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
        gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
        gSprites[data->frontspriteId].oam.priority = 0;

        HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
        gSprites[data->backspriteId].callback = SpriteCallbackDummy;
        gSprites[data->backspriteId].oam.priority = 0;

        //Icon Sprite
        data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
        gSprites[data->iconspriteId].oam.priority = 0;

        PlaySE(SE_DEX_SCROLL);

        while (!(gMain.intrCheck & INTR_FLAG_VBLANK));

    }
    else if (gMain.newKeys & DPAD_UP) // || gMain.heldKeys & DPAD_UP)
    {
        if (data->currentmonId == 1)
        {
            data->currentmonId = 1;
        }
        else
        {
            data->currentmonId--;
        }
        
        PrintOnCurrentMonWindow(data->currentmonWindowId, data->currentmonId);

        DestroySprite(&gSprites[data->frontspriteId]);
        DestroySprite(&gSprites[data->backspriteId]);
        DestroySprite(&gSprites[data->iconspriteId]);

        FreeMonSpritesGfx();
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetAllPicSprites();
        AllocateMonSpritesGfx();

        FreeAllSpritePalettes();
        FreeMonIconPalettes();

        LoadMonIconPalettes();

        HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
        gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
        gSprites[data->frontspriteId].oam.priority = 0;

        HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
        gSprites[data->backspriteId].callback = SpriteCallbackDummy;
        gSprites[data->backspriteId].oam.priority = 0;

        //Icon Sprite
        data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
        gSprites[data->iconspriteId].oam.priority = 0;

        PlaySE(SE_DEX_SCROLL);

    }
    else if (gMain.newKeys & DPAD_LEFT) // || gMain.heldKeys & DPAD_LEFT)
    {
        if (data->currentmonId < 10)
        {
            data->currentmonId = 1;
        }
        else
        {
            data->currentmonId = data->currentmonId - 10;
        }

        PrintOnCurrentMonWindow(data->currentmonWindowId, data->currentmonId);

        DestroySprite(&gSprites[data->frontspriteId]);
        DestroySprite(&gSprites[data->backspriteId]);
        DestroySprite(&gSprites[data->iconspriteId]);

        FreeMonSpritesGfx();
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetAllPicSprites();
        AllocateMonSpritesGfx();

        FreeAllSpritePalettes();
        FreeMonIconPalettes();

        LoadMonIconPalettes();

        HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
        gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
        gSprites[data->frontspriteId].oam.priority = 0;

        HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
        gSprites[data->backspriteId].callback = SpriteCallbackDummy;
        gSprites[data->backspriteId].oam.priority = 0;

        //Icon Sprite
        data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
        gSprites[data->iconspriteId].oam.priority = 0;

        PlaySE(SE_DEX_PAGE);

    }
    else if (gMain.newKeys & DPAD_RIGHT) // || gMain.heldKeys & DPAD_RIGHT)
    {
        data->currentmonId = data->currentmonId + 10;
        if (data->currentmonId > (NUM_SPECIES - 1))
        {
            data->currentmonId = NUM_SPECIES - 1;
        }
        PrintOnCurrentMonWindow(data->currentmonWindowId, data->currentmonId);

        DestroySprite(&gSprites[data->frontspriteId]);
        DestroySprite(&gSprites[data->backspriteId]);
        DestroySprite(&gSprites[data->iconspriteId]);

        FreeMonSpritesGfx();
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetAllPicSprites();
        AllocateMonSpritesGfx();

        FreeAllSpritePalettes();
        FreeMonIconPalettes();

        LoadMonIconPalettes();

        HandleLoadSpecialPokePic(&gMonFrontPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[1], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 1);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->frontspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_X + 32, DEBUG_MON_Y + 40, 0);
        gSprites[data->frontspriteId].callback = SpriteCallbackDummy;
        gSprites[data->frontspriteId].oam.priority = 0;

        HandleLoadSpecialPokePic(&gMonBackPicTable[data->currentmonId], gMonSpritesGfxPtr->sprites.ptr[2], data->currentmonId, 0);
        palette = GetMonSpritePalStructFromOtIdPersonality(data->currentmonId, 0, data->isshiny);
        LoadCompressedSpritePalette(palette);
        SetMultiuseSpriteTemplateToPokemon(data->currentmonId, 2);
        gMultiuseSpriteTemplate.paletteTag = palette->tag;
        data->backspriteId = CreateSprite(&gMultiuseSpriteTemplate, DEBUG_MON_BACK_X + 32, DEBUG_MON_BACK_Y + 40, 0);
        gSprites[data->backspriteId].callback = SpriteCallbackDummy;
        gSprites[data->backspriteId].oam.priority = 0;

        //Icon Sprite
        data->iconspriteId = CreateMonIcon(data->currentmonId, SpriteCB_MonIcon, DEBUG_ICON_X + 32, DEBUG_ICON_Y + 40, 4, data->isshiny);
        gSprites[data->iconspriteId].oam.priority = 0;

        PlaySE(SE_DEX_PAGE);

    }
    else if (gMain.newKeys & R_BUTTON)
    {
        PokemonSummaryDoMonAnimation(Frontsprite, data->currentmonId, 0);
    }
    else
    {
    }
}

void Handle_Input_Debug_Item(u8 taskId)
{

    struct ItemDebugMenu *data = GetStructPtr2(taskId);

    if ((gMain.newKeys & A_BUTTON))
    {

    }
    else if (gMain.newKeys & START_BUTTON)
    {
        AddBagItem(data->currentitemId,1);
        PlaySE(SE_SUCCESS);
    }
    else if (gMain.newKeys & SELECT_BUTTON)
    {

    }
    else if (gMain.newKeys & B_BUTTON)
    {
        BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 0x10, RGB_BLACK);
        gTasks[taskId].func = Exit_Debug_Item;
        PlaySE(SE_PC_OFF);
    }
    else if (gMain.newKeys & DPAD_DOWN)
    {
        data->currentitemId++;
        if (data->currentitemId > (ITEMS_COUNT - 1))
        {
            data->currentitemId = ITEMS_COUNT - 1;
        }

        PrintOnCurrentItemWindow(data->currentitemWindowId, data->currentitemId);

        DestroySprite(&gSprites[data->itemspriteId]);
        FreeSpriteTilesByTag(102);
        FreeSpritePaletteByTag(102);

        data->itemspriteId = AddItemIconSprite(102, 102, data->currentitemId);
        gSprites[data->itemspriteId].x2 = DEBUG_ITEM_X;
        gSprites[data->itemspriteId].y2 = DEBUG_ITEM_Y;

    }
    else if (gMain.newKeys & DPAD_UP)
    {
        if (data->currentitemId == 1)
        {
            data->currentitemId = 1;
        }
        else
        {
            data->currentitemId--;
        }
        
        PrintOnCurrentItemWindow(data->currentitemWindowId, data->currentitemId);

        DestroySprite(&gSprites[data->itemspriteId]);
        FreeSpriteTilesByTag(102);
        FreeSpritePaletteByTag(102);

        data->itemspriteId = AddItemIconSprite(102, 102, data->currentitemId);
        gSprites[data->itemspriteId].x2 = DEBUG_ITEM_X;
        gSprites[data->itemspriteId].y2 = DEBUG_ITEM_Y;

    }
    else if (gMain.newKeys & DPAD_LEFT)
    {
        if (data->currentitemId < 10)
        {
            data->currentitemId = 1;
        }
        else
        {
            data->currentitemId = data->currentitemId - 10;
        }

        PrintOnCurrentItemWindow(data->currentitemWindowId, data->currentitemId);

        DestroySprite(&gSprites[data->itemspriteId]);
        FreeSpriteTilesByTag(102);
        FreeSpritePaletteByTag(102);

        data->itemspriteId = AddItemIconSprite(102, 102, data->currentitemId);
        gSprites[data->itemspriteId].x2 = DEBUG_ITEM_X;
        gSprites[data->itemspriteId].y2 = DEBUG_ITEM_Y;

    }
    else if (gMain.newKeys & DPAD_RIGHT)
    {
        data->currentitemId = data->currentitemId + 10;
        if (data->currentitemId > (ITEMS_COUNT - 1))
        {
            data->currentitemId = ITEMS_COUNT - 1;
        }
        PrintOnCurrentItemWindow(data->currentitemWindowId, data->currentitemId);

        DestroySprite(&gSprites[data->itemspriteId]);
        FreeSpriteTilesByTag(102);
        FreeSpritePaletteByTag(102);

        data->itemspriteId = AddItemIconSprite(102, 102, data->currentitemId);
        gSprites[data->itemspriteId].x2 = DEBUG_ITEM_X;
        gSprites[data->itemspriteId].y2 = DEBUG_ITEM_Y;

    }
    else if (gMain.newKeys & R_BUTTON)
    {

    }
    else
    {
    }
}

void Exit_Debug_Pokemon(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        struct PokemonDebugMenu *data = GetStructPtr(taskId);
        Free(data);
        FreeMonSpritesGfx();
        DestroyTask(taskId);
        SetMainCallback2(CB2_ReturnToFieldWithOpenMenu);
        m4aMPlayVolumeControl(&gMPlayInfo_BGM, 0xFFFF, 0x100);
    }
}

void Exit_Debug_Item(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        struct ItemDebugMenu *data = GetStructPtr2(taskId);
        Free(data);
        FreeMonSpritesGfx();
        DestroyTask(taskId);
        SetMainCallback2(CB2_ReturnToFieldWithOpenMenu);
        m4aMPlayVolumeControl(&gMPlayInfo_BGM, 0xFFFF, 0x100);
    }
}


#endif