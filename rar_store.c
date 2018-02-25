#include <windows.h>
// Biniam F. Demissie, 2007
#pragma pack(push, 1)
typedef struct rar_file_hdr
{
	WORD	head_crc;
	BYTE	head_type;
	WORD	head_flags;
	WORD	head_size;
	DWORD	pack_size;
	DWORD	unp_size;
	BYTE	host_os;
	DWORD	file_crc;
	DWORD	ftime;
	BYTE	unp_version;
	BYTE	method;
	WORD	name_size;
	DWORD	attr;
};
#pragma pack(pop)

unsigned long crc32_tab[256];

void crc32_table(unsigned long *tab)
{
	static const unsigned long poly = 0xedb88320;
	unsigned long crc;
	register int i, j;
	/* make CRC32 table */	
	for (i=0; i<256; i++) {
		crc = i;
		for (j=0; j<8; j++) {
			if (crc & 0x0001)
				crc = (crc >> 0x0001) ^ poly;		
			else
				crc >>= 0x0001;
			}
		tab[i] = crc;
	}
}

unsigned long crc32(unsigned char *in, DWORD len)
{	
	unsigned long tmp=0xFFFFFFFFL;
	register unsigned long c;	
	
	for (c=0; c<len; c++) {
		tmp = ((tmp >> 8) & 0xFFFFFF) ^ (crc32_tab[((tmp & 0xFF) ^ in[c])]);		
	}
	return tmp ^ 0xFFFFFFFFL;	
}


void archive_putcurtime(HANDLE hFile, WORD *f_time, WORD *f_date)
{
	SYSTEMTIME systime;
	FILETIME ftime, lftime;
	
	if (hFile) {
		GetFileTime(hFile, &ftime, NULL, NULL);
		FileTimeToLocalFileTime(&ftime, &lftime);
		FileTimeToSystemTime(&lftime, &systime);
	} 
    
	/* convert to DOS time */
	*f_date = ((systime.wYear-1980) << 9) |
			  (systime.wMonth << 5) |  systime.wDay;

	*f_time = (systime.wHour << 11) |
		      (systime.wMinute << 5) |
			  (systime.wSecond / 2);
}
/* Arguments:
 *
 * rar_file: rar file to be created. e.g. C:\test.rar
 * file_to_add: file to be added to test.rar e.g., test.txt
 * packed_name: the name of file_to_add, after put into test.rar, e.g., test.txt or document.txt	
 */
int add_to_rar(char *rar_file, char *file_to_add, char *packed_name, int RAR_CREATE_NEW)
{
	struct rar_file_hdr rar_hdr;
	char buf[128], *p;
	WORD f_date, f_time;
	HANDLE hRar, hfile, hMap, MapBase;
	DWORD dw, fSize, RarfSize;

	unsigned char rar_main_hdr[20] = { /* RAR_MARK_HEAD & RAR_MAIN_HEAD */
		0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00, 0xCF, 0x90, 0x73,
		0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	unsigned char EndOfRar[7] = {
		0xC4, 0x3D, 0x7B, 0x00, 0x40, 0x07, 0x00
	};


	hfile = CreateFile(file_to_add, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)		
		return 1;	

	fSize = GetFileSize(hfile, NULL);

	if (fSize == 0xFFFFFFFF) {
		CloseHandle(hfile);
		return 1;
	}
	
	memset(&rar_hdr, 0, sizeof(rar_hdr));	
	if (RAR_CREATE_NEW) {		
		hRar = CreateFile(rar_file, GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		if(hRar == INVALID_HANDLE_VALUE) {
			CloseHandle(hfile);
			return 1;
		}		

		WriteFile(hRar, &rar_main_hdr, sizeof(rar_main_hdr), &dw, NULL);
		archive_putcurtime(0, &f_time, &f_date);
	    
		__asm {		
			movzx   eax, f_date
			shl     eax, 16
			or      ax, f_time
			mov		dword ptr [rar_hdr.ftime], eax		
		}

	}
	else {		
		hRar = CreateFile(rar_file, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		RarfSize = GetFileSize(hRar, NULL);	
		if(RarfSize == 0xFFFFFFFF) {
			CloseHandle(hfile);
			CloseHandle(hRar);
			return 1;
		}
		
		SetFilePointer(hRar, RarfSize - sizeof(EndOfRar), NULL, FILE_BEGIN);
		archive_putcurtime(hRar, &f_time, &f_date);
	    
		__asm {		
			movzx   eax, f_date
			shl     eax, 16
			or      ax, f_time
			mov		dword ptr [rar_hdr.ftime], eax		
		}
	}
	
	hMap = CreateFileMapping(hfile, NULL, PAGE_READONLY, 0, 0, 0);
	if(hMap == NULL) {
		CloseHandle(hfile);
		CloseHandle(hRar);
		return 1;
	}

	MapBase = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (MapBase == NULL) {
		CloseHandle(hMap);
		CloseHandle(hfile);
		CloseHandle(hRar);
		return 1;
	}
	
	rar_hdr.head_type = 0x74;
	rar_hdr.file_crc = crc32(MapBase, fSize);
	rar_hdr.head_flags = 0x8000;
	rar_hdr.method = 0x30;
	rar_hdr.unp_version = 0x14;
	rar_hdr.attr = 0;	
	rar_hdr.unp_size = fSize;
	rar_hdr.pack_size = rar_hdr.unp_size;
	rar_hdr.name_size = lstrlen(packed_name);
	rar_hdr.head_size = (sizeof(rar_hdr) + rar_hdr.name_size);
	memset(buf, 0, sizeof(buf));
	memcpy(buf, &rar_hdr, sizeof(rar_hdr));
	p = buf + sizeof(rar_hdr);	
	memcpy(p, packed_name, rar_hdr.name_size);
	crc32(buf+2, sizeof(rar_hdr) + rar_hdr.name_size - 2);	//+2 = skip the HEAD_CRC field
	
	__asm {
		mov word ptr [rar_hdr.head_crc], ax
	}
	
	WriteFile(hRar, &rar_hdr, sizeof(rar_hdr), &dw, NULL);
	WriteFile(hRar, packed_name, rar_hdr.name_size, &dw, NULL);
	WriteFile(hRar, MapBase, fSize, &dw, NULL);
	WriteFile(hRar, EndOfRar, sizeof(EndOfRar), &dw, NULL);

	UnmapViewOfFile(MapBase);
	CloseHandle(hMap);
	CloseHandle(hfile);
	CloseHandle(hRar);
	return 0;
}

void main()
{
	crc32_table(crc32_tab);
	// create a new rar file
	add_to_rar("C:\\file.rar", "C:\\buff.txt", "buff.txt", 1);

	// add file to exisiting rar file
	add_to_rar("C:\\file.rar", "C:\\buff.txt", "buff.txt", 0);
}
