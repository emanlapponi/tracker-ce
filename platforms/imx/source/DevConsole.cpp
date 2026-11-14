#include "DevConsole.h"

#ifdef ENABLE_DEV_CONSOLE

#include "PlatformImpl.h"
#include "TimeControl.h"
#include "config.h"
#include "fsl_common.h"

#include <cstdio>
#include <cstring>

bool onDevStorageMode(bool enable);

extern "C"
{
    void USB_PollFromFault(void);
    extern void (*const g_pfnVectors[])(void);
    extern const char __exidx_start;
    extern const uint32_t _vStackBase;
    extern const uint32_t _vStackTop;
}

namespace
{
constexpr uint32_t kTxBufferSize = 4096; // must be a power of two
constexpr uint32_t kTxBufferMask = kTxBufferSize - 1;
constexpr size_t kFormatBufferSize = 256;
constexpr size_t kLineBufferSize = 96;
// The Original Tracker's bootloader reads the name into a 32-byte buffer;
// keep the same limit here so one .ptf naming rule covers all devices.
constexpr size_t kMaxFirmwareNameLength = 31;
// Time for the reply to reach the host before the MCU resets under it.
constexpr uint32_t kPendingActionDelayMs = 250;
constexpr uint32_t kPendingActionTimeoutMs = 2000;

#ifdef TRACKER_MINI
constexpr char kProductName[] = "mini";
#else
constexpr char kProductName[] = "plus";
#endif

enum class PendingAction
{
    kNone,
    kFlash,
    kReboot,
};

uint8_t txBuffer[kTxBufferSize];
// Free-running indices: head is advanced by writers under an interrupt lock,
// tail only by the single reader in the main loop.
volatile uint32_t txHead = 0;
volatile uint32_t txTail = 0;
volatile uint32_t txDroppedBytes = 0;
volatile uint8_t txLastByte = '\n';

char lineBuffer[kLineBufferSize];
size_t lineLength = 0;
bool lineOverflow = false;

// The fault report. A fault handler never returns, so this is what
// the console answers with from then on.
constexpr uint32_t kFaultStackAddresses = 16;
// About two seconds between repeats of the report, for a host that opens
// the port after the fault.
constexpr uint32_t kFaultRepeatPasses = 5000000;
constexpr uint16_t kSemihostingBreakpoint = 0xBEAB;
constexpr uint32_t kSemihostingResult = 32;
enum FaultFrame : uint32_t
{
    kFrameR0 = 0,
    kFrameLr = 5,
    kFramePc = 6,
    kFrameXpsr = 7,
    kFrameWords = 8,
};
const uint32_t* faultFrame = nullptr;
bool faulted = false;

PlatformImpl* devPlatform = nullptr;
PendingAction pendingAction = PendingAction::kNone;
uint32_t pendingSinceMs = 0;
char pendingFirmwareName[kMaxFirmwareNameLength + 1];

void pushLocked(const uint8_t* data, size_t length)
{
    uint32_t freeBytes = kTxBufferSize - (txHead - txTail);
    size_t accepted = length < freeBytes ? length : freeBytes;

    for (size_t i = 0; i < accepted; i++)
    {
        txBuffer[(txHead + i) & kTxBufferMask] = data[i];
    }

    txHead += accepted;
    txDroppedBytes += length - accepted;

    if (accepted > 0)
    {
        txLastByte = data[accepted - 1];
    }
}

// Replies start with '@' at the beginning of a line so the host can tell them
// apart from log output, which may be mid-line when a command arrives.
void reply(const char* format, ...)
{
    char buffer[kFormatBufferSize];
    buffer[0] = '@';

    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer + 1, sizeof(buffer) - 2, format, args);
    va_end(args);

    if (length < 0)
    {
        return;
    }

    size_t total =
      1 + (static_cast<size_t>(length) < sizeof(buffer) - 2 ? static_cast<size_t>(length)
                                                            : sizeof(buffer) - 3);
    buffer[total++] = '\n';

    uint32_t primask = DisableGlobalIRQ();
    if (txLastByte != '\n')
    {
        const uint8_t newline = '\n';
        pushLocked(&newline, 1);
    }
    pushLocked(reinterpret_cast<const uint8_t*>(buffer), total);
    EnableGlobalIRQ(primask);
}

bool isValidFirmwareName(const char* name)
{
    size_t length = strlen(name);

    if (length == 0 || length > kMaxFirmwareNameLength)
    {
        return false;
    }

    return strpbrk(name, "/\\: ") == nullptr;
}

void schedule(PendingAction action)
{
    pendingAction = action;
    pendingSinceMs = millis();
}

void handleStorage(const char* argument)
{
    bool enable = strcmp(argument, "on") == 0;

    if (!enable && strcmp(argument, "off") != 0)
    {
        reply("err usage: storage on|off");
        return;
    }

    bool active = onDevStorageMode(enable);

    if (active == enable)
    {
        reply("ok storage %s", argument);
    }
    else
    {
        // The interface ignores the request outside its normal run state,
        // for example while a project is still loading.
        reply("err storage is still %s", active ? "on" : "off");
    }
}

void handleFlash(const char* name)
{
    if (!isValidFirmwareName(name))
    {
        reply("err bad firmware name");
        return;
    }

    strcpy(pendingFirmwareName, name);
    schedule(PendingAction::kFlash);
    reply("ok flash pending");
}

bool isCodeAddress(uint32_t address)
{
    // From the vector table to the unwind index the linker script puts
    // after all code in flash. _etext is no use: more code follows it.
    return address >= reinterpret_cast<uint32_t>(&g_pfnVectors) &&
           address < reinterpret_cast<uint32_t>(&__exidx_start);
}

bool isOnStack(const uint32_t* address)
{
    return (reinterpret_cast<uint32_t>(address) & 3) == 0 && address >= &_vStackBase &&
           address < &_vStackTop;
}

// The stack words above the exception frame that look like return
// addresses: a rough call chain, with stale entries possible.
void replyFaultStack()
{
    char line[16 + kFaultStackAddresses * 9];
    int length = snprintf(line, sizeof(line), "fault stack");
    uint32_t found = 0;
    for (const uint32_t* word = faultFrame + kFrameWords;
         word < &_vStackTop && found < kFaultStackAddresses;
         word++)
    {
        const bool thumbReturnAddress = (*word & 1) != 0 && isCodeAddress(*word);
        if (thumbReturnAddress)
        {
            length += snprintf(line + length, sizeof(line) - length, " %08lx", *word);
            found++;
        }
    }
    reply("%s", line);
}

void replyFault()
{
    if (faultFrame == nullptr)
    {
        reply("fault cfsr=%08lx hfsr=%08lx mmfar=%08lx bfar=%08lx, no usable stack",
              SCB->CFSR,
              SCB->HFSR,
              SCB->MMFAR,
              SCB->BFAR);
        return;
    }

    // The low bits of xpsr name the exception that was running when the
    // fault came; 0 is the main loop.
    // The build id lets the host check that it resolves the addresses
    // against the image of this build.
    reply("fault pc=%08lx lr=%08lx xpsr=%08lx cfsr=%08lx hfsr=%08lx mmfar=%08lx bfar=%08lx sp=%08lx handler=%lu build=%s",
          faultFrame[kFramePc],
          faultFrame[kFrameLr],
          faultFrame[kFrameXpsr],
          SCB->CFSR,
          SCB->HFSR,
          SCB->MMFAR,
          SCB->BFAR,
          reinterpret_cast<uint32_t>(faultFrame),
          __get_IPSR(),
          devPlatform != nullptr ? devPlatform->getSystemInfo()->buildId : "?");
    replyFaultStack();
}

// Nothing else runs after a fault, interrupts included, so the USB
// controller is polled from here.
void serveConsoleFromFault(uint32_t passes)
{
    for (uint32_t i = 0; i < passes; i++)
    {
        USB_PollFromFault();
    }
}

void handleLine(char* line)
{
    if (faulted)
    {
        replyFault();
        return;
    }

    char* argument = strchr(line, ' ');

    if (argument != nullptr)
    {
        *argument++ = '\0';
    }
    else
    {
        argument = line + strlen(line);
    }

    if (strcmp(line, "ping") == 0)
    {
        reply("tracker-dev %s %s", kProductName, devPlatform->getSystemInfo()->buildId);
    }
    else if (strcmp(line, "storage") == 0)
    {
        handleStorage(argument);
    }
    else if (strcmp(line, "flash") == 0)
    {
        handleFlash(argument);
    }
    else if (strcmp(line, "reboot") == 0)
    {
        schedule(PendingAction::kReboot);
        reply("ok reboot pending");
    }
    else if (strcmp(line, "help") == 0)
    {
        reply("ok commands: ping, storage on|off, flash <name>, reboot, help");
    }
    else
    {
        reply("err unknown command: %s", line);
    }
}

void endLine()
{
    if (lineOverflow)
    {
        reply("err line too long");
    }
    else if (lineLength > 0)
    {
        lineBuffer[lineLength] = '\0';
        handleLine(lineBuffer);
    }

    lineLength = 0;
    lineOverflow = false;
}

void writeTurnOnFile()
{
    FileSystem* fileSystem = devPlatform->getFileSystem();
    constexpr char kTurnOnFileData[] = "start";
    auto [isOpen, fileHandle] =
      fileSystem->openFile(kTurnOnFilePath, FileSystem::FileOpenMode::kWriteCreateNew);

    if (isOpen)
    {
        fileSystem->writeFile(kTurnOnFileData, strlen(kTurnOnFileData), fileHandle);
        fileSystem->closeFile(fileHandle);
    }
}

void runFlash()
{
    // The host may have rewritten the card through USB storage mode behind
    // FatFs's back; cached allocation state would be stale without this.
    if (!devPlatform->getFileSystemImpl()->remount())
    {
        reply("err cannot remount the SD card");
        return;
    }

    char path[sizeof("/Firmware/") + kMaxFirmwareNameLength];
    snprintf(path, sizeof(path), "/Firmware/%s", pendingFirmwareName);

    if (!devPlatform->getFileSystem()->exists(path))
    {
        reply("err no such file: %s", path);
        return;
    }

    devPlatform->runBootloader(pendingFirmwareName);
    reply("err cannot write the firmware info file");
}

void runReboot()
{
    // Without this a Mini release build powers itself off after the reset
    // unless the power button happens to be held.
    writeTurnOnFile();
    devPlatform->resetMCU();
}
} // namespace

extern "C" void DevConsole_Write(const uint8_t* data, size_t length)
{
    uint32_t primask = DisableGlobalIRQ();
    pushLocked(data, length);
    EnableGlobalIRQ(primask);
}

extern "C" void DevConsole_Vprintf(const char* format, va_list args)
{
    char buffer[kFormatBufferSize];
    int length = vsnprintf(buffer, sizeof(buffer), format, args);

    if (length <= 0)
    {
        return;
    }

    size_t stored = static_cast<size_t>(length) < sizeof(buffer) ? static_cast<size_t>(length)
                                                                 : sizeof(buffer) - 1;
    DevConsole_Write(reinterpret_cast<const uint8_t*>(buffer), stored);
}

extern "C" void DevConsole_Printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    DevConsole_Vprintf(format, args);
    va_end(args);
}

extern "C" size_t DevConsole_TxRead(uint8_t* destination, size_t maxLength)
{
    uint32_t tail = txTail;
    uint32_t available = txHead - tail;

    if (available == 0)
    {
        uint32_t primask = DisableGlobalIRQ();
        uint32_t dropped = txDroppedBytes;
        txDroppedBytes = 0;
        EnableGlobalIRQ(primask);

        if (dropped == 0)
        {
            return 0;
        }

        int length = snprintf(reinterpret_cast<char*>(destination),
                              maxLength,
                              "\n@log dropped %lu bytes\n",
                              static_cast<unsigned long>(dropped));
        txLastByte = '\n';
        return length > 0 && static_cast<size_t>(length) < maxLength ? length : 0;
    }

    size_t count = available < maxLength ? available : maxLength;

    for (size_t i = 0; i < count; i++)
    {
        destination[i] = txBuffer[(tail + i) & kTxBufferMask];
    }

    txTail = tail + count;
    return count;
}

extern "C" void DevConsole_RxBytes(const uint8_t* data, size_t length)
{
    if (devPlatform == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < length; i++)
    {
        char c = static_cast<char>(data[i]);

        if (c == '\n' || c == '\r')
        {
            endLine();
        }
        else if (lineLength < kLineBufferSize - 1)
        {
            lineBuffer[lineLength++] = c;
        }
        else
        {
            lineOverflow = true;
        }
    }
}

// Reports the fault whose exception frame this is, for as long as the
// device stays on.
extern "C" __attribute__((used)) void DevConsole_Fault(uint32_t* frame)
{
    const bool frameUsable = isOnStack(frame) && isOnStack(frame + kFrameWords - 1);
    faultFrame = frameUsable ? frame : nullptr;
    faulted = true;
    for (;;)
    {
        // Log lines queued before the fault go out first: with the buffer
        // full the report would be dropped.
        serveConsoleFromFault(kFaultRepeatPasses);
        replyFault();
    }
}

// Called by HardFault_Handler with the exception frame. Returns only for a
// semihosting breakpoint, which is stepped over as the stock handler does.
extern "C" void DevConsole_HardFault(uint32_t* frame)
{
    const bool frameUsable = isOnStack(frame) && isOnStack(frame + kFrameWords - 1);
    if (frameUsable && isCodeAddress(frame[kFramePc]) &&
        *reinterpret_cast<const uint16_t*>(frame[kFramePc]) == kSemihostingBreakpoint)
    {
        frame[kFramePc] += 2;
        frame[kFrameR0] = kSemihostingResult;
        return;
    }

    DevConsole_Fault(frame);
}

// The fault exceptions other than the hard fault, taken instead of it when
// enabled. This firmware enables none, but it is started by a bootloader:
// on the Tracker Mini SCB->SHCSR already has MEMFAULTENA set, and memory
// access violations arrive at MemManage_Handler. The defaults in the
// startup file hang.
#define DEV_CONSOLE_FAULT_HANDLER(name)                                                            \
    extern "C" __attribute__((naked)) void name(void)                                              \
    {                                                                                              \
        __asm volatile("tst lr, #4 \n"                                                             \
                       "ite eq \n"                                                                 \
                       "mrseq r0, msp \n"                                                          \
                       "mrsne r0, psp \n"                                                          \
                       "b DevConsole_Fault \n");                                                   \
    }

DEV_CONSOLE_FAULT_HANDLER(MemManage_Handler)
DEV_CONSOLE_FAULT_HANDLER(BusFault_Handler)
DEV_CONSOLE_FAULT_HANDLER(UsageFault_Handler)

void DevConsole_Init(PlatformImpl* platformImpl)
{
    devPlatform = platformImpl;
}

void DevConsole_Update()
{
    if (pendingAction == PendingAction::kNone)
    {
        return;
    }

    uint32_t elapsedMs = millis() - pendingSinceMs;
    bool outputDrained = txHead == txTail;

    // The timeout covers a host that sent the command and stopped reading.
    if (elapsedMs < kPendingActionDelayMs ||
        (!outputDrained && elapsedMs < kPendingActionTimeoutMs))
    {
        return;
    }

    PendingAction action = pendingAction;
    pendingAction = PendingAction::kNone;

    if (action == PendingAction::kFlash)
    {
        runFlash();
    }
    else
    {
        runReboot();
    }
}

#endif /* ENABLE_DEV_CONSOLE */
