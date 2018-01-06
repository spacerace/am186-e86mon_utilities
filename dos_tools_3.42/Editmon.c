/******************************************************************************
 *                                                                            *
 *     EDITMON.C                                                              *
 *                                                                            *
 *     This file edits values of permanent variables stored in                *
 *     the monitor.                                                           *
 *                                                                            *
 *                                                                            *
 ******************************************************************************
 *                                                                            *
 * Copyright 1996 Advanced Micro Devices, Inc.                                *
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

#define MAXLEN (0x9000)

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
} ExeHdr, far * ExeHdrPtr;
 

typedef struct {
    WORD     Name;                 // Text name of the permanent variable
    WORD     Ptr;                  // Pointer to the internal variable
    DWORD    Default;              // Default value of the variable
} PermVar, far * LPPERM;


char SourceBuffer[MAXLEN];
FILE* SourceFile;

//////////////////////////////////////////////////////////////////////////
// ErrExit() prints an error message and exits the program.
//
void ErrExit(char * s,...)
{
    char Buffer[400];
    vsprintf(Buffer,s,(LPVOID)(&s+1));
    printf("\nEditMon Error -- %s\n\n",Buffer);
    exit(2);
}


//////////////////////////////////////////////////////////////////////////
// ShowHelp() shows the user all his choices
//
void ShowHelp(void)
{
    printf(
                                                                      "\n"
"    EditMon -- AMD 186 EMon editor version 3.10.\n"
"                      Copyright (C) 1996, Advanced Micro Devices.\n"
"    Syntax:\n"
"         EditMon <filename>  [string value]\n"
                                                                      "\n"
"    EditMon will show the permanent variables stored in <filename>.exe.\n"
                                                                      "\n"
"    If the string and value are also given, EditMon will alter and\n"
"    save the .EXE file with new parameters.\n"
    );
    exit(1);
}

//////////////////////////////////////////////////////////////////////////
// ParseDecimal() parses decimal numbers. Only returns TRUE if no non-decimal
// characters are encountered (unlike builtins such as scanf).
//
BOOL ParseDecimal(char* t,DWORD* value)
{
    *value = 0;

    while(isdigit(*t))
        *value  = *value * 10 + *(t++) - '0';

    return (*t == 0);
}

//////////////////////////////////////////////////////////////////////////
// Main program.  Parse command line, and then show the help message,
// or update a file.
//
void main(int argc, char* argv[])
{
    char ExeName[128];
    WORD      Length;
    DWORD     FileLength;
    LPBYTE    DataPtr;

    ExeHdrPtr ep = (ExeHdrPtr)SourceBuffer;
    LPPERM    PermArray;


    if ((argc != 2) && (argc != 4))
        ShowHelp();

    strcpy(ExeName,argv[1]);
    strcat(ExeName,".exe");

    if ((SourceFile=fopen(ExeName,"r+b")) == 0)
        ErrExit("Cannot open source file %s",ExeName);


    Length = fread(SourceBuffer,1,MAXLEN,SourceFile);
    if (Length < 1024)
        ErrExit("File read error");

    if (ep->MagicNumber != 0x5A4D)
        ErrExit("Invalid EXE signature");

    if (ep->PagesInFile > MAXLEN/512)
        ErrExit("File Too Big");

    FileLength = ep->PagesInFile*512-((512-ep->BytesLastPg)%512);
    if (Length <  FileLength)
        ErrExit("File Read Error: expected %lu, got %u",
                 FileLength, Length);

    Length =  FileLength;

    DataPtr = SourceBuffer + ep->ParsInHdr*16;

    Length -= ep->ParsInHdr*16;

    if ( (ep->EntrySegment != 0) ||
         (ep->EntryOffset != 0) ||
         (_fmemcmp(DataPtr+2,"AMD LPD 01",10) != 0) )
        ErrExit("Not a valid E86MON executable");

    if (argc == 4)
    {
        PermArray = (LPPERM)(DataPtr + *(LPWORD)(DataPtr+12));

        if (PermArray->Name == 0)
            PermArray++;

        while ((PermArray->Name != 0) &&
            (strcmp((char far *)(DataPtr+PermArray->Name),argv[2]) != 0) )
            PermArray++;

        if (PermArray->Name != 0)
            if (ParseDecimal(argv[3],(LPDWORD)(DataPtr+PermArray->Ptr)))
            {
                if ((fseek(SourceFile,0,SEEK_SET) != 0) ||
                    (fwrite(SourceBuffer,1,(WORD)FileLength,SourceFile)
                         != FileLength)  || (fclose(SourceFile) != 0))
                    ErrExit("File write failed");
            }
            else
                ErrExit("'%s' is not a valid decimal value",argv[3]);
        else
            ErrExit("Cannot find variable '%s'!",argv[2]);
    }


    PermArray = (LPPERM)(DataPtr + *(LPWORD)(DataPtr+12));

    if (PermArray->Name == 0)
        PermArray++;

    printf("\n\nCurrent permanent variable values:\n\n");
    while (PermArray->Name != 0)
    {
        printf("    %-10s = %lu\n",DataPtr+PermArray->Name,
               *(LPDWORD)(DataPtr+PermArray->Ptr));
        if (PermArray->Default != 0xFFFFFFFF)
            printf("           WARNING!  Default is not -1!!!!!\n");
        PermArray++;
    }
    exit(0);
}

