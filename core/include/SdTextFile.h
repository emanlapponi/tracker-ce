#ifndef INCLUDE_SDTEXTFILE_H_
#define INCLUDE_SDTEXTFILE_H_

#include "Platform.h"

#include <cstddef>
#include <cstdint>

/**
 * @brief RAII guard for FileSystem::FileHandle that closes the file on scope exit.
 */
class SdScopedFile
{
  public:
    SdScopedFile(FileSystem* fs, const char* path, FileSystem::FileOpenMode mode);
    SdScopedFile(FileSystem* fs, FileSystem::FileHandle handle, bool isOpen);
    ~SdScopedFile();

    SdScopedFile(const SdScopedFile&) = delete;
    SdScopedFile& operator=(const SdScopedFile&) = delete;

    explicit operator bool() const { return open_ && handle_ != nullptr; }
    FileSystem::FileHandle handle() const { return handle_; }

  private:
    FileSystem* fs_;
    FileSystem::FileHandle handle_;
    bool open_;
};

namespace SdTextFile
{
/**
 * @brief Open a file, retrying without a leading '/' if the initial open fails.
 */
std::tuple<bool, FileSystem::FileHandle> openWithFallback(
  FileSystem* fs,
  const char* path,
  FileSystem::FileOpenMode mode = FileSystem::kRead);

/**
 * @brief Read an entire text file into a caller-supplied buffer and null-terminate it.
 * @return true if at least 1 byte was read.
 */
bool readAll(FileSystem* fs,
             const char* path,
             char* buf,
             size_t bufSize,
             size_t* outLen = nullptr);

/**
 * @brief Pick a random word from a newline/delimiter-separated text file on SD without loading
 *        the whole file into RAM.
 * @return true if a non-empty word was extracted into outWord.
 */
bool readRandomWord(FileSystem* fs,
                    const char* path,
                    uint32_t seed,
                    char* outWord,
                    size_t maxLen);
} // namespace SdTextFile

#endif /* INCLUDE_SDTEXTFILE_H_ */
