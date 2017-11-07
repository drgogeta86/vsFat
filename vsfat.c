/*
 * busexmp - example memory-based block device using BUSE
 * Copyright (C) 2013 Adam Cozzette
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buse.h"
#include "vsfat.h"

static unsigned char *mbr;
static const u_int32_t part1_base = 1048576;

static BootEntry bootentry;

static unsigned char *fat;
static unsigned char *files;

static AddressRegion *address_regions;
static u_int32_t address_regions_count;

static int xmpl_debug = 1;



static int xmp_read (void *buf, u_int32_t len, u_int64_t offset,
		     void *userdata);
static int xmp_write (const void *buf, u_int32_t len, u_int64_t offset,
		      void *userdata);
static void xmp_disc (void *userdata);
static int xmp_flush (void *userdata);
static int xmp_trim (u_int64_t from, u_int32_t len, void *userdata);
static void add_address_region (u_int64_t base, u_int64_t length,
				void *mem_pointer, char *file_path);
static void printBootSect (struct BootEntry bootentry);


static struct buse_operations aop = {
  .read = xmp_read,
  .write = xmp_write,
  .disc = xmp_disc,
  .flush = xmp_flush,
  .trim = xmp_trim,
  .blksize = 512,
  .size_blocks = 4292870144,
};

//static u_int64_t part_blocks = aop.size_blocks-2048;


static int
xmp_read (void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  if (*(int *) userdata)
    fprintf (stderr, "Read %#x bytes from  %#llx\n", len, offset);
  //Make sure the buffer is zeroed
  memset (buf, 0, len);
  //Check if this read falls within a mapped area
  //No point doing this if we've already used up len
  for (u_int32_t a=0; a < address_regions_count && len>0; a++)
    {
      if ((offset >= address_regions[a].base &&	// See if the beginning is inside our range
	   offset <= address_regions[a].base + address_regions[a].length) || 
	   (offset + len >= address_regions[a].base &&  // See if the end is (we'll also accept both)
	   offset + len <= address_regions[a].base + address_regions[a].length) ||
	   (offset <= address_regions[a].base && //Or, we're entirely contained within it
	   offset + len >= address_regions[a].base + address_regions[a].length)
	   )
	{
	  //For real memory mapped stuff
	  if (address_regions[a].mem_pointer)
	    {
	      //Make sure the buffer is zeroed
	      //memset (buf, 0, len);
	      u_int32_t uselen = len;
	      u_int32_t usepos;
	      u_int32_t usetarget;
	      if (offset < address_regions[a].base)
		{
		  usepos = 0;
		  usetarget = address_regions[a].base - offset;
		}
	      else
		{
		  usepos = offset - address_regions[a].base;
		  usetarget = 0;
		}

	      //If they're only asking for part of what we have
	      if (address_regions[a].base + address_regions[a].length >= offset + len)
		{
		  uselen = offset + len - address_regions[a].base;
		}

	      //Make sure we don't go off the end
	      if (uselen > address_regions[a].length-usepos)
		{
		  uselen = address_regions[a].length-usepos;
		}
              //Or give them more than what they want
              if(uselen > len)
                {
                  uselen = len;
                }

//	      if (*(int *) userdata)
//		{
		  fprintf (stderr,
			   "base: %#llx length: %#llx usepos: %#x offset: %#llx len: %#x usetarget: %#x uselen: %#x\n",
			   address_regions[a].base, address_regions[a].length,
			   usepos, offset, len, usetarget, uselen);
//		}
	      memcpy ((unsigned char *) buf + usetarget,
		      (unsigned char *) address_regions[a].mem_pointer +
		      usepos, uselen);
	      len = len - (uselen + usetarget);
	      offset += uselen + usetarget;
	      buf = (unsigned char*)buf + uselen + usetarget;
	    }
	  //A mapped in file
	  if (address_regions[a].file_path)
	    {
	      //TODO: Implement me
	    }
	}
      }
      //If we've gotten here, we've used up all the mapped in areas, so fill the rest with 0s
      memset (buf, 0, len);
      //Pedantry
      buf = (unsigned char*)buf + len;
      len = 0;
  return 0;
}

static int
xmp_write (const void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  buf;
  if (*(int *) userdata)
    fprintf (stderr, "W - %llu, %u\n", offset, len);

  if (offset > (u_int64_t) aop.blksize * aop.size_blocks)
    {
      return 0;
    }
  //memcpy((char *)data + offset, buf, len);
  return 0;
}

static void
xmp_disc (void *userdata)
{
  (void) (userdata);
  fprintf (stderr, "Received a disconnect request.\n");
}

static int
xmp_flush (void *userdata)
{
  (void) (userdata);
  fprintf (stderr, "Received a flush request.\n");
  return 0;
}

static int
xmp_trim (u_int64_t from, u_int32_t len, void *userdata)
{
  (void) (userdata);
  fprintf (stderr, "T - %llu, %u\n", from, len);
  return 0;
}

static void
add_address_region (u_int64_t base, u_int64_t length, void *mem_pointer,
		    char *file_path)
{
  address_regions_count++;
  address_regions =
    realloc (address_regions, address_regions_count * sizeof (AddressRegion));
  address_regions[address_regions_count - 1].base = base;
  address_regions[address_regions_count - 1].length = length;
  address_regions[address_regions_count - 1].mem_pointer = mem_pointer;
  address_regions[address_regions_count - 1].file_path = file_path;
}

static void
build_mbr ()
{
  unsigned char bootcode[] =
    { 0xFA, 0xB8, 0x00, 0x10, 0x8E, 0xD0, 0xBC, 0x00, 0xB0, 0xB8, 0x00, 0x00,
0x8E, 0xD8, 0x8E, 0xC0, 0xFB, 0xBE, 0x00, 0x7C, 0xBF, 0x00, 0x06, 0xB9, 0x00, 0x02, 0xF3, 0xA4,
0xEA, 0x21, 0x06, 0x00, 0x00, 0xBE, 0xBE, 0x07, 0x38, 0x04, 0x75, 0x0B, 0x83, 0xC6, 0x10, 0x81,
0xFE, 0xFE, 0x07, 0x75, 0xF3, 0xEB, 0x16, 0xB4, 0x02, 0xB0, 0x01, 0xBB, 0x00, 0x7C, 0xB2, 0x80,
0x8A, 0x74, 0x01, 0x8B, 0x4C, 0x02, 0xCD, 0x13, 0xEA, 0x00, 0x7C, 0x00, 0x00, 0xEB, 0xFE, 0x00 };
  unsigned char serial[] = { 0xDE, 0xAB, 0xBE, 0xEF };
  unsigned char parts[4][16] =
    { {0x00, 0x20, 0x21, 0x00, 0x0c, 0xcd, 0xfb, 0xd2, 0x00, 0x08, 0x00, 0x00,
       0x00, 0xf8, 0xdf, 0xff},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00}
  };
  unsigned char footer[] = { 0x55, 0xAA };

  mbr = malloc (512);
  memset (mbr, 0, 512);

  memcpy (mbr, &bootcode, sizeof (bootcode));
  memcpy (mbr + 440, &serial, sizeof (serial));

  for (int a = 0; a < 4; a++)
    {
      memcpy (mbr + 446 + 16 * a, &parts[a], 16);
    }
  memcpy (mbr + 510, &footer, sizeof (footer));
  add_address_region (0, 512, mbr, 0);

}

static void
build_boot_sector ()
{

  //Build our bootsector
  bootentry.BS_jmpBoot[0] = 0xeb;
  bootentry.BS_jmpBoot[1] = 0x58;
  bootentry.BS_jmpBoot[2] = 0x90;
  strncpy ((char *) &bootentry.BS_OEMName, "MSDOS5.0", 8);
  bootentry.BPB_BytsPerSec = 512;
  bootentry.BPB_SecPerClus = 1;
  bootentry.BPB_RsvdSecCnt = 32;
  bootentry.BPB_NumFATs = 2;
  bootentry.BPB_RootEntCnt = 0;
  bootentry.BPB_TotSec16 = 0;
  bootentry.BPB_Media = 248;
  bootentry.BPB_FATSz16 = 0;
  bootentry.BPB_SecPerTrk = 32;
  bootentry.BPB_NumHeads = 64;
  bootentry.BPB_HiddSec = 0;
  bootentry.BPB_TotSec32 = 102400;
  bootentry.BPB_FATSz32 = 788;
  bootentry.BPB_ExtFlags = 0;
  bootentry.BPB_FSVer = 0;
  bootentry.BPB_RootClus = 2;
  bootentry.BPB_FSInfo = 1;
  bootentry.BPB_BkBootSec = 6;
  bootentry.BS_DrvNum = 128;
  bootentry.BS_Reserved1 = 0;
  bootentry.BS_BootSig = 29;
  bootentry.BS_VolID = 0x8456f237;
  bootentry.BS_BootSign = 0xAA55;

  unsigned char vol[] =
    { 0x56, 0x53, 0x46, 0x41, 0x54, 0x46, 0x53, 0x20, 0x20, 0x20, 0x20 };
  unsigned char fstype[] = { 0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20 };

  memcpy (&bootentry.BS_VolLab, vol, sizeof (vol));
  memcpy (&bootentry.BS_FilSysType, fstype, sizeof (fstype));
  if (xmpl_debug)
    {
      printBootSect (bootentry);
      fprintf (stderr, "BS 1: %x\n", part1_base);
      fprintf (stderr, "BS 2: %x\n",
	       part1_base +
	       bootentry.BPB_BkBootSec * bootentry.BPB_BytsPerSec);
    }

  //Main copy
  add_address_region (part1_base, 512, &bootentry, 0);
  add_address_region (part1_base +
		      bootentry.BPB_BkBootSec * bootentry.BPB_BytsPerSec, 512,
		      &bootentry, 0);
}

static u_int64_t
address_from_fatclus (u_int32_t fatclus)
{
  return (u_int64_t) part1_base +
    (u_int64_t) bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus * fatclus;
}

static u_int64_t
address_from_fatsec (u_int32_t fatsec)
{
  return (u_int64_t) part1_base +
    (u_int64_t) bootentry.BPB_BytsPerSec * fatsec;
}

static u_int32_t
fat_location (u_int32_t fatnum)
{
  return bootentry.BPB_RsvdSecCnt + bootentry.BPB_FATSz32 * fatnum;
}

//Probably useless
static u_int32_t
root_dir_loc ()
{
  //BPB_NumFATs is 1 based, so this actually gives us the end of the last fat
  return fat_location (bootentry.BPB_NumFATs);
}

static u_int32_t
data_loc ()
{
  //The formula for this is:
  //DataStartSector = RootDirStartSector + RootDirSectors;
  //RootDirSectors = (32 * BPB_RootEntCnt + BPB_BytsPerSec - 1) / BPB_BytsPerSec;
  //We're 100% FAT32, so BPB_RootEntCnt=0, which makes RootDirSectors = (511)/512 = 1
  return root_dir_loc () + 1;
}

static void
build_fats ()
{
  unsigned char fatspecial[] = { 0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xF8, 0xFF, 0xFF, 0x0F,	//Special entries
    0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F
  };				//My 3 files
  //0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F};

//  unsigned char fatspecial[] = {0xF8,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F};

//  0xF8,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F};
  fat = malloc (bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec);
  memset (fat, 0, bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec);

  memcpy (fat, fatspecial, sizeof (fatspecial));

  //For now, just map these in blind
  printf ("fat0: %llx\n", address_from_fatsec (fat_location (0)));
  printf ("fat1: %llx\n", address_from_fatsec (fat_location (1)));

  add_address_region (address_from_fatsec (fat_location (0)),
		      bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec, fat,
		      0);
  add_address_region (address_from_fatsec (fat_location (1)),
		      bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec, fat,
		      0);
}


static void
build_files ()
{
  unsigned char filedata[] =
    { 0x42, 0x74, 0x00, 0x77, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x6B, 0x00, 0x0F,
0x00, 0x6D, 0x2E, 0x00, 0x74, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x6D, 0x00, 0x61, 0x00, 0x63, 0x00, 0x20, 0x00, 0x66, 0x00, 0x0F,
0x00, 0x6D, 0x61, 0x00, 0x73, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x20, 0x00, 0x00, 0x00,
0x6E, 0x00, 0x65, 0x00, 0x4D, 0x41, 0x43, 0x46, 0x41, 0x53, 0x7E, 0x31, 0x54, 0x58, 0x54, 0x20,
0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x66, 0x4B, 0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x03, 0x00,
0x9C, 0x00, 0x00, 0x00, 0x42, 0x68, 0x00, 0x65, 0x00, 0x73, 0x00, 0x73, 0x00, 0x2E, 0x00, 0x0F,
0x00, 0x44, 0x74, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x70, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x61, 0x00, 0x0F,
0x00, 0x44, 0x20, 0x00, 0x69, 0x00, 0x61, 0x00, 0x20, 0x00, 0x64, 0x00, 0x75, 0x00, 0x00, 0x00,
0x74, 0x00, 0x63, 0x00, 0x50, 0x45, 0x4C, 0x4C, 0x41, 0x49, 0x7E, 0x31, 0x54, 0x58, 0x54, 0x20,
0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x66, 0x4B, 0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x04, 0x00,
0x2F, 0x00, 0x00, 0x00, 0x42, 0x61, 0x00, 0x67, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x64, 0x00, 0x0F,
0x00, 0x78, 0x61, 0x00, 0x2E, 0x00, 0x74, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x72, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x20, 0x00, 0x0F,
0x00, 0x78, 0x70, 0x00, 0x61, 0x00, 0x72, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x00, 0x00,
0x73, 0x00, 0x20, 0x00, 0x52, 0x45, 0x4E, 0x53, 0x50, 0x41, 0x7E, 0x31, 0x54, 0x58, 0x54, 0x20,
0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x66, 0x4B, 0x00, 0x00, 0xA0, 0xB8, 0x66, 0x4B, 0x05, 0x00,
0xD7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x73, 0x79, 0x73, 0x63, 0x74, 0x6C, 0x20, 0x2D, 0x77, 0x20, 0x6B, 0x65,
0x72, 0x6E, 0x2E, 0x69, 0x70, 0x63, 0x2E, 0x6D, 0x61, 0x78, 0x73, 0x6F, 0x63, 0x6B, 0x62, 0x75,
0x66, 0x3D, 0x38, 0x33, 0x38, 0x38, 0x36, 0x30, 0x38, 0x0A, 0x73, 0x79, 0x73, 0x63, 0x74, 0x6C,
0x20, 0x2D, 0x77, 0x20, 0x6E, 0x65, 0x74, 0x2E, 0x69, 0x6E, 0x65, 0x74, 0x2E, 0x74, 0x63, 0x70,
0x2E, 0x73, 0x65, 0x6E, 0x64, 0x73, 0x70, 0x61, 0x63, 0x65, 0x3D, 0x32, 0x30, 0x39, 0x37, 0x31,
0x35, 0x32, 0x0A, 0x73, 0x79, 0x73, 0x63, 0x74, 0x6C, 0x20, 0x2D, 0x77, 0x20, 0x6E, 0x65, 0x74,
0x2E, 0x69, 0x6E, 0x65, 0x74, 0x2E, 0x74, 0x63, 0x70, 0x2E, 0x72, 0x65, 0x63, 0x76, 0x73, 0x70,
0x61, 0x63, 0x65, 0x3D, 0x32, 0x30, 0x39, 0x37, 0x31, 0x35, 0x32, 0x0A, 0x73, 0x79, 0x73, 0x63,
0x74, 0x6C, 0x20, 0x2D, 0x77, 0x20, 0x6E, 0x65, 0x74, 0x2E, 0x69, 0x6E, 0x65, 0x74, 0x2E, 0x74,
0x63, 0x70, 0x2E, 0x64, 0x65, 0x6C, 0x61, 0x79, 0x65, 0x64, 0x5F, 0x61, 0x63, 0x6B, 0x3D, 0x32,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x6A, 0x65, 0x66, 0x66, 0x0A, 0x36, 0x34, 0x31, 0x2D, 0x37, 0x38, 0x30,
0x2D, 0x32, 0x39, 0x30, 0x36, 0x0A, 0x6A, 0x65, 0x66, 0x66, 0x75, 0x40, 0x66, 0x6C, 0x79, 0x63,
0x6C, 0x61, 0x73, 0x73, 0x69, 0x63, 0x61, 0x76, 0x69, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x2E, 0x63,
0x6F, 0x6D, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x32, 0x31, 0x3A, 0x0A, 0x44, 0x69, 0x6D, 0x73, 0x75, 0x6D, 0x0A, 0x4E,
0x65, 0x6C, 0x73, 0x6F, 0x6E, 0x2D, 0x41, 0x74, 0x6B, 0x69, 0x6E, 0x73, 0x20, 0x4D, 0x75, 0x73,
0x65, 0x75, 0x6D, 0x20, 0x6F, 0x66, 0x20, 0x41, 0x72, 0x74, 0x0A, 0x3F, 0x20, 0x41, 0x69, 0x72,
0x6C, 0x69, 0x6E, 0x65, 0x20, 0x48, 0x69, 0x73, 0x74, 0x6F, 0x72, 0x79, 0x20, 0x4D, 0x75, 0x73,
0x65, 0x75, 0x6D, 0x0A, 0x51, 0x33, 0x39, 0x0A, 0x43, 0x68, 0x65, 0x63, 0x6B, 0x69, 0x6E, 0x20,
0x48, 0x6F, 0x74, 0x65, 0x6C, 0x0A, 0x0A, 0x32, 0x32, 0x3A, 0x20, 0x0A, 0x42, 0x72, 0x65, 0x61,
0x6B, 0x66, 0x61, 0x73, 0x74, 0x20, 0x40, 0x20, 0x52, 0x69, 0x76, 0x65, 0x72, 0x20, 0x4D, 0x61,
0x72, 0x6B, 0x65, 0x74, 0x0A, 0x45, 0x77, 0x69, 0x6E, 0x67, 0x20, 0x61, 0x6E, 0x64, 0x20, 0x4D,
0x75, 0x72, 0x69, 0x65, 0x6C, 0x20, 0x4B, 0x61, 0x75, 0x66, 0x6D, 0x61, 0x6E, 0x20, 0x4D, 0x65,
0x6D, 0x6F, 0x72, 0x69, 0x61, 0x6C, 0x20, 0x47, 0x61, 0x72, 0x64, 0x65, 0x6E, 0x0A, 0x57, 0x65,
0x73, 0x74, 0x70, 0x6F, 0x72, 0x74, 0x0A, 0x50, 0x68, 0x6F, 0x20, 0x6C, 0x75, 0x6E, 0x63, 0x68,
0x0A, 0x50, 0x6C, 0x61, 0x7A, 0x61, 0x0A, 0x52, 0x65, 0x74, 0x75, 0x72, 0x6E, 0x20, 0x68, 0x6F,
0x6D, 0x65, 0x20, 0x2D, 0x20, 0x77, 0x6F, 0x72, 0x6B, 0x20, 0x6F, 0x6E, 0x20, 0x52, 0x65, 0x6E,
0x27, 0x73, 0x20, 0x73, 0x74, 0x75, 0x66, 0x66, 0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00 };

  files = malloc (sizeof (filedata));
  memcpy (files, filedata, sizeof (filedata));

  add_address_region (part1_base + 823296, sizeof (filedata), files, 0);
}

int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      fprintf (stderr,
	       "Usage:\n"
	       "  %s /dev/nbd0\n"
	       "Don't forget to load nbd kernel module (`modprobe nbd`) and\n"
	       "run example from root.\n", argv[0]);
      return 1;
    }

//  fprintf(stderr,"Creating virtual disk of size %llu\n",aop.size);

//  data = malloc(aop.size);

  build_mbr ();
  build_boot_sector ();
  build_fats ();
  build_files ();
/*
  u_int32_t size=atoi(argv[2]); // 100;
  u_int32_t location = part1_base + atoi(argv[3]);// 3072;
  unsigned char *buf = malloc(size);
  xmp_read (buf, size, location, &xmpl_debug);
  
  u_int32_t pos=0;
  while(size>0){
    if(pos%8==0){
      fprintf(stderr,"\n%02x ",location);
      }
    fprintf(stderr,"%02x ",buf[pos++]);
    size--;
    location++;
  }  
  fprintf(stderr,"\n");
  
*/
  return buse_main (argv[1], &aop, (void *) &xmpl_debug);
}


static void
printBootSect (BootEntry bootentry)
{
  fprintf (stderr, "BS_jmpBoot[3] %02x %02x %02x\n",
	   bootentry.BS_jmpBoot[0],
	   bootentry.BS_jmpBoot[1], bootentry.BS_jmpBoot[2]);

  fprintf (stderr,
	   "BS_OEMName[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
	   bootentry.BS_OEMName[0], bootentry.BS_OEMName[1],
	   bootentry.BS_OEMName[2], bootentry.BS_OEMName[3],
	   bootentry.BS_OEMName[4], bootentry.BS_OEMName[5],
	   bootentry.BS_OEMName[6], bootentry.BS_OEMName[7],
	   bootentry.BS_OEMName);

  fprintf (stderr, "BPB_BytsPerSec=%u\n", bootentry.BPB_BytsPerSec);
  fprintf (stderr, "BPB_SecPerClus=%u\n", bootentry.BPB_SecPerClus);
  fprintf (stderr, "BPB_RsvdSecCnt=%u\n", bootentry.BPB_RsvdSecCnt);
  fprintf (stderr, "BPB_NumFATs=%u\n", bootentry.BPB_NumFATs);
  fprintf (stderr, "BPB_RootEntCnt=%u\n", bootentry.BPB_RootEntCnt);
  fprintf (stderr, "BPB_TotSec16=%u\n", bootentry.BPB_TotSec16);
  fprintf (stderr, "BPB_Media=%u\n", bootentry.BPB_Media);
  fprintf (stderr, "BPB_FATSz16=%u\n", bootentry.BPB_FATSz16);
  fprintf (stderr, "BPB_SecPerTrk=%u\n", bootentry.BPB_SecPerTrk);
  fprintf (stderr, "BPB_NumHeads=%u\n", bootentry.BPB_NumHeads);
  fprintf (stderr, "BPB_HiddSec=%u\n", bootentry.BPB_HiddSec);
  fprintf (stderr, "BPB_TotSec32=%u\n", bootentry.BPB_TotSec32);
  fprintf (stderr, "BPB_FATSz32=%u\n", bootentry.BPB_FATSz32);
  fprintf (stderr, "BPB_ExtFlags=%u\n", bootentry.BPB_ExtFlags);
  fprintf (stderr, "BPB_FSVer=%u\n", bootentry.BPB_FSVer);
  fprintf (stderr, "BPB_RootClus=%u\n", bootentry.BPB_RootClus);
  fprintf (stderr, "BPB_FSInfo=%u\n", bootentry.BPB_FSInfo);
  fprintf (stderr, "BPB_BkBootSec=%u\n", bootentry.BPB_BkBootSec);
  fprintf (stderr, "BPB_Reserved[12] %02x\n", bootentry.BPB_Reserved[12]);
  fprintf (stderr, "BS_DrvNum=%u\n", bootentry.BS_DrvNum);
  fprintf (stderr, "BS_Reserved1=%02x\n", bootentry.BS_Reserved1);
  fprintf (stderr, "BS_BootSig=%02x\n", bootentry.BS_BootSig);
  fprintf (stderr, "BS_VolID=%02x\n", bootentry.BS_VolID);
  fprintf (stderr,
	   "BS_VolLab[11] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %.11s\n",
	   bootentry.BS_VolLab[0], bootentry.BS_VolLab[1],
	   bootentry.BS_VolLab[2], bootentry.BS_VolLab[3],
	   bootentry.BS_VolLab[4], bootentry.BS_VolLab[5],
	   bootentry.BS_VolLab[6], bootentry.BS_VolLab[7],
	   bootentry.BS_VolLab[8], bootentry.BS_VolLab[9],
	   bootentry.BS_VolLab[10], bootentry.BS_VolLab);

  fprintf (stderr,
	   "BS_FilSysType[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
	   bootentry.BS_FilSysType[0], bootentry.BS_FilSysType[1],
	   bootentry.BS_FilSysType[2], bootentry.BS_FilSysType[3],
	   bootentry.BS_FilSysType[4], bootentry.BS_FilSysType[5],
	   bootentry.BS_FilSysType[6], bootentry.BS_FilSysType[7],
	   bootentry.BS_FilSysType);

  fprintf (stderr, "\n");
}
