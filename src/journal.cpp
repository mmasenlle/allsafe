#include <Windows.h>
#include <WinIoCtl.h>
#include <stdio.h>
#include <tchar.h>

#include <list>
#include <string>
#include <map>
#include <set>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include "fentry.h"

extern bool stop;
typedef std::map<DWORDLONG,std::string> file_refs_t;
static file_refs_t folders_in;
static file_refs_t folders_md5;
static file_refs_t files_in;
static std::set<DWORDLONG> folders_out;
static std::set<DWORDLONG> files_out;

static std::list<boost::shared_ptr<fentry_t> > changed_files;
static std::set<std::string> changed_files_set;
static boost::mutex mtx_changed_files;

#define BUF_LEN 4096

HANDLE hVol;
static void get_more_file_info(DWORDLONG fr)
{
#if 0
   DWORD dwBytes;
   DWORD dwRetBytes;
   PUSN_RECORD UsnRecord;
   int j=0;
   CHAR Buffer[BUF_LEN];
   MFT_ENUM_DATA EnumData = {fr,0,0xFFFFFFFFFF};
   EnumData.StartFileReferenceNumber = fr;
   memset( Buffer, 0, BUF_LEN );
//   printf( "get_more_file_info(%I64d)----------->\n", fr);
   if( !DeviceIoControl( hVol,
            FSCTL_ENUM_USN_DATA,
            &EnumData,
            sizeof(EnumData),
            &Buffer,
            BUF_LEN,
            &dwBytes,
            NULL) )
      {
         printf( "get_more_file_info(): Read journal failed (%d)\n", GetLastError());
         return;
      }
     dwRetBytes = dwBytes - sizeof(USN);
	if( dwRetBytes < 10 ) {
		printf( "\n\ndwRetBytes: %d\n", dwRetBytes);
		return;
	}

      // Find the first record
      UsnRecord = (PUSN_RECORD)(((PUCHAR)Buffer) + sizeof(USN));

 //     printf( "****************************************\n");

      // This loop could go on for a long time, given the current buffer size.
      while( dwRetBytes > 0 )
      {
		  if (UsnRecord->FileReferenceNumber == fr){
//         printf( "Parent USN: %I64x\n", UsnRecord->Usn );
//		 printf( "Tstamp: %I64d\n", UsnRecord->TimeStamp );
//		 printf( "FileReferenceNumber: %I64d\n", UsnRecord->FileReferenceNumber );
//		 printf( "ParentFileReferenceNumber: %I64d\n", UsnRecord->ParentFileReferenceNumber );
//         printf("Parent name: %.*S\n",
			// printf(" / %.*S",
                  // UsnRecord->FileNameLength/2,
                  // UsnRecord->FileName );
//         printf( "Reason: %x\n", UsnRecord->Reason );
//			printf( "\n");
			if (UsnRecord->ParentFileReferenceNumber)
				get_more_file_info(UsnRecord->ParentFileReferenceNumber);
			printf("/%.*S",
                  UsnRecord->FileNameLength/2,
                  UsnRecord->FileName );
			return;
		  }
         dwRetBytes -= UsnRecord->RecordLength;

         // Find the next record
         UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) +
                  UsnRecord->RecordLength);
		j++;
      }
 //    printf( "get_more_file_info(%I64x):%d<<<<<<<\n", fr,j);
 #endif
}


//std::string journal_get_next()
boost::shared_ptr<fentry_t> journal_get_next()
{
    boost::shared_ptr<fentry_t> s;
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    if (!changed_files.empty()) {
        s = changed_files.front();
        changed_files_set.erase(s->fname);
        changed_files.pop_front();
    }
    return s;
}

std::string journal_to_string()
{
    std::string s; char buf[1024];
    int n = sprintf(buf, "<br/>Folders out: %d", folders_out.size()); s.append(buf, n);
    n = sprintf(buf, "<br/>Files out: %d", files_out.size()); s.append(buf, n);
    n = sprintf(buf, "<h3>Folders in (%d):</h3>", folders_in.size()); s.append(buf, n);
    BOOST_FOREACH(const file_refs_t::value_type &fs, folders_in) {
        n = sprintf(buf, "%lld->", fs.first);
        s.append(buf, n); s += fs.second; s+= "<br/>\n";
    }
    n = sprintf(buf, "<h3>Files in (%d):</h3>", files_in.size()); s.append(buf, n);
    BOOST_FOREACH(const file_refs_t::value_type &fs, files_in) {
        n = sprintf(buf, "%lld->", fs.first);
        s.append(buf, n); s += fs.second; s+= "<br/>\n";
    }
    return s;
}
extern std::string md5(const std::string &str);
extern int persistence_insert0(const std::string &fpath, int tt, int cnt);
static void journal_main()
{
   CHAR Buffer[BUF_LEN];
#if 0
   USN_JOURNAL_DATA JournalData;
   READ_USN_JOURNAL_DATA ReadData = {0, 0xFFFFFFFF, FALSE, 0, 0};
   PUSN_RECORD UsnRecord;

   DWORD dwBytes;
   DWORD dwRetBytes;
   int I,j=0;
   USN usn;

   hVol = CreateFile( TEXT("\\\\.\\c:"),
               GENERIC_READ | GENERIC_WRITE,
               FILE_SHARE_READ | FILE_SHARE_WRITE,
               NULL,
               OPEN_EXISTING,
               0,
               NULL);

   if( hVol == INVALID_HANDLE_VALUE )
   {
      printf("CreateFile failed (%d)\n", GetLastError());
      return;
   }

   if( !DeviceIoControl( hVol,
          FSCTL_QUERY_USN_JOURNAL,
          NULL,
          0,
          &JournalData,
          sizeof(JournalData),
          &dwBytes,
          NULL) )
   {
      printf( "Query journal failed (%d)\n", GetLastError());
      return;
   }

   ReadData.UsnJournalID = JournalData.UsnJournalID;

//   ReadData.StartUsn = JournalData.NextUsn & ~0x0ffff;
   ReadData.StartUsn = JournalData.NextUsn & ~0x0fff;
   if (ReadData.StartUsn > JournalData.NextUsn) ReadData.StartUsn = 0;
   usn = ReadData.StartUsn;

   printf( "Journal ID: %I64x\n", JournalData.UsnJournalID );
   printf( "FirstUsn: %I64x\n", JournalData.FirstUsn );
   printf( "NextUsn: %I64x\n", JournalData.NextUsn );
   printf( "LowestValidUsn: %I64x\n", JournalData.LowestValidUsn );
   printf( "MaxUsn: %I64x\n", JournalData.MaxUsn );
   printf( "MaximumSize: %I64x\n\n", JournalData.MaximumSize );
   printf( "StartUsn: %I64x\n\n", usn );

//   for(I=0; I<1; I++)
	for (;;)
   {
      memset( Buffer, 0, BUF_LEN );

      if( !DeviceIoControl( hVol,
            FSCTL_READ_USN_JOURNAL,
            &ReadData,
            sizeof(ReadData),
            &Buffer,
            BUF_LEN,
            &dwBytes,
            NULL) )
      {
         printf( "Read journal failed (%d)\n", GetLastError());
         break;
      }

      dwRetBytes = dwBytes - sizeof(USN);
	if( dwRetBytes < 10 ) {
//		printf( "\n\n(%d) dwRetBytes: %d\n", j, dwRetBytes);
//		if (getchar() != 'q') continue;
		if (stop) break;
		Sleep(1000);
		continue;
	}

      // Find the first record
      UsnRecord = (PUSN_RECORD)(((PUCHAR)Buffer) + sizeof(USN));

 //     printf( "****************************************\n");

      // This loop could go on for a long time, given the current buffer size.
      while( dwRetBytes > 0 )
      {
          if (folders_out.find(UsnRecord->ParentFileReferenceNumber) == folders_out.end()) {
          if (files_out.find(UsnRecord->FileReferenceNumber) == files_out.end()) {
         printf( "USN: %I64x    (%d)\n", UsnRecord->Usn, j);
		 printf( "Tstamp: %I64d\n", UsnRecord->TimeStamp );
		 printf( "Reason: %x\n", UsnRecord->Reason );
		 printf( "FileReferenceNumber: %I64d\n", UsnRecord->FileReferenceNumber );
		 printf( "ParentFileReferenceNumber: %I64d\n", UsnRecord->ParentFileReferenceNumber );
		get_more_file_info(UsnRecord->ParentFileReferenceNumber);
         printf("/%.*S\n\n",
                  UsnRecord->FileNameLength/2,
                  UsnRecord->FileName );
        if (files_in.find(UsnRecord->FileReferenceNumber) == files_in.end()) {
        if (folders_in.find(UsnRecord->ParentFileReferenceNumber) != folders_in.end()) {
            std::string s;
            for (int i = 0; i < UsnRecord->FileNameLength/2; i++)
                s.push_back(UsnRecord->FileName[i]);
//            s.assign(UsnRecord->FileName, UsnRecord->FileNameLength);
            files_in[UsnRecord->FileReferenceNumber] =
                folders_in[UsnRecord->ParentFileReferenceNumber] + s;
//            printf("File to string: %s\n", files_in[UsnRecord->FileReferenceNumber].c_str());
        }
        }
         if (files_in.find(UsnRecord->FileReferenceNumber) != files_in.end()) {
            if (UsnRecord->Reason & 0x80000000) { //closes file
             int tt = (*((long long*)&(UsnRecord->TimeStamp))/10000000) - 11644473600;
//printf("NTFS to posix time: %d\n", tt);
persistence_insert0(files_in[UsnRecord->FileReferenceNumber], tt, 1);
            boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
            if (changed_files_set.find(files_in[UsnRecord->FileReferenceNumber]) == changed_files_set.end()) {
                changed_files_set.insert(files_in[UsnRecord->FileReferenceNumber]);
//                changed_files.push_back(files_in[UsnRecord->FileReferenceNumber]);
                boost::shared_ptr<fentry_t> fentry(new fentry_t);
                fentry->fname = files_in[UsnRecord->FileReferenceNumber];
                fentry->initime = (*((long long*)&(UsnRecord->TimeStamp))/10000) - 11644473600000L;
                fentry->sname = folders_md5[UsnRecord->ParentFileReferenceNumber] +
                                    "@" + md5(fentry->fname);
                fentry->event = "MOD";
printf("\n ---  M  D  5  ---\n%s => %s\n\n", fentry->fname.c_str(), fentry->sname.c_str());
                changed_files.push_back(fentry);
            }
            }
         } else {
            files_out.insert(UsnRecord->FileReferenceNumber);
         }
          }
          }
         dwRetBytes -= UsnRecord->RecordLength;

         // Find the next record
         UsnRecord = (PUSN_RECORD)(((PCHAR)UsnRecord) +
                  UsnRecord->RecordLength);
		j++;
//		get_more_file_info(UsnRecord->FileReferenceNumber);

      }
      // Update starting USN for next call
      ReadData.StartUsn = *(USN *)&Buffer;
   }

   CloseHandle(hVol);
   printf( "****************************************\n");
   printf( "Journal ID: %I64x\n", JournalData.UsnJournalID );
   printf( "FirstUsn: %I64x\n", JournalData.FirstUsn );
   printf( "NextUsn: %I64x\n", JournalData.NextUsn );
   printf( "LowestValidUsn: %I64x\n", JournalData.LowestValidUsn );
   printf( "MaxUsn: %I64x\n", JournalData.MaxUsn );
   printf( "MaximumSize: %I64x\n", JournalData.MaximumSize );
   printf( "StartUsn: %I64x\n", usn );
   printf( "Total: %d\n\n", j );
#endif
}

void journal_init()
{
//    files_in[16607023626066288] = "C:/fluxu_vigila/prueba0.dat";
//    folders_in[1125899907120494] = "C:/fluxu_vigila/";
    folders_in[19140298416596352] = "C:/fluxu_vigila/";
    folders_md5[19140298416596352] = md5("C:/fluxu_vigila");
//   folders_in[13229323905538787] = "C:/fluxu_vigila/test_bck2/";
    folders_out.insert(2533274790410866); //firefox cache entries
    folders_out.insert(6192449487695190); //firefox cache doomed
    folders_out.insert(2251799813744837); //firefox sessionstore
    folders_out.insert(281474976714251); //Temp
    folders_out.insert(72339069014660293); //run (stats.db)
    new boost::thread(journal_main);
}
