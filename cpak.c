#include <stdio.h>
#include <stdint.h>

// C-Pak SRAM is 32 KiB
uint8_t SRAM[32768] = {0};

/*
IndexTable parser flags
When checking IndexTable, bit flags are used for each index:

  0x01 = IndexTable 1, Secured chain slot
  0x02 = IndexTable 2, Secured chain slot
  0x04 = IndexTable 1, Dupe found
  0x08 = IndexTable 2, Dupe found
  0x10 = 
  0x20 =
  0x40 = IndexTable 1, Referenced
  0x80 = IndexTable 2, Referenced
*/
uint8_t iFlags[128] = {0};

uint8_t repair() {
    uint8_t si_total = 0, ends = 0;
    // Count the end markers
    for(uint8_t i = 5; i < 128; i++) {
        if(SRAM[0x101 + i*2] == 0x01) ends++;
    }
    // Check all startIndexes in NoteTable
    // First pass - find any conflicts
    for(uint8_t i = 0; i < 16; i++) {
        // Read startIndex value from each NoteEntry
        uint8_t si = SRAM[0x307 + i*32];
        // If present & valid, attempt to traverse list
        if(si >= 0x05 && si <= 0x7F) {
            si_total++;
            uint8_t ci = si, ni;
            // primary
            while(ci != 1) {
                // Get nextIndex value from Primary iTable
                ni = SRAM[0x101 + ci*2];
                // Reached a valid end point
                if(ni == 1) break;
                // Error: Value outside range 
                if(ni > 0x7F || ni < 0x05) break;
                // Error: Reverse order, Mostly impossible
                if(ni == ci || ni < ci) break;
                if(iFlags[ci] & 0x040) {
                    printf("We have already seen %02X \n", ci);
                    iFlags[ci] |= 0x04;
                }
                iFlags[ci] |= 0x40;
                // Move nextIndex to currentIndex
                ci = ni;
            }
            // backup
            while(ci != 1) {
                // Get nextIndex value from Primary iTable
                ni = SRAM[0x101 + ci*2];
                // Reached a valid end point
                if(ni == 1) break;
                // Error: Value outside range 
                if(ni > 0x7F || ni < 0x05) break;
                // Error: Reverse order, Mostly impossible
                if(ni == ci || ni < ci) break;
                if(iFlags[ci] & 0x080) {
                    //printf("We have already seen %02X \n", ci);
                    iFlags[ci] |= 0x8;
                }
                iFlags[ci] |= 0x80;
                // Move nextIndex to currentIndex
                ci = ni;
            }
        }
    }
    // Check all startIndexes in NoteTable
    // Second pass - complete check
    for(uint8_t i = 0; i < 16; i++) {
        // Read startIndex value from each NoteEntry
        uint8_t si = SRAM[0x307 + i*32], count = 0;
        // If present & valid, attempt to traverse list
        if(si >= 0x05 && si <= 0x7F) {
            uint8_t ci = si, ni, valid = 0;
            while(ci != 1) {
                count++;
                // Get nextIndex value from Primary iTable
                ni = SRAM[0x101 + ci*2];
                // Reached a valid end point
                if(ni == 1) {
                    // Detect truncation errors
                    // If either length = 1 or secondIndex matches... 
                    // ...check backup ni + total ends found
                    if( (count == 1 || SRAM[0x101 + si*2] == SRAM[0x201 + si*2]) &&
                        (ends != si_total && SRAM[0x201 + ci*2] != 1) ) {
                        break;
                    }
                    valid = 1;
                    break;
                }
                // Error: Value outside range 
                if(ni > 0x7F || ni < 0x05) break;
                // Error: Reverse order, Mostly impossible
                if(ni == ci || ni < ci) break;
                if(iFlags[ci] & 0x04) break;
                // Move nextIndex to currentIndex
                ci = ni;
            }
            // Valid chain found in Primary iTable
            // Let's mark them as safe!
            if(valid) {
                // Reset currentIndex to the start
                ci = si;
                // Output sequence to screen
                printf("p[ ");
                // Repeat the loop again
                while(ci != 1) {
                    ni = SRAM[0x101 + ci*2];
                    // Mark secured indexes (Primary)
                    iFlags[ci] |= 1;
                    printf("%02X ", ci);
                    ci = ni;
                }
                printf("]\n");
            // Check backup if valid chain wasn't found
            } else {
                // Same loop as before
                ci = si;
                while(ci != 1) {
                    // This time, target the backup
                    ni = SRAM[0x201 + ci*2];
                    if(ni == 1) {valid = 1; break;}
                    if(ni > 0x7F || ni < 0x05) break;
                    if(ni == ci || ni < ci) break;
                    if(iFlags[ci] & 0x08) break;
                    ci = ni;
                }
                // Valid chain found in Backup iTable
                // Let's mark them as safe!
                if(valid) {
                    ci = si;
                    printf("B[ ");
                    while(ci != 1) {
                        // Target backup location
                        ni = SRAM[0x201 + ci*2];
                        // Mark secured indexes (Backup)
                        iFlags[ci] |= 2;
                        printf("%02X ", ci);
                        ci = ni;
                    }
                    printf("]\n");
                }
            }
        }
    }
    // Rebuild IndexTable (clear any invalid data)
    for(uint8_t i = 0; i < 128; i++) {
        // If backup valid and primary not, restore backup
        if(i >= 5 && !(iFlags[i]&1) && iFlags[i]&2) {
            SRAM[0x101 + i*2] = SRAM[0x201 + i*2];
        }
        // Reset any invalid slot
        else if(i >= 5 && !(iFlags[i]&1)) {
            SRAM[0x101 + i*2] = 0x03;
        }
        // These bytes will be 0 on official C-Pak
        // This actually does repairs that libultra can't!
        if(SRAM[0x100 + i*2]) SRAM[0x100 + i*2] = 0x00;
    }
    // Reset unused bytes; They should be 0
    // Prevents garbage accumulation, avoids confusion
    if(SRAM[0x103]) SRAM[0x103] = 0x00; // pg 1
    if(SRAM[0x105]) SRAM[0x105] = 0x00; // pg 2
    if(SRAM[0x107]) SRAM[0x107] = 0x00; // pg 3
    if(SRAM[0x109]) SRAM[0x109] = 0x00; // pg 4
    // Update checksum
    uint8_t sum8 = 0;
    for(uint8_t i = 1; i < 128; i++) {
        sum8 += SRAM[0x101 + i*2];
    }
    SRAM[0x101] = sum8;
    // Copy Primary -> Backup
    for(uint8_t i = 0;; i++) {
        SRAM[0x201 + i*2] = SRAM[0x101 + i*2];
        if(i == 255) break;
    }
}

int main() {
    // Load C-Pak file into SRAM state
    FILE* file = fopen("SimCity 2000 (Japan).pak", "rb");
    // Note: SimCity 2000 silently repairs C-Pak during boot
    // which is what I'm using to verify correct behaviors
    fread(SRAM, sizeof(SRAM), 1, file);
    
    // Perform a repair
    repair();
  
    // Display hex dump of IndexTable   
    for(int i = 0x100; i < 0x200; i++) {
        if((i & 15) == 0) printf("\n");
        printf("%02X ", SRAM[i] & 0xFF);
    }
    return 0;
}
