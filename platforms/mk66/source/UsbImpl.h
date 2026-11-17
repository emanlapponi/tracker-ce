#ifndef USBIMPL_H_
#define USBIMPL_H_

#include "Platform.h"
#include "config.h"

class UsbImpl final : public Usb
{
  public:
    virtual void init() override;
    virtual void reset(void) override;
    virtual void update(void) override;
    virtual void setClassState(Usb::ClassType type, bool enable) override;
    virtual bool getClassState(Usb::ClassType type) override;
    virtual bool isAttached() override;

  private:
    bool midiEnabled = true;
    bool audioInEnabled = false;
    bool audioOutEnabled = false;
    bool storageEnabled = false;
    bool serialEnabled = true;
    bool rawHidEnabled = true;
    bool resetRequested = false;
};

#endif /* USBIMPL_H_ */
