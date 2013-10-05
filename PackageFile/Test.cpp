//-----------------------------------------------------------------------------
#include "SoPackageFile.h"
using namespace GGUI;
//-----------------------------------------------------------------------------
void main()
{
	SoPackageFile* pPackage = new SoPackageFile;
	pPackage->InitPackageFile("D:/myPackage.sof", SoPackageFile::Mode_Write);
	pPackage->InsertSingleFile("D:/game.ini");
	pPackage->InsertSingleFile("D:\\!1.png");
	pPackage->InsertSingleFile("D:\\Vidio/《仙三外传·问情篇》宣传动画.exe");
	pPackage->InsertSingleFile("E:\\Games\\WOW\\Screenshots/WoWScrnShot_060113_121540.jpg");
	pPackage->FlushPackageFile();
	pPackage->ReleasePackageFile();
	delete pPackage;

	pPackage = new SoPackageFile;
	pPackage->InitPackageFile("D:/myPackage.sof", SoPackageFile::Mode_Read);
	SoPackageFile::stReadSingleFile theFile;
	pPackage->Open("D:/game.ini", theFile);
	pPackage->Seek(0, SoPackageFile::Seek_End, theFile);
	__int64 nFileSize = 0;
	pPackage->Tell(nFileSize, theFile);
	char* pBuff = (char*)malloc((size_t)nFileSize);
	pPackage->Seek(0, SoPackageFile::Seek_Set, theFile);
	__int64 nActuallyReadCount = 0;
	pPackage->Read(pBuff, 1, nFileSize, nActuallyReadCount, theFile);
	pPackage->Close(theFile);
	FILE* pFile = fopen("D:/DestFile.ddd", "w+b");
	fwrite(pBuff, 1, (size_t)nFileSize, pFile);
	fclose(pFile);
	free(pBuff);
	delete pPackage;
}