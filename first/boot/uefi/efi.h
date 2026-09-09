// efi.h - 极简 UEFI 类型定义 (仅包含 UEFI 引导加载器所需成员, 偏移严格对照 UEFI 2.x)
//
// 说明: 这是手写的最小子集, 不依赖 gnu-efi/edk2。所有结构体字段偏移必须与
// UEFI 规范一致, 因为 BootServices / SystemTable 是固件按 ABI 填充的。
// x86-64 UEFI 使用 Microsoft x64 调用约定 (MinGW 默认即此), 故 EFIAPI 为空。
#ifndef EFI_H
#define EFI_H

#include <stdint.h>
#include <stddef.h>   // offsetof (用于 BootServices 偏移的静态断言)

typedef uint8_t  BOOLEAN;
typedef uint16_t CHAR16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t  INT32;
typedef int64_t  INT64;
typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef void*    EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef UINTN    EFI_STATUS;
typedef UINTN    EFI_TPL;

// x86-64 UEFI 使用 Microsoft x64 调用约定 (MinGW 默认即此); 显式标注以防万一。
#ifndef EFIAPI
#define EFIAPI __attribute__((ms_abi))
#endif

#define EFI_SUCCESS                0
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL  0x00000002

// ---- EFI_GUID (二进制布局: 1+2+2+8) ----
typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

// ---- EFI_TABLE_HEADER ----
typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

// ---- EFI_SYSTEM_TABLE ----
// 仅保证我们用到的字段偏移正确: BootServices @96, ConOut @64
typedef struct {
    EFI_TABLE_HEADER Hdr;            // 0
    CHAR16*        FirmwareVendor;   // 24
    uint32_t       FirmwareRevision; // 32
    EFI_HANDLE     ConsoleInHandle;  // 40
    void*          ConIn;            // 48
    EFI_HANDLE     ConsoleOutHandle; // 56
    void*          ConOut;           // 64  -> EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*
    EFI_HANDLE     StandardErrorHandle; // 72
    void*          StdErr;           // 80
    void*          RuntimeServices;  // 88
    struct EFI_BOOT_SERVICES_* BootServices; // 96
    UINTN          NumberOfTableEntries;    // 104
    void*          ConfigurationTable;      // 112
} EFI_SYSTEM_TABLE;

// ---- 控制台文本输出协议 ----
// 真实布局: Reset(0) OutputString(8) TestString(16) QueryMode(24) SetMode(32)
//          SetAttribute(40) ClearScreen(48) SetCursorPosition(56)
//          EnableCursor(64) Mode(72)
typedef struct {
    UINTN _r0;                                          // Reset
    EFI_STATUS (EFIAPI* OutputString)(void* This, CHAR16* String); // 8
    UINTN _r2;                                          // TestString
    UINTN _r3;                                          // QueryMode
    UINTN _r4;                                          // SetMode
    UINTN _r5;                                          // SetAttribute
    EFI_STATUS (EFIAPI* ClearScreen)(void* This);       // ClearScreen
    UINTN _r7;                                          // SetCursorPosition
    UINTN _r8;                                          // EnableCursor
    UINTN _r9;                                          // Mode
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// ---- EFI_BOOT_SERVICES ----
// 函数指针从 offset 24 (Hdr 之后) 起, 每项 8 字节, 共 44 项。顺序严格对齐 UEFI 2.x。
//
// ⚠️ 历史 bug (2026-09-04 修复): 本结构体曾漏掉 LocateHandle(176) 与 LocateDevicePath(184)
// 两项, 并把 LocateProtocol 误放在 176, 导致其后所有成员整体错位 2 个槽位 (16 字节)。
// 后果: ExitBootServices 实际指向固件的 UnloadImage(224), 而 UnloadImage 对已启动且不支持
// 卸载的镜像返回 EFI_UNSUPPORTED(0x3) —— 表现为 ExitBootServices 永远失败、内核无法启动。
// 修正后 ExitBootServices 落在规范的 232。下面用静态断言锁死偏移, 防止再次回归。
typedef struct EFI_BOOT_SERVICES_ {
    EFI_TABLE_HEADER Hdr;                  // 0..23
    EFI_TPL      (EFIAPI* RaiseTPL)(EFI_TPL NewTpl);                       // 24
    void         (EFIAPI* RestoreTPL)(EFI_TPL OldTpl);                     // 32
    EFI_STATUS   (EFIAPI* AllocatePages)(UINTN Type, UINT32 MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS* Memory); // 40
    EFI_STATUS   (EFIAPI* FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages); // 48
    EFI_STATUS   (EFIAPI* GetMemoryMap)(UINTN* MemoryMapSize, void* MemoryMap, UINTN* MapKey, UINTN* DescriptorSize, UINT32* DescriptorVersion); // 56
    EFI_STATUS   (EFIAPI* AllocatePool)(UINT32 PoolType, UINTN Size, void** Buffer); // 64
    EFI_STATUS   (EFIAPI* FreePool)(void* Buffer);                          // 72
    UINTN _f08;                                                            // 80  CreateEvent
    UINTN _f09;                                                            // 88  SetTimer
    UINTN _f10;                                                            // 96  WaitForEvent
    UINTN _f11;                                                            // 104 SignalEvent
    UINTN _f12;                                                            // 112 CloseEvent
    UINTN _f13;                                                            // 120 CheckEvent
    UINTN _f14;                                                            // 128 InstallProtocolInterface
    UINTN _f15;                                                            // 136 ReinstallProtocolInterface
    UINTN _f16;                                                            // 144 UninstallProtocolInterface
    EFI_STATUS   (EFIAPI* HandleProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface); // 152
    UINTN _f18;                                                            // 160 Reserved
    UINTN _f19;                                                            // 168 RegisterProtocolNotify
    UINTN _f20;                                                            // 176 LocateHandle
    UINTN _f21;                                                            // 184 LocateDevicePath
    UINTN _f22;                                                            // 192 InstallConfigurationTable
    UINTN _f23;                                                            // 200 LoadImage
    UINTN _f24;                                                            // 208 StartImage
    UINTN _f25;                                                            // 216 Exit
    UINTN _f26;                                                            // 224 UnloadImage
    EFI_STATUS   (EFIAPI* ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey); // 232 (真正的 ExitBootServices)
    UINTN _f28;                                                            // 240 GetNextHighMonotonicCount
    UINTN _f29;                                                            // 248 Stall
    UINTN _f30;                                                            // 256 SetWatchdogTimer
    UINTN _f31;                                                            // 264 ConnectController
    UINTN _f32;                                                            // 272 DisconnectController
    EFI_STATUS   (EFIAPI* OpenProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface,
                                        EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes); // 280
    UINTN _f34;                                                            // 288 CloseProtocol
    UINTN _f35;                                                            // 296 OpenProtocolInformation
    UINTN _f36;                                                            // 304 ProtocolsPerHandle
    EFI_STATUS   (EFIAPI* LocateHandleBuffer)(UINTN SearchType, EFI_GUID* Protocol,
                                              void* SearchKey, UINTN* NoHandles, EFI_HANDLE** Buffer); // 312
    EFI_STATUS   (EFIAPI* LocateProtocol)(EFI_GUID* Protocol, void* Registration, void** Interface); // 320
    UINTN _f39;                                                            // 328 InstallMultipleProtocolInterfaces
    UINTN _f40;                                                            // 336 UninstallMultipleProtocolInterfaces
    UINTN _f41;                                                            // 344 CalculateCrc32
    UINTN _f42;                                                            // 352 CopyMem
    UINTN _f43;                                                            // 360 SetMem
    UINTN _f44;                                                            // 368 CreateEventEx
} EFI_BOOT_SERVICES;

// 静态断言: 锁定关键成员偏移 (固件按 ABI 填充, 错一项就会调到错误的服务)
_Static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePages)    == 40,  "EFI_BOOT_SERVICES.AllocatePages offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap)     == 56,  "EFI_BOOT_SERVICES.GetMemoryMap offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePool)     == 64,  "EFI_BOOT_SERVICES.AllocatePool offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, HandleProtocol)   == 152, "EFI_BOOT_SERVICES.HandleProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 232, "EFI_BOOT_SERVICES.ExitBootServices offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, OpenProtocol)     == 280, "EFI_BOOT_SERVICES.OpenProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol)   == 320, "EFI_BOOT_SERVICES.LocateProtocol offset");

// ---- EFI_LOADED_IMAGE_PROTOCOL (获取启动设备句柄) ----
typedef struct {
    UINT32   Revision;        // 0
    EFI_HANDLE ParentHandle;  // 8
    EFI_SYSTEM_TABLE* SystemTable; // 16
    EFI_HANDLE DeviceHandle;  // 24
    void*     FilePath;       // 32
    void*     Reserved;       // 40
    UINT32   LoadOptionsSize; // 48
    void*     LoadOptions;    // 56
} EFI_LOADED_IMAGE_PROTOCOL;

// ---- EFI_BLOCK_IO_PROTOCOL (直接按 LBA 读内核, 绕过文件系统) ----
typedef struct {
    UINT32 MediaId;             // 0
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition;
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    uint8_t Pad[4];
} EFI_BLOCK_IO_MEDIA;

typedef struct {
    UINT64 Revision;                       // 0
    EFI_BLOCK_IO_MEDIA* Media;            // 8
    EFI_STATUS (EFIAPI* Reset)(void* This, BOOLEAN ExtendedVerification); // 16
    EFI_STATUS (EFIAPI* ReadBlocks)(void* This, UINT32 MediaId, UINT64 LBA,
                                    UINTN BufferSize, void* Buffer);     // 24
    EFI_STATUS (EFIAPI* WriteBlocks)(void* This, UINT32 MediaId, UINT64 LBA,
                                     UINTN BufferSize, const void* Buffer); // 32
    UINTN _f;                                                            // 40 FlushBlocks
} EFI_BLOCK_IO_PROTOCOL;

// ---- EFI_SIMPLE_FILE_SYSTEM_PROTOCOL ----
// Revision @0, OpenVolume @8
typedef struct {
    UINT64 Revision;                                            // 0
    EFI_STATUS (EFIAPI* OpenVolume)(void* This, void** Root);   // 8
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

// ---- EFI_FILE_PROTOCOL ----
// 字段偏移严格对照 UEFI 2.x。Offset 0 为 64 位 Revision。
typedef struct {
    UINT64  Revision;                                                       // 0
    EFI_STATUS (EFIAPI* Open)(void* This, void** NewHandle, CHAR16* FileName,
                              UINT64 OpenMode, UINT64 Attributes);           // 8
    EFI_STATUS (EFIAPI* Close)(void* This);                                 // 16
    UINTN _r1;                                                              // 24 Delete
    EFI_STATUS (EFIAPI* Read)(void* This, UINTN* BufferSize, void* Buffer);  // 32
    UINTN _r2;                                                              // 40 Write
    UINTN _r3;                                                              // 48 GetPosition
    UINTN _r4;                                                              // 56 SetPosition
    EFI_STATUS (EFIAPI* GetInfo)(void* This, EFI_GUID* InformationType,
                                 UINTN* BufferSize, void* Buffer);          // 64
    UINTN _r5;                                                              // 72 SetInfo
    UINTN _r6;                                                              // 80 Flush
} EFI_FILE_PROTOCOL;

// EFI_FILE_INFO (GetInfo 用) - 仅取 FileSize @offset 8
typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT64 _t0, _t1, _t2;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

// ---- 协议 GUID ----
static const EFI_GUID gEfiBlockIoProtocolGuid =
    {0x964E5B21,0x6459,0x11D2,{0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const EFI_GUID gEfiSimpleFileSystemProtocolGuid =
    {0x964E5B22,0x6459,0x11D2,{0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const EFI_GUID gEfiLoadedImageProtocolGuid =
    {0x5B1B31A1,0x9562,0x11D2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const EFI_GUID gEfiFileInfoGuid =
    {0x09576E92,0x6D3F,0x11D2,{0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static const EFI_GUID gEfiGraphicsOutputProtocolGuid =
    {0x9042A9DE,0x23DC,0x4A38,{0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A}};

// ---- EFI_GRAPHICS_OUTPUT_PROTOCOL ----
// UEFI 下固件用 GOP 把显卡置于线性帧缓冲模式; 实测此时 legacy VGA 已经不连着屏幕
// (往 0xB8000 文本显存写字符不会上屏, mode 13h 也不显示)。因此屏幕的唯一通道是
// GOP 帧缓冲, 引导器必须在 ExitBootServices 之前把它取出来交给内核。
typedef struct {
    UINT32    Version;
    UINT32    HorizontalResolution;
    UINT32    VerticalResolution;
    UINT32    PixelFormat;              // 0=RGBX 1=BGRX 2=bitmask 3=blt-only
    UINT32    PixelInformation[4];
    UINT32    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32    MaxMode;
    UINT32    Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN     SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN     FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void*     QueryMode;
    void*     SetMode;
    void*     Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// 交给内核的显示信息块: 引导器写在物理 GOP_INFO_ADDR, 内核 gfx.c 读取。
// (复用内核原本留给 VBE 信息的 0x6400; BIOS 路径无人写它, 内核会走 mode13h 回退)
#define GOP_INFO_ADDR   0x6400UL
#define GOP_INFO_MAGIC  0x4C584647UL   // 'GFXL'
typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;      // 每行字节数
    uint32_t bpp;
    uint32_t format;     // GOP PixelFormat: 0=RGBX 1=BGRX
    uint64_t fb_addr;
} gop_info_t;

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL

#define EfiLoaderCode   1
#define EfiLoaderData   2
#define AllocateAddress 2

#endif // EFI_H
