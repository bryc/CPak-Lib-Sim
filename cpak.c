#include <stdio.h>
#include <string.h>

typedef unsigned char  u8;
typedef unsigned short u16;

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
u8 SRAM[32768] = {0}, iFlags[128] = {0}, nTable[16] = {0}, nTotal = 0, ends = 0;

u8 readChain(u8 si, u8 id, u8 mode) {
    u16 adrA = id == 1 ? 0x101 : 0x201, adrB = id == 1 ? 0x201 : 0x101;
    for(u8 ci = si, ni, cnt = 1; ci != 1; ci = ni, cnt++) {
        ni = SRAM[adrA + (ci << 1)];
        if(mode == 1 && id == 1 && ni == 1 &&
          (ends != nTotal && SRAM[adrB + (ci << 1)] != 1) &&
          (cnt == 1 || SRAM[adrA + (si << 1)] == SRAM[adrB + (si << 1)])) {
            return 0;
        }
        if(ni == 1) return 1;
        if(ni <= ci || ni > 0x7F || ni < 0x05) return 0;
        // if(ni != ci+1 && SRAM[adrA-1 + (ci << 1)] && ni != SRAM[adrB + (ci << 1)]) return 0;
        if(mode == 0) {
            if(iFlags[ni] & (id << 6)) iFlags[ni] |= (id << 2);
            iFlags[ni] |= (id << 6);
        } else if(mode == 1) {
            if( (ni != ci+1 && ni != SRAM[adrB + (ci << 1)]) ) {
            if(iFlags[ni] & (id << 2)) return 0;
            }
        }
    }
}

u8 check() {
    // Count endpoints (values of 1) in Primary iTable
    for(u8 i = 0; i < 246; i += 2) if(SRAM[0x10b+i] == 1) ends++;

    // Extract valid start indexes from NoteTable
    for(u8 i = 0, gid, pid, sum, sih, si; i < 16; i++) {
        si  = SRAM[0x307 + i*32], sih = SRAM[0x306 + i*32];
        sum = SRAM[0x30A + i*32] | SRAM[0x30B + i*32];
        pid = SRAM[0x304 + i*32] | SRAM[0x305 + i*32];
        gid = SRAM[0x300 + i*32] | SRAM[0x301 + i*32] | 
              SRAM[0x302 + i*32] | SRAM[0x303 + i*32];
        if(!sih && !sum && gid && pid && si < 128 && si > 4) {
            //iFlags[si] |= 0x80;
            nTable[nTotal] = si;
            nTotal++;
        }
    }

    // IndexTable (Pass 1): Check for duplicates
    for(u8 i = 0; nTable[i] && i < 16; i++) {
        readChain(nTable[i], 1, 0); // Primary
        readChain(nTable[i], 2, 0); // Backup
    }

    // IndexTable (Pass 2): Validate notes
    for(u8 i = 0, ci, ni, si; nTable[i] && i < 16; i++) {
        si = nTable[i];
        // First check Primary iTable
        if(readChain(si, 1, 1)) {
            printf("p[ ");
            for(ci = si; ci != 1; ci = ni) {
                ni = SRAM[0x101 + (ci << 1)];
                iFlags[ci] |= 0x01; // Secure indexes (Primary)
                printf("%02X ", ci);
            }
            printf("]\n");
        // If unsuccessful, check Backup iTable
        } else if(readChain(si, 2, 1)) {
            printf("B[ ");
            for(ci = si; ci != 1; ci = ni) {
                ni = SRAM[0x201 + (ci << 1)];
                iFlags[ci] |= 0x02; // Secure indexes (Backup)
                printf("%02X ", ci);
            }
            printf("]\n");
        // Note cannot be validated from either Primary or Backup :(
        } else {
            printf("CRITICAL ERROR: Note %02X is cannot be recovered.\n", si);
        }
    }

    // Rebuild IndexTable (clear any invalid data)
    for(u8 i = 0; i < 128; i++) {
        // If backup valid and primary not, restore backup
        if(i >= 5 && !(iFlags[i]& 0x01) && iFlags[i] & 0x02) {
            SRAM[0x101 + i*2] = SRAM[0x201 + i*2];
        }
        // Reset any invalid slot
        else if(i >= 5 && !(iFlags[i] & 0x01)) {
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
    u8 sum8 = 0;
    for(u8 i = 1; i < 128; i++) {
        sum8 += SRAM[0x101 + i*2];
    }
    SRAM[0x101] = sum8;
    // Copy Primary -> Backup
    for(u8 i = 0;; i++) {
        SRAM[0x201 + i*2] = SRAM[0x101 + i*2];
        if(i == 255) break;
    }
}

int main(int argc, char * argv[]) {
    if(!argv[1]) {printf("ERROR: A file must be supplied.\n"); return 0;}
    // Load C-Pak file into SRAM state
    FILE* fp = fopen(argv[1], "rb");
    if(!fp) {printf("ERROR: File not found.\n"); fclose(fp); return 0;}
    // Detect and skip DexDrive header (via .n64 extension)
    if(!strcmp(&argv[1][strlen(argv[1]) - 2], "64")) {
        if(fseek(fp, 0x1040, SEEK_SET) != 0) {fclose(fp); return 0;}
    }
    fread(SRAM, sizeof(SRAM), 1, fp); fclose(fp);

    printf("-------- [ %s ] --------\n", argv[1]);
    check();
}
