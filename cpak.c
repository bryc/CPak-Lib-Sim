#include <stdio.h>

// C-Pak SRAM is 32 KiB
char SRAM[32768] = {0};

// IndexTable parser flags
//   When checking IndexTable, bit flags are used for each index:
//   0x01 = this index is a startIndex in NoteTable  (we expect both)
//   0x02 = this index is a startIndex in IndexTable (we expect both)
//   0x04 = this index is referenced in IndexTable
//   0x08 = this index is referenced twice (critical error)
//   0x10 = this index is part of a verified chain
//   0x20 = .. verified chain in backup (?) TODO
//   0x40 = ..
//   0x80 = ..
char indexFlags[128] = {0};

int check() {
    // Obtain startIndexes from NoteTable
    printf("Finding startIndexes...\nnTable: ");
    for(int i = 0x300; i < 0x500; i += 32) {
        char si = SRAM[i + 7];
        if(si >= 5 && si <= 127) {
            // set ntblStart flag
            indexFlags[si] |= 1;
            printf("%02X ", si);
        }
    }
    printf("\n");

    // Mark nextIndex values and free slots
    // This is done to isolate startIndexes
    for(int i = 0x10A; i < 0x200; i += 2) {
        char ni = SRAM[i + 1];
        // values under 5 must also be marked, except 1
        if(ni != 1 && ni < 5) ni = (i - 0x100) / 2;
        // set isDupe flag if a duplicate is found
        if(indexFlags[ni] & 4) indexFlags[ni] |= 8;
        // set isFound flag.
        indexFlags[ni] |= 4;
    }

    // Mark all startIndexes found
    printf("iTable: ");
    for(int i = 5; i < 128; i++) {
        // any unreferenced indexes must be startIndexes
        if((indexFlags[i] & 4) == 0)
        // set itblStart flag
        indexFlags[i] |= 2,
        printf("%02X ", i);
    }
    // DEBUG: Display the startIndex parity values
    printf("\nParity: ");
    // Check startIndexes
    for(int i = 5; i < 128; i++) {
        if(indexFlags[i] & 3) printf("%02X ", indexFlags[i] & 3);
    }
    printf("\n");

    // Validate any index chains found
    for(int i = 5; i < 128; i++) {
        // Only check startIndex if seen in both sources
        if((indexFlags[i] & 3) == 3) {
            char ci = i, ni, valid = 0, count = 0;
            printf("\n -> Chain @ %02X:\n", ci);
            while(1) {
                printf("%02X ", ci);
                count++;
                ni = SRAM[0x100 + (ci*2) + 1];
                // If the nextIndex is a dupe in our flags, the chain is corrupt/unreliable
                if(ni != 1 && indexFlags[ni] & 8) {
                    printf("\n    \x1b[31mERROR: Encountered duplicate index %02X\x1b[0m\n", ni);
                    break; // kill the loop
                }
                // If the nextIndex is a value of 1, we made it to the end of a sequence
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
                    indexFlags[ci] |= 16;
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
        if(i >= 0x10A && (indexFlags[ (i - 0x100) / 2 ] & 16) == 0) {
            SRAM[i + 1] = 0x03;
        }
        // These bytes should be zero on a standard C-Pak
        // This can repair errors that libultra would miss
        if(SRAM[i + 0] != 0) SRAM[i + 0] = 0xff;
    }
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