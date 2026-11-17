#ifndef LIBS_AUDIO_INPUT_H_
#define LIBS_AUDIO_INPUT_H_

#include "AudioStream.h"

class AudioInputStream : public AudioStream
{
  public:
    explicit AudioInputStream(bool silentWithoutData = false)
      : AudioStream(0, NULL)
      , silentWithoutData(silentWithoutData)
    {
    }

    virtual void update();
    void setInBuffer(void* data);

  private:
    void* data = nullptr;
    bool silentWithoutData;
};

#endif /* LIBS_AUDIO_INPUT_H_ */
