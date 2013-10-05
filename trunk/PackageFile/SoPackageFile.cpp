//-----------------------------------------------------------------------------
// SoPackageFile
// (C) oil
// 2013-07-13
//
// 1，与文件长度大小相关的变量都使用64位整数，支持无限大的资源包文件。
// 2，最大限度满足跨平台需求。目前，CRITICAL_SECTION不是跨平台的。
// 3，用户自己定义版本号规则。
// 4，SingleFile的文件名最好全部是ASCII字符，最好不出现中文等等。
// 5，在Mode_Read模式下支持多线程。
// 6，尚未对资源包内的SingleFile做压缩处理，也没有任何加密。
// 7，SingleFile的文件名长度不能超过SoPackageFileMAX_PATH。
// 8，在Mode_Read模式下，为了快速定位要读取的文件，使用了哈希操作。魔兽世界的MPQ文件就是这么干的。
//-----------------------------------------------------------------------------
#include "SoPackageFile.h"
#include "SoHash.h"
//-----------------------------------------------------------------------------
namespace GGUI
{
	//-----------------------------------------------------------------------------
	SoPackageFile::SoPackageFile()
	:m_theFileMode(Mode_None)
	,m_pFile(0)
	,m_pSingleFileInfoList(0)
	,m_nSingleFileInfoListCapacity(0)
	,m_nSingleFileInfoListSize(0)
	,m_pHashList(0)
	{
		InitializeCriticalSection(&m_Lock);
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::~SoPackageFile()
	{
		ReleasePackageFile();
		DeleteCriticalSection(&m_Lock);
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::InitPackageFile(const char* pszPackageFile, FileMode theFileMode)
	{
		if (pszPackageFile == 0 //空指针
			|| pszPackageFile[0] == 0 //空字符串
			|| theFileMode == Mode_None) //无效的文件操作模式
		{
			return Result_InvalidParam;
		}
		if (m_pFile || m_theFileMode != Mode_None)
		{
			//资源包已经打开，必须先关闭才能打开下一个资源包。
			return Result_PackageFileAlreadyOpen;
		}
		FILE* pFile = 0;
		//记录是否需要解析资源包，即提取资源包已有的文件结构信息。
		bool bParsePackageFile = false;
		if (theFileMode == Mode_Read)
		{
			//读模式，资源包文件必须存在。
			pFile = fopen(pszPackageFile, "rb");
			if (pFile == 0)
			{
				//打开失败。
				return Result_OpenFileFail;
			}
			else
			{
				//资源包存在，需要解析资源包。
				bParsePackageFile = true;
			}
		}
		else if (theFileMode == Mode_Write)
		{
			//写模式，如果资源包不存在则创建。
			pFile = fopen(pszPackageFile, "r+b");
			if (pFile == 0)
			{
				//资源包不存在，则创建。
				pFile = fopen(pszPackageFile, "w+b");
				if (pFile == 0)
				{
					return Result_CreateFileFail;
				}
				else
				{
					bParsePackageFile = false;
				}
			}
			else
			{
				//资源包存在，需要解析资源包。
				bParsePackageFile = true;
			}
		}
		m_theFileMode = theFileMode;
		m_pFile = pFile;
		if (bParsePackageFile)
		{
			//解析资源包。
			OperationResult eResult = ParsePackageFile();
			if (eResult != Result_OK)
			{
				ReleasePackageFile();
				return eResult;
			}
		}
		else
		{
			//不需要解析资源包。
			//预先创建某些资源。
			ReCreateSingleFileInfoList(10);
			//预先写入文件头。
			WritePackageHead();
		}
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::ReleasePackageFile()
	{
		m_theFileMode = Mode_None;
		if (m_pFile)
		{
			fclose(m_pFile);
			m_pFile = 0;
		}
		m_stPackageHead.Clear();
		ReleaseSingleFileInfoList();
		if (m_pHashList)
		{
			free(m_pHashList);
			m_pHashList = 0;
		}
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::Open(const char* pszFileName, stReadSingleFile& theFile)
	{
		if (pszFileName == 0 || pszFileName[0] == 0)
		{
			//空指针或者空字符串。
			return Result_InvalidParam;
		}
		if (m_theFileMode != Mode_Read)
		{
			return Result_FileModeMismatch;
		}
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		//格式化文件名。
		char szFormatFileName[SoPackageFileMAX_PATH];
		FormatFileFullName(szFormatFileName, pszFileName);
		EnterCriticalSection(&m_Lock);
		//从m_pSingleFileInfoList中找到索引位置。
		soint64 theIndex_SingleFileInfoList = GetIndex_SingleFileInfoList(szFormatFileName);
		if (theIndex_SingleFileInfoList == -1)
		{
			//文件不存在。
			LeaveCriticalSection(&m_Lock);
			return Result_SingleFileNotExist;
		}
		theFile.nFileID = theIndex_SingleFileInfoList;
		theFile.nFileSize = m_pSingleFileInfoList[theFile.nFileID].nOriginalFileSize;
		LeaveCriticalSection(&m_Lock);
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::Close(stReadSingleFile& theFile)
	{
		theFile.Clear();
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::Read(void* pBuff, soint64 nElementSize, soint64 nElementCount, soint64& nActuallyReadCount, stReadSingleFile& theFile)
	{
		nActuallyReadCount = 0;
		if (pBuff == 0)
		{
			return Result_InvalidParam;
		}
		if (theFile.nFileID < 0)
		{
			return Result_InvalidFileID;
		}
		//把nElementCount修正一下。
		if (nElementCount * nElementSize > theFile.nFileSize - theFile.nFilePointer)
		{
			nElementCount = (theFile.nFileSize - theFile.nFilePointer) / nElementSize;
		}
		EnterCriticalSection(&m_Lock);
		if (m_theFileMode != Mode_Read)
		{
			LeaveCriticalSection(&m_Lock);
			return Result_FileModeMismatch;
		}
		if (m_pFile == 0)
		{
			LeaveCriticalSection(&m_Lock);
			return Result_PackageFileHaveNotOpen;
		}
		if (theFile.nFileID >= m_nSingleFileInfoListSize)
		{
			LeaveCriticalSection(&m_Lock);
			return Result_InvalidFileID;
		}
		const stSingleFileInfo& theFileInfo = m_pSingleFileInfoList[theFile.nFileID];
		soint64 nSeekResult = _fseeki64(m_pFile, theFileInfo.nOffset+theFile.nFilePointer, SEEK_SET);
		if (nSeekResult != 0)
		{
			LeaveCriticalSection(&m_Lock);
			return Result_FileOperationError;
		}
		nActuallyReadCount = fread(pBuff, (size_t)nElementSize, (size_t)nElementCount, m_pFile);
		LeaveCriticalSection(&m_Lock);
		//读取完毕。
		theFile.nFilePointer += nElementSize * nActuallyReadCount;
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::Tell(soint64& nFilePos, stReadSingleFile& theFile)
	{
		nFilePos = theFile.nFilePointer;
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::Seek(soint64 nOffset, SeekOrigin theOrigin, stReadSingleFile& theFile)
	{
		soint64 destPos = 0;
		switch (theOrigin)
		{
		case Seek_Set:
			destPos = nOffset;
			break;
		case Seek_Cur:
			destPos = theFile.nFilePointer + nOffset;
			break;
		case Seek_End:
			destPos = theFile.nFileSize + nOffset;
			break;
		default:
			break;
		}
		if (destPos < 0)
		{
			destPos = 0;
		}
		else if (destPos > theFile.nFileSize)
		{
			destPos = theFile.nFileSize;
		}
		theFile.nFilePointer = destPos;
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::InsertSingleFile(const char* pszDiskFile)
	{
		if (pszDiskFile == 0 || pszDiskFile[0] == 0)
		{
			//无效指针或者是空字符串。
			return Result_InvalidParam;
		}
		soint64 nDiskFileNameLength = strlen(pszDiskFile);
		if (nDiskFileNameLength >= SoPackageFileMAX_PATH)
		{
			return Result_FileNameLengthTooLong;
		}
		if (m_theFileMode != Mode_Write)
		{
			return Result_FileModeMismatch;
		}
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		//
		stSingleFileInfo newSingleFile;
		FormatFileFullName(newSingleFile.szFileName, pszDiskFile);
		newSingleFile.uiHashA = SoHash_Index(newSingleFile.szFileName);
		newSingleFile.uiHashB = SoHash_PHP(newSingleFile.szFileName);
		newSingleFile.uiHashC = SoHash_BKDR(newSingleFile.szFileName);
		//判断该文件是否已经存在了。
		bool bAlreadyExist = false;
		for (soint64 i=0; i<m_nSingleFileInfoListSize; ++i)
		{
			if (newSingleFile.uiHashA == m_pSingleFileInfoList[i].uiHashA
				&& newSingleFile.uiHashB == m_pSingleFileInfoList[i].uiHashB
				&& newSingleFile.uiHashC == m_pSingleFileInfoList[i].uiHashC)
			{
				bAlreadyExist = true;
				break;
			}
		}
		if (bAlreadyExist)
		{
			return Result_SingleFileAlreadyExist;
		}
		//打开磁盘文件。
		FILE* pSingleFile = fopen(newSingleFile.szFileName, "rb");
		if (pSingleFile == 0)
		{
			return Result_OpenFileFail;
		}
		//获取磁盘文件大小。
		_fseeki64(pSingleFile, 0, SEEK_END);
		soint64 nDiskFileSize = _ftelli64(pSingleFile);
		newSingleFile.nOriginalFileSize = nDiskFileSize;
		newSingleFile.nOffset = m_stPackageHead.nOffsetForFirstSingleFileInfo;
		//向资源包中写入这个文件。
		OperationResult writeResult = WriteSingleFile(pSingleFile, newSingleFile);
		fclose(pSingleFile);
		pSingleFile = 0;
		if (writeResult != Result_OK)
		{
			//写入失败。
			return writeResult;
		}
		//分配结构体对象，并填充参数。
		soint64 nFileID = AssignSingleFileInfo();
		if (nFileID == -1)
		{
			return Result_MemoryIsEmpty;
		}
		memcpy(&(m_pSingleFileInfoList[nFileID]), &newSingleFile, sizeof(stSingleFileInfo));
		//完善文件头信息。
		++m_stPackageHead.nFileCount;
		m_stPackageHead.nOffsetForFirstSingleFileInfo += m_pSingleFileInfoList[nFileID].nEmbededFileSize;
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::FlushPackageFile()
	{
		if (m_theFileMode != Mode_Write)
		{
			return Result_FileModeMismatch;
		}
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		OperationResult theResult = WritePackageHead();
		if (theResult == Result_OK)
		{
			theResult = WriteAllSingleFileInfo();
			if (theResult == Result_OK)
			{
				fflush(m_pFile);
			}
		}
		return theResult;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::WritePackageHead()
	{
		//判断当前文件状态。
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		soint64 nSeekResult = _fseeki64(m_pFile, 0, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		//生成合法的文件头。
		const char* pszFileFlag = SoPackageFileFlag;
		for (soint64 i=0; i<SoPackageFileFlagLength; ++i)
		{
			m_stPackageHead.szFileFlag[i] = pszFileFlag[i];
		}
		m_stPackageHead.nVersion = SoPackageFileVersion;
		//写入。
		const size_t sizePackageHead = sizeof(m_stPackageHead);
		size_t nActuallyWrite = fwrite(&m_stPackageHead, 1, sizePackageHead, m_pFile);
		if (nActuallyWrite != sizePackageHead)
		{
			//文件头没有写入完整。
			return Result_FileOperationError;
		}
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::WriteSingleFile(FILE* pSingleFile, SoPackageFile::stSingleFileInfo& theFileInfo)
	{
		if (pSingleFile == 0)
		{
			return Result_InvalidParam;
		}
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		//调整文件指针位置，准备写入。
		soint64 nSeekResult = _fseeki64(pSingleFile, 0, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		nSeekResult = _fseeki64(m_pFile, theFileInfo.nOffset, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		//读取源文件。
		char* pBuff = (char*)malloc((size_t)(theFileInfo.nOriginalFileSize));
		const size_t sizeOriginalFileSize = (size_t)(theFileInfo.nOriginalFileSize);
		size_t nActuallyRead = fread(pBuff, 1, sizeOriginalFileSize, pSingleFile);
		if (nActuallyRead != sizeOriginalFileSize)
		{
			free(pBuff);
			return Result_FileOperationError;
		}
		if (feof(pSingleFile) != 0)
		{
			//越界了，这不应该发生。
			//这时，文件指针应该在文件末尾，再读一个字节才会越界。
			free(pBuff);
			return Result_FileOperationError;
		}
		//写入到资源包。
		size_t nActuallyWrite = fwrite(pBuff, 1, sizeOriginalFileSize, m_pFile);
		if (nActuallyWrite != sizeOriginalFileSize)
		{
			free(pBuff);
			return Result_FileOperationError;
		}
		free(pBuff);
		//完善参数。
		//没有进行压缩操作。
		theFileInfo.nEmbededFileSize = theFileInfo.nOriginalFileSize;
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::WriteAllSingleFileInfo()
	{
		//判断当前文件状态。
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		soint64 nSeekResult = _fseeki64(m_pFile, m_stPackageHead.nOffsetForFirstSingleFileInfo, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		//写入。
		const size_t sizeAllSingleFileInfo = ((size_t)m_nSingleFileInfoListSize) * sizeof(stSingleFileInfo);
		size_t nActuallyWrite = fwrite(m_pSingleFileInfoList, 1, sizeAllSingleFileInfo, m_pFile);
		if (nActuallyWrite != sizeAllSingleFileInfo)
		{
			//SingleFile信息列表没有写入完整。
			return Result_FileOperationError;
		}
		return Result_OK;
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::ParsePackageFile()
	{
		if (m_pFile == 0)
		{
			return Result_PackageFileHaveNotOpen;
		}
		soint64 nSeekResult = _fseeki64(m_pFile, 0, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		//读取文件头。
		const size_t sizePackageHead = sizeof(m_stPackageHead);
		size_t nActuallyReadSize = fread(&m_stPackageHead, 1, sizePackageHead, m_pFile);
		if (nActuallyReadSize != sizePackageHead)
		{
			//文件头没有读取完整。
			return Result_FileOperationError;
		}
		//判断文件头。
		if (!CheckValid_PackageHead(m_stPackageHead))
		{
			return Result_IsNotPackageFile;
		}
		//获取SingleFile信息列表。
		nSeekResult = _fseeki64(m_pFile, m_stPackageHead.nOffsetForFirstSingleFileInfo, SEEK_SET);
		if (nSeekResult != 0)
		{
			return Result_FileOperationError;
		}
		ReleaseSingleFileInfoList();
		ReCreateSingleFileInfoList(m_stPackageHead.nFileCount);
		if (m_pSingleFileInfoList == 0)
		{
			return Result_MemoryIsEmpty;
		}
		const size_t sizeSingleFileInfoList = ((size_t)(m_stPackageHead.nFileCount)) * sizeof(stSingleFileInfo);
		size_t nActuallyReadInfoListSize = fread(m_pSingleFileInfoList, 1, sizeSingleFileInfoList, m_pFile);
		if (nActuallyReadInfoListSize != sizeSingleFileInfoList)
		{
			//SingleFile信息列表没有读取完整。
			return Result_FileOperationError;
		}
		//SingleFile信息列表读取成功。
		m_nSingleFileInfoListSize = m_stPackageHead.nFileCount;
		//如果是Mode_Read模式，则生成m_pHashList，帮助快速定位目标文件。
		if (m_theFileMode == Mode_Read)
		{
			return BuildHashList();
		}
		else
		{
			return Result_OK;
		}
	}
	//-----------------------------------------------------------------------------
	SoPackageFile::OperationResult SoPackageFile::BuildHashList()
	{
		OperationResult theResult = Result_OK;
		if (m_pHashList)
		{
			free(m_pHashList);
		}
		const souint32 uiCount = (souint32)m_nSingleFileInfoListSize;
		const size_t theSize = (size_t)(uiCount * sizeof(stHashInfo));
		m_pHashList = (stHashInfo*)malloc(theSize);
		memset(m_pHashList, 0, theSize);
		//
		for (souint32 i=0; i<uiCount; ++i)
		{
			souint32 uiIndex = m_pSingleFileInfoList[i].uiHashA % uiCount;
			//根据哈希值的计算，uiIndex是应该放置的位置，但是这个位置上可能已经有值了，
			//则顺延找到一个空的位置。
			bool bFindEmpty = false;
			for (souint32 j=0; j<uiCount; ++j)
			{
				souint32 uiCheckIndex = uiIndex + j;
				if (uiCheckIndex >= uiCount)
				{
					uiCheckIndex = uiCheckIndex - uiCount;
				}
				if (m_pHashList[uiCheckIndex].uiHashA == 0
					&& m_pHashList[uiCheckIndex].uiHashB == 0
					&& m_pHashList[uiCheckIndex].uiHashC == 0)
				{
					//uiCheckIndex位置的元素为空。
					uiIndex = uiCheckIndex;
					bFindEmpty = true;
					break;
				}
			}
			if (bFindEmpty)
			{
				m_pHashList[uiIndex].uiIndex_SingleFileInfoList = i;
				m_pHashList[uiIndex].uiHashA = m_pSingleFileInfoList[i].uiHashA;
				m_pHashList[uiIndex].uiHashB = m_pSingleFileInfoList[i].uiHashB;
				m_pHashList[uiIndex].uiHashC = m_pSingleFileInfoList[i].uiHashC;
			}
			else
			{
				theResult = Result_BuildHashListFail;
				break;
			}
		}
		return theResult;
	}
	//-----------------------------------------------------------------------------
	void SoPackageFile::ReCreateSingleFileInfoList(soint64 nCapacity)
	{
		stSingleFileInfo* pSingleFileInfoList_Temp = m_pSingleFileInfoList;
		//
		size_t sizeCapacity = (size_t)nCapacity;
		m_pSingleFileInfoList = (stSingleFileInfo*)malloc(sizeCapacity * sizeof(stSingleFileInfo));
		if (m_pSingleFileInfoList)
		{
			//清零
			memset(m_pSingleFileInfoList, 0, sizeCapacity * sizeof(stSingleFileInfo));
			//拷贝已有的值
			if (m_nSingleFileInfoListSize > 0)
			{
				size_t sizeCount_SingleFileInfo = (size_t)m_nSingleFileInfoListSize;
				memcpy(m_pSingleFileInfoList, pSingleFileInfoList_Temp, sizeCount_SingleFileInfo*sizeof(stSingleFileInfo));
			}
			m_nSingleFileInfoListCapacity = nCapacity;
		}
		else
		{
			//申请内存失败。
			m_pSingleFileInfoList = 0;
			m_nSingleFileInfoListCapacity = 0;
			m_nSingleFileInfoListSize = 0;
		}
		//
		if (pSingleFileInfoList_Temp)
		{
			free(pSingleFileInfoList_Temp);
			pSingleFileInfoList_Temp = 0;
		}
	}
	//-----------------------------------------------------------------------------
	void SoPackageFile::ReleaseSingleFileInfoList()
	{
		m_nSingleFileInfoListCapacity = 0;
		m_nSingleFileInfoListSize = 0;
		if (m_pSingleFileInfoList)
		{
			free(m_pSingleFileInfoList);
			m_pSingleFileInfoList = 0;
		}
	}
	//-----------------------------------------------------------------------------
	soint64 SoPackageFile::AssignSingleFileInfo()
	{
		soint64 nResult = -1;
		//
		if (m_nSingleFileInfoListSize >= m_nSingleFileInfoListCapacity)
		{
			ReCreateSingleFileInfoList(m_nSingleFileInfoListCapacity*2);
		}
		if (m_nSingleFileInfoListSize < m_nSingleFileInfoListCapacity)
		{
			nResult = m_nSingleFileInfoListSize;
			++m_nSingleFileInfoListSize;
		}
		//
		return nResult;
	}
	//-----------------------------------------------------------------------------
	soint64 SoPackageFile::GetIndex_SingleFileInfoList(const char* pszFileName)
	{
		soint64 theIndex = -1;
		const souint32 uiHashA = SoHash_Index(pszFileName);
		const souint32 uiHashB = SoHash_PHP(pszFileName);
		const souint32 uiHashC = SoHash_BKDR(pszFileName);
		const souint32 uiCount = (souint32)m_nSingleFileInfoListSize;
		souint32 uiIndex = uiHashA % uiCount;
		//
		for (souint32 j=0; j<uiCount; ++j)
		{
			souint32 uiCheckIndex = uiIndex + j;
			if (uiCheckIndex >= uiCount)
			{
				uiCheckIndex = uiCheckIndex - uiCount;
			}
			if (m_pHashList[uiCheckIndex].uiHashA == uiHashA
				&& m_pHashList[uiCheckIndex].uiHashB == uiHashB
				&& m_pHashList[uiCheckIndex].uiHashC == uiHashC)
			{
				theIndex = m_pHashList[uiCheckIndex].uiIndex_SingleFileInfoList;
				break;
			}
		}
		return theIndex;
	}
	//-----------------------------------------------------------------------------
	void SoPackageFile::FormatFileFullName(char* pszOut, const char* pszIn) const
	{
		soint64 nCount = 0;
		while (pszIn[nCount] != 0)
		{
			char theC = pszIn[nCount];
			if (theC >= 'A' && theC <= 'Z')
			{
				//大写字母转换为小写字母
				theC += 32;
			}
			else if (theC == '\\')
			{
				theC = '/';
			}
			pszOut[nCount] = theC;
			//
			++nCount;
			if (nCount >= SoPackageFileMAX_PATH)
			{
				pszOut[SoPackageFileMAX_PATH-1] = 0;
				break;
			}
		}
		if (nCount < SoPackageFileMAX_PATH)
		{
			pszOut[nCount] = 0;
		}
	}
	//-----------------------------------------------------------------------------
	bool SoPackageFile::CheckValid_PackageHead(const SoPackageFile::stPackageHead& theHead)
	{
		bool br = true;
		//判断文件标志
		if (br)
		{
			const char* pszFileFlag = SoPackageFileFlag;
			for (soint64 i=0; i<SoPackageFileFlagLength; ++i)
			{
				if (theHead.szFileFlag[i] != pszFileFlag[i])
				{
					br = false;
					break;
				}
			}
		}
		//判断版本号
		if (br)
		{
			if (theHead.nVersion != SoPackageFileVersion)
			{
				br = false;
			}
		}
		//判断文件个数
		if (br)
		{
			if (theHead.nFileCount < 0)
			{
				br = false;
			}
		}
		//判断偏移量
		if (br)
		{
			if (theHead.nOffsetForFirstSingleFileInfo < 0)
			{
				br = false;
			}
			else
			{
				if (theHead.nFileCount > 0 && theHead.nOffsetForFirstSingleFileInfo <= sizeof(theHead))
				{
					br = false;
				}
			}
		}
		return br;
	}
	//-----------------------------------------------------------------------------
	bool SoPackageFile::CheckValid_SingleFileInfo(const SoPackageFile::stSingleFileInfo& theSingleFile)
	{
		bool br = true;
		//判断文件名
		if (br)
		{
			if (theSingleFile.szFileName[0] == 0 //文件名为空
				|| theSingleFile.szFileName[SoPackageFileMAX_PATH-1] != 0) //不是以0结尾的字符串
			{
				br = false;
			}
		}
		//
		if (br)
		{
			if (theSingleFile.nOriginalFileSize <= 0)
			{
				br = false;
			}
		}
		//
		if (br)
		{
			if (theSingleFile.nEmbededFileSize <= 0)
			{
				br = false;
			}
		}
		//
		if (br)
		{
			if (theSingleFile.nOffset < sizeof(stPackageHead))
			{
				br = false;
			}
		}
		return br;
	}
}
//-----------------------------------------------------------------------------
