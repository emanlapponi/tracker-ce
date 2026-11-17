#ifndef CONFIG_INTERRUPTSCONFIG_H_
#define CONFIG_INTERRUPTSCONFIG_H_

#ifndef TRACKER_MINI

#define value2priority(x) (x << 4) // low 4 bits are ignored in k66 interrupt priority

// needs to be higher than audio irq to work properly
static const int kMidiJackInUARTPriorityLevel = value2priority(0);

// SysTick priority cannot be set, but has default priority=32 (or value=2)
// for proper time measuring of ellapsedmillis etc, audio out has to have lower priority than
// SysTick
// more info: K66 Sub-Family Reference Manual, 3.4 Interrupt channel assignments

static const int kAudioInputInterruptLevel = value2priority(4);

static const int kAudioOutputInterruptLevel = value2priority(5);

static const int kSDHCInterruptLevel = value2priority(6);

// Higher than audio (4/5) so 1ms USB isochronous frames are not starved during heavy AudioStream::update_all()
static const int kUSBOTGPriorityLevel = value2priority(3);

#endif

#endif /* CONFIG_INTERRUPTSCONFIG_H_ */
