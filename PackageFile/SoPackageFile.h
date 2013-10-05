//-----------------------------------------------------------------------------
// SoPackageFile
// (C) oil
// 2013-07-13
//-----------------------------------------------------------------------------
#ifndef _SoPackageFile_h_
#define _SoPackageFile_h_
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string>
#include <map>
#include <Windows.h>
//-----------------------------------------------------------------------------
#define SoPackageFileFlag "SOPACKAG"
#define SoPackageFileFlagLength 8
#define SoPackageFileVersion 1
#define SoPackageFileMAX_PATH 150
//-----------------------------------------------------------------------------
namespace GGUI
{
	class SoPackageFile
	{
	public:
		enum FileMode
		{
			Mode_None,
			Mode_Read,
			Mode_Write,
		};
		enum SeekOrigin
		{
			Seek_Set = 0,
			Seek_Cur = 1,
			Seek_End = 2,
		};
		enum OperationResult
		{
			Result_OK, //顺利完成
			Result_InvalidParam, //存在无效的参数
			Result_PackageFileHaveNotOpen, //资源包尚未打开，不能执行操作。
			Result_PackageFileAlreadyOpen, //资源包已经打开，必须先关闭才能打开下一个资源包。
			Result_IsNotPackageFile, //不是SoPackageFile文件
			Result_OpenFileFail, //打开磁盘文件失败
			Result_CreateFileFail, //创建磁盘文件失败
			Result_FileOperationError, //C标准库中的文件操作函数返回了失败。
			Result_MemoryIsEmpty, //申请内存失败，内存不足。
			Result_FileModeMismatch, //文件模式不匹配，例如Mode_Read模式下，你却想执行Write操作。
			Result_SingleFileAlreadyExist, //文件已经存在。可能是文件名重复了。
			Result_SingleFileNotExist, //文件不存在。
			Result_InvalidFileID, //FileID不正确。
			Result_FileNameLengthTooLong, //文件名太长了。
		};
		//资源包文件头。
		struct stPackageHead
		{
			//文件标志。
			char szFileFlag[SoPackageFileFlagLength];
			//版本号。
			__int64 nVersion;
			//一共有多少个SingleFile。
			__int64 nFileCount;
			//SingleFile信息集合中，第一个stSingleFileInfo距离文件开始处的偏移量。
			__int64 nOffsetForFirstSingleFileInfo;

			stPackageHead()
			{
				Clear();
			}
			void Clear()
			{
				memset(this, 0, sizeof(*this));
				nOffsetForFirstSingleFileInfo = sizeof(*this);
			}
		};
		//SingleFile的信息。
		struct stSingleFileInfo
		{
			//文件名
			char szFileName[SoPackageFileMAX_PATH];
			//文件原始大小
			__int64 nOriginalFileSize;
			//文件在资源包内的大小（有可能经过了压缩）
			__int64 nEmbededFileSize;
			//该文件距离资源包文件开始处的偏移量。
			__int64 nOffset;

			void Clear()
			{
				memset(this, 0, sizeof(*this));
			}
		};
		struct stReadSingleFile
		{
			//资源包内每个文件都有一个文件ID。-1为无效值。
			__int64 nFileID;
			//文件原始大小。
			__int64 nFileSize;
			//文件指针。
			__int64 nFilePointer;
			//存储文件原始的完整内容。
			//如果资源包内对SingleFile做了压缩操作，则pFileBuff存储解压之后的原始文件，
			//才能向外界提供对SingleFile的读操作。
			char* pFileBuff;

			stReadSingleFile():nFileID(-1),nFileSize(0),nFilePointer(0),pFileBuff(0)
			{
			}
			void Clear()
			{
				nFileID = -1;
				nFileSize = 0;
				nFilePointer = 0;
				if (pFileBuff)
				{
					free(pFileBuff);
					pFileBuff = 0;
				}
			}
		};

	public:
		SoPackageFile();
		~SoPackageFile();
		OperationResult InitPackageFile(const char* pszPackageFile, FileMode theFileMode);
		OperationResult ReleasePackageFile();

		//<<<<<<<<<<<<<<<< 从资源包内读取一个文件 <<<<<<<<<<<<<<<<<<<<<<<<<
		OperationResult Open(const char* pszFileName, stReadSingleFile& theFile);
		OperationResult Close(stReadSingleFile& theFile);
		OperationResult Read(void* pBuff, __int64 nElementSize, __int64 nElementCount, __int64& nActuallyReadCount, stReadSingleFile& theFile);
		OperationResult Tell(__int64& nFilePos, stReadSingleFile& theFile);
		OperationResult Seek(__int64 nOffset, SeekOrigin theOrigin, stReadSingleFile& theFile);
		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

		//<<<<<<<<<<<<<<<< 把一个磁盘文件写入资源包 <<<<<<<<<<<<<<<<<<<<<<<
		OperationResult InsertSingleFile(const char* pszDiskFile);
		OperationResult FlushPackageFile();
		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

	private:
		//写入资源包文件头。
		OperationResult WritePackageHead();
		//把一个原始的SingleFile写入到资源包中。
		OperationResult WriteSingleFile(FILE* pSingleFile, stSingleFileInfo& theFileInfo);
		//把stSingleFileInfo信息集合写入到资源包中。
		OperationResult WriteAllSingleFileInfo();
		//解析资源包，即提取资源包已有的文件结构信息。
		OperationResult ParsePackageFile();

		void ReCreateSingleFileInfoList(__int64 nCapacity);
		void ReleaseSingleFileInfoList();
		__int64 AssignSingleFileInfo();

		//把文件名格式化成如下格式：
		//1，把'\\'修改成'/'；
		//2，把大写字母修改成小写字母；
		void FormatFileFullName(char* pszOut, const char* pszIn) const;
		//判断文件头是否合法。合法返回true，不合法返回false。
		bool CheckValid_PackageHead(const stPackageHead& theHead);
		//判断SingleFile信息是否合法。合法返回true，不合法返回false。
		bool CheckValid_SingleFileInfo(const stSingleFileInfo& theSingleFile);

	private:
		typedef std::map<std::string, __int64> mapFileName2FileID;

	private:
		FileMode m_theFileMode;
		FILE* m_pFile;
		//文件头。
		stPackageHead m_stPackageHead;
		//SingleFile信息列表。
		stSingleFileInfo* m_pSingleFileInfoList;
		//m_pSingleFileInfoList中可以容纳多少个stSingleFileInfo对象。
		__int64 m_nSingleFileInfoListCapacity;
		//m_pSingleFileInfoList中有效stSingleFileInfo对象的个数。
		__int64 m_nSingleFileInfoListSize;
		mapFileName2FileID m_mapFileName2FileID;
		//多线程锁。
		CRITICAL_SECTION m_Lock;
	};
}
//-----------------------------------------------------------------------------
#endif //_SoPackageFile_h_
//-----------------------------------------------------------------------------
