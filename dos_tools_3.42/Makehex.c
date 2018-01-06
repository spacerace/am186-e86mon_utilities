/******************************************************************************
 *                                                                            *
 *     MAKEHEX.C                                                              *
 *                                                                            *
 *     This file performs a simplistic conversion of .EXE files into          *
 *     .HEX files compatible with AMD's EMON v. 3.30 and above.               *
 *                                                                            *
 *   AUTHOR:    Pat Maupin                                                    *
 *                                                                            *
 *   REVISION HISTORY:                                                        *
 *                                                                            *
 *    3.30 -- Made simpler relocatable file format (can support               *
 *            symbolic debug information in future as well)                   *
 *            Also made MAKEHEX recognize new library format and              *
 *            not create start address record for these files.                *
 *                                                                            *
 *                                                                            *
 ******************************************************************************
 *                                                                            *
 * Copyright 1996, 1997 Advanced Micro Devices, Inc.                          *
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

#include <io.h>
#include <stdio.h>
#include <dos.h>
#include <string.h>
#include <stdlib.h>

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;

typedef WORD BOOL;

typedef void far *    LPVOID;
typedef DWORD far *   LPDWORD;
typedef WORD far *    LPWORD;
typedef BYTE far *    LPBYTE;

#define FALSE 0
#define TRUE 1
#define BYTESPERLINE  32    // MUST Be power of 2!!!

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

//
// E86Mon library extension definition:
struct {
    WORD    ShortJmp;          // Jump around the rest of this
    BYTE    Signature[22];
} LibSig = {0x16EB,"E86Mon Lib Extension 1"};
 
char ExeName[128];
char ComName[128];
char BinName[128];
char DestName[128];

FILE* SourceFile;
FILE* DestFile;
BYTE CheckSum;
BOOL WholeFileInMemory = FALSE;
WORD  OutputAddress = 0;
WORD  OutputSegment = 0;

//////////////////////////////////////////////////////////////////////////
// ErrExit() prints an error message and exits the program.
//
void ErrExit(char * s,...)
{
    char Buffer[400];
    vsprintf(Buffer,s,(LPVOID)(&s+1));
    printf("\nMakeHex Error -- %s\n\n",Buffer);
    exit(2);
}


//////////////////////////////////////////////////////////////////////////
// ShowHelp() shows the user all his choices
//
void ShowHelp(void)
{
    printf(
                                                                      "\n"
"    MakeHex -- AMD 186 EMon relocater version 3.30.\n"
"                      Copyright (C) 1996, Advanced Micro Devices.\n"
"    Syntax:\n"
"         MakeHex <filename>  [<segment address>]\n"
                                                                      "\n"
"    MakeHex will take <filename>.exe, and generate <filename>.hex.\n"
                                                                      "\n"
"    The <segment address> parameter should only be given if the .EXE\n"
"    file contains no relocation information.  In this case, MakeHex\n"
"    will simply output the binary information in the file, to the\n"
"    given segment address.  This is used to build EMON itself.\n"
                                                                 "\n"
"    If no <segment address> parameter is given, MakeHex will generate\n"
"    a special hex file with relocation records which EMON understands.\n"
                                                                 "\n"
"    This hex file will be loaded into RAM and relocated by EMON.\n\n"

    );
    exit(1);
}


//////////////////////////////////////////////////////////////////////////
// ReadFile() reads from the source file
//
void far * ReadFile(DWORD offset, WORD size)
{
    static char SourceBuffer[0xA000];
    static DWORD CurOffset = 0;
    static WORD  CurSize = 0;

    if ((offset < CurOffset) || (offset + size > CurOffset + CurSize))
    {
        if (WholeFileInMemory)
            ErrExit("previous read failed to return full set of bytes");
        CurOffset = offset & 0xFFFFFE00;
        if (fseek(SourceFile,CurOffset, SEEK_SET))
            ErrExit("File seek failed");
        CurSize = fread(SourceBuffer,1,sizeof(SourceBuffer),SourceFile);

        if ((offset < CurOffset) || (offset + size > CurOffset + CurSize))
            ErrExit("file read failed");

        WholeFileInMemory = (CurOffset == 0) && (CurSize < sizeof(SourceBuffer));
    };
    return SourceBuffer + (WORD)(offset-CurOffset);
}

//////////////////////////////////////////////////////////////////////////
// ParseHex() parses hexadecimal numbers. Only returns TRUE if no non-hex
// characters are encountered (unlike builtins such as scanf).
//
BOOL ParseHex(char* t,DWORD* value)
{
    *value = 0;

    while(isxdigit(*t))
    {
        *value *= 16;
        if (*t < '9')
            *value += *t - '0';
        else
            *value += toupper(*t) - 'A' + 10;
        t++;
    }
    return (*t == 0);
}

//////////////////////////////////////////////////////////////////////////
// PrintByte() prints a byte, and adds it to the line checksum.
//
void PrintByte(BYTE what)
{
    fprintf(DestFile, "%02X", what);
    CheckSum += what;
}

//////////////////////////////////////////////////////////////////////////
// PrintWord() prints a word, and adds it to the line checksum.
//
void PrintWord(WORD what)
{
    PrintByte((BYTE)(what >> 8));
    PrintByte((BYTE)what);
}

//////////////////////////////////////////////////////////////////////////
// PrintDWord() prints a double word, and adds it to the line checksum.
//
void PrintDWord(DWORD what)
{
    PrintWord((WORD)(what >> 16));
    PrintWord((WORD)what);
}

//////////////////////////////////////////////////////////////////////////
// StartLine() initializes the checksum and prints the start
//             of a line record
//
void StartLine(BYTE DataLen, WORD DataAddr, BYTE DataType)
{
    CheckSum = 0;
    fprintf(DestFile, ":");
    PrintByte(DataLen);
    PrintWord(DataAddr);
    PrintByte(DataType);
}

//////////////////////////////////////////////////////////////////////////
// FinishLine() prints the checksum and end of line
//
void FinishLine(void)
{
    CheckSum = 0 - CheckSum;
    fprintf(DestFile, "%02X\n",CheckSum);
}

//////////////////////////////////////////////////////////////////////////
// SegRecord() prints a segment record
//
void SegRecord(WORD SegNum)
{
    StartLine(2,0,2);
    PrintWord(SegNum);
    FinishLine();
}

//////////////////////////////////////////////////////////////////////////
// DataRecord() prints a entire data record
//
void DataRecord(LPBYTE Data, BYTE DataLen)
{
    StartLine(DataLen,OutputAddress,0);

    for ( ; DataLen>0; DataLen--)
        PrintByte(*(Data++));

    FinishLine();

    OutputAddress += BYTESPERLINE;
    if (OutputAddress == 0)
    {
        OutputSegment += 0x1000;
        SegRecord (OutputSegment);
    }
}

//////////////////////////////////////////////////////////////////////////
// OutputDataFromFile() uses DataRecord() to output the bulk of the
// program data.
//
void OutputDataFromFile(DWORD FileLoc, DWORD Length)
{
    BYTE RecLen;
    LPBYTE Data;

    while (Length>0)
    {
        RecLen = BYTESPERLINE;
        if (RecLen > Length)
            RecLen = (BYTE)(Length);

        Data = ReadFile(FileLoc,RecLen);

        DataRecord(Data,RecLen);
        FileLoc += RecLen;
        Length -= RecLen;
    }
}

//////////////////////////////////////////////////////////////////////////
// OutputMiscData() uses DataRecord() to output miscellaneous data
//
void OutputMiscData(LPBYTE Data, WORD DataLen)
{
    static WORD BufLen = 0;
    static BYTE Buffer[BYTESPERLINE];

    if (DataLen+BufLen > sizeof(Buffer))
        ErrExit("Unaligned data passed to OutputMiscData");

    _fmemcpy(Buffer+BufLen,Data,DataLen);

    BufLen += DataLen;

    if (BufLen == sizeof(Buffer))
    {
        DataRecord(Buffer,(BYTE)sizeof(Buffer));
        BufLen = 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// RelocationRecord() stores relocation items.  These records
// *must* appear *after* the actual data records.
//
RelocationRecords(ExeHdr * eh)
{
    LPDWORD DataPtr = ReadFile(eh->ReloTableAddr,eh->Relocations * 4);
    DWORD   EndProgram = OutputSegment * 16L + OutputAddress;
    WORD i;

    for (i = eh->Relocations; i > 0; i--)
    {
        DWORD value = *(DataPtr++);
        value = ((value & 0xFFFF0000L) >> 12) + (value & 0xFFFF);
        OutputMiscData((LPVOID)&value,4);
    }

    for (i = BYTESPERLINE/4-1; i > 0; i--)
        OutputMiscData((LPVOID)&EndProgram,4);
}

//////////////////////////////////////////////////////////////////////////
// DGROUPRelocations() stores relocation records for DGROUP only,
// in a special format, after the main program.
//
void DGROUPRelocations(ExeHdr * eh, DWORD ProgLength)
{
    LPBYTE FilePtr = ReadFile(0,0);
    LPBYTE ProgPtr = FilePtr + (eh->ParsInHdr*16);
    DWORD   DGROUPOffset = 0;
    WORD   i;
    WORD TargetValue;
    DWORD  Relo;

    OutputAddress = ProgLength;

    if (!WholeFileInMemory)
        ErrExit("Relocations are only processed if file fits in memory");
    if ((ProgLength & 0xF) != 0)
        ErrExit("Relocations only processed if data ends"
                " on paragraph boundary");

    for (i=0; i<eh->Relocations; i++)
    {
        Relo = *((LPDWORD)(FilePtr+eh->ReloTableAddr) + i);
        Relo = (Relo & 0xFFFF) + ((Relo & 0xFFFF0000L) >> 12);
        if (Relo > 0x7FFF)
            ErrExit("Relocation target > 32K");

        TargetValue = *((LPWORD)(ProgPtr+Relo));
        if (TargetValue != 0)
            if (DGROUPOffset == 0)
                DGROUPOffset = TargetValue;
            else if (TargetValue != DGROUPOffset)
                ErrExit("more than one target data segment for relocation");
    }

    DGROUPOffset = DGROUPOffset << 4;
    if (DGROUPOffset == 0)
        ErrExit("Cannot tell where DGROUP starts!");

    Relo = eh->Relocations * 4 + 4 - 1;
    OutputMiscData((LPBYTE)&Relo,4);

    for (i=0; i<eh->Relocations; i++)
    {
        Relo = *((LPDWORD)(FilePtr+eh->ReloTableAddr) + i);
        Relo = (Relo & 0xFFFF) + ((Relo & 0xFFFF0000L) >> 12);

        TargetValue = *((LPWORD)(ProgPtr+Relo));

        if (Relo < DGROUPOffset)
            ErrExit("Attempt to relocate item in code segment at %4X",Relo);

        Relo -= DGROUPOffset;
        if (TargetValue != 0)
            Relo += ((DWORD)TargetValue << 16L);

        OutputMiscData((LPBYTE)&Relo,4);
    }

    Relo = 0xFFFFFFFFL;
    for (i = BYTESPERLINE/4-1; i > 0; i--)
        OutputMiscData((LPVOID)&Relo,4);
}


//////////////////////////////////////////////////////////////////////////
// EOFRecord() prints an end of file record
//
void EOFRecord(void)
{
    StartLine(0,0,1);
    FinishLine();
}

//////////////////////////////////////////////////////////////////////////
// JumpRecord() prints a far jump
//
void JumpRecord(WORD SrcSeg, WORD SrcOff, WORD TargetSeg, WORD TargetOff)
{
    SegRecord(SrcSeg);

    StartLine(5,SrcOff,0);
    PrintByte(0xEA);
    PrintByte((BYTE)TargetOff);
    PrintByte((BYTE)(TargetOff >> 8));
    PrintByte((BYTE)TargetSeg);
    PrintByte((BYTE)(TargetSeg >> 8));
    FinishLine();
}

//////////////////////////////////////////////////////////////////////////
// StartAddressRecord() prints the starting address of the hex file
//
void StartAddressRecord(WORD Segment,WORD Offset)
{
    StartLine(4,0,3);
    PrintWord(Segment);
    PrintWord(Offset);
    FinishLine();
}

//////////////////////////////////////////////////////////////////////////
// AMDStartRecord() prints a record which identifies this as a
// proprietary relocatable hex file.  The total number of program
// paragraphs, and the stack segment/offset are stored in this
// record, as well as the normal segment identifier.
//
void AMDStartRecord(DWORD ProgLength, ExeHdr * eh)
{
    DWORD ReloLength;

    char IDString[] = "AMD LPD ";
    WORD i;

    StartAddressRecord(eh->EntrySegment,eh->EntryOffset);

    StartLine(2+8+2+4*4,0,2);
    PrintWord(0);     // Segment offset

    for (i=0;i<8;i++)
        PrintByte(IDString[i]);

    ProgLength = (ProgLength+BYTESPERLINE-1)&(0L-BYTESPERLINE);
    ReloLength = ((eh->Relocations * 4L) + BYTESPERLINE-1) & (0L-BYTESPERLINE);

    PrintWord((WORD)(ProgLength >> 4) + 2 + eh->ExtraParsNeeded);
    PrintWord(eh->InitStackSegment);
    PrintWord(eh->InitStackOffset);
    PrintDWord(ProgLength);
    PrintDWord(ProgLength + ReloLength);
    PrintDWord(ProgLength + ReloLength);
    FinishLine();
}

//////////////////////////////////////////////////////////////////////////
// Main program.  Parse command line, and then show the help message,
// or copy a file.
//
void main(int argc, char* argv[])
{
    BOOL      IsLibrary = FALSE;
    BOOL      IsComFile = FALSE;
    BOOL      IsBinFile = FALSE;
    DWORD     Length;
    DWORD     FileLength;
    DWORD     DataPtr;
    DWORD     SegAddress = 0;

    ExeHdr    eh;
    BOOL      Relocatable = (argc == 2);


    if ((argc < 2) || (argc > 3))
        ShowHelp();

    if (!Relocatable && (!ParseHex(argv[2],&SegAddress) ||
        (SegAddress >= 0x10000)))
       ShowHelp();

    strcpy(ComName,argv[1]);
    strcat(ComName,".com");
    strcpy(BinName,argv[1]);
    strcat(BinName,".bin");
    strcpy(ExeName,argv[1]);
    strcat(ExeName,".exe");
    strcpy(DestName,argv[1]);
    strcat(DestName,".hex");

    if ((SourceFile=fopen(BinName,"rb")) != 0)
        IsBinFile = TRUE;
    else if ((SourceFile=fopen(ComName,"rb")) != 0)
        IsComFile = TRUE;
    else if ((SourceFile=fopen(ExeName,"rb")) == 0)
        ErrExit("Cannot open source file %s",ExeName);

    if ((DestFile=fopen(DestName,"w")) == 0)
        ErrExit("Cannot create destination file %s",DestName);

    FileLength = _filelength(_fileno(SourceFile));

    if (IsComFile || IsBinFile)
    {
        DataPtr = 0;

        eh.ExtraParsNeeded  = 0x10;  // Min. .COM stack is 256 bytes
        eh.InitStackSegment = 0;
        eh.InitStackOffset  = 0;
        eh.EntrySegment     = 0;
        eh.EntryOffset      = 0;
        eh.Relocations      = 0;
        if (IsComFile)
        {
            eh.EntrySegment -= 0x10;
            eh.EntryOffset  += 0x100;
        }
        Length = FileLength;
    }
    else   // .EXE file encountered
    {
        _fmemcpy(&eh,ReadFile(0,sizeof(eh)),sizeof(eh));

        if (eh.MagicNumber != 0x5A4D)
            ErrExit("Invalid EXE signature");

        Length = eh.PagesInFile*512L-((512-eh.BytesLastPg)%512);
        if (Length >  FileLength)
            ErrExit("File Read Error");

        DataPtr = eh.ParsInHdr*16;

        Length -= eh.ParsInHdr*16;
    }

    if (Relocatable)
        AMDStartRecord(Length, &eh);
    else
        SegRecord((WORD)SegAddress);

    IsLibrary = !Relocatable &&
      (_fmemcmp(&LibSig,ReadFile(DataPtr,sizeof(LibSig)),sizeof(LibSig)) == 0);

    OutputSegment    = (WORD)SegAddress;
    eh.EntrySegment += OutputSegment;

    OutputDataFromFile(DataPtr,Length);

    if (Relocatable)
        RelocationRecords(&eh);
    else
    {
        if (eh.Relocations != 0)
            DGROUPRelocations(&eh,Length);
        if (SegAddress >= 0xF800)
            JumpRecord(0xFFFF,0,eh.EntrySegment,eh.EntryOffset);
        else if (!IsLibrary)
            StartAddressRecord(eh.EntrySegment,eh.EntryOffset);
    }

    EOFRecord();

    fclose(DestFile);
    printf("File %s written successfully.\n\n",DestName);
    exit(0);
}
