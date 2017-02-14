/******************************************************************************
 *                                                                            *
 *     MAKEBIN.C                                                              *
 *                                                                            *
 *     This file performs a simplistic conversion of E86MON into ROM images   *
 *     suitable for use by Running Start.                                     *
 *                                                                            *
 *     This is a slightly modified version to get it running on linux.        *
 *                                                                            *
 ******************************************************************************
 *                                                                            *
 * Copyright 1996, 1997 Advanced Micro Devices, Inc.                          *
 *                 2017 Nils Stec
 *                                                                            *
 * This software is the property of Advanced Micro Devices, Inc  (AMD)  which *
 * specifically  grants the user the right to modify, use and distribute this *
 * software provided this notice is not removed or altered.  All other rights *
 * are reserved by AMD.                                                       *
 *                                                                            *
 * AMD MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH REGARD TO THIS *
 * SOFTWARE.  IN NO EVENT SHALL AMD BE LIABLE FOR INCIDENTAL OR CONSEQUENTIAL *
 * DAMAGES IN CONNECTION WITH OR ARISING FROM THE FURNISHING, PERFORMANCE, OR *
 * USE OF THIS SOFTWARE.                                                      *
 *                                                                            *
 * So that all may benefit from your experience, please report  any  problems *
 * or suggestions about this software back to AMD.  Please include your name, *
 * company,  telephone number,  AMD product requiring support and question or *
 * problem encountered.                                                       *
 *                                                                            *
 * Advanced Micro Devices, Inc.       Worldwide support and contact           *
 * Logic Products Division            information available at:               *
 * Systems Engineering                                                        *
 * 5204 E. Ben White Blvd.     http://www.amd.com/html/support/techsup.html   *
 * Austin, TX 78741                                                           *
 *****************************************************************************/

//#include <io.h>
#include <stdio.h>
//#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;

typedef WORD BOOL;

typedef void *    LPVOID;
typedef DWORD *   LPDWORD;
typedef WORD *    LPWORD;
typedef BYTE *    LPBYTE;
typedef char *    LPSTR;

#define FALSE 0
#define TRUE 1

//
// ExeHeader structure from Microsoft's MS-DOS programmer's manual,
// version 5.0
//
typedef struct {
   WORD MagicNumber;
   WORD BytesLastPg;        // NOTE!  Modulo-512
   WORD PagesInFile;
   WORD Relocations;
   WORD ParsInHdr;
   WORD ExtraParsNeeded;
   WORD ExtraParsWanted;
   WORD InitStackSegment;
   WORD InitStackOffset;
   WORD WordXsum;
   WORD EntryOffset;
   WORD EntrySegment;
   WORD ReloTableAddr;
} ExeHdr;
 
char ExeName[128];

FILE* SourceFile;
FILE* DestFile;

//////////////////////////////////////////////////////////////////////////
// ErrExit() prints an error message and exits the program.
//
void ErrExit(char * s,...)
{
    char Buffer[400];
    vsprintf(Buffer,s,(LPVOID)(&s+1));
    printf("\nMakeBin Error -- %s\n\n",Buffer);
    exit(2);
}


//////////////////////////////////////////////////////////////////////////
// ShowHelp() shows the user all his choices
//
void ShowHelp(void)
{
    printf(
                                                                      "\n"
"    MakeBin -- AMD E86Mon ROM image generator version 1.0.\n"
"                      Copyright (C) 1997, Advanced Micro Devices.\n"
"    Syntax:\n"
"         MakeBin <filename>\n"
                                                                      "\n"
"    MakeBin will take <filename>.exe, and generate the following files:\n\n"
"        F010_ALL.BIN     -- Used in 188ES, 188EM boards\n"
"        F010_LOW.BIN,\n"
"        F010_HI.BIN      -- Used in 186ES, 186EM boards\n"
"        F200_ALL.BIN     -- Used in 18xER boards\n"
"        F400_ALL.BIN     -- Used in Net186 and 186ED boards\n\n"

    );
    exit(1);
}


//////////////////////////////////////////////////////////////////////////
// ReadFile reads from the source file and returns a pointer to data
// which has been read.  It always physically reads on sector (512 byte)
// boundaries, and its buffer is 16K + 512 bytes, so that the maximum
// size read it can return is 16K.
//
void * ReadFile(DWORD offset, WORD size)
{
    static char SourceBuffer[0x4200];
    static DWORD CurOffset = 0;
    static WORD  CurSize = 0;

    if ((offset < CurOffset) || (offset + size > CurOffset + CurSize))
    {
        CurOffset = offset & 0xFFFFFE00;
        if (fseek(SourceFile,CurOffset, SEEK_SET))
            ErrExit("File seek failed");
        CurSize = fread(SourceBuffer,1,sizeof(SourceBuffer),SourceFile);

        if ((offset < CurOffset) || (offset + size > CurOffset + CurSize))
            ErrExit("file read failed");
    };
    return SourceBuffer + (WORD)(offset-CurOffset);
}

void WriteFile(LPVOID Data, WORD Size)
{
    if (fwrite(Data,1,Size,DestFile) != Size)
        ErrExit("File Write Error");
}

void CreateFile(LPSTR FName, DWORD RomSize, DWORD BootSize,
                    DWORD NumRoms, DWORD WhichRom,
                    DWORD SrcFileLoc, DWORD SrcLength)
{
    DWORD DestLength    = (SrcLength + WhichRom) / NumRoms;
    DWORD FirstFFLength = (RomSize - BootSize/NumRoms);
    DWORD LastFFLength  = (BootSize - 0x10)/NumRoms - DestLength;
    BYTE  FarJump       = 0xEA;
    WORD  AddrOffset    = 0;
    WORD  AddrSegment   = 0 - (WORD)(BootSize/16);

    DWORD Checksum      = 0xFFL * (FirstFFLength+LastFFLength);

    static BYTE FileBuffer[8192];  // Half the size of FileRead's buffer
    WORD  Chunk;
    LPBYTE SrcPtr;
    WORD i;


    if ((DestFile=fopen(FName,"wb")) == 0)
        ErrExit("Cannot create destination file %s",FName);

    memset(FileBuffer,0xFF,sizeof(FileBuffer));

    while (FirstFFLength > 0)
    {
        Chunk = sizeof(FileBuffer);
        if (Chunk > FirstFFLength)
            Chunk = FirstFFLength;
        WriteFile(FileBuffer,Chunk);
        FirstFFLength  -= Chunk;
    }

    while (DestLength>0)
    {
        Chunk = sizeof(FileBuffer);
        if (Chunk > DestLength)
            Chunk = DestLength;

        SrcPtr = ReadFile(SrcFileLoc+WhichRom, (WORD)(Chunk*NumRoms));

        for (i=0;i<Chunk;i++)
        {
            Checksum += (FileBuffer[i] = *SrcPtr);
            SrcPtr += NumRoms;
        }

        WriteFile(FileBuffer,Chunk);

        SrcFileLoc += Chunk*NumRoms;
        DestLength  -= Chunk;
    }

    memset(FileBuffer,0xFF,sizeof(FileBuffer));

    while (LastFFLength > 0)
    {
        Chunk = sizeof(FileBuffer);
        if (Chunk > LastFFLength)
            Chunk = LastFFLength;
        WriteFile(FileBuffer,Chunk);
        LastFFLength  -= Chunk;
    }

    FileBuffer[16] = FarJump;
    *((LPWORD)(FileBuffer+17)) = AddrOffset;
    *((LPWORD)(FileBuffer+19)) = AddrSegment;
    for (i=0; i<16/NumRoms; i++)
        Checksum+= (FileBuffer[i] = FileBuffer[16+i*NumRoms+WhichRom]);

    WriteFile(FileBuffer,(16/(WORD)NumRoms));

    fclose(DestFile);
    printf("File %s written successfully, checksum = %lX.\n",
           FName,Checksum);
}


//////////////////////////////////////////////////////////////////////////
// Main program.  Parse command line, and then show the help message,
// or create the ROM files.
//
int main(int argc, char* argv[])
{
    DWORD     Length;
    DWORD     FileLength;
    DWORD     SrcFileLoc;
//    WORD      ProgAddress;
	struct stat sr;
    ExeHdr    eh;

    if (argc != 2)
        ShowHelp();

    strcpy(ExeName,argv[1]);
    strcat(ExeName,".exe");

    if ((SourceFile=fopen(ExeName,"rb")) == 0)
        ErrExit("Cannot open source file %s",ExeName);

	stat(ExeName, &sr);
	FileLength = sr.st_size;

    //FileLength = _filelength(_fileno(SourceFile));

    memcpy(&eh,ReadFile(0,sizeof(eh)),sizeof(eh));

    if (eh.MagicNumber != 0x5A4D)
        ErrExit("Invalid EXE signature");

    Length = eh.PagesInFile*512L-((512-eh.BytesLastPg)%512);
    if (Length >  FileLength)
        ErrExit("File Read Error");

    if (eh.Relocations > 1)
        ErrExit("More than 1 relocations");

    if ((eh.EntryOffset != 0) || (eh.EntrySegment != 0))
        ErrExit("Program start is not at 0:0");

    SrcFileLoc = eh.ParsInHdr*16;
    Length -= eh.ParsInHdr*16;

    CreateFile("F010_ALL.BIN",0x20000L, 0x8000L, 1, 0, SrcFileLoc, Length);
    CreateFile("F010_LOW.BIN",0x20000L, 0x8000L, 2, 0, SrcFileLoc, Length);
    CreateFile("F010_HI.BIN", 0x20000L, 0x8000L, 2, 1, SrcFileLoc, Length);
    CreateFile("F200_ALL.BIN",0x40000L, 0x8000L, 1, 0, SrcFileLoc, Length);
    CreateFile("F400_ALL.BIN",0x80000L, 0x8000L, 1, 0, SrcFileLoc, Length);

    exit(0);
}
