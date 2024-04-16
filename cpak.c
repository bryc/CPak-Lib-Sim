#include <stdio.h>
#include <stdint.h>

// C-Pak SRAM is 32 KiB
uint8_t SRAM[32768] = {0};

// IndexTable parser flags
//   When checking IndexTable, bit flags are used for each index:
//
//   0x01 = IndexTable 1 - Verified startIndex
//   0x02 = IndexTable 1 - Dupe index error
//   0x04 = IndexTable 2 - Verified startIndex
//   0x08 = IndexTable 2 - Dupe index error
//   0x10 = IndexTable 1 - Verified chain member (safe)
//   0x20 = IndexTable 2 - Verified chain member (safe)
//   0x40 = Backup is NOT equal to Primary
//   0x80 = 
//   Temp flags during initial steps
//   0x20 = IndexTable 2 - Index is referenced
//   0x40 = IndexTable 1 - Index is referenced
//   0x80 = NoteTable    - startIndex found
uint8_t indexFlags[128] = {0};

int check() {
    // Obtain startIndexes from NoteTable
    // Provides important context for IndexTable check
    for(int i = 0x300; i < 0x500; i += 32) {
        uint8_t si = SRAM[i + 7];
        if(si >= 5 && si <= 127) {
            // TempFlag: NoteTable - startIndex found
            indexFlags[si] |= 0x80;
        }
    }

    // Obtain nextIndex values within IndexTable
    // This is to isolate the startIndexes from table
    for(int i = 5; i < 128; i++) {
        // Primary
        uint8_t ni = SRAM[0x100 + (i*2) + 1];
        // Values < 5 must also be filtered, except 1
        if(ni != 1 && ni < 5) ni = i;
        // PermFlag: IndexTable 1 - Dupe index error
        if(indexFlags[ni] & 0x40) indexFlags[ni] |= 0x02;
        // TempFlag: IndexTable 1 - Index is referenced
        indexFlags[ni] |= 0x40;

        // Backup
        ni = SRAM[0x200 + (i*2) + 1];
        // Values < 5 must also be filtered, except 1
        if(ni != 1 && ni < 5) ni = i;
        // PermFlag: IndexTable 2 - Dupe index error
        if(indexFlags[ni] & 0x20) indexFlags[ni] |= 0x08;
        // TempFlag: IndexTable 2 - Index is referenced
        indexFlags[ni] |= 0x20;
    }

    // Mark the valid startIndexes
    for(int i = 5; i < 128; i++) {
        // NoteTable startIndex  +  IndexTable startIndex = good
        if(indexFlags[i] & 0x80 && !(indexFlags[i] & 0x40)) {
            indexFlags[i] |= 0x01;
        }
        // Same, but for backup
        if(indexFlags[i] & 0x80 && !(indexFlags[i] & 0x20)) {
            indexFlags[i] |= 0x04;
        }
        // Clear these bits, since we're done with them
        indexFlags[i] &= 0x1F; // clear bits 5+6+7
        // Mark whether backup differs from primary
        if(SRAM[0x100 + (i*2) + 1] != SRAM[0x200 + (i*2) + 1]) {
            indexFlags[i] |= 0x40;
        }
    }

    // Validate any index chains found
    for(int i = 5; i < 128; i++) {
        // Only check startIndex if seen in both sources
        if(indexFlags[i] & 0x05) {
            uint8_t ci = i, ni, nv, valid = 0, count = 0;
            printf("\n -> Chain @ %02X:\n", ci);
            while(ci != 1) {
                printf("%02X ", ci);
                count++;
                ni = SRAM[0x100 + (ci*2) + 1];
                nv = SRAM[0x100 + (ni*2) + 1];

                // Repairs various errors using backup table
                if(indexFlags[ci] & 0x40 && !(indexFlags[ni] & 0x1) || (nv < 0x05 || nv > 0x7F) || (indexFlags[ni] & 0x02 && !(indexFlags[ni] & 0x08))) {
                    // Copy backup index to primary
                    if(SRAM[0x100 + (ci*2) + 1] != SRAM[0x200 + (ci*2) + 1]) {
                    printf("\n    \x1b[33mRepaired %02X", ni);
                    SRAM[0x100 + (ci*2) + 1] = SRAM[0x200 + (ci*2) + 1];
                    ni = SRAM[0x100 + (ci*2) + 1];
                    printf(" -> %02X\x1b[0m\n", ni);
                    } else {
                    // Clear dupe error flag since we are repairing it via bkup
                    indexFlags[ni] &= ~0x02;
                    }
                }
                if(indexFlags[ci] & 0x02) {
                    printf("\n    \x1b[31mERROR: Encountered duplicate index %02X\x1b[0m\n", ni);
                    break; // kill the loop
                }
                // If nextIndex is 1, we are done here
                if(ni == 1) {
                    valid = 1; // reaching this point means we found valid data to preserve.
                    break;
                }
                ci = ni; // change currentIndex to nextIndex.
            }
            if(valid) {
                printf("\n    \x1b[32mFound %d valid chain links (pages)\x1b[0m\n", count);
                // Mark members of valid chain as 'safe' so they don't get removed.
                ci = i;
                while(1) {
                    indexFlags[ci] |= 0x10;
                    ni = SRAM[0x100 + (ci*2) + 1];
                    if(ni == 1) break;
                    ci = ni;
                }
            }
        }
    }
    
    // Rebuild IndexTable (clear any invalid data)
    for(int i = 0x100; i < 0x200; i += 2) {
        // Reset any slot that wasn't validated
        if(i >= 0x10A && (indexFlags[ (i - 0x100) / 2 ] & 0x10) == 0) {
            SRAM[i + 1] = 0x03;
        }
        // These bytes should be zero on a standard C-Pak
        // This can actually repair errors that libultra can't
        if(SRAM[i + 0] != 0x00) SRAM[i + 0] = 0x00;
    }
    // Clean potential garbage data in unused area
    // These bytes were undefined, but garbage data can exist here
    // We should sanitize these to zero to avoid confusion
    if(SRAM[0x103] != 0x00) SRAM[0x103] = 0x00; // page 1
    if(SRAM[0x105] != 0x00) SRAM[0x105] = 0x00; // page 2
    if(SRAM[0x107] != 0x00) SRAM[0x107] = 0x00; // page 3
    if(SRAM[0x109] != 0x00) SRAM[0x109] = 0x00; // page 4
}

int main() {
    // Load C-Pak file into SRAM state
    FILE* file = fopen("SimCity 2000 (Japan).pak", "rb");
    // Note: SimCity 2000 silently repairs C-Pak during boot
    // which is what I'm using to verify correct behaviors
    fread(SRAM, sizeof(SRAM), 1, file);
    
    // Check and repair (if needed)
    // TODO: incorporate argument for backup control
    check();
  
    // Display hex dump of IndexTable   
    for(int i = 0x100; i < 0x200; i++) {
        if((i & 15) == 0) printf("\n");
        printf("%02X ", SRAM[i] & 0xFF);
    }
    return 0;
}
