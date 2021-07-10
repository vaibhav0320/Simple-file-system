#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define BLOCK_SUPER 0
#define BLOCK_BLOCK_BITMAP 1
#define BLOCK_INODE_BITMAP 2
#define BLOCK_INODE_TABLE 3
#define BLOCK_MAX 99
#define INODE_MAX 127

// structure of an inode entry
typedef struct
{
	char TT[2];				  // entry type; "DI" means directory and "FI" means file
	char XX[2], YY[2], ZZ[2]; // the blocks for this entry; 00 means not used
} _inode_entry;

// structure of a directory entry
typedef struct
{
	char F;			 // '1' means used; '0' means unused
	char fname[252]; // name of this entry; remember to include null character into it
	char MMM[3];	 // inode entry index which holds more info about this entry
} _directory_entry;

// SFS metadata; read during mounting
int BLB;						// total number of blocks
int INB;						// total number of entries in inode table
char _block_bitmap[1024];		// the block bitmap array
char _inode_bitmap[1024];		// the inode bitmap array
_inode_entry _inode_table[128]; // the inode table containing 128 inode entries

// useful info
int free_disk_blocks;					   // number of available disk blocks
int free_inode_entries;					   // number of available entries in inode table
int CD_INODE_ENTRY = 0;					   // index of inode entry of the current directory in the inode table
char current_working_directory[252] = "/"; // name of current directory (useful in the prompt)

FILE *df = NULL; // THE DISK FILE

// function declarations
// HELPERS
int stoi(char *, int);
void itos(char *, int, int);
void printPrompt();

// DISK ACCESS
void mountSFS();
int readSFS(int, char *);
int writeSFS(int, char *);

// BITMAP ACCESS
int getBlock();
void returnBlock(int);
int getInode();
void returnInode(int);

// COMMANDS
void ls();
void rd();
void cd(char *);
void md(char *);
void stats();

/*############################################################################*/
/****************************************************************************/
/* returns the integer value of string s; -1 on error
/*
/****************************************************************************/

int stoi(char *s, int n)
{
	int i;
	int ret = 0;

	for (i = 0; i < n; i++)
	{
		if (s[i] < 48 || s[i] > 57)
			return -1; // non-digit
		ret += pow(10, n - i - 1) * (s[i] - 48);
	}

	return ret;
}

/****************************************************************************/
/* returns the string representation of num in s
/* n is the width of the number; 0 padded if required
/*
/****************************************************************************/

void itos(char *s, int num, int n)
{
	char st[1024];
	sprintf(st, "%0*d", n, num);
	strncpy(s, st, n);
}

/****************************************************************************/
/* prints a prompt with current working directory
/*
/****************************************************************************/

void printPrompt()
{
	printf("SFS::%s# ", current_working_directory);
}

/*############################################################################*/
/****************************************************************************/
/* reads SFS metadata into memory structures
/* 
/****************************************************************************/

void mountSFS()
{
	int i;
	char buffer[1024];

	df = fopen("sfs.disk", "r+b");
	if (df == NULL)
	{
		printf("Disk file sfs.disk not found.\n");
		exit(1);
	}

	// read superblock
	fread(buffer, 1, 1024, df);
	BLB = stoi(buffer, 3);
	INB = stoi(buffer + 3, 3);
	printf("BLB: %d INB:%d\n",BLB,INB);
	// read block bitmap
	fread(_block_bitmap, 1, 1024, df);
	// initialize number of free disk blocks
	free_disk_blocks = BLB;
	for (i = 0; i < BLB; i++)
		free_disk_blocks -= (_block_bitmap[i] - 48);

	// read inode bitmap
	fread(_inode_bitmap, 1, 1024, df);
	// initialize number of unused inode entries
	free_inode_entries = INB;
	for (i = 0; i < INB; i++)
		free_inode_entries -= (_inode_bitmap[i] - 48);

	// read the inode table
	fread(_inode_table, 1, 1024, df);
}

/****************************************************************************/
/* reads a block of data from disk file into buffer
/* returns 0 if invalid block number
/*
/****************************************************************************/

int readSFS(int block_number, char buffer[1024])
{

	if (block_number < 0 || block_number > BLOCK_MAX)
		return 0;

	if (df == NULL)
		mountSFS(); // trying to read without mounting...!!!

	fseek(df, block_number * 1024, SEEK_SET); // set file pointer at right position
	fread(buffer, 1, 1024, df);				  // read a block, i.e. 1024 bytes into buffer

	return 1;
}

/****************************************************************************/
/* writes a block of data from buffer to disk file
/* if buffer is null pointer, then writes all zeros
/* returns 0 if invalid block number
/*
/****************************************************************************/

int writeSFS(int block_number, char buffer[1024])
{
	char empty_buffer[1024];

	if (block_number < 0 || block_number > BLOCK_MAX)
		return 0;

	if (df == NULL)
		mountSFS(); // trying to write without mounting...!!!

	fseek(df, block_number * 1024, SEEK_SET); // set file pointer at right position

	if (buffer == NULL)
	{ // if buffer is null
		memset(empty_buffer, '0', 1024);
		fwrite(empty_buffer, 1, 1024, df); // write all zeros
	}
	else
		fwrite(buffer, 1, 1024, df);

	fflush(df); // making sure disk file is always updated

	return 1;
}

/*############################################################################*/
/****************************************************************************/
/* finds the first available block using the block bitmap
/* updates the bitmap
/* writes the block bitmap to disk file
/* returns -1 on error; otherwise the block number
/*
/****************************************************************************/

int getBlock()
{
	int i;

	if (free_disk_blocks == 0)
		return -1;

	for (i = 0; i < BLB; i++)
		if (_block_bitmap[i] == '0')
			break; // 0 means available

	_block_bitmap[i] = '1';
	free_disk_blocks--;

	writeSFS(BLOCK_BLOCK_BITMAP, _block_bitmap);

	return i;
}

/****************************************************************************/
/* updates block bitmap when a block is no longer used
/* blocks 0 through 3 are treated special; so they are always in use
/*
/****************************************************************************/

void returnBlock(int index)
{
	if (index > 3 && index <= BLOCK_MAX)
	{
		_block_bitmap[index] = '0';
		free_disk_blocks++;

		writeSFS(BLOCK_BLOCK_BITMAP, _block_bitmap);
	}
}

/****************************************************************************/
/* finds the first unused position in inode table using the inode bitmap
/* updates the bitmap
/* writes the inode bitmap to disk file
/* returns -1 if table is full; otherwise the position
/*
/****************************************************************************/

int getInode()
{
	int i;

	if (free_inode_entries == 0)
		return -1;

	for (i = 0; i < INB; i++)
		if (_inode_bitmap[i] == '0')
			break; // 0 means available

	_inode_bitmap[i] = '1';
	free_inode_entries--;

	writeSFS(BLOCK_INODE_BITMAP, _inode_bitmap);

	return i;
}

/****************************************************************************/
/* updates inode bitmap when an inode entry is no longer used
/*
/****************************************************************************/

void returnInode(int index)
{
	if (index > 0 && index <= INODE_MAX)
	{
		_inode_bitmap[index] = '0';
		free_inode_entries++;

		writeSFS(BLOCK_INODE_BITMAP, _inode_bitmap);
	}
}

/*############################################################################*/
/****************************************************************************/
/* makes root directory the current directory 
/*
/****************************************************************************/

void rd()
{
	CD_INODE_ENTRY = 0; // first inode entry is for root directory
	current_working_directory[0] = '/';
	current_working_directory[1] = 0;
}

/****************************************************************************/
/* lists all files and directories in the current directory 
/*
/****************************************************************************/

void ls()
{
	char itype;
	int blocks[3];
	_directory_entry _directory_entries[4];

	int total_files = 0, total_dirs = 0;

	int i, j;
	int e_inode;

	// read inode entry for current directory
	// in SFS, an inode can point to three blocks at the most
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);

	// its a directory; so the following should never happen
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}

	// lets traverse the directory entries in all three blocks
	for (i = 0; i < 3; i++)
	{
		if (blocks[i] == 0)
			continue; // 0 means pointing at nothing

		readSFS(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

		// so, we got four possible directory entries now
		for (j = 0; j < 4; j++)
		{
			if (_directory_entries[j].F == '0')
				continue; // means unused entry

			e_inode = stoi(_directory_entries[j].MMM, 3); // this is the inode that has more info about this entry

			if (_inode_table[e_inode].TT[0] == 'F')
			{ // entry is for a file
				printf("%s\t", _directory_entries[j].fname);
				total_files++;
			}
			else if (_inode_table[e_inode].TT[0] == 'D')
			{ // entry is for a directory; print it in BRED
				printf("\x1B[31m%s\x1B[0m\t", _directory_entries[j].fname);
				total_dirs++;
			}
		}
	}

	printf("\n%d file%c and %d director%s.\n", total_files, (total_files <= 1 ? 0 : 's'), total_dirs, (total_dirs <= 1 ? "y" : "ies"));
}

/****************************************************************************/
/* moves into the directory <dname> within the current directory if 
/* it exists
/*
/****************************************************************************/

void cd(char *dname)
{
	char itype;
	int blocks[3];
	_directory_entry _directory_entries[4];

	int i, j;
	int e_inode;

	char found = 0;

	// read inode entry for current directory
	// in SFS, an inode can point to three blocks at the most
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);

	// its a directory; so the following should never happen
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}

	// now lets try to see if a directory by the name already exists
	for (i = 0; i < 3; i++)
	{
		if (blocks[i] == 0)
			continue; // 0 means pointing at nothing

		readSFS(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

		// so, we got four possible directory entries now
		for (j = 0; j < 4; j++)
		{
			if (_directory_entries[j].F == '0')
				continue; // means unused entry

			e_inode = stoi(_directory_entries[j].MMM, 3); // this is the inode that has more info about this entry

			if (_inode_table[e_inode].TT[0] == 'D')
			{ // entry is for a directory; can't cd into a file, right?
				if (strncmp(dname, _directory_entries[j].fname, 252) == 0)
				{			   // and it is the one we are looking for
					found = 1; // VOILA
					break;
				}
			}
		}
		if (found)
			break; // no need to search more
	}

	if (found)
	{
		CD_INODE_ENTRY = e_inode;						// just keep track of which inode entry in the table corresponds to this directory
		strncpy(current_working_directory, dname, 252); // can use it in the prompt
	}
	else
	{
		printf("%.252s: No such directory.\n", dname);
	}
}

/****************************************************************************/
/* creates a new directory called <dname> in the current directory if the 
/* name is not already taken and there is still space available
/*
/****************************************************************************/

void md(char *dname)
{
	char itype;
	int blocks[3];
	_directory_entry _directory_entries[4];

	int i, j;

	int empty_dblock = -1, empty_dentry = -1;
	int empty_ientry;

	// non-empty name
	if (strlen(dname) == 0)
	{
		printf("Usage: md <directory name>\n");
		return;
	}

	// do we have free inodes
	if (free_inode_entries == 0)
	{
		printf("Error: Inode table is full.\n");
		return;
	}

	// read inode entry for current directory
	// in SFS, an inode can point to three blocks at the most
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);

	// its a directory; so the following should never happen
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}

	// now lets try to see if the name already exists
	for (i = 0; i < 3; i++)
	{
		if (blocks[i] == 0)
		{ // 0 means pointing at nothing
			if (empty_dblock == -1)
				empty_dblock = i; // we can later add a block if needed
			continue;
		}

		readSFS(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

		// so, we got four possible directory entries now
		for (j = 0; j < 4; j++)
		{
			if (_directory_entries[j].F == '0')
			{ // means unused entry
				if (empty_dentry == -1)
				{
					empty_dentry = j;
					empty_dblock = i;
				} // AAHA! lets keep a note of it, just in case we have to create the new directory
				continue;
			}

			if (strncmp(dname, _directory_entries[j].fname, 252) == 0)
			{ // compare with user given name
				printf("%.252s: Already exists.\n", dname);
				return;
			}
		}
	}
	// so directory name is new

	// if we did not find an empty directory entry and all three blocks are in use; then no new directory can be made
	if (empty_dentry == -1 && empty_dblock == -1)
	{
		printf("Error: Maximum directory entries reached.\n");
		return;
	}
	else
	{ // otherwise
		if (empty_dentry == -1)
		{ // Great! didn't find an empty entry but not all three blocks have been used
			empty_dentry = 0;

			if ((blocks[empty_dblock] = getBlock()) == -1)
			{ // first get a new block using the block bitmap
				printf("Error: Disk is full.\n");
				return;
			}

			writeSFS(blocks[empty_dblock], NULL); // write all zeros to the block (there may be junk from the past!)

			switch (empty_dblock)
			{ // update the inode entry of current dir to reflect that we are using a new block
			case 0:
				itos(_inode_table[CD_INODE_ENTRY].XX, blocks[empty_dblock], 2);
				break;
			case 1:
				itos(_inode_table[CD_INODE_ENTRY].YY, blocks[empty_dblock], 2);
				break;
			case 2:
				itos(_inode_table[CD_INODE_ENTRY].ZZ, blocks[empty_dblock], 2);
				break;
			}
		}

		// NOTE: all error checkings have already been done at this point!!
		// time to put everything together

		empty_ientry = getInode(); // get an empty place in the inode table which will store info about blocks for this new directory

		readSFS(blocks[empty_dblock], (char *)_directory_entries);   // read block of current directory where info on this new directory will be written
		_directory_entries[empty_dentry].F = '1';					 // remember we found which directory entry is unused; well, set it to used now
		strncpy(_directory_entries[empty_dentry].fname, dname, 252); // put the name in there
		itos(_directory_entries[empty_dentry].MMM, empty_ientry, 3); // and the index of the inode that will hold info inside this directory
		writeSFS(blocks[empty_dblock], (char *)_directory_entries);  // now write this block back to the disk

		strncpy(_inode_table[empty_ientry].TT, "DI", 2); // create the inode entry...first, its a directory, so DI
		strncpy(_inode_table[empty_ientry].XX, "00", 2); // directory is just created; so no blocks assigned to it yet
		strncpy(_inode_table[empty_ientry].YY, "00", 2);
		strncpy(_inode_table[empty_ientry].ZZ, "00", 2);

		writeSFS(BLOCK_INODE_TABLE, (char *)_inode_table); // phew!! write the inode table back to the disk
	}
}

/****************************************************************************/
/* prints number of free blocks in the disk and free inode entries in the inode table
/*
/****************************************************************************/

void stats()
{
	int blocks_free = BLB, inodes_free = INB;
	int i;

	for (i = 0; i < BLB; i++)
		blocks_free -= (_block_bitmap[i] - 48);
	for (i = 0; i < INB; i++)
		inodes_free -= (_inode_bitmap[i] - 48);

	printf("%d block%c free.\n", blocks_free, (blocks_free <= 1 ? 0 : 's'));
	printf("%d inode entr%s free.\n", inodes_free, (inodes_free <= 1 ? "y" : "ies"));
}
int display_file(char *fname)
{
	char itype;
	int blocks[3];
	_directory_entry _directory_entries[4];

	int total_files = 0, total_dirs = 0;

	int i, j;
	int e_inode;

	// read inode entry for current directory
	// in SFS, an inode can point to three blocks at the most
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);
	//printf("%s\n",_inode_table[CD_INODE_ENTRY].YY);
	// its a directory; so the following should never happen
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}

	// lets traverse the directory entries in all three blocks
	for (i = 0; i < 3; i++)
	{
		if (blocks[i] == 0)
			continue; // 0 means pointing at nothing

		readSFS(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

		// so, we got four possible directory entries now
		for (j = 0; j < 4; j++)
		{
			if (_directory_entries[j].F == '0')
				continue; // means unused entry

			e_inode = stoi(_directory_entries[j].MMM, 3); // this is the inode that has more info about this entry

			if (_inode_table[e_inode].TT[0] == 'F' && !strcmp(_directory_entries[j].fname, fname))
			{ // entry is for a file
				char buf[1024];
				int data_block[3];
				data_block[0] = stoi(_inode_table[e_inode].XX, 2);
				data_block[1] = stoi(_inode_table[e_inode].YY, 2);
				data_block[2] = stoi(_inode_table[e_inode].ZZ, 2);
				for (int db = 0; db < 3; db++)
				{
					memset(buf, 0, 1024);
					//printf("%d\n",data_block[db]);
					if (data_block[db] != 0 && readSFS(data_block[db], buf))
					{
						printf("%s", buf);
					}
				}
				printf("\n");
				return 1;
			}
		}
	}
	return 0;
}
int write_file_data(int *blocks, int n, char *buf)
{
	for (int i = 0; i < n; i++)
	{
		writeSFS(blocks[i], buf + i * 1024);
	}
	return 1;
}

int creat_file(char *fname)
{
	if (free_inode_entries == 0)
	{
		printf("Inodes full\n");
		return 0;
	}
	char itype;
	int blocks[3];
	int dir_dnode = -1, block_number = -1;
	_directory_entry _directory_entries[4];
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);
	//printf("%s\n",_inode_table[CD_INODE_ENTRY].YY);
	// its a directory; so the following should never happen
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}
	for (int i = 0; i < 3; i++)
	{
		if (blocks[i] != 0)
		{
			readSFS(blocks[i], (char *)_directory_entries);
			for (int j = 0; j < 4; j++)
			{
				if (_directory_entries[j].F == '0')
				{
					block_number = i;
					dir_dnode = j;
				}
				else
				{
					if (strcmp(_directory_entries[j].fname, fname) == 0)
					{
						printf("File already exist\n");
						return 0;
					}
				}
			}
		}
		else
		{
			if (dir_dnode == -1)
			{
				block_number = i;
			}
		}
	}
	if (block_number == -1)
	{
		printf("Maximum limit reached for directory\n");
	}
	if (dir_dnode == -1)
	{
		int bn = getBlock();
		if (bn == -1)
		{
			printf("ERROR: datablock limit reached\n");
			return 0;
		}
		char temp[2];
		itos(temp, bn, 2);
		if (!blocks[0])
		{
			strncpy(_inode_table[CD_INODE_ENTRY].XX, temp, 2);
		}
		else if (!blocks[1])
		{
			strncpy(_inode_table[CD_INODE_ENTRY].YY, temp, 2);
		}
		else if (!blocks[2])
		{
			strncpy(_inode_table[CD_INODE_ENTRY].ZZ, temp, 2);
		}
		block_number = bn;
		dir_dnode = 0;
	}
	readSFS(blocks[block_number], (char *)_directory_entries);
	_directory_entries[dir_dnode].F = '1';
	strncpy(_directory_entries[dir_dnode].fname, fname, 253);
	char inode_number[3];
	int inn = getInode();
	if (inn == -1)
	{
		printf("ERROR: Inode limit reached\n");
		return 0;
	}
	itos(inode_number, inn, 3);
	strncpy(_directory_entries[dir_dnode].MMM, inode_number, 3);
	writeSFS(block_number, (char *)_directory_entries);
	char input_buf[3072];
	int input_char;
	int input_len = 0, written_block = 0;
	memset(input_buf, 0, 3072);
	printf("give input\n");
	while (input_len < 3071)
	{
		input_char = getchar();
		if (input_char == 27)
		{
			break;
		}
		input_buf[input_len++] = input_char;
	}
	blocks[0] = 0;
	blocks[1] = 0;
	blocks[2] = 0;
	if (input_len < 1024)
	{
		blocks[0] = getBlock();
		if (blocks[0] == -1)
		{
			printf("Out of space\n");
			return 0;
		}
		write_file_data(blocks, 1, input_buf);
		written_block = 1;
	}
	else if (input_len > 1023 && input_len < 2048)
	{
		blocks[0] = getBlock();
		blocks[1] = getBlock();
		if (blocks[0] == -1 || blocks[1] == -1)
		{
			printf("Out of space\n");
			return 0;
		}
		write_file_data(blocks, 2, input_buf);
		written_block = 2;
	}
	else if (input_len > 2047)
	{
		blocks[0] = getBlock();
		blocks[1] = getBlock();
		blocks[2] = getBlock();
		if (blocks[0] == -1 || blocks[1] == -1 || blocks[2] == -1)
		{
			printf("Out of space\n");
			return 0;
		}
		write_file_data(blocks, 3, input_buf);
		written_block = 3;
	}
	strncpy(_inode_table[inn].TT, "FI", 2);
	char temp[2];
	itos(temp, blocks[0], 2);
	strncpy(_inode_table[inn].XX, temp, 2);
	itos(temp, blocks[1], 2);
	strncpy(_inode_table[inn].YY, temp, 2);
	itos(temp, blocks[2], 2);
	strncpy(_inode_table[inn].ZZ, temp, 2);
	writeSFS(3, (char *)_inode_table);
	return 1;
}
int get_files_name(int inode_number, char names[12][252])
{
	int blk[3], index = 0;
	_directory_entry store_file_name[4];
	blk[0] = stoi(_inode_table[inode_number].XX, 2);
	blk[1] = stoi(_inode_table[inode_number].YY, 2);
	blk[2] = stoi(_inode_table[inode_number].ZZ, 2);
	for (int kk = 0; kk < 3; kk++)
	{
		if (blk[kk] != 0)
		{
			readSFS(blk[kk], (char *)store_file_name);
			for (int jj = 0; jj < 4; jj++)
			{
				if (store_file_name[jj].F == '1')
				{
					strncpy(names[index++], store_file_name[jj].fname, 252);
				}
			}
		}
	}
	return 1;
}
int remove_file(int inode)
{
	int blocks[3];
	char itype;
	itype = _inode_table[inode].TT[0];
	blocks[0] = stoi(_inode_table[inode].XX, 2);
	blocks[1] = stoi(_inode_table[inode].YY, 2);
	blocks[2] = stoi(_inode_table[inode].ZZ, 2);
	for (int i = 0; i < 3; i++)
	{
		if (blocks[i] != 0)
		{
			returnBlock(blocks[i]);
		}
	}
	strncpy(_inode_table[inode].XX, "00", 2);
	strncpy(_inode_table[inode].YY, "00", 2);
	strncpy(_inode_table[inode].ZZ, "00", 2);
	writeSFS(3, (char *)_inode_table);
	returnInode(inode);
	return 1;
}
void check_dir_block()
{
	int blocks[3];
	char itype;
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);
	if (itype == 'F')
	{
		printf("Fatal Error! Aborting.\n");
		exit(1);
	}
	_directory_entry de[4];
	for (int i = 0; i < 3; i++)
	{
		readSFS(blocks[i], (char *)de);
		if (de[0].F == '0' && de[1].F == '0' && de[2].F == '0' && de[3].F == '0')
		{
			returnBlock(blocks[i]);
			if (i == 0)
			{
				strncpy(_inode_table[CD_INODE_ENTRY].XX, "00", 2);
			}
			if (i == 1)
			{
				strncpy(_inode_table[CD_INODE_ENTRY].YY, "00", 2);
			}
			if (i == 2)
			{
				strncpy(_inode_table[CD_INODE_ENTRY].ZZ, "00", 2);
			}
		}
	}
	writeSFS(3, (char *)_inode_table);
}
int remove(char *fname)
{
	int blocks[3], inode_number, is_file;
	char inode_number_string[3], itype;
	_directory_entry _directory_entries[4];
	itype = _inode_table[CD_INODE_ENTRY].TT[0];
	blocks[0] = stoi(_inode_table[CD_INODE_ENTRY].XX, 2);
	blocks[1] = stoi(_inode_table[CD_INODE_ENTRY].YY, 2);
	blocks[2] = stoi(_inode_table[CD_INODE_ENTRY].ZZ, 2);
	for (int i = 0; i < 3; i++)
	{
		if (blocks[i] != 0)
		{
			readSFS(blocks[i], (char *)_directory_entries);
			for (int j = 0; j < 4; j++)
			{
				if (_directory_entries[j].F == '1' && !strncmp(_directory_entries[j].fname, fname, 252))
				{
					inode_number = stoi(_directory_entries[j].MMM, 3);
					is_file = (_inode_table[inode_number].TT[0] == 'F');
					if (is_file)
					{
						remove_file(inode_number);
					}
					else
					{
						int store_prev_dir = CD_INODE_ENTRY;
						char prev_dir_name[252];
						strncpy(prev_dir_name, current_working_directory, 252);
						cd(fname);
						char list[12][252];
						int index = get_files_name(inode_number, list);
						for (int kk = 0; kk < index; kk++)
						{
							remove(list[kk]);
						}
						returnInode(inode_number);
						CD_INODE_ENTRY = store_prev_dir;
						strncpy(current_working_directory, prev_dir_name, 252);
					}
					_directory_entries[j].F = '0';
					writeSFS(blocks[i], (char *)_directory_entries);
					check_dir_block();
					return 1;
				}
			}
		}
	}
	return 0;
}
int parse_line(char buf[1024], char tokens[8][64])
{
	int i, j = 0, ctr = 0;
	for (i = 0; i <= (strlen(buf)); i++)
	{
		if (buf[i] == ' ' || buf[i] == '\0' || buf[i] == '\n')
		{
			tokens[ctr][j] = '\0';
			ctr++;
			j = 0;
		}
		else
		{
			tokens[ctr][j] = buf[i];
			j++;
		}
	}
	return ctr;
}
int main()
{
	int t;
	char ib[1024];
	mountSFS();
	while (1)
	{
		printPrompt();
		memset(ib, 0, 1024);
		char tokens[8][64];
		fgets(ib, 1024, stdin);
		if (ib[0] == '\n')
		{
			continue;
		}
		else
		{
			t = parse_line(ib, tokens);

			if (!strcmp(tokens[0], "display"))
			{
				display_file(tokens[1]);
			}
			else if (!strcmp(tokens[0], "creat"))
			{
				creat_file(tokens[1]);
			}
			else if (!strcmp(tokens[0], "rm"))
			{
				if (remove(tokens[1]) == 0)
				{
					printf("ERROR: file or dir not found\n.");
				};
			}
			else if (!strcmp(tokens[0], "ls"))
			{
				ls();
			}
			else if (!strcmp(tokens[0], "cd"))
			{
				cd(tokens[1]);
			}
			else if (!strcmp(tokens[0], "stat"))
			{
				stats();
			}
			else if (!strcmp(tokens[0], "md"))
			{
				md(tokens[1]);
			}
			else if (!strcmp(tokens[0], "rd"))
			{
				rd();
			}
			else if (!strcmp(tokens[0], "exit"))
			{
				exit(0);
			}
			else
			{
				printf("No command found\n");
			}
		}
	}
	return 0;
}