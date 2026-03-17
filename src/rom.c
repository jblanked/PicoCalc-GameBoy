#include "rom.h"
#include "sdcard.h"
#include "shared.h"
#include "flash.h"

#include "config.h"
#include LCD_INCLUDE

/**
 * Load ROM File
 *
 * Loads a Game Boy ROM file from the SD card into flash memory.
 * This makes the ROM available for the emulator to run.
 *
 * @param filename Name of the ROM file to load
 */
void load_cart_rom_file(char *filename)
{
    UINT br;
    uint8_t buffer[FLASH_SECTOR_SIZE];
    bool mismatch = false;
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    FIL fil;
    fr = f_open(&fil, filename, FA_READ);
    if (fr == FR_OK)
    {
        uint32_t flash_target_offset = FLASH_TARGET_OFFSET;
        uint32_t ctl_flash = 0;
        for (;;)
        {
            f_read(&fil, buffer, sizeof buffer, &br);
            if (br == 0)
                break; /* end of file */

            DBG_INFO("I Erasing target region...\n");
            flash_erase(flash_target_offset, FLASH_SECTOR_SIZE);
            DBG_INFO("I Programming target region...\n");
            flash_program(flash_target_offset, buffer, FLASH_SECTOR_SIZE);

            /* Read back target region and check programming */
            DBG_INFO("I Done. Reading back target region...\n");
            for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; i++)
            {
                if (rom[ctl_flash + i] != buffer[i])
                {
                    DBG_INFO("E Mismatch at address 0x%08X: read 0x%02X, expected 0x%02X\n",
                             (unsigned)(flash_target_offset + i),
                             rom[ctl_flash + i], buffer[i]);
                    mismatch = true;
                }
            }

            /* Next sector */
            ctl_flash += FLASH_SECTOR_SIZE;
            flash_target_offset += FLASH_SECTOR_SIZE;
        }
        if (!mismatch)
        {
            DBG_INFO("I Programming successful!\n");
        }
        else
        {
            DBG_INFO("E Programming failed!\n");
        }
    }
    else
    {
        DBG_INFO("E f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
    }

    fr = f_close(&fil);
    if (fr != FR_OK)
    {
        DBG_INFO("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    f_unmount(pSD->pcName);

    DBG_INFO("I load_cart_rom_file(%s) COMPLETE (%lu bytes)\n", filename, br);
}

/**
 * Display ROM Selection Page
 *
 * Displays one page of Game Boy ROM files found on the SD card.
 * Used by the ROM file selector interface.
 *
 * @param filename Array to store found filenames
 * @param num_page Page number to display (each page shows up to 22 files)
 * @return Number of files found on the page
 */
uint16_t rom_file_selector_display_page(char filename[22][256], uint16_t num_page)
{
    sd_card_t *pSD = sd_get_by_num(0);
    DIR dj;
    FILINFO fno;
    FRESULT fr;

    fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return 0;
    }

    /* clear the filenames array */
    for (uint8_t ifile = 0; ifile < 22; ifile++)
    {
        strcpy(filename[ifile], "");
    }

    /* search *.gb files */
    uint16_t num_file = 0;
    fr = f_findfirst(&dj, &fno, ".", "?*.gb");

    /* skip the first N pages */
    if (num_page > 0)
    {
        while (num_file < num_page * 22 && fr == FR_OK && fno.fname[0])
        {
            num_file++;
            fr = f_findnext(&dj, &fno);
        }
    }

    /* store the filenames of this page */
    num_file = 0;
    while (num_file < 22 && fr == FR_OK && fno.fname[0])
    {
        if (fno.fname[0] != '.')
        {
            /* Skip any file starting with dot. These are hidden files. */
            strcpy(filename[num_file], fno.fname);
            num_file++;
        }

        fr = f_findnext(&dj, &fno);
    }
    f_closedir(&dj);
    f_unmount(pSD->pcName);

/* display *.gb rom files on screen */
#ifdef LCD_CLEAR
    LCD_CLEAR();
#endif
    for (uint8_t ifile = 0; ifile < num_file; ifile++)
    {
        DBG_INFO("Game: %s\n", filename[ifile]);
#ifdef LCD_STRING
        LCD_STRING(20, ifile * 20, filename[ifile], 0xFFFF);
#endif
    }

    return num_file;
}

/**
 * ROM File Selector
 *
 * Presents a user interface to select a Game Boy ROM file to play.
 * Displays pages of up to 22 ROM files and allows navigation between them.
 * ROM files (.gb) should be placed in the root directory of the SD card.
 * The selected ROM will be loaded into flash memory for execution.
 */
void rom_file_selector()
{
    DBG_INFO("ROM File Selector: Starting...\n");
    uint16_t num_page = 0;
    char filename[22][256];
    uint16_t num_file;
    char buf[6];
    bool break_outer = false;

    /* display the first page with up to 22 rom files */
    num_file = rom_file_selector_display_page(filename, num_page);
    DBG_INFO("ROM File Selector: Found %d files on first page\n", num_file);

    /* select the first rom */
    uint8_t selected = 0;
    DBG_INFO("ROM File Selector: Waiting 5 seconds before highlighting first ROM\n");

    DBG_INFO("ROM File Selector: Highlighting first ROM: %s\n", filename[selected]);
    sprintf(buf, "%02d", selected + 1);
#ifdef LCD_STRING
    LCD_STRING(0, FRAME_BUFF_HEIGHT - 20, buf, 0xFFFF);
    LCD_STRING(0, (selected % 22) * 20, "=>", 0xFFFF);
#endif

    /* get user's input */
    bool up = true, down = true, left = true, right = true, a = true, b = true, select = true, start = true;
    while (true)
    {
        switch (wait_key())
        {
        case KEY_A:
        case KEY_B:
            DBG_INFO("ROM File Selector: A/B button pressed - loading ROM: %s\n", filename[selected]);

            rom_file_selector_display_page(filename, num_page);
            sprintf(buf, "Loading %s", filename[selected]);
#ifdef LCD_STRING
            LCD_STRING(0, FRAME_BUFF_HEIGHT - 20, buf, 0xFFFF);
#endif
            sleep_ms(150);

            load_cart_rom_file(filename[selected]);
            break_outer = true;
            break;

        case KEY_START:
            DBG_INFO("ROM File Selector: Start button pressed - resuming last game\n");
            break_outer = true;
            break;

        case KEY_UP:
            DBG_INFO("ROM File Selector: Up button - selecting previous ROM\n");
            rom_file_selector_display_page(filename, num_page);
#ifdef LCD_STRING
            LCD_STRING(0, (selected % 22) * 20, "", 0xFFFF);
#endif
            if (selected == 0)
            {
                selected = num_file - 1;
            }
            else
            {
                selected--;
            }
            DBG_INFO("ROM File Selector: Selected ROM: %s\n", filename[selected]);
            sprintf(buf, "%02d", selected + 1);
#ifdef LCD_STRING
            LCD_STRING(0, FRAME_BUFF_HEIGHT - 20, buf, 0xFFFF);
            LCD_STRING(0, (selected % 22) * 20, "=>", 0xFFFF);
#endif
            sleep_ms(150);
            break;

        case KEY_DOWN:

            DBG_INFO("ROM File Selector: Down button - selecting next ROM\n");
            rom_file_selector_display_page(filename, num_page);
            selected++;
            if (selected >= num_file)
                selected = 0;
            DBG_INFO("ROM File Selector: Selected ROM: %s\n", filename[selected]);
            sprintf(buf, "%02d", selected + 1);
#ifdef LCD_STRING
            LCD_STRING(0, FRAME_BUFF_HEIGHT - 20, buf, 0xFFFF);
            LCD_STRING(0, (selected % 22) * 20, "=>", 0xFFFF);
#endif
            sleep_ms(150);
            break;
        }

        if (break_outer)
            break;
    }

    DBG_INFO("ROM File Selector: Exiting selector\n");
}
