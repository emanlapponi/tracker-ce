#ifndef SOURCE_MTRANDOMNAMEGENERATOR_H_
#define SOURCE_MTRANDOMNAMEGENERATOR_H_

#include "mtStructs.h"

constexpr uint8_t RANDOM_NAME_WORD_1_MAX_SIZE = 30;
constexpr uint8_t RANDOM_NAME_WORD_2_MAX_SIZE = 30;
constexpr uint8_t RANDOM_NAME_MAX_SIZE =
  RANDOM_NAME_WORD_1_MAX_SIZE + RANDOM_NAME_WORD_2_MAX_SIZE + 1;

class FileSystem;
class Timer;

class mtRandomNameGenerator
{
  public:
    void init(FileSystem* fs) { fileSystem_ = fs; }
    const char* getRandomName(Timer* timer);

  private:
    FileSystem* fileSystem_ = nullptr;
    char randomName[RANDOM_NAME_MAX_SIZE]{};
};

extern mtRandomNameGenerator randomNameGenerator;

#endif /* SOURCE_MTRANDOMNAMEGENERATOR_H_ */
