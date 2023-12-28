/*
Author: Hodak
Created: 11/29/2023
*/

#define _CRT_SECURE_NO_WARNINGS // enable unsafe functions

#include <filesystem>
#include <Windows.h>
#include <Shobjidl.h>
#include "zlib-1.3/zlib.h"
#include "argh-1.3.2/argh.h"

#define READ_MODE "rb" // read binary
#define WRITE_MODE "wb" // write binary
#define SIGNATURE 0x53544152 // "STAR" in hex
#define BUFFER_SIZE 1024 // 1024 bytes

#pragma pack(push, 1)

struct HeaderX
{
private:
	uint32_t signature;
	bool compressionEnabled;
	uint16_t compressionMethod;
	uint32_t numChunks;

public:
	void SetSignature(uint32_t signature)
	{
		this->signature = signature;
	}
	uint32_t GetSignature()
	{
		return signature;
	}
	void SetCompressionEnabled(bool compressionEnabled)
	{
		this->compressionEnabled = compressionEnabled;
	}
	bool GetCompressionEnabled()
	{
		return compressionEnabled;
	}
	void SetCompressionMethod(uint16_t compressionMethod)
	{
		this->compressionMethod = compressionMethod;
	}
	uint16_t GetCompressionMethod()
	{
		return compressionMethod;
	}
	void SetNumChunks(uint32_t numChunks)
	{
		this->numChunks = numChunks;
	}
	uint32_t GetNumChunks()
	{
		return numChunks;
	}

	HeaderX()
	{
		signature = NULL;
		compressionEnabled = NULL;
		compressionMethod = NULL;
		numChunks = NULL;
	}

	HeaderX(
		uint32_t signature,
		bool compressionEnabled,
		uint16_t compressionMethod,
		uint32_t numChunks)
	{
		this->signature = signature;
		this->compressionEnabled = compressionEnabled;
		this->compressionMethod = compressionMethod;
		this->numChunks = numChunks;
	}
};
struct EntryX
{
private:
	uint32_t crc32;
	uint32_t compressedSize;
	uint32_t uncompressedSize;
	uint16_t fileNameLength;

public:
	void SetCrc32(uint32_t crc32)
	{
		this->crc32 = crc32;
	}
	uint32_t GetCrc32()
	{
		return crc32;
	}
	void SetCompressedSize(uint32_t compressedSize)
	{
		this->compressedSize = compressedSize;
	}
	uint32_t GetCompressedSize()
	{
		return compressedSize;
	}
	void SetUncompressedSize(uint32_t uncompressedSize)
	{
		this->uncompressedSize = uncompressedSize;
	}
	uint32_t GetUncompressedSize()
	{
		return uncompressedSize;
	}
	void SetFileNameLength(uint16_t fileNameLength)
	{
		this->fileNameLength = fileNameLength;
	}
	uint16_t GetFileNameLength()
	{
		return fileNameLength;
	}

	EntryX()
	{
		crc32 = NULL;
		compressedSize = NULL;
		uncompressedSize = NULL;
		fileNameLength = NULL;
	}

	EntryX(
		uint32_t crc32,
		uint32_t compressedSize,
		uint32_t uncompressedSize,
		uint16_t fileNameLength)
	{
		this->crc32 = crc32;
		this->compressedSize = compressedSize;
		this->uncompressedSize = uncompressedSize;
		this->fileNameLength = fileNameLength;
	}
};

#pragma pack(pop)

struct Chunk {
	EntryX entryX;
	std::string fileName;

	Chunk()
	{
		entryX = EntryX();
		fileName = "";
	}
};

#pragma warning(disable:4267) // disable conversion warnings

HWND hwnd;
std::vector<Chunk> chunks;
bool crc32_check = false;
ITaskbarList3* pTaskbarList = nullptr;

// helper functions
bool Init()
{
	if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
		return false;
	if (FAILED(CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList))))
		return false;

	hwnd = GetConsoleWindow();
	pTaskbarList->HrInit();

	return true;
}
void Release()
{
	if (pTaskbarList) pTaskbarList->Release();
	CoUninitialize();
}
bool SetTaskbarProgressState(TBPFLAG state)
{
	if (FAILED(pTaskbarList->SetProgressState(hwnd, state)))
		return false;

	return true;
}
bool SetTaskbarProgressValue(ULONGLONG value, ULONGLONG max)
{
	if (FAILED(pTaskbarList->SetProgressValue(hwnd, value, max)))
		return false;

	return true;
}
bool WriteToFile(const void* buffer, size_t size, std::FILE* file)
{
	if (!file)
		return false;

	fwrite(buffer, 1, size, file);

	if (ferror(file))
		return false;

	return true;
}
bool WriteHeader(HeaderX in, std::FILE* file)
{
	if (!file)
		return false;

	void* data = (void*)&in;

	if (!WriteToFile(data, sizeof(HeaderX), file))
		return false;

	return true;
}
bool WriteEntry(EntryX in, std::FILE* file)
{
	if (!file)
		return false;

	void* data = (void*)&in;

	if (!WriteToFile(data, sizeof(EntryX), file))
		return false;

	return true;
}
void GetFiles(const char* dir, std::vector<Chunk>* files)
{
	for (const auto& index : std::filesystem::directory_iterator(dir))
	{
		if (index.is_directory())
		{
			GetFiles(index.path().string().c_str(), files);
		}
		else
		{
			std::string filename = index.path().string();

			Chunk chunk = Chunk();
			chunk.fileName = filename;
			chunk.entryX.SetFileNameLength(filename.length());
			files->push_back(chunk);
		}
	}
}
bool GetFileSize(size_t* size, std::FILE* file)
{
	if (file)
	{
		std::fseek(file, 0, SEEK_END);
		*size = std::ftell(file);
		std::rewind(file);
	}
	else
	{
		return false;
	}

	return true;
}
bool GetCrc32(uLong* crc, std::FILE* file)
{
	printf("Processing crc32 buffer");

	*crc = crc32(0, Z_NULL, 0);
	uint8_t* buffer = new uint8_t[BUFFER_SIZE];
	while (!feof(file))
	{
		size_t readSize = fread(buffer, 1, BUFFER_SIZE, file);
		if (ferror(file))
			return false;
		*crc = crc32(*crc, buffer, readSize);
	}

	printf("            Done!\n");

	delete[] buffer;
	std::rewind(file);
	return true;
}
bool GetCompressSize(size_t* size, std::FILE* file, int method)
{
	printf("Processing compress size buffer");

	int ret, flush;
	unsigned have;
	z_stream strm;
	ZeroMemory(&strm, sizeof(z_stream));
	uint8_t* in = new uint8_t[BUFFER_SIZE];
	uint8_t* out = new uint8_t[BUFFER_SIZE];

	/* allocate deflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, method);
	if (ret != Z_OK)
		return false;

	/* compress until end of file */
	do {
		strm.avail_in = fread(in, 1, BUFFER_SIZE, file);
		if (ferror(file)) {
			(void)deflateEnd(&strm);
			return false;
		}
		flush = feof(file) ? Z_FINISH : Z_NO_FLUSH;
		strm.next_in = in;

		/* run deflate() on input until output buffer not full, finish
		   compression if all of source has been read in */
		do {
			strm.avail_out = BUFFER_SIZE;
			strm.next_out = out;
			ret = deflate(&strm, flush);    /* no bad return value */
			//assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
			have = BUFFER_SIZE - strm.avail_out;
			/*
			if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
				(void)deflateEnd(&strm);
				return false;
			}
			*/
			*size += have;
		} while (strm.avail_out == 0);
		//assert(strm.avail_in == 0);     /* all input will be used */

		/* done when last data in file processed */
	} while (flush != Z_FINISH);
	//assert(ret == Z_STREAM_END);        /* stream will be complete */

	/* clean up and return */
	(void)deflateEnd(&strm);

	printf("    Done!\n");

	delete[] in;
	delete[] out;
	std::rewind(file);
	return true;
}
bool CompressFile(std::FILE* in_file, std::FILE* out_file, int method)
{
	printf("Processing compress buffer");

	int ret, flush;
	unsigned have;
	z_stream strm;
	ZeroMemory(&strm, sizeof(z_stream));
	uint8_t* in = new uint8_t[BUFFER_SIZE];
	uint8_t* out = new uint8_t[BUFFER_SIZE];

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, method);
	if (ret != Z_OK)
		return false;

	/* compress until end of file */
	do {
		strm.avail_in = fread(in, 1, BUFFER_SIZE, in_file);
		if (ferror(in_file)) {
			(void)deflateEnd(&strm);
			return false;
		}
		flush = feof(in_file) ? Z_FINISH : Z_NO_FLUSH;
		strm.next_in = in;

		/* run deflate() on input until output buffer not full, finish
		   compression if all of source has been read in */
		do {
			strm.avail_out = BUFFER_SIZE;
			strm.next_out = out;
			ret = deflate(&strm, flush);    /* no bad return value */
			have = BUFFER_SIZE - strm.avail_out;
			if (fwrite(out, 1, have, out_file) != have || ferror(out_file)) {
				(void)deflateEnd(&strm);
				return false;
			}
		} while (strm.avail_out == 0);

		/* done when last data in file processed */
	} while (flush != Z_FINISH);

	/* clean up */
	(void)deflateEnd(&strm);

	printf("         Done!\n");

	delete[] in;
	delete[] out;
	std::rewind(in_file);
	return true;
}
bool ReadFromFile(void* buffer, size_t size, std::FILE* file)
{
	if (!file)
		return false;

	fread(buffer, 1, size, file);

	if (ferror(file))
		return false;

	return true;
}
bool ReadHeader(HeaderX* out, std::FILE* file)
{
	size_t size = sizeof(HeaderX);
	uint8_t* data = new uint8_t[size];
	ReadFromFile(data, size, file);
	memcpy(out, data, size);
	delete[] data;
	return true;
}
bool ReadEntry(EntryX* out, std::FILE* file)
{
	size_t size = sizeof(EntryX);
	uint8_t* data = new uint8_t[size];
	ReadFromFile(data, size, file);
	memcpy(out, data, size);
	delete[] data;
	return true;
}
bool CreateDir(const char* path)
{
	if (!std::filesystem::exists(path))
		if (!std::filesystem::create_directories(path))
			return false;

	return true;
}
bool GetDirFromPath(std::string* path)
{
	size_t last_slash = path->find_last_of("\\");

	if (last_slash != std::string::npos)
	{
		*path = path->substr(0, last_slash);
	}
	else return false;

	return true;
}
bool DecompressFile(std::FILE* in_file, std::FILE* out_file, size_t size)
{
	int ret;
	unsigned have;
	z_stream strm;
	ZeroMemory(&strm, sizeof(z_stream));
	uint8_t* in = new uint8_t[BUFFER_SIZE];
	uint8_t* out = new uint8_t[BUFFER_SIZE];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;

	/* decompress until deflate stream ends or end of file */
	do {
		size_t readSize = fread(in, 1, std::min<size_t>(BUFFER_SIZE, size), in_file);

		if (ferror(in_file)) {
			(void)inflateEnd(&strm);
			return false;
		}

		size -= readSize;

		if (readSize == 0)
			break;

		strm.avail_in = readSize;
		strm.next_in = in;

		/* run inflate() on input until output buffer not full */
		do {
			strm.avail_out = BUFFER_SIZE;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);
			have = BUFFER_SIZE - strm.avail_out;
			if (fwrite(out, 1, have, out_file) != have || ferror(out_file)) {
				(void)inflateEnd(&strm);
				return false;
			}
		} while (strm.avail_out == 0);

		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up */
	(void)inflateEnd(&strm);

	delete[] in;
	delete[] out;
	return true;
}
bool CompressAll(const char* in_dir, const char* out_file, const char* compression_mode)
{
	std::string file_name = std::string(out_file) + ".star";
	std::string path = file_name;
	if (GetDirFromPath(&path))
		CreateDir(path.c_str());

	std::FILE* file = std::fopen(file_name.c_str(), WRITE_MODE);

	if (!file)
		return false;

	GetFiles(in_dir, &chunks);

	bool compressionEnabled = std::strcmp(compression_mode, "NO_COMPRESSION");
	int compressionMethod = 0;

	if (std::strcmp(compression_mode, "NO_COMPRESSION") == 0)
		compressionMethod = Z_NO_COMPRESSION;
	else if (std::strcmp(compression_mode, "DEFAULT_COMPRESSION") == 0)
		compressionMethod = Z_DEFAULT_COMPRESSION;
	else if (std::strcmp(compression_mode, "BEST_SPEED") == 0)
		compressionMethod = Z_BEST_SPEED;
	else if (std::strcmp(compression_mode, "BEST_COMPRESSION") == 0)
		compressionMethod = Z_BEST_COMPRESSION;
	else
		return false;

	{
		HeaderX headerX = HeaderX();
		headerX.SetSignature(SIGNATURE);
		headerX.SetCompressionEnabled(compressionEnabled);
		headerX.SetCompressionMethod(compressionMethod);
		headerX.SetNumChunks(chunks.size());

		printf("Signature             %x\n", headerX.GetSignature());
		printf("CompressionEnabled    %s\n", headerX.GetCompressionEnabled() ? "true" : "false");
		printf("CompressionMethod     %hu\n", headerX.GetCompressionMethod());
		printf("NumChunks             %u\n", headerX.GetNumChunks());
		printf("------------------------------\n");
		printf("\n");

		if (!WriteHeader(headerX, file)) // section 1
			return false;
	}

	SetTaskbarProgressState(TBPF_NORMAL);
	SetTaskbarProgressValue(0, chunks.size());

	for (size_t i = 0; i < chunks.size(); i++)
	{
		std::FILE* entryFile = std::fopen(chunks[i].fileName.c_str(), READ_MODE);
		{
			if (!entryFile)
				return false;

			size_t uncompressedSize = 0;
			if (!GetFileSize(&uncompressedSize, entryFile))
				return false;
			chunks[i].entryX.SetUncompressedSize(uncompressedSize);

			uLong crc32 = 0;
			if (!GetCrc32(&crc32, entryFile))
				return false;
			chunks[i].entryX.SetCrc32(crc32);

			size_t compressedSize = 0;
			if (!GetCompressSize(&compressedSize, entryFile, compressionMethod))
				return false;
			chunks[i].entryX.SetCompressedSize(compressedSize);

			if (!WriteEntry(chunks[i].entryX, file)) // section 2
				return false;
			if (!WriteToFile(chunks[i].fileName.c_str(), chunks[i].fileName.size(), file)) // section 3
				return false;
			if (!CompressFile(entryFile, file, compressionMethod)) // section 4
				return false;
		}
		std::fclose(entryFile);

		printf("FileName            %s\n", chunks[i].fileName.c_str());
		printf("FileNameLength      %hu\n", chunks[i].entryX.GetFileNameLength());
		printf("UncompressedSize    %u bytes \n", chunks[i].entryX.GetUncompressedSize());
		printf("CompressedSize      %u bytes \n", chunks[i].entryX.GetCompressedSize());
		printf("Crc32               %u\n", chunks[i].entryX.GetCrc32());
		printf("------------------------------\n");
		printf("\n");

		SetTaskbarProgressValue(i + 1, chunks.size());
	}

	std::fclose(file);
	return true;
}
void PrintHelp(int argc, char* argv[])
{
	printf("\n");
	printf("Website: https://github.com/HODAKdev/StarFileFormat\n");
	printf("Usage: %s [options]\n", argv[0]);
	//printf("Build Date: %s\n", __DATE__);
	//printf("Build Time: %s\n", __TIME__);
	printf("\n");
	printf("Options:\n");
	printf("   --help                                   Display this message\n");
	printf("   --compress                               Compress mode\n");
	printf("   --decompress                             Decompress mode\n");
	printf("   --crc32_check                            crc32 check (optional)\n");
	printf("   --highest_thread_priority                Set thread priority to highest (optional)\n");
	printf("\n");
	printf("Compress:\n");
	printf("   --in_dir            DIR                  Specify the input directory for compression\n");
	printf("   --out_file          FILE                 Specify the output file for compressed data\n");
	printf("   --compression_mode  TEXT\n"
		"      NO_COMPRESSION, DEFAULT_COMPRESSION,\n"
		"      BEST_SPEED, BEST_COMPRESSION          Set the compression mode\n");
	printf("\n");
	printf("Decompress:\n");
	printf("   --in_file           FILE                 Specify the input file for decompression\n");
	printf("   --out_dir           DIR                  Specify the output directory for decompressed data\n");
	printf("\n");
	printf("Example:\n");
	printf("%s --compress --in_dir data --out_file data --compression_mode DEFAULT_COMPRESSION --highest_thread_priority\n", argv[0]);
	printf("%s --decompress --in_file data --out_dir data --highest_thread_priority\n", argv[0]);
}
bool DecompressAll(const char* in_file, const char* out_dir)
{
	std::string file_name = std::string(in_file) + ".star";
	std::FILE* file = std::fopen(file_name.c_str(), READ_MODE);
	{
		if (!file)
			return false;

		HeaderX headerX = HeaderX();
		ReadHeader(&headerX, file); // section 1

		printf("Signature             %x\n", headerX.GetSignature());
		printf("CompressionEnabled    %s\n", headerX.GetCompressionEnabled() ? "true" : "false");
		printf("CompressionMethod     %hu\n", headerX.GetCompressionMethod());
		printf("NumChunks             %u\n", headerX.GetNumChunks());
		printf("------------------------------\n");
		printf("\n");

		SetTaskbarProgressState(TBPF_NORMAL);
		SetTaskbarProgressValue(0, headerX.GetNumChunks());

		for (size_t i = 0; i < headerX.GetNumChunks(); i++)
		{
			EntryX entryX = EntryX();
			ReadEntry(&entryX, file); // section 2

			Chunk chunk = Chunk();
			std::memcpy(&chunk.entryX, &entryX, sizeof(EntryX)); // copy data
			chunks.push_back(chunk);

			ReadFromFile(&chunk.fileName, entryX.GetFileNameLength(), file); // section 3

			std::string path = std::string(chunk.fileName.c_str()); // format problem
			GetDirFromPath(&path);
			std::string full = std::string(out_dir) + "\\" + path;
			CreateDir(full.c_str());
			std::string file_path = std::string(out_dir) + "\\" + std::string(chunk.fileName.c_str()); // format problem

			std::FILE* entryFile = std::fopen(file_path.c_str(), WRITE_MODE);
			{
				if (!entryFile)
					return false;

				DecompressFile(file, entryFile, chunk.entryX.GetCompressedSize()); // section 4
			}
			std::fclose(entryFile);

			{
				if (crc32_check)
				{
					std::FILE* entryFile = std::fopen(file_path.c_str(), READ_MODE);

					uLong crc32 = 0;
					if (!GetCrc32(&crc32, entryFile))
						return false;

					if (chunk.entryX.GetCrc32() == crc32)
						printf("crc32 is valid\n");
					else
						printf("crc32 is not valid\n");

					printf("\n");
					std::fclose(entryFile);
				}
			}

			printf("FileName            %s\n", chunk.fileName.c_str());
			printf("FileNameLength      %hu\n", chunk.entryX.GetFileNameLength());
			printf("UncompressedSize    %u bytes \n", chunk.entryX.GetUncompressedSize());
			printf("CompressedSize      %u bytes \n", chunk.entryX.GetCompressedSize());
			printf("Crc32               %u\n", chunk.entryX.GetCrc32());
			printf("------------------------------\n");
			printf("\n");

			SetTaskbarProgressValue(i + 1, headerX.GetNumChunks());
		}
	}
	std::fclose(file);

	return true;
}
bool SetThreadPriorityToHighest()
{
	HANDLE currentThread = GetCurrentThread();
	if (!SetThreadPriority(currentThread, THREAD_PRIORITY_HIGHEST))
		return false;

	return true;
}

int main(int argc, char* argv[])
{
	Init();
	SetTaskbarProgressState(TBPF_NOPROGRESS);

	argh::parser cmdl;
	cmdl.add_param("help");
	cmdl.add_param("compress");
	cmdl.add_param("decompress");
	cmdl.add_param("crc32_check");
	cmdl.add_param("highest_thread_priority");
	cmdl.add_param("in_dir");
	cmdl.add_param("out_file");
	cmdl.add_param("compression_mode");
	cmdl.add_param("in_file");
	cmdl.add_param("out_dir");
	cmdl.parse(argc, argv);

	if (cmdl["help"])
	{
		PrintHelp(argc, argv);
		return EXIT_SUCCESS;
	}

	if (cmdl["compress"] && cmdl["decompress"])
		return EXIT_SUCCESS;

	if (cmdl["highest_thread_priority"])
		SetThreadPriorityToHighest();

	if (cmdl["crc32_check"])
		crc32_check = true;

	if (cmdl["compress"])
	{
		std::string in_dir;
		std::string out_file;
		std::string compression_mode;

		in_dir = cmdl("in_dir").str();
		out_file = cmdl("out_file").str();
		compression_mode = cmdl("compression_mode").str();

		if (in_dir.empty()) return EXIT_SUCCESS;
		if (out_file.empty()) return EXIT_SUCCESS;
		if (compression_mode.empty()) return EXIT_SUCCESS;

		if (!CompressAll(in_dir.c_str(), out_file.c_str(), compression_mode.c_str()))
			return EXIT_FAILURE;
	}

	if (cmdl["decompress"])
	{
		std::string in_file;
		std::string out_dir;

		in_file = cmdl("in_file").str();
		out_dir = cmdl("out_dir").str();

		if (in_file.empty()) return EXIT_SUCCESS;
		if (out_dir.empty()) return EXIT_SUCCESS;

		if (!DecompressAll(in_file.c_str(), out_dir.c_str()))
			return EXIT_FAILURE;
	}

	Release();
	return EXIT_SUCCESS;
}