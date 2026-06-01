// HGCC preload hook for the 32-bit NWN 1.69 Linux client.
//
// Build with build.sh and launch nwmain through LD_PRELOAD.  This mirrors the
// Windows pipe protocol over an AF_UNIX socket so the Python GUI can use the
// same command helpers for Linux clients.

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0
#endif

extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureQuickbarExec(int32_t panel, int32_t slot_index);
extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureQuickbarSlotDispatch(int32_t slot_ptr);
extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureChatWindowLog(
    int32_t chat_window,
    const void* nwn_string);

struct _XDisplay;
typedef _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long Colormap;
typedef unsigned long Cursor;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizei;

constexpr GLenum GL_QUADS = 0x0007u;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401u;
constexpr GLenum GL_VIEWPORT = 0x0BA2u;
constexpr GLenum GL_CULL_FACE = 0x0B44u;
constexpr GLenum GL_DEPTH_TEST = 0x0B71u;
constexpr GLenum GL_DEPTH_WRITEMASK = 0x0B72u;
constexpr GLenum GL_BLEND = 0x0BE2u;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1u;
constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069u;
constexpr GLenum GL_MATRIX_MODE = 0x0BA0u;
constexpr GLenum GL_MODELVIEW = 0x1700u;
constexpr GLenum GL_PROJECTION = 0x1701u;
constexpr GLenum GL_SRC_ALPHA = 0x0302u;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303u;
constexpr GLenum GL_RGBA = 0x1908u;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5u;
constexpr GLbitfield GL_CURRENT_BIT = 0x00000001u;
constexpr GLbitfield GL_ENABLE_BIT = 0x00002000u;
constexpr GLbitfield GL_TRANSFORM_BIT = 0x00001000u;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000u;

namespace {

static_assert(sizeof(void*) == 4, "Build SimKeysHookLinux as a 32-bit shared object.");

constexpr uint32_t kOpQuery = 3000;
constexpr uint32_t kOpTriggerSlot = 3001;
constexpr uint32_t kOpTriggerVk = 3002;
constexpr uint32_t kOpSetLog = 3003;
constexpr uint32_t kOpReplayLast = 3004;
constexpr uint32_t kOpSnapshotText = 3005;
constexpr uint32_t kOpChatSend = 3006;
constexpr uint32_t kOpChatPoll = 3007;
constexpr uint32_t kOpTriggerPageSlot = 3008;
constexpr uint32_t kOpOverlayText = 3009;
constexpr uint32_t kOpOverlayClear = 3010;
constexpr uint32_t kOpOverlayClearAll = 3011;
constexpr uint32_t kOpMoveToLocation = 3012;
constexpr uint32_t kOpSetWalkBypass = 3013;
constexpr uint32_t kOpSetActionMode = 3014;
constexpr uint32_t kOpMapPin = 3015;

constexpr uint32_t kImageBase = 0x08048000u;
constexpr uint32_t kAppGlobalSlotAddress = 0x0862C354u;
constexpr uint32_t kQuickbarExec = 0x080D9B20u;
constexpr uint32_t kQuickbarPageSelect = 0x080D6C28u;
constexpr uint32_t kQuickbarSlotDispatch = 0x080CAA50u;
constexpr uint32_t kQuickbarPanelVtable = 0x0862F900u;
constexpr uint32_t kChatSend = 0x08265054u;
constexpr uint32_t kChatWindowLog = 0x080B89F0u;
constexpr uint32_t kWalkToWaypoint = 0x08077260u;
constexpr uint32_t kWalkNoWalkBlock = 0x0807E84Au;
constexpr uint32_t kWalkNoWalkBypassTarget = 0x0807E878u;
constexpr uint32_t kCurrentGuiResolver = 0x08077008u;
constexpr uint32_t kCurrentClientPlayerResolver = 0x08076A9Cu;
constexpr uint32_t kQuickbarItemResolver = 0x08076B4Cu;
constexpr uint32_t kItemEquippedOwnerResolver = 0x0819BF34u;
constexpr uint32_t kServerObjectByIdResolver = 0x082AA024u;
constexpr uint32_t kSetActionMode = 0x08315B2Cu;
constexpr uint32_t kGetActionMode = 0x08305538u;
constexpr uint32_t kToggleModeInput = 0x081365CCu;
constexpr uint32_t kPlayerNameBuilder = 0x08138B68u;
constexpr uint32_t kNwnStringConstructFromCString = 0x085A6070u;
constexpr uint32_t kNwnStringDestroy = 0x085A61DCu;
constexpr uint32_t kCurrentPlayerGlobalAddress = 0x0862EF18u;

constexpr uint32_t kQuickbarPanelVtableOffset = 0x20u;
constexpr uint32_t kQuickbarPanelSlotsOffset = 0x74u;
constexpr uint32_t kQuickbarCurrentPageOffset = 0x3704u;
constexpr uint32_t kQuickbarEnabledOffset = 0x3708u;
constexpr uint32_t kQuickbarSlotStride = 0x184u;
constexpr uint32_t kQuickbarPageStride = 0x1230u;
constexpr uint32_t kQuickbarSlotPrimaryItemOffset = 0x6Cu;
constexpr uint32_t kQuickbarSlotSecondaryItemOffset = 0x70u;
constexpr uint32_t kQuickbarSlotTypeOffset = 0xA0u;
constexpr uint32_t kCurrentPlayerObjectIdOffset = 0x24u;
constexpr uint32_t kPlayerCreatureOffset = 0x2BCu;
constexpr uint8_t kQuickbarItemSlotType = 1u;
constexpr int kQuickbarPageCount = 3;
constexpr int kQuickbarSlotCount = 12;
constexpr int kQuickbarTotalSlots = 36;
constexpr int kActionModeDefensiveCast = 10;
constexpr uint32_t kInvalidObjectId = 0x7F000000u;
constexpr uint32_t kInvalidObjectIdAlt = 0xFFFFFFFFu;
constexpr uint32_t kCurrentPlayerPositionOffset = 0x30u;
constexpr uint32_t kClientDefensiveCastingStateOffset = 0x188u;
constexpr uint32_t kObjectAsCreatureVtableOffset = 0x38u;
constexpr uint32_t kCreatureCurrentHitPointsOffset = 0x48u;
constexpr uint32_t kCreatureMaxHitPointsOffset = 0x4Au;

constexpr int kLogError = 0;
constexpr int kLogInfo = 1;
constexpr int kLogDebug = 2;
constexpr int kPipeBufferSize = 65536;
constexpr int kDispatchTimeoutMs = 2000;
constexpr int kPendingChatCapacity = 1024;
constexpr int kMapPinTextCapacity = 512;
constexpr int kMapPinXmlCapacity = 1024;
constexpr int kChatQueueCapacity = 1024;
constexpr int kChatTextCapacity = 768;
constexpr int kCharacterNameCapacity = 128;
constexpr int kMaxOverlays = 32;
constexpr int kOverlayTextCapacity = 4096;
constexpr int kOverlayMaxControls = 16;
constexpr int kOverlayControlIdCapacity = 32;
constexpr int kOverlayControlLabelCapacity = 8;
constexpr int kOverlayControlButtonSize = 22;
constexpr int kOverlayControlGap = 4;
constexpr int kOverlayControlPadding = 3;
constexpr int kOverlayTextPadding = 6;
constexpr int kOverlayMaxDimension = 1024;
constexpr int kOverlayLineColorMarkerLength = 8;
constexpr char kOverlayControlMarker = '\x1D';
constexpr char kOverlayEventMarker = '\x1E';
constexpr char kOverlayLineColorMarker = '\x1F';

constexpr int kErrSuccess = 0;
constexpr int kErrInvalidFunction = 1;
constexpr int kErrInvalidData = 13;
constexpr int kErrNotSupported = 50;
constexpr int kErrInvalidParameter = 87;
constexpr int kErrTimeout = 258;
constexpr int kErrBusy = 170;
constexpr int kErrNotFound = 1168;
constexpr int kErrInvalidState = 5023;
constexpr uint8_t kSdlActiveEvent = 1;
constexpr uint8_t kSdlUserEvent = 24;
constexpr uint32_t kSdlUserEventMask = 1u << kSdlUserEvent;
constexpr size_t kSdlEventSize = 24;
constexpr int kSdlGetEvent = 2;
constexpr int32_t kSdlWakeEventCode = 0x534B574Bu;  // SKWK
constexpr uint8_t kSdlActiveMask = 0x07u;
constexpr uint8_t kSdlAppActiveMask = 0x07u;

#pragma pack(push, 1)
struct PipeHeader {
  uint32_t op;
  uint32_t size;
};

struct QueryResponse {
  uint32_t module_base;
  uint32_t hook_wndproc;
  uint32_t hwnd;
  uint32_t current_wndproc;
  uint32_t original_wndproc;
  uint32_t window_thread_id;
  uint32_t installed;
  uint32_t expected_runtime_nwn_wndproc;
  uint32_t expected_runtime_key_pre_dispatch;
  uint32_t expected_runtime_dispatcher_thunk;
  uint32_t expected_runtime_dispatcher_slot0;
  uint32_t app_global_slot;
  uint32_t app_holder;
  uint32_t app_object;
  uint32_t app_inner;
  uint32_t dispatcher_ptr;
  uint32_t gate_90;
  uint32_t gate_94;
  uint32_t gate_98;
  uint32_t quickbar_exec;
  uint32_t quickbar_slot_dispatch;
  uint32_t quickbar_panel_vtable;
  uint32_t quickbar_slot_ptr;
  uint32_t quickbar_this;
  int32_t quickbar_page;
  int32_t quickbar_slot;
  int32_t quickbar_slot_type;
  int32_t quickbar_calls;
  int32_t quickbar_scan_attempts;
  int32_t quickbar_scan_hits;
  int32_t last_vk;
  int32_t last_rc;
  int32_t last_error;
  int32_t log_level;
  uint32_t player_object;
  uint32_t player_creature;
  int32_t identity_refresh_count;
  int32_t identity_error;
  uint32_t quickbar_item_mask_low;
  uint32_t quickbar_item_mask_high;
  uint32_t quickbar_equipped_mask_low;
  uint32_t quickbar_equipped_mask_high;
  int32_t position_valid;
  float position_x;
  float position_y;
  float position_z;
  char character_name[kCharacterNameCapacity];
  int32_t player_current_hp;
  int32_t player_max_hp;
  uint32_t player_current_hp_address;
  uint32_t player_max_hp_address;
  int32_t player_hp_error;
};

struct TriggerResponse {
  int32_t success;
  int32_t vk;
  int32_t rc;
  int32_t aux_rc;
  int32_t last_error;
  int32_t path;
};

struct ChatSendResponse {
  int32_t success;
  int32_t mode;
  int32_t rc;
  int32_t last_error;
};

struct MapPinRequestHeader {
  float x;
  float y;
  int32_t text_length;
};

struct MapPinResponse {
  int32_t success;
  int32_t rc;
  int32_t last_error;
};

struct MoveToLocationRequest {
  float x;
  float y;
  float z;
  int32_t client_side;
  uint32_t action_object_id;
  int32_t bypass_no_walk;
};

struct MoveToLocationResponse {
  int32_t success;
  int32_t rc;
  int32_t last_error;
  float x;
  float y;
  float z;
};

struct WalkBypassRequest {
  int32_t enabled;
};

struct WalkBypassResponse {
  int32_t success;
  int32_t enabled;
  int32_t last_error;
};

struct SetActionModeRequest {
  int32_t mode;
  int32_t enabled;
};

struct SetActionModeResponse {
  int32_t success;
  int32_t mode;
  int32_t enabled;
  int32_t active;
  int32_t rc;
  int32_t last_error;
};

struct ChatPollRequest {
  int32_t after_sequence;
  int32_t max_lines;
};

struct ChatPollResponseHeader {
  int32_t latest_sequence;
  int32_t line_count;
};

struct ChatPollLineHeader {
  int32_t sequence;
  int32_t text_length;
};

struct OverlayTextRequestHeader {
  int32_t id;
  int32_t position;
  int32_t offset_x;
  int32_t offset_y;
  int32_t font_size;
  uint32_t color_rgb;
  int32_t text_length;
};

struct OverlayResponse {
  int32_t success;
  int32_t width;
  int32_t height;
  int32_t last_error;
};
#pragma pack(pop)

struct ChatLineEntry {
  int32_t sequence;
  char text[kChatTextCapacity];
};

struct OverlayControlButton {
  char script_id[kOverlayControlIdCapacity];
  char label[kOverlayControlLabelCapacity];
  int32_t enabled;
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
};

struct OverlayRecord {
  int32_t active;
  int32_t id;
  int32_t position;
  int32_t offset_x;
  int32_t offset_y;
  int32_t font_size;
  uint32_t color_rgb;
  int32_t width;
  int32_t height;
  int32_t screen_x;
  int32_t screen_y;
  int32_t control_count;
  char text[kOverlayTextCapacity];
  OverlayControlButton controls[kOverlayMaxControls];
};

enum PendingKind {
  kPendingNone = 0,
  kPendingTriggerSlot,
  kPendingChatSend,
  kPendingMove,
  kPendingWalkBypass,
  kPendingActionMode,
  kPendingMapPin,
  kPendingRefreshIdentity,
};

struct PendingCommand {
  int32_t busy;
  int32_t done;
  PendingKind kind;
  int32_t slot;
  int32_t page;
  int32_t mode;
  int32_t enabled;
  float x;
  float y;
  float z;
  int32_t client_side;
  uint32_t action_object_id;
  int32_t bypass_no_walk;
  char text[kPendingChatCapacity];
  TriggerResponse trigger_response;
  ChatSendResponse chat_response;
  MapPinResponse map_pin_response;
  MoveToLocationResponse move_response;
  WalkBypassResponse walk_response;
  SetActionModeResponse action_response;
  int32_t refresh_error;
};

struct HookState {
  pthread_mutex_t log_mutex;
  pthread_mutex_t chat_mutex;
  pthread_mutex_t overlay_mutex;
  int32_t initialized;
  int32_t installed;
  int32_t log_level;
  int32_t pipe_state;
  int32_t pipe_thread_error;
  pthread_t pipe_thread;
  char socket_path[PATH_MAX];
  char log_path[PATH_MAX];
  FILE* log_file;
  int32_t quickbar_trace_installed;
  int32_t quickbar_slot_trace_installed;
  int32_t chat_trace_installed;
  int32_t overlay_hook_installed;
  int32_t quickbar_this;
  int32_t quickbar_slot_ptr;
  int32_t quickbar_page;
  int32_t quickbar_slot;
  int32_t quickbar_slot_type;
  int32_t quickbar_calls;
  int32_t quickbar_scan_attempts;
  int32_t quickbar_scan_hits;
  int32_t chat_sequence;
  int32_t chat_write_index;
  int32_t chat_count;
  int32_t overlay_count;
  int32_t overlay_draws;
  int32_t overlay_last_error;
  int32_t walk_no_walk_bypass_enabled;
  int32_t last_vk;
  int32_t last_result;
  int32_t last_error;
  int32_t last_chat_mode;
  int32_t last_chat_result;
  int32_t last_chat_error;
  int32_t player_object;
  int32_t player_creature;
  int32_t identity_refresh_count;
  int32_t identity_error;
  int32_t quickbar_item_mask_low;
  int32_t quickbar_item_mask_high;
  int32_t quickbar_equipped_mask_low;
  int32_t quickbar_equipped_mask_high;
  int32_t pending_drain_count;
  int32_t pending_wake_attempts;
  int32_t pending_wake_success;
  int32_t pending_wake_swallowed;
  int32_t pending_wake_missing_logged;
  int32_t pending_signal_wake_attempts;
  int32_t pending_signal_wake_success;
  int32_t focus_loss_swallowed;
  int32_t position_valid;
  float position_x;
  float position_y;
  float position_z;
  char character_name[kCharacterNameCapacity];
  ChatLineEntry chat_lines[kChatQueueCapacity];
  OverlayRecord overlays[kMaxOverlays];
};

HookState g_state = {};
PendingCommand g_pending = {};
pthread_mutex_t g_pending_mutex;
pthread_cond_t g_pending_cond;

uint8_t g_quickbar_exec_original[16] = {};
void* g_quickbar_exec_gateway = nullptr;
uint8_t g_quickbar_slot_original[16] = {};
void* g_quickbar_slot_gateway = nullptr;
uint8_t g_chat_log_original[32] = {};
void* g_chat_log_gateway = nullptr;
uint8_t g_walk_no_walk_original[8] = {};
bool g_walk_no_walk_bypass_installed = false;

struct GraphicsApi {
  void (*glGetIntegerv)(GLenum, GLint*);
  GLboolean (*glIsEnabled)(GLenum);
  void (*glDisable)(GLenum);
  void (*glEnable)(GLenum);
  void (*glBlendFunc)(GLenum, GLenum);
  void (*glDepthMask)(GLboolean);
  void (*glColor3f)(float, float, float);
  void (*glMatrixMode)(GLenum);
  void (*glPushMatrix)();
  void (*glLoadIdentity)();
  void (*glPopMatrix)();
  void (*glBindTexture)(GLenum, GLuint);
  void (*glRasterPos2f)(float, float);
  void (*glDrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const void*);
  void (*glPixelStorei)(GLenum, GLint);
};

GraphicsApi g_graphics = {};
int g_graphics_ready = 0;
int g_graphics_failed = 0;
int32_t g_overlay_render_failed = 0;
void* g_libgl_handle = nullptr;

typedef void (*SdlGlSwapBuffersFn)();
typedef int (*SdlPollEventFn)(void*);
typedef int (*SdlWaitEventFn)(void*);
typedef void (*SdlDelayFn)(uint32_t);
typedef int (*SdlPushEventFn)(void*);
typedef int (*SdlPeepEventsFn)(void*, int, int, uint32_t);
typedef uint8_t (*SdlGetAppStateFn)();
typedef void* (*SdlGetVideoSurfaceFn)();
typedef int (*SdlGetWmInfoFn)(void*);
SdlGlSwapBuffersFn g_real_sdl_gl_swap_buffers = nullptr;
SdlPollEventFn g_real_sdl_poll_event = nullptr;
SdlWaitEventFn g_real_sdl_wait_event = nullptr;
SdlDelayFn g_real_sdl_delay = nullptr;
SdlPushEventFn g_real_sdl_push_event = nullptr;
SdlPeepEventsFn g_real_sdl_peep_events = nullptr;
SdlGetAppStateFn g_real_sdl_get_app_state = nullptr;
SdlGetVideoSurfaceFn g_real_sdl_get_video_surface = nullptr;
SdlGetWmInfoFn g_real_sdl_get_wm_info = nullptr;
void* g_sdl12_handle = nullptr;
void* g_x11_handle = nullptr;
int32_t g_sdl_wake_event_queued = 0;
pthread_t g_main_thread = {};
int32_t g_main_thread_ready = 0;
int32_t g_wake_signal_installed = 0;
int32_t g_keep_sdl_active = -1;
int32_t g_overlay_render_enabled = -1;

struct FaultGuard {
  sigjmp_buf jump;
  FaultGuard* previous;
  int signal_number;
  uintptr_t instruction_pointer;
  uintptr_t fault_address;
};

__thread FaultGuard* g_active_fault_guard = nullptr;
int32_t g_fault_handlers_installed = 0;
struct sigaction g_previous_sigsegv = {};
struct sigaction g_previous_sigbus = {};
struct sigaction g_previous_sigill = {};

void LogMessage(int level, const char* format, ...);
void DrainPendingOnMainThread();

void WakeSignalHandler(int) {
}

uint32_t AtomicGet(const int32_t* value) {
  return static_cast<uint32_t>(__sync_add_and_fetch(const_cast<int32_t*>(value), 0));
}

void AtomicSet(int32_t* value, int32_t next) {
  __sync_lock_test_and_set(value, next);
}

int32_t AtomicIncrement(int32_t* value) {
  return __sync_add_and_fetch(value, 1);
}

bool IsHookMainThread() {
  return AtomicGet(&g_main_thread_ready) != 0 && pthread_equal(pthread_self(), g_main_thread);
}

bool KeepSdlActiveEnabled() {
  int32_t cached = static_cast<int32_t>(AtomicGet(&g_keep_sdl_active));
  if (cached >= 0) {
    return cached != 0;
  }

  const char* value = getenv("SIMKEYS_LINUX_KEEP_ACTIVE");
  const bool enabled = value == nullptr ||
      value[0] == '\0' ||
      (value[0] != '0' && value[0] != 'f' && value[0] != 'F' && value[0] != 'n' && value[0] != 'N');
  AtomicSet(&g_keep_sdl_active, enabled ? 1 : 0);
  return enabled;
}

bool OverlayRenderingEnabled() {
  int32_t cached = static_cast<int32_t>(AtomicGet(&g_overlay_render_enabled));
  if (cached >= 0) {
    return cached != 0;
  }

  const char* disable = getenv("SIMKEYS_LINUX_DISABLE_OVERLAY");
  bool enabled = !(disable != nullptr &&
      disable[0] != '\0' &&
      (disable[0] == '1' ||
       disable[0] == 't' ||
       disable[0] == 'T' ||
       disable[0] == 'y' ||
       disable[0] == 'Y'));
  const char* value = getenv("SIMKEYS_LINUX_ENABLE_OVERLAY");
  if (value != nullptr && value[0] != '\0') {
    enabled = value[0] != '0' &&
        value[0] != 'f' &&
        value[0] != 'F' &&
        value[0] != 'n' &&
        value[0] != 'N';
  }
  AtomicSet(&g_overlay_render_enabled, enabled ? 1 : 0);
  return enabled;
}

bool EnvFlagEnabled(const char* name) {
  const char* value = getenv(name);
  return value != nullptr &&
      value[0] != '\0' &&
      value[0] != '0' &&
      value[0] != 'f' &&
      value[0] != 'F' &&
      value[0] != 'n' &&
      value[0] != 'N';
}

bool EnvFlagExplicitlyDisabled(const char* name) {
  const char* value = getenv(name);
  return value != nullptr &&
      value[0] != '\0' &&
      (value[0] == '0' ||
       value[0] == 'f' ||
       value[0] == 'F' ||
       value[0] == 'n' ||
       value[0] == 'N');
}

const struct sigaction* PreviousFaultAction(int signal_number) {
  switch (signal_number) {
    case SIGBUS:
      return &g_previous_sigbus;
    case SIGILL:
      return &g_previous_sigill;
    case SIGSEGV:
    default:
      return &g_previous_sigsegv;
  }
}

void FaultSignalHandler(int signal_number, siginfo_t* info, void* context) {
  FaultGuard* guard = g_active_fault_guard;
  if (guard != nullptr) {
    guard->signal_number = signal_number;
    guard->fault_address = info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
#if defined(__i386__) && defined(REG_EIP)
    const ucontext_t* ucontext = reinterpret_cast<const ucontext_t*>(context);
    if (ucontext != nullptr) {
      guard->instruction_pointer = static_cast<uintptr_t>(ucontext->uc_mcontext.gregs[REG_EIP]);
    }
#else
    (void)context;
#endif
    siglongjmp(guard->jump, 1);
  }

  const struct sigaction* previous = PreviousFaultAction(signal_number);
  sigaction(signal_number, previous, nullptr);
  raise(signal_number);
}

void InstallFaultHandlers() {
  if (__sync_lock_test_and_set(&g_fault_handlers_installed, 1) != 0) {
    return;
  }

  struct sigaction action = {};
  action.sa_sigaction = FaultSignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigaction(SIGSEGV, &action, &g_previous_sigsegv);
  sigaction(SIGBUS, &action, &g_previous_sigbus);
  sigaction(SIGILL, &action, &g_previous_sigill);
}

void InstallWakeSignalHandler() {
  struct sigaction previous = {};
  if (sigaction(SIGUSR1, nullptr, &previous) != 0) {
    return;
  }
  if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN) {
    LogMessage(kLogDebug, "SIGUSR1 is already handled; pending wake signal nudge disabled");
    return;
  }

  struct sigaction action = {};
  action.sa_handler = WakeSignalHandler;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGUSR1, &action, nullptr) == 0) {
    AtomicSet(&g_wake_signal_installed, 1);
  }
}

template <typename Fn>
bool RunWithFaultGuard(
    Fn fn,
    int* out_signal,
    uintptr_t* out_instruction_pointer = nullptr,
    uintptr_t* out_fault_address = nullptr) {
  FaultGuard guard = {};
  guard.previous = g_active_fault_guard;
  g_active_fault_guard = &guard;
  const int jumped = sigsetjmp(guard.jump, 1);
  if (jumped == 0) {
    fn();
  }
  g_active_fault_guard = guard.previous;
  if (jumped != 0) {
    if (out_signal != nullptr) {
      *out_signal = guard.signal_number;
    }
    if (out_instruction_pointer != nullptr) {
      *out_instruction_pointer = guard.instruction_pointer;
    }
    if (out_fault_address != nullptr) {
      *out_fault_address = guard.fault_address;
    }
    errno = EFAULT;
    return false;
  }
  if (out_signal != nullptr) {
    *out_signal = 0;
  }
  return true;
}

// Keep NWN text addresses as runtime immediates; PIC can otherwise fold a
// constant function pointer call into a .so-relative direct call.
__attribute__((noinline)) uintptr_t AbsoluteNwnAddress(uint32_t address) {
  volatile uintptr_t resolved = static_cast<uintptr_t>(address);
  return resolved;
}

template <typename Fn>
Fn NwnFunction(uint32_t address) {
  return reinterpret_cast<Fn>(AbsoluteNwnAddress(address));
}

void* NwnPointer(uint32_t address) {
  return reinterpret_cast<void*>(AbsoluteNwnAddress(address));
}

void NudgeMainThreadForPending() {
  if (AtomicGet(&g_wake_signal_installed) == 0 || AtomicGet(&g_main_thread_ready) == 0) {
    return;
  }
  if (IsHookMainThread()) {
    return;
  }

  AtomicIncrement(&g_state.pending_signal_wake_attempts);
  if (pthread_kill(g_main_thread, SIGUSR1) == 0) {
    AtomicIncrement(&g_state.pending_signal_wake_success);
  }
}

void* ResolveSdl12Symbol(const char* name) {
  if (g_sdl12_handle == nullptr) {
    g_sdl12_handle = dlopen("libSDL-1.2.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (g_sdl12_handle == nullptr) {
      g_sdl12_handle = dlopen("libSDL-1.2.so", RTLD_LAZY | RTLD_NOLOAD);
    }
  }
  if (g_sdl12_handle != nullptr) {
    void* symbol = dlsym(g_sdl12_handle, name);
    if (symbol != nullptr) {
      return symbol;
    }
  }
  return dlsym(RTLD_NEXT, name);
}

bool GetSdlVideoSurfaceSize(int* out_width, int* out_height) {
  if (out_width != nullptr) {
    *out_width = 0;
  }
  if (out_height != nullptr) {
    *out_height = 0;
  }
  if (g_real_sdl_get_video_surface == nullptr) {
    g_real_sdl_get_video_surface =
        reinterpret_cast<SdlGetVideoSurfaceFn>(ResolveSdl12Symbol("SDL_GetVideoSurface"));
  }
  if (g_real_sdl_get_video_surface == nullptr) {
    return false;
  }

  void* surface = g_real_sdl_get_video_surface();
  if (surface == nullptr) {
    return false;
  }

  struct SdlSurfaceHeader {
    uint32_t flags;
    void* format;
    int width;
    int height;
  };
  const SdlSurfaceHeader* header = static_cast<const SdlSurfaceHeader*>(surface);
  if (header->width <= 0 || header->height <= 0 ||
      header->width > 10000 || header->height > 10000) {
    return false;
  }
  if (out_width != nullptr) {
    *out_width = header->width;
  }
  if (out_height != nullptr) {
    *out_height = header->height;
  }
  return true;
}

struct SdlVersion {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
};

struct SdlSysWmInfo {
  SdlVersion version;
  int subsystem;
  union {
    struct {
      Display* display;
      Window window;
      void (*lock_func)();
      void (*unlock_func)();
      Window fswindow;
      Window wmwindow;
      Display* gfxdisplay;
    } x11;
  } info;
};

bool GetSdlX11Window(Display** out_display, Window* out_window) {
  if (out_display != nullptr) {
    *out_display = nullptr;
  }
  if (out_window != nullptr) {
    *out_window = 0;
  }
  if (g_real_sdl_get_wm_info == nullptr) {
    g_real_sdl_get_wm_info =
        reinterpret_cast<SdlGetWmInfoFn>(ResolveSdl12Symbol("SDL_GetWMInfo"));
  }
  if (g_real_sdl_get_wm_info == nullptr) {
    return false;
  }

  SdlSysWmInfo wm_info = {};
  wm_info.version.major = 1;
  wm_info.version.minor = 2;
  wm_info.version.patch = 0;
  if (!g_real_sdl_get_wm_info(&wm_info) ||
      wm_info.info.x11.display == nullptr ||
      wm_info.info.x11.window == 0) {
    return false;
  }
  if (out_display != nullptr) {
    *out_display = wm_info.info.x11.display;
  }
  if (out_window != nullptr) {
    *out_window = wm_info.info.x11.window;
  }
  return true;
}

void* ResolveX11Symbol(const char* name) {
  void* symbol = dlsym(RTLD_DEFAULT, name);
  if (symbol != nullptr) {
    return symbol;
  }
  if (g_x11_handle == nullptr) {
    g_x11_handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
  }
  return g_x11_handle != nullptr ? dlsym(g_x11_handle, name) : nullptr;
}

bool GetSdlWindowSize(int* out_width, int* out_height) {
  if (out_width != nullptr) {
    *out_width = 0;
  }
  if (out_height != nullptr) {
    *out_height = 0;
  }
  if (g_real_sdl_get_wm_info == nullptr) {
    g_real_sdl_get_wm_info =
        reinterpret_cast<SdlGetWmInfoFn>(ResolveSdl12Symbol("SDL_GetWMInfo"));
  }
  if (g_real_sdl_get_wm_info == nullptr) {
    return false;
  }

  Display* display = nullptr;
  Window window = 0;
  if (!GetSdlX11Window(&display, &window)) {
    return false;
  }

  struct XWindowAttributes {
    int x;
    int y;
    int width;
    int height;
    int border_width;
    int depth;
    void* visual;
    Window root;
    int c_class;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    Colormap colormap;
    int map_installed;
    int map_state;
    long all_event_masks;
    long your_event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    void* screen;
  };
  typedef int (*XGetWindowAttributesFn)(Display*, Window, XWindowAttributes*);
  XGetWindowAttributesFn get_window_attributes =
      reinterpret_cast<XGetWindowAttributesFn>(ResolveX11Symbol("XGetWindowAttributes"));
  if (get_window_attributes == nullptr) {
    return false;
  }

  XWindowAttributes attrs = {};
  if (!get_window_attributes(display, window, &attrs) ||
      attrs.width <= 0 || attrs.height <= 0 ||
      attrs.width > 10000 || attrs.height > 10000) {
    return false;
  }
  if (out_width != nullptr) {
    *out_width = attrs.width;
  }
  if (out_height != nullptr) {
    *out_height = attrs.height;
  }
  return true;
}

bool GetSdlPointerPosition(int* out_x, int* out_y) {
  if (out_x != nullptr) {
    *out_x = 0;
  }
  if (out_y != nullptr) {
    *out_y = 0;
  }

  Display* display = nullptr;
  Window window = 0;
  if (!GetSdlX11Window(&display, &window)) {
    return false;
  }

  typedef int (*XQueryPointerFn)(
      Display*,
      Window,
      Window*,
      Window*,
      int*,
      int*,
      int*,
      int*,
      unsigned int*);
  XQueryPointerFn query_pointer =
      reinterpret_cast<XQueryPointerFn>(ResolveX11Symbol("XQueryPointer"));
  if (query_pointer == nullptr) {
    return false;
  }

  Window root = 0;
  Window child = 0;
  int root_x = 0;
  int root_y = 0;
  int win_x = 0;
  int win_y = 0;
  unsigned int mask = 0;
  if (!query_pointer(display, window, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
    return false;
  }
  if (win_x < 0 || win_y < 0 || win_x > 10000 || win_y > 10000) {
    return false;
  }
  if (out_x != nullptr) {
    *out_x = win_x;
  }
  if (out_y != nullptr) {
    *out_y = win_y;
  }
  return true;
}

SdlPushEventFn ResolveSdlPushEvent() {
  if (g_real_sdl_push_event == nullptr) {
    g_real_sdl_push_event = reinterpret_cast<SdlPushEventFn>(ResolveSdl12Symbol("SDL_PushEvent"));
  }
  return g_real_sdl_push_event;
}

SdlPeepEventsFn ResolveSdlPeepEvents() {
  if (g_real_sdl_peep_events == nullptr) {
    g_real_sdl_peep_events = reinterpret_cast<SdlPeepEventsFn>(ResolveSdl12Symbol("SDL_PeepEvents"));
  }
  return g_real_sdl_peep_events;
}

bool IsSimKeysWakeEvent(const void* event) {
  if (event == nullptr) {
    return false;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(event);
  if (bytes[0] != kSdlUserEvent) {
    return false;
  }
  int32_t code = 0;
  memcpy(&code, bytes + 4, sizeof(code));
  return code == kSdlWakeEventCode;
}

bool IsSdlFocusLossEvent(const void* event) {
  if (!KeepSdlActiveEnabled() || event == nullptr) {
    return false;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(event);
  if (bytes[0] != kSdlActiveEvent) {
    return false;
  }

  const uint8_t gain = bytes[1];
  const uint8_t state = bytes[2];
  return gain == 0 && (state & kSdlActiveMask) != 0;
}

bool FilterSdlInternalEvent(void* event) {
  if (IsSimKeysWakeEvent(event)) {
    AtomicIncrement(&g_state.pending_wake_swallowed);
    AtomicSet(&g_sdl_wake_event_queued, 0);
    return true;
  }

  if (IsSdlFocusLossEvent(event)) {
    AtomicIncrement(&g_state.focus_loss_swallowed);
    return true;
  }

  return false;
}

int FilterSdlInternalEvents(void* events, int count) {
  if (events == nullptr || count <= 0) {
    return count;
  }

  uint8_t* bytes = static_cast<uint8_t*>(events);
  int kept = 0;
  for (int i = 0; i < count; ++i) {
    uint8_t* event = bytes + static_cast<size_t>(i) * kSdlEventSize;
    if (FilterSdlInternalEvent(event)) {
      continue;
    }

    if (kept != i) {
      memmove(
          bytes + static_cast<size_t>(kept) * kSdlEventSize,
          event,
          kSdlEventSize);
    }
    ++kept;
  }
  return kept;
}

void WakeMainThreadForPending(PendingKind kind) {
  AtomicIncrement(&g_state.pending_wake_attempts);
  const bool coalesce = kind == kPendingRefreshIdentity;
  if (coalesce && __sync_lock_test_and_set(&g_sdl_wake_event_queued, 1) != 0) {
    return;
  }
  if (!coalesce) {
    AtomicSet(&g_sdl_wake_event_queued, 1);
  }

  SdlPushEventFn push_event = ResolveSdlPushEvent();
  if (push_event == nullptr) {
    AtomicSet(&g_sdl_wake_event_queued, 0);
    if (__sync_bool_compare_and_swap(&g_state.pending_wake_missing_logged, 0, 1)) {
      LogMessage(kLogDebug, "SDL_PushEvent is unavailable; pending work will drain on the next client tick");
    }
    NudgeMainThreadForPending();
    return;
  }

  uint8_t event[64] = {};
  event[0] = kSdlUserEvent;
  const int32_t code = kSdlWakeEventCode;
  memcpy(event + 4, &code, sizeof(code));

  const int rc = push_event(event);
  if (rc == 0) {
    AtomicIncrement(&g_state.pending_wake_success);
  } else {
    AtomicSet(&g_sdl_wake_event_queued, 0);
    LogMessage(kLogDebug, "SDL_PushEvent wake failed rc=%d errno=%d", rc, errno);
  }
  NudgeMainThreadForPending();
}

bool IsPlausibleCoordinate(float value) {
  return value == value && value > -1000000.0f && value < 1000000.0f;
}

bool IsPlausiblePosition(float x, float y, float z) {
  return IsPlausibleCoordinate(x) && IsPlausibleCoordinate(y) && IsPlausibleCoordinate(z);
}

void EnsureLogReady() {
  if (g_state.log_file != nullptr) {
    return;
  }

  const char* root = getenv("SIMKEYS_LINUX_LOG_DIR");
  char dir[PATH_MAX] = {};
  if (root != nullptr && root[0] != '\0') {
    snprintf(dir, sizeof(dir), "%s", root);
  } else {
    const char* home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.local/state/hgcc/logs", home != nullptr ? home : "/tmp");
  }
  mkdir(dir, 0755);

  snprintf(g_state.log_path, sizeof(g_state.log_path), "%s/simkeys_linux_%ld.log", dir, static_cast<long>(getpid()));
  g_state.log_file = fopen(g_state.log_path, "a");
}

void LogMessage(int level, const char* format, ...) {
  if (level > static_cast<int>(AtomicGet(&g_state.log_level))) {
    return;
  }

  char message[768] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  timeval tv = {};
  gettimeofday(&tv, nullptr);
  tm local_tm = {};
  localtime_r(&tv.tv_sec, &local_tm);

  char line[1024] = {};
  snprintf(
      line,
      sizeof(line),
      "[simkeys-linux][%04d-%02d-%02d %02d:%02d:%02d.%03ld][pid=%ld][tid=%lu][L%d] %s\n",
      local_tm.tm_year + 1900,
      local_tm.tm_mon + 1,
      local_tm.tm_mday,
      local_tm.tm_hour,
      local_tm.tm_min,
      local_tm.tm_sec,
      static_cast<long>(tv.tv_usec / 1000),
      static_cast<long>(getpid()),
      static_cast<unsigned long>(pthread_self()),
      level,
      message);

  pthread_mutex_lock(&g_state.log_mutex);
  EnsureLogReady();
  if (g_state.log_file != nullptr) {
    fputs(line, g_state.log_file);
    fflush(g_state.log_file);
  }
  pthread_mutex_unlock(&g_state.log_mutex);
}

int InitialLogLevel() {
  const char* raw = getenv("SIMKEYS_LINUX_LOG_LEVEL");
  if (raw == nullptr || raw[0] == '\0') {
    return kLogInfo;
  }
  char* end = nullptr;
  long value = strtol(raw, &end, 10);
  if (end == raw) {
    return kLogInfo;
  }
  if (value < kLogError) {
    return kLogError;
  }
  if (value > kLogDebug) {
    return kLogDebug;
  }
  return static_cast<int>(value);
}

bool ReadExact(int fd, void* buffer, size_t size) {
  uint8_t* out = static_cast<uint8_t*>(buffer);
  size_t offset = 0;
  while (offset < size) {
    const ssize_t got = recv(fd, out + offset, size - offset, 0);
    if (got == 0) {
      return false;
    }
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<size_t>(got);
  }
  return true;
}

bool WriteExact(int fd, const void* buffer, size_t size) {
  const uint8_t* in = static_cast<const uint8_t*>(buffer);
  size_t offset = 0;
  while (offset < size) {
    const ssize_t sent = send(fd, in + offset, size - offset, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<size_t>(sent);
  }
  return true;
}

bool WriteResponse(int fd, uint32_t op, const void* payload, uint32_t size) {
  PipeHeader header = {op, size};
  if (!WriteExact(fd, &header, sizeof(header))) {
    LogMessage(kLogDebug, "socket write header failed op=%u size=%u errno=%d", op, size, errno);
    return false;
  }
  const bool ok = size == 0 || WriteExact(fd, payload, size);
  if (!ok) {
    LogMessage(kLogDebug, "socket write payload failed op=%u size=%u errno=%d", op, size, errno);
  }
  return ok;
}

struct MapRange {
  uintptr_t start;
  uintptr_t end;
  bool readable;
  bool writable;
};

bool RangeIsMapped(uintptr_t address, size_t size, bool need_write) {
  FILE* fp = fopen("/proc/self/maps", "r");
  if (fp == nullptr) {
    return address >= 0x08048000u && address + size < 0xF0000000u;
  }

  char line[512] = {};
  const uintptr_t wanted_end = address + size;
  bool ok = false;
  while (fgets(line, sizeof(line), fp) != nullptr) {
    unsigned long start = 0;
    unsigned long end = 0;
    char perms[8] = {};
    if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) {
      continue;
    }
    if (address >= start && wanted_end <= end && perms[0] == 'r' && (!need_write || perms[1] == 'w')) {
      ok = true;
      break;
    }
  }
  fclose(fp);
  return ok;
}

bool RangeIsExecutable(uintptr_t address, size_t size) {
  FILE* fp = fopen("/proc/self/maps", "r");
  if (fp == nullptr) {
    return address >= kImageBase && address + size < 0xF0000000u;
  }

  char line[512] = {};
  const uintptr_t wanted_end = address + size;
  bool ok = false;
  while (fgets(line, sizeof(line), fp) != nullptr) {
    unsigned long start = 0;
    unsigned long end = 0;
    char perms[8] = {};
    if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) {
      continue;
    }
    if (address >= start && wanted_end <= end && perms[2] == 'x') {
      ok = true;
      break;
    }
  }
  fclose(fp);
  return ok;
}

template <typename T>
bool SafeReadValue(uintptr_t address, T* out) {
  if (out == nullptr || !RangeIsMapped(address, sizeof(T), false)) {
    if (out != nullptr) {
      memset(out, 0, sizeof(T));
    }
    return false;
  }
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            memcpy(out, reinterpret_cast<const void*>(address), sizeof(T));
          },
          &signal_number)) {
    memset(out, 0, sizeof(T));
    return false;
  }
  return true;
}

uint32_t SafeReadPointer32(uintptr_t address) {
  uint32_t value = 0;
  SafeReadValue(address, &value);
  return value;
}

bool SafeReadString(const void* nwn_string, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0 || nwn_string == nullptr) {
    return false;
  }
  out[0] = '\0';

  uint32_t text_ptr = 0;
  int32_t text_length = 0;
  const uintptr_t base = reinterpret_cast<uintptr_t>(nwn_string);
  if (!SafeReadValue(base, &text_ptr) || !SafeReadValue(base + 4, &text_length)) {
    return false;
  }
  if (text_ptr == 0 || text_length <= 0 || text_length > 16384) {
    return false;
  }
  size_t copy_length = static_cast<size_t>(text_length);
  if (copy_length >= capacity) {
    copy_length = capacity - 1;
  }
  if (!RangeIsMapped(text_ptr, copy_length, false)) {
    return false;
  }

  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            memcpy(out, reinterpret_cast<const void*>(text_ptr), copy_length);
          },
          &signal_number)) {
    out[0] = '\0';
    return false;
  }
  out[copy_length] = '\0';
  return true;
}

bool IsValidObjectId(uint32_t object_id) {
  return object_id != 0 &&
      object_id != kInvalidObjectId &&
      object_id != kInvalidObjectIdAlt &&
      object_id < 0x80000000u;
}

bool IsValidQuickbarItemId(uint32_t object_id) {
  return object_id != 0 &&
      object_id != kInvalidObjectId &&
      object_id != kInvalidObjectIdAlt;
}

uint32_t ReadAppHolderPointer() {
  return SafeReadPointer32(kAppGlobalSlotAddress);
}

uint32_t ReadAppObjectPointer() {
  const uint32_t holder = ReadAppHolderPointer();
  return holder != 0 ? SafeReadPointer32(holder) : 0;
}

uint32_t ReadServerAppObjectPointer() {
  const uint32_t holder = ReadAppHolderPointer();
  return holder != 0 ? SafeReadPointer32(static_cast<uintptr_t>(holder) + 4u) : 0;
}

uint32_t ReadAppInnerPointer() {
  const uint32_t app_object = ReadAppObjectPointer();
  return app_object != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_object) + 4u) : 0;
}

uint32_t ReadCurrentPlayerObjectId() {
  const uint32_t app_inner = ReadAppInnerPointer();
  return app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_inner) + kCurrentPlayerObjectIdOffset) : 0;
}

uint32_t ReadCurrentGuiPointer() {
  const uint32_t app_inner = ReadAppInnerPointer();
  return app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x48u) : 0;
}

uint32_t ReadCurrentPlayerGlobalPointer() {
  return SafeReadPointer32(kCurrentPlayerGlobalAddress);
}

bool IsQuickbarPanel(uint32_t panel) {
  return panel != 0 &&
      SafeReadPointer32(static_cast<uintptr_t>(panel) + kQuickbarPanelVtableOffset) == kQuickbarPanelVtable;
}

bool TryAdoptQuickbarPanel(uint32_t panel, int32_t slot, const char* source) {
  if (!IsQuickbarPanel(panel)) {
    return false;
  }
  const int32_t old_panel = static_cast<int32_t>(AtomicGet(&g_state.quickbar_this));
  AtomicSet(&g_state.quickbar_this, static_cast<int32_t>(panel));
  if (slot >= 0 && slot < kQuickbarTotalSlots) {
    AtomicSet(&g_state.quickbar_slot, slot % kQuickbarSlotCount);
    AtomicSet(&g_state.quickbar_page, slot / kQuickbarSlotCount);
  }
  if (old_panel != static_cast<int32_t>(panel)) {
    LogMessage(kLogInfo, "quickbar panel captured via %s panel=0x%08X slot=%d", source, panel, slot);
  }
  return true;
}

bool DiscoverQuickbarPanel(const char* reason) {
  AtomicIncrement(&g_state.quickbar_scan_attempts);
  const uint32_t gui = ReadCurrentGuiPointer();
  const uint32_t panel = gui != 0 ? SafeReadPointer32(static_cast<uintptr_t>(gui) + 0x3Cu) : 0;
  if (TryAdoptQuickbarPanel(panel, -1, reason != nullptr ? reason : "gui")) {
    AtomicIncrement(&g_state.quickbar_scan_hits);
    return true;
  }
  return false;
}

int32_t ResolveQuickbarPageIndex(uint32_t panel) {
  if (!IsQuickbarPanel(panel)) {
    return -1;
  }

  const uint32_t current_page_base = SafeReadPointer32(static_cast<uintptr_t>(panel) + kQuickbarCurrentPageOffset);
  if (current_page_base == 0) {
    return -1;
  }

  for (int32_t page = 0; page < kQuickbarPageCount; ++page) {
    const uint32_t expected_page_base =
        panel + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(page) * kQuickbarPageStride;
    if (current_page_base == expected_page_base) {
      return page;
    }
  }

  return -1;
}

bool TryDeriveQuickbarPanelFromSlot(uint32_t slot_ptr, uint32_t* out_panel, int32_t* out_index) {
  if (slot_ptr == 0) {
    return false;
  }

  uint32_t candidates[2] = {
      static_cast<uint32_t>(AtomicGet(&g_state.quickbar_this)),
      0,
  };
  const uint32_t gui = ReadCurrentGuiPointer();
  if (gui != 0) {
    candidates[1] = SafeReadPointer32(static_cast<uintptr_t>(gui) + 0x3Cu);
  }

  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    const uint32_t panel = candidates[i];
    if (!IsQuickbarPanel(panel)) {
      continue;
    }
    const uint32_t base = panel + kQuickbarPanelSlotsOffset;
    if (slot_ptr < base) {
      continue;
    }
    const uint32_t delta = slot_ptr - base;
    if (delta % kQuickbarSlotStride != 0) {
      continue;
    }
    const int32_t index = static_cast<int32_t>(delta / kQuickbarSlotStride);
    if (index < 0 || index >= kQuickbarTotalSlots) {
      continue;
    }
    if (out_panel != nullptr) {
      *out_panel = panel;
    }
    if (out_index != nullptr) {
      *out_index = index;
    }
    return true;
  }

  return false;
}

void SetQuickbarMaskBit(uint32_t* low, uint32_t* high, int bit_index) {
  if (low == nullptr || high == nullptr || bit_index < 0) {
    return;
  }

  if (bit_index < 32) {
    *low |= (1u << bit_index);
  } else if (bit_index < kQuickbarTotalSlots) {
    *high |= (1u << (bit_index - 32));
  }
}

void StoreQuickbarItemMasks(uint32_t item_low, uint32_t item_high, uint32_t equipped_low, uint32_t equipped_high) {
  AtomicSet(&g_state.quickbar_item_mask_low, static_cast<int32_t>(item_low));
  AtomicSet(&g_state.quickbar_item_mask_high, static_cast<int32_t>(item_high));
  AtomicSet(&g_state.quickbar_equipped_mask_low, static_cast<int32_t>(equipped_low));
  AtomicSet(&g_state.quickbar_equipped_mask_high, static_cast<int32_t>(equipped_high));
}

uint32_t ResolveCurrentClientPlayer();

bool ResolveQuickbarItemById(uint32_t app_object, uint32_t object_id, uint32_t* out_object) {
  if (out_object != nullptr) {
    *out_object = 0;
  }
  if (app_object == 0 || !IsValidQuickbarItemId(object_id)) {
    return false;
  }

  typedef void* (*ItemByIdFn)(void*, uint32_t);
  void* object = nullptr;
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            object = NwnFunction<ItemByIdFn>(kQuickbarItemResolver)(
                reinterpret_cast<void*>(app_object),
                object_id);
          },
          &signal_number)) {
    LogMessage(
        kLogDebug,
        "quickbar item resolver faulted signal=%d appObject=0x%08X objectId=0x%08X",
        signal_number,
        app_object,
        object_id);
    return false;
  }

  if (object == nullptr) {
    return false;
  }
  if (out_object != nullptr) {
    *out_object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
  }
  return true;
}

bool ResolveItemEquippedOwner(uint32_t item_object, uint32_t* out_owner) {
  if (out_owner != nullptr) {
    *out_owner = 0;
  }
  if (item_object == 0) {
    return false;
  }

  typedef void* (*ItemEquippedOwnerFn)(void*);
  void* owner = nullptr;
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            owner = NwnFunction<ItemEquippedOwnerFn>(kItemEquippedOwnerResolver)(
                reinterpret_cast<void*>(item_object));
          },
          &signal_number)) {
    LogMessage(
        kLogDebug,
        "item equipped owner resolver faulted signal=%d itemObject=0x%08X",
        signal_number,
        item_object);
    return false;
  }

  if (owner == nullptr) {
    return false;
  }
  if (out_owner != nullptr) {
    *out_owner = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner));
  }
  return true;
}

bool ItemIsEquippedByCurrentPlayer(uint32_t item_object, uint32_t current_player, uint32_t current_player_object_id) {
  uint32_t owner = 0;
  if (!ResolveItemEquippedOwner(item_object, &owner)) {
    return false;
  }
  if (owner == current_player) {
    return true;
  }
  return IsValidObjectId(current_player_object_id) &&
      SafeReadPointer32(static_cast<uintptr_t>(owner) + 4u) == current_player_object_id;
}

void UpdateQuickbarItemMasks() {
  uint32_t item_low = 0;
  uint32_t item_high = 0;
  uint32_t equipped_low = 0;
  uint32_t equipped_high = 0;
  const uint32_t panel = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_this));
  if (!IsQuickbarPanel(panel)) {
    StoreQuickbarItemMasks(0, 0, 0, 0);
    return;
  }

  const uint32_t app_object = ReadAppObjectPointer();
  const uint32_t current_player_object_id = ReadCurrentPlayerObjectId();
  const uint32_t current_player = ResolveCurrentClientPlayer();
  if (app_object == 0 || current_player == 0) {
    StoreQuickbarItemMasks(0, 0, 0, 0);
    return;
  }

  for (int index = 0; index < kQuickbarTotalSlots; ++index) {
    const uintptr_t slot = panel + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(index) * kQuickbarSlotStride;
    uint8_t slot_type = 0xFF;
    if (!SafeReadValue(slot + kQuickbarSlotTypeOffset, &slot_type)) {
      continue;
    }
    if (slot_type != kQuickbarItemSlotType) {
      continue;
    }
    const uint32_t primary_item_id = SafeReadPointer32(slot + kQuickbarSlotPrimaryItemOffset);
    if (!IsValidQuickbarItemId(primary_item_id)) {
      continue;
    }

    uint32_t primary_item = 0;
    if (!ResolveQuickbarItemById(app_object, primary_item_id, &primary_item)) {
      continue;
    }
    SetQuickbarMaskBit(&item_low, &item_high, index);

    if (!ItemIsEquippedByCurrentPlayer(primary_item, current_player, current_player_object_id)) {
      continue;
    }

    const uint32_t secondary_item_id = SafeReadPointer32(slot + kQuickbarSlotSecondaryItemOffset);
    if (IsValidQuickbarItemId(secondary_item_id)) {
      uint32_t secondary_item = 0;
      if (!ResolveQuickbarItemById(app_object, secondary_item_id, &secondary_item) ||
          !ItemIsEquippedByCurrentPlayer(secondary_item, current_player, current_player_object_id)) {
        continue;
      }
    }

    SetQuickbarMaskBit(&equipped_low, &equipped_high, index);
  }

  StoreQuickbarItemMasks(item_low, item_high, equipped_low, equipped_high);
  LogMessage(
      kLogDebug,
      "quickbar item masks refreshed item=0x%08X%08X equipped=0x%08X%08X",
      item_high,
      item_low,
      equipped_high,
      equipped_low);
}

void QueueChatLine(const char* text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  pthread_mutex_lock(&g_state.chat_mutex);
  const int32_t sequence = AtomicIncrement(&g_state.chat_sequence);
  const int32_t write_index = static_cast<int32_t>(AtomicGet(&g_state.chat_write_index));
  ChatLineEntry* entry = &g_state.chat_lines[write_index % kChatQueueCapacity];
  entry->sequence = sequence;
  snprintf(entry->text, sizeof(entry->text), "%s", text);
  AtomicSet(&g_state.chat_write_index, (write_index + 1) % kChatQueueCapacity);
  const int32_t count = static_cast<int32_t>(AtomicGet(&g_state.chat_count));
  if (count < kChatQueueCapacity) {
    AtomicSet(&g_state.chat_count, count + 1);
  }
  pthread_mutex_unlock(&g_state.chat_mutex);
}

bool BuildChatPollResponse(const ChatPollRequest& request, uint8_t* out, uint32_t capacity, uint32_t* out_size) {
  if (out == nullptr || out_size == nullptr || capacity < sizeof(ChatPollResponseHeader)) {
    return false;
  }
  ChatPollResponseHeader* response = reinterpret_cast<ChatPollResponseHeader*>(out);
  response->latest_sequence = static_cast<int32_t>(AtomicGet(&g_state.chat_sequence));
  response->line_count = 0;
  uint32_t offset = sizeof(ChatPollResponseHeader);

  pthread_mutex_lock(&g_state.chat_mutex);
  const int32_t latest = static_cast<int32_t>(AtomicGet(&g_state.chat_sequence));
  const int32_t count = static_cast<int32_t>(AtomicGet(&g_state.chat_count));
  const int32_t write = static_cast<int32_t>(AtomicGet(&g_state.chat_write_index));
  response->latest_sequence = latest;

  if (count > 0 && latest > request.after_sequence) {
    int32_t oldest = latest - count + 1;
    if (oldest < 1) {
      oldest = 1;
    }
    int32_t first = request.after_sequence + 1;
    if (first < oldest) {
      first = oldest;
    }
    int32_t max_lines = request.max_lines;
    if (max_lines <= 0 || max_lines > kChatQueueCapacity) {
      max_lines = kChatQueueCapacity;
    }
    if (latest >= first && latest - first + 1 > max_lines) {
      first = latest - max_lines + 1;
    }
    const int32_t oldest_index = (write - count + kChatQueueCapacity) % kChatQueueCapacity;
    for (int32_t sequence = first; sequence <= latest; ++sequence) {
      const int32_t index = (oldest_index + (sequence - oldest)) % kChatQueueCapacity;
      const ChatLineEntry& entry = g_state.chat_lines[index];
      if (entry.sequence != sequence) {
        continue;
      }
      const uint32_t text_length = static_cast<uint32_t>(strnlen(entry.text, sizeof(entry.text)));
      const uint32_t needed = sizeof(ChatPollLineHeader) + text_length;
      if (offset + needed > capacity) {
        break;
      }
      ChatPollLineHeader* line = reinterpret_cast<ChatPollLineHeader*>(out + offset);
      line->sequence = sequence;
      line->text_length = static_cast<int32_t>(text_length);
      offset += sizeof(ChatPollLineHeader);
      memcpy(out + offset, entry.text, text_length);
      offset += text_length;
      ++response->line_count;
    }
  }
  pthread_mutex_unlock(&g_state.chat_mutex);
  *out_size = offset;
  return true;
}

void CaptureQuickbarExec(int32_t panel, int32_t slot_index) {
  TryAdoptQuickbarPanel(static_cast<uint32_t>(panel), slot_index, "quickbar-exec");
}

void CaptureQuickbarSlotDispatch(int32_t slot_ptr) {
  AtomicIncrement(&g_state.quickbar_calls);
  AtomicSet(&g_state.quickbar_slot_ptr, slot_ptr);
  if (slot_ptr != 0) {
    uint8_t raw = 0;
    SafeReadValue(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotTypeOffset, &raw);
    AtomicSet(&g_state.quickbar_slot_type, raw);
  }
  uint32_t panel = 0;
  int32_t index = -1;
  if (TryDeriveQuickbarPanelFromSlot(static_cast<uint32_t>(slot_ptr), &panel, &index)) {
    TryAdoptQuickbarPanel(panel, index, "slot-dispatch");
  }
}

void CaptureChatWindowLog(int32_t, const void* nwn_string) {
  char text[kChatTextCapacity] = {};
  if (SafeReadString(nwn_string, text, sizeof(text))) {
    QueueChatLine(text);
  }
}

extern "C" void QuickbarExecTraceThunk();
extern "C" void QuickbarSlotTraceThunk();
extern "C" void ChatWindowLogTraceThunk();

__attribute__((naked)) void QuickbarExecTraceThunk() {
  asm volatile(
      "pusha\n"
      "movl %%esp, %%esi\n"
      "andl $-16, %%esp\n"
      "subl $8, %%esp\n"
      "movl 40(%%esi), %%eax\n"
      "movl 36(%%esi), %%edx\n"
      "pushl %%eax\n"
      "pushl %%edx\n"
      "call SimKeysLinuxCaptureQuickbarExec\n"
      "movl %%esi, %%esp\n"
      "popa\n"
      "jmp *%0\n"
      :
      : "m"(g_quickbar_exec_gateway));
}

__attribute__((naked)) void QuickbarSlotTraceThunk() {
  asm volatile(
      "pusha\n"
      "movl %%esp, %%esi\n"
      "andl $-16, %%esp\n"
      "subl $12, %%esp\n"
      "movl 36(%%esi), %%eax\n"
      "pushl %%eax\n"
      "call SimKeysLinuxCaptureQuickbarSlotDispatch\n"
      "movl %%esi, %%esp\n"
      "popa\n"
      "jmp *%0\n"
      :
      : "m"(g_quickbar_slot_gateway));
}

__attribute__((naked)) void ChatWindowLogTraceThunk() {
  asm volatile(
      "pusha\n"
      "movl %%esp, %%esi\n"
      "andl $-16, %%esp\n"
      "subl $8, %%esp\n"
      "movl 40(%%esi), %%eax\n"
      "movl 36(%%esi), %%edx\n"
      "pushl %%eax\n"
      "pushl %%edx\n"
      "call SimKeysLinuxCaptureChatWindowLog\n"
      "movl %%esi, %%esp\n"
      "popa\n"
      "jmp *%0\n"
      :
      : "m"(g_chat_log_gateway));
}

bool WriteExecutableMemory(void* destination, const void* source, size_t size) {
  const uintptr_t page = reinterpret_cast<uintptr_t>(destination) & ~(static_cast<uintptr_t>(getpagesize()) - 1u);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(destination) + size + getpagesize() - 1u) &
      ~(static_cast<uintptr_t>(getpagesize()) - 1u);
  if (mprotect(reinterpret_cast<void*>(page), end - page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    return false;
  }
  memcpy(destination, source, size);
  __builtin___clear_cache(static_cast<char*>(destination), static_cast<char*>(destination) + size);
  mprotect(reinterpret_cast<void*>(page), end - page, PROT_READ | PROT_EXEC);
  return true;
}

void* MakeGateway(uint8_t* target, size_t stolen) {
  uint8_t* gateway = static_cast<uint8_t*>(
      mmap(nullptr, stolen + 5, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (gateway == MAP_FAILED) {
    return nullptr;
  }
  memcpy(gateway, target, stolen);
  gateway[stolen] = 0xE9;
  const intptr_t rel = reinterpret_cast<intptr_t>(target + stolen) -
      reinterpret_cast<intptr_t>(gateway + stolen + 5);
  *reinterpret_cast<int32_t*>(gateway + stolen + 1) = static_cast<int32_t>(rel);
  return gateway;
}

bool InstallInlineHook(uint32_t address, size_t stolen, void* thunk, uint8_t* original, void** gateway) {
  uint8_t* target = reinterpret_cast<uint8_t*>(address);
  memcpy(original, target, stolen);
  *gateway = MakeGateway(target, stolen);
  if (*gateway == nullptr) {
    return false;
  }
  uint8_t patch[32] = {};
  patch[0] = 0xE9;
  const intptr_t rel = reinterpret_cast<intptr_t>(thunk) - reinterpret_cast<intptr_t>(target + 5);
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(rel);
  for (size_t i = 5; i < stolen; ++i) {
    patch[i] = 0x90;
  }
  return WriteExecutableMemory(target, patch, stolen);
}

void InstallHooks() {
  if (EnvFlagEnabled("SIMKEYS_LINUX_ENABLE_QUICKBAR_TRACE")) {
    if (AtomicGet(&g_state.quickbar_trace_installed) == 0 &&
        InstallInlineHook(kQuickbarExec, 6, reinterpret_cast<void*>(QuickbarExecTraceThunk), g_quickbar_exec_original, &g_quickbar_exec_gateway)) {
      AtomicSet(&g_state.quickbar_trace_installed, 1);
    }
    if (AtomicGet(&g_state.quickbar_slot_trace_installed) == 0 &&
        InstallInlineHook(kQuickbarSlotDispatch, 6, reinterpret_cast<void*>(QuickbarSlotTraceThunk), g_quickbar_slot_original, &g_quickbar_slot_gateway)) {
      AtomicSet(&g_state.quickbar_slot_trace_installed, 1);
    }
  }
  if (!EnvFlagExplicitlyDisabled("SIMKEYS_LINUX_ENABLE_CHAT_TRACE") &&
      !EnvFlagEnabled("SIMKEYS_LINUX_DISABLE_CHAT_TRACE") &&
      AtomicGet(&g_state.chat_trace_installed) == 0 &&
      InstallInlineHook(kChatWindowLog, 9, reinterpret_cast<void*>(ChatWindowLogTraceThunk), g_chat_log_original, &g_chat_log_gateway)) {
    AtomicSet(&g_state.chat_trace_installed, 1);
  }
}

uint32_t EnsureQuickbarPanel(const char* reason) {
  uint32_t panel = AtomicGet(&g_state.quickbar_this);
  if (!IsQuickbarPanel(panel)) {
    DiscoverQuickbarPanel(reason);
    panel = AtomicGet(&g_state.quickbar_this);
  }
  return IsQuickbarPanel(panel) ? panel : 0;
}

bool CallQuickbarPageSelectDirect(int32_t page_index, int32_t* out_resolved_page) {
  if (page_index < 0 || page_index >= kQuickbarPageCount) {
    errno = EINVAL;
    return false;
  }

  const uint32_t panel = EnsureQuickbarPanel("direct-page-select");
  if (panel == 0) {
    errno = ENOENT;
    return false;
  }

  typedef void (*QuickbarPageSelectFn)(void*, int32_t);
  const QuickbarPageSelectFn fn = NwnFunction<QuickbarPageSelectFn>(kQuickbarPageSelect);
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            fn(reinterpret_cast<void*>(panel), page_index);
          },
          &signal_number)) {
    LogMessage(
        kLogDebug,
        "quickbar page select faulted signal=%d panel=0x%08X page=%d",
        signal_number,
        panel,
        page_index);
    errno = EFAULT;
    return false;
  }

  const int32_t resolved_page = ResolveQuickbarPageIndex(panel);
  if (out_resolved_page != nullptr) {
    *out_resolved_page = resolved_page;
  }
  if (resolved_page >= 0) {
    AtomicSet(&g_state.quickbar_page, resolved_page);
  }
  if (resolved_page != page_index) {
    errno = EINVAL;
    return false;
  }
  return true;
}

int32_t CallQuickbarExecDirect(int32_t slot_index) {
  if (slot_index < 0 || slot_index >= kQuickbarSlotCount) {
    errno = EINVAL;
    return 0;
  }

  const uint32_t panel = EnsureQuickbarPanel("direct-call");
  if (panel == 0) {
    errno = ENOENT;
    return 0;
  }

  const uint32_t enabled = SafeReadPointer32(static_cast<uintptr_t>(panel) + kQuickbarEnabledOffset);
  if (enabled == 0) {
    errno = EAGAIN;
    return 0;
  }

  uint32_t current_page_base = SafeReadPointer32(static_cast<uintptr_t>(panel) + kQuickbarCurrentPageOffset);
  if (current_page_base == 0) {
    const int32_t cached_page = static_cast<int32_t>(AtomicGet(&g_state.quickbar_page));
    if (cached_page >= 0 && cached_page < kQuickbarPageCount) {
      current_page_base = panel + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(cached_page) * kQuickbarPageStride;
    }
  }
  if (current_page_base == 0) {
    errno = EINVAL;
    return 0;
  }

  const uint32_t slot_ptr = current_page_base + static_cast<uint32_t>(slot_index) * kQuickbarSlotStride;
  if (!RangeIsMapped(slot_ptr, kQuickbarSlotTypeOffset + 1, false)) {
    errno = EFAULT;
    return 0;
  }

  typedef int32_t (*QuickbarExecFn)(void*, int32_t);
  const QuickbarExecFn fn = NwnFunction<QuickbarExecFn>(kQuickbarExec);
  int32_t native_rc = 0;
  int signal_number = 0;
  uintptr_t fault_ip = 0;
  uintptr_t fault_address = 0;
  if (!RunWithFaultGuard(
          [&]() {
            native_rc = fn(reinterpret_cast<void*>(panel), slot_index);
          },
          &signal_number,
          &fault_ip,
          &fault_address)) {
    LogMessage(
        kLogDebug,
        "quickbar exec faulted signal=%d ip=0x%08X addr=0x%08X panel=0x%08X slot=%d slotPtr=0x%08X",
        signal_number,
        static_cast<uint32_t>(fault_ip),
        static_cast<uint32_t>(fault_address),
        panel,
        slot_index,
        slot_ptr);
    errno = EFAULT;
    return 0;
  }
  (void)native_rc;

  const int32_t resolved_page = ResolveQuickbarPageIndex(panel);
  if (resolved_page >= 0) {
    AtomicSet(&g_state.quickbar_page, resolved_page);
  }
  AtomicSet(&g_state.quickbar_slot_ptr, static_cast<int32_t>(slot_ptr));
  AtomicSet(&g_state.quickbar_slot, slot_index);
  AtomicSet(&g_state.last_result, 1);
  AtomicSet(&g_state.last_error, 0);
  return 1;
}

int32_t CallQuickbarPageSlotDirect(int32_t page, int32_t slot, int32_t* out_aux_rc, int32_t* out_path) {
  if (out_aux_rc != nullptr) {
    *out_aux_rc = -1;
  }
  if (out_path != nullptr) {
    *out_path = 0;
  }
  if (page < 0 || page >= kQuickbarPageCount || slot < 1 || slot > kQuickbarSlotCount) {
    errno = EINVAL;
    return 0;
  }

  const uint32_t panel = EnsureQuickbarPanel("page-slot-trigger");
  if (panel == 0) {
    errno = ENOENT;
    return 0;
  }

  int32_t original_page = ResolveQuickbarPageIndex(panel);
  if (original_page < 0) {
    const int32_t cached_page = static_cast<int32_t>(AtomicGet(&g_state.quickbar_page));
    if (cached_page >= 0 && cached_page < kQuickbarPageCount) {
      original_page = cached_page;
    }
  }
  if (out_aux_rc != nullptr) {
    *out_aux_rc = original_page;
  }

  const bool restore_needed = original_page >= 0 && original_page != page;
  if (original_page != page) {
    if (!CallQuickbarPageSelectDirect(page, nullptr)) {
      return 0;
    }
  }

  const int32_t rc = CallQuickbarExecDirect(slot - 1);
  const int saved_errno = errno;
  if (rc != 0 && out_path != nullptr) {
    *out_path = page == 0 ? 2 : 3;
  }

  if (rc != 0 && restore_needed) {
    int32_t restored_page = -1;
    if (!CallQuickbarPageSelectDirect(original_page, &restored_page)) {
      return 0;
    }
  } else if (rc == 0) {
    errno = saved_errno;
  }

  return rc;
}

int32_t CallChatSendDirect(const char* text, int32_t mode) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };
  if (text == nullptr || text[0] == '\0') {
    errno = EINVAL;
    return 0;
  }
  NwnStringRef message = {};
  message.text = const_cast<char*>(text);
  message.length = static_cast<int32_t>(strnlen(text, kPendingChatCapacity));
  typedef void (*ChatSendFn)(const void*, int32_t);
  NwnFunction<ChatSendFn>(kChatSend)(&message, mode);
  AtomicSet(&g_state.last_chat_mode, mode);
  AtomicSet(&g_state.last_chat_result, 1);
  AtomicSet(&g_state.last_chat_error, 0);
  return 1;
}

bool AppendMapPinXmlText(char* out, size_t capacity, size_t* offset, const char* text) {
  if (out == nullptr || offset == nullptr || *offset >= capacity) {
    return false;
  }
  const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text != nullptr ? text : "");
  while (*cursor != '\0') {
    const char* replacement = nullptr;
    char single[2] = {static_cast<char>(*cursor), '\0'};
    switch (*cursor) {
      case '&':
        replacement = "&amp;";
        break;
      case '<':
        replacement = "&lt;";
        break;
      case '>':
        replacement = "&gt;";
        break;
      case '"':
        replacement = "&quot;";
        break;
      default:
        if (*cursor < 0x20u && *cursor != '\t') {
          replacement = "?";
        } else {
          replacement = single;
        }
        break;
    }
    const size_t length = strlen(replacement);
    if (*offset + length >= capacity) {
      return false;
    }
    memcpy(out + *offset, replacement, length);
    *offset += length;
    ++cursor;
  }
  out[*offset] = '\0';
  return true;
}

bool AppendMapPinXmlLiteral(char* out, size_t capacity, size_t* offset, const char* text) {
  if (out == nullptr || offset == nullptr || text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  if (*offset + length >= capacity) {
    return false;
  }
  memcpy(out + *offset, text, length);
  *offset += length;
  out[*offset] = '\0';
  return true;
}

bool BuildMapPinXml(float x, float y, const char* text, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0 || text == nullptr || text[0] == '\0' ||
      !IsPlausibleCoordinate(x) || !IsPlausibleCoordinate(y)) {
    errno = EINVAL;
    return false;
  }
  const int written = snprintf(
      out,
      capacity,
      "<pin x=\"%.6f\" y=\"%.6f\" text=\"",
      static_cast<double>(x),
      static_cast<double>(y));
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    errno = ENAMETOOLONG;
    return false;
  }
  size_t offset = static_cast<size_t>(written);
  if (!AppendMapPinXmlText(out, capacity, &offset, text) ||
      !AppendMapPinXmlLiteral(out, capacity, &offset, "\" />")) {
    errno = ENAMETOOLONG;
    return false;
  }
  return true;
}

int32_t CallAddMapPinDirect(float x, float y, const char* text) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };
  struct ClientMessageExtra {
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3;
  };

  char xml[kMapPinXmlCapacity] = {};
  if (!BuildMapPinXml(x, y, text, xml, sizeof(xml))) {
    return 0;
  }

  const uint32_t gui = ReadCurrentGuiPointer();
  if (gui == 0) {
    errno = ENOENT;
    return 0;
  }

  typedef NwnStringRef* (*ConstructStringFn)(NwnStringRef*, const char*);
  typedef void (*ClientMessageDispatchFn)(
      void* gui,
      NwnStringRef* message,
      int32_t opcode,
      int32_t arg0,
      int32_t arg1,
      ClientMessageExtra extra);

  NwnStringRef message = {};
  ClientMessageExtra extra = {};
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            NwnFunction<ConstructStringFn>(kNwnStringConstructFromCString)(&message, xml);
          },
          &signal_number)) {
    errno = EFAULT;
    LogMessage(kLogError, "map pin string construction faulted signal=%d text=%s", signal_number, text);
    return 0;
  }
  if (message.text == nullptr) {
    errno = ENOMEM;
    return 0;
  }

  signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            NwnFunction<ClientMessageDispatchFn>(kChatWindowLog)(
                reinterpret_cast<void*>(gui),
                &message,
                0x80,
                0,
                0,
                extra);
          },
          &signal_number)) {
    errno = EFAULT;
    LogMessage(
        kLogError,
        "map pin dispatch faulted signal=%d gui=0x%08X x=%.3f y=%.3f text=%s",
        signal_number,
        gui,
        static_cast<double>(x),
        static_cast<double>(y),
        text);
    return 0;
  }

  LogMessage(kLogInfo, "map pin dispatched x=%.3f y=%.3f text=%s", static_cast<double>(x), static_cast<double>(y), text);
  return 1;
}

bool SetWalkBypassEnabled(bool enabled) {
  uint8_t* source = reinterpret_cast<uint8_t*>(kWalkNoWalkBlock);
  if (enabled) {
    if (g_walk_no_walk_bypass_installed) {
      AtomicSet(&g_state.walk_no_walk_bypass_enabled, 1);
      return true;
    }
    memcpy(g_walk_no_walk_original, source, 5);
    const uint8_t expected[] = {0x83, 0xEC, 0x20, 0x89, 0xE0};
    // The Linux block entry starts at a different instruction than Windows; do
    // not force the exact bytes because this target remains live-test guarded.
    (void)expected;
    uint8_t patch[5] = {};
    patch[0] = 0xE9;
    *reinterpret_cast<int32_t*>(patch + 1) =
        static_cast<int32_t>(kWalkNoWalkBypassTarget - (kWalkNoWalkBlock + 5));
    if (!WriteExecutableMemory(source, patch, sizeof(patch))) {
      return false;
    }
    g_walk_no_walk_bypass_installed = true;
    AtomicSet(&g_state.walk_no_walk_bypass_enabled, 1);
    return true;
  }

  if (g_walk_no_walk_bypass_installed) {
    WriteExecutableMemory(source, g_walk_no_walk_original, 5);
    memset(g_walk_no_walk_original, 0, sizeof(g_walk_no_walk_original));
    g_walk_no_walk_bypass_installed = false;
  }
  AtomicSet(&g_state.walk_no_walk_bypass_enabled, 0);
  return true;
}

int32_t CallMoveToLocationDirect(float x, float y, float z, int32_t client_side, uint32_t action_object_id, int32_t bypass_no_walk) {
  if (!IsPlausiblePosition(x, y, z)) {
    errno = EINVAL;
    return 0;
  }
  const uint32_t app_object = ReadAppObjectPointer();
  if (app_object == 0) {
    errno = ENOENT;
    return 0;
  }
  bool temporary_bypass = false;
  if (bypass_no_walk && !g_walk_no_walk_bypass_installed) {
    if (!SetWalkBypassEnabled(true)) {
      return 0;
    }
    temporary_bypass = true;
  }
  typedef int32_t (*WalkFn)(void*, float, float, float, int32_t, uint32_t);
  const uint32_t resolved_action = action_object_id != 0 ? action_object_id : kInvalidObjectId;
  int32_t rc = 0;
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            rc = NwnFunction<WalkFn>(kWalkToWaypoint)(
                reinterpret_cast<void*>(app_object),
                x,
                y,
                z,
                client_side ? 1 : 0,
                resolved_action);
          },
          &signal_number)) {
    errno = EFAULT;
    LogMessage(
        kLogError,
        "move-to-location faulted signal=%d wrapper=0x%08X appObject=0x%08X xyz=(%.3f, %.3f, %.3f) clientSide=%d action=0x%08X",
        signal_number,
        kWalkToWaypoint,
        app_object,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
        client_side ? 1 : 0,
        resolved_action);
    if (temporary_bypass) {
      SetWalkBypassEnabled(false);
    }
    return 0;
  }
  if (temporary_bypass) {
    SetWalkBypassEnabled(false);
  }
  return rc;
}

uint32_t ResolveCreatureFromPlayerObject(uint32_t player_object) {
  if (player_object == 0 || !RangeIsMapped(player_object, kPlayerCreatureOffset + sizeof(uint32_t), false)) {
    return 0;
  }
  const uint32_t creature = SafeReadPointer32(static_cast<uintptr_t>(player_object) + kPlayerCreatureOffset);
  return RangeIsMapped(creature, 0x20u, false) ? creature : 0;
}

uint32_t ResolveCurrentPlayerGlobal() {
  const uint32_t player_object = ReadCurrentPlayerGlobalPointer();
  if (ResolveCreatureFromPlayerObject(player_object) == 0) {
    return 0;
  }
  return player_object;
}

uint32_t ResolveCurrentClientPlayer() {
  const uint32_t app_object = ReadAppObjectPointer();
  const uint32_t fallback_player = ResolveCurrentPlayerGlobal();
  if (app_object == 0) {
    return fallback_player;
  }
  const uint32_t object_id = ReadCurrentPlayerObjectId();
  if (!IsValidObjectId(object_id)) {
    return fallback_player;
  }
  typedef void* (*ResolverFn)(void*);
  uint32_t result = 0;
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            result = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                NwnFunction<ResolverFn>(kCurrentClientPlayerResolver)(reinterpret_cast<void*>(app_object))));
          },
          &signal_number)) {
    LogMessage(kLogDebug, "current player resolver faulted signal=%d appObject=0x%08X", signal_number, app_object);
    return fallback_player;
  }
  return result != 0 ? result : fallback_player;
}

uint32_t ResolveCurrentServerCreature(uint32_t* out_game_object) {
  if (out_game_object != nullptr) {
    *out_game_object = 0;
  }
  const uint32_t object_id = ReadCurrentPlayerObjectId();
  const uint32_t server_app = ReadServerAppObjectPointer();
  if (!IsValidObjectId(object_id) || server_app == 0) {
    return ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
  }
  typedef void* (*ObjectByIdFn)(void*, uint32_t);
  void* game_object = nullptr;
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            game_object = NwnFunction<ObjectByIdFn>(kServerObjectByIdResolver)(
                reinterpret_cast<void*>(server_app),
                object_id);
          },
          &signal_number)) {
    LogMessage(
        kLogDebug,
        "server object resolver faulted signal=%d appObject=0x%08X objectId=0x%08X",
        signal_number,
        server_app,
        object_id);
    return ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
  }
  if (game_object == nullptr) {
    return ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
  }
  if (out_game_object != nullptr) {
    *out_game_object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game_object));
  }
  const uint32_t vtable = SafeReadPointer32(reinterpret_cast<uintptr_t>(game_object));
  const uint32_t as_creature_ptr = vtable != 0 ? SafeReadPointer32(vtable + kObjectAsCreatureVtableOffset) : 0;
  if (as_creature_ptr == 0 || !RangeIsExecutable(as_creature_ptr, 1)) {
    return ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
  }
  typedef void* (*AsCreatureFn)(void*);
  uint32_t creature = 0;
  if (!RunWithFaultGuard(
          [&]() {
            creature = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                reinterpret_cast<AsCreatureFn>(as_creature_ptr)(game_object)));
          },
          &signal_number)) {
    LogMessage(
        kLogDebug,
        "as-creature resolver faulted signal=%d gameObject=0x%08X fn=0x%08X",
        signal_number,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game_object)),
        as_creature_ptr);
    return ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
  }
  return creature != 0 ? creature : ResolveCreatureFromPlayerObject(ResolveCurrentClientPlayer());
}

bool BuildCurrentPlayerName(uint32_t client_player, char* out, size_t capacity) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };

  if (out == nullptr || capacity == 0) {
    return false;
  }
  out[0] = '\0';
  if (client_player == 0 ||
      !RangeIsMapped(client_player, kPlayerCreatureOffset + sizeof(uint32_t), false) ||
      ResolveCreatureFromPlayerObject(client_player) == 0) {
    return false;
  }

  NwnStringRef name = {};
#if defined(__i386__)
  void* ignored = nullptr;
  void* const fn = NwnPointer(kPlayerNameBuilder);
  int signal_number = 0;
  if (!RunWithFaultGuard(
          [&]() {
            asm volatile(
                "pushl %[player]\n"
                "pushl %[name]\n"
                "call *%[fn]\n"
                "addl $4, %%esp\n"
                : "=a"(ignored)
                : [fn] "r"(fn), [name] "r"(&name), [player] "r"(reinterpret_cast<void*>(client_player))
                : "ecx", "edx", "memory", "cc");
          },
          &signal_number)) {
    LogMessage(kLogDebug, "player name builder faulted signal=%d player=0x%08X", signal_number, client_player);
    return false;
  }
  (void)ignored;
#else
  return false;
#endif

  const bool ok = SafeReadString(&name, out, capacity) && out[0] != '\0';
  if (name.text != nullptr) {
    typedef void (*DestroyNwnStringFn)(NwnStringRef*, int32_t);
    int destroy_signal = 0;
    if (!RunWithFaultGuard(
            [&]() {
              NwnFunction<DestroyNwnStringFn>(kNwnStringDestroy)(&name, 2);
            },
            &destroy_signal)) {
      LogMessage(kLogDebug, "NWN string destroy faulted signal=%d namePtr=0x%08X", destroy_signal, reinterpret_cast<uint32_t>(name.text));
    }
  }
  return ok;
}

bool BuildCreatureName(uint32_t creature, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return false;
  }
  out[0] = '\0';
  if (creature == 0 || !RangeIsMapped(creature, 0x20u, false)) {
    return false;
  }

  char first[64] = {};
  char last[64] = {};
  SafeReadString(reinterpret_cast<const void*>(static_cast<uintptr_t>(creature) + 0x10u), first, sizeof(first));
  SafeReadString(reinterpret_cast<const void*>(static_cast<uintptr_t>(creature) + 0x18u), last, sizeof(last));
  if (first[0] != '\0' && last[0] != '\0') {
    snprintf(out, capacity, "%s %s", first, last);
  } else if (first[0] != '\0') {
    snprintf(out, capacity, "%s", first);
  } else if (last[0] != '\0') {
    snprintf(out, capacity, "%s", last);
  }
  return out[0] != '\0';
}

bool ReadCreatureHitPoints(
    uint32_t creature,
    int32_t* out_current_hp,
    int32_t* out_max_hp,
    uint32_t* out_current_hp_address,
    uint32_t* out_max_hp_address,
    int32_t* out_error) {
  if (out_current_hp != nullptr) {
    *out_current_hp = 0;
  }
  if (out_max_hp != nullptr) {
    *out_max_hp = 0;
  }
  if (out_current_hp_address != nullptr) {
    *out_current_hp_address = 0;
  }
  if (out_max_hp_address != nullptr) {
    *out_max_hp_address = 0;
  }
  if (out_error != nullptr) {
    *out_error = kErrNotFound;
  }

  if (creature == 0 ||
      !RangeIsMapped(creature, kCreatureMaxHitPointsOffset + sizeof(int16_t), false)) {
    return false;
  }

  int16_t current_hp = 0;
  int16_t max_hp = 0;
  const uintptr_t current_hp_address =
      static_cast<uintptr_t>(creature) + kCreatureCurrentHitPointsOffset;
  const uintptr_t max_hp_address =
      static_cast<uintptr_t>(creature) + kCreatureMaxHitPointsOffset;
  if (!SafeReadValue(current_hp_address, &current_hp) ||
      !SafeReadValue(max_hp_address, &max_hp)) {
    if (out_error != nullptr) {
      *out_error = kErrInvalidData;
    }
    return false;
  }

  if (out_current_hp != nullptr) {
    *out_current_hp = static_cast<int32_t>(current_hp);
  }
  if (out_max_hp != nullptr) {
    *out_max_hp = static_cast<int32_t>(max_hp);
  }
  if (out_current_hp_address != nullptr) {
    *out_current_hp_address = static_cast<uint32_t>(current_hp_address);
  }
  if (out_max_hp_address != nullptr) {
    *out_max_hp_address = static_cast<uint32_t>(max_hp_address);
  }
  if (out_error != nullptr) {
    *out_error = kErrSuccess;
  }
  return true;
}

bool RefreshCharacterIdentity(int32_t* out_error) {
  uint32_t game_object = 0;
  const uint32_t client_player = ResolveCurrentClientPlayer();
  uint32_t creature = ResolveCurrentServerCreature(&game_object);
  char name[kCharacterNameCapacity] = {};
  int32_t position_valid = 0;
  float position_x = 0.0f;
  float position_y = 0.0f;
  float position_z = 0.0f;

  const uint32_t player_creature = ResolveCreatureFromPlayerObject(client_player);
  if (creature == 0) {
    creature = player_creature;
  }

  if (creature != 0) {
    BuildCreatureName(creature, name, sizeof(name));
  }

  if (name[0] == '\0' && client_player != 0) {
    BuildCurrentPlayerName(client_player, name, sizeof(name));
  }

  if (name[0] == '\0' && game_object != 0) {
    const uint32_t vtable = SafeReadPointer32(game_object);
    const uint32_t first_name_fn = vtable != 0 ? SafeReadPointer32(vtable + 0x98u) : 0;
    const uint32_t last_name_fn = vtable != 0 ? SafeReadPointer32(vtable + 0x9Cu) : 0;
    char first[64] = {};
    char last[64] = {};
    if (first_name_fn != 0 && RangeIsExecutable(first_name_fn, 1)) {
      typedef void* (*NameFn)(void*);
      void* first_obj = nullptr;
      int signal_number = 0;
      if (RunWithFaultGuard(
              [&]() {
                first_obj = reinterpret_cast<NameFn>(first_name_fn)(reinterpret_cast<void*>(game_object));
              },
              &signal_number)) {
        SafeReadString(first_obj, first, sizeof(first));
      } else {
        LogMessage(kLogDebug, "first-name resolver faulted signal=%d gameObject=0x%08X fn=0x%08X", signal_number, game_object, first_name_fn);
      }
    }
    if (last_name_fn != 0 && RangeIsExecutable(last_name_fn, 1)) {
      typedef void* (*NameFn)(void*);
      void* last_obj = nullptr;
      int signal_number = 0;
      if (RunWithFaultGuard(
              [&]() {
                last_obj = reinterpret_cast<NameFn>(last_name_fn)(reinterpret_cast<void*>(game_object));
              },
              &signal_number)) {
        SafeReadString(last_obj, last, sizeof(last));
      } else {
        LogMessage(kLogDebug, "last-name resolver faulted signal=%d gameObject=0x%08X fn=0x%08X", signal_number, game_object, last_name_fn);
      }
    }
    if (first[0] != '\0' && last[0] != '\0') {
      snprintf(name, sizeof(name), "%s %s", first, last);
    } else if (first[0] != '\0') {
      snprintf(name, sizeof(name), "%s", first);
    }
  }

  if (client_player != 0) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (SafeReadValue(client_player + kCurrentPlayerPositionOffset + 0, &x) &&
        SafeReadValue(client_player + kCurrentPlayerPositionOffset + 4, &y) &&
        SafeReadValue(client_player + kCurrentPlayerPositionOffset + 8, &z) &&
        IsPlausiblePosition(x, y, z)) {
      position_valid = 1;
      position_x = x;
      position_y = y;
      position_z = z;
    }
  }

  pthread_mutex_lock(&g_state.overlay_mutex);
  snprintf(g_state.character_name, sizeof(g_state.character_name), "%s", name);
  g_state.position_valid = position_valid;
  g_state.position_x = position_x;
  g_state.position_y = position_y;
  g_state.position_z = position_z;
  pthread_mutex_unlock(&g_state.overlay_mutex);
  AtomicSet(&g_state.player_object, static_cast<int32_t>(client_player != 0 ? client_player : game_object));
  AtomicSet(&g_state.player_creature, static_cast<int32_t>(creature));
  const int32_t err = (name[0] != '\0' || client_player != 0 || creature != 0) ? kErrSuccess : kErrNotFound;
  AtomicSet(&g_state.identity_error, err);
  AtomicIncrement(&g_state.identity_refresh_count);
  UpdateQuickbarItemMasks();
  if (out_error != nullptr) {
    *out_error = err;
  }
  return err == kErrSuccess;
}

bool IsLikelyClientPlayer(uint32_t player_object) {
  return player_object != 0 && ResolveCreatureFromPlayerObject(player_object) != 0;
}

bool ReadClientDefensiveCastingModeFlag(uint32_t client_player, int32_t* out_active) {
  if (out_active == nullptr) {
    return false;
  }
  *out_active = 0;

  if (!IsLikelyClientPlayer(client_player)) {
    return false;
  }

  uint32_t value = 0;
  if (!SafeReadValue(
          static_cast<uintptr_t>(client_player) + kClientDefensiveCastingStateOffset,
          &value)) {
    return false;
  }
  if (value > 1u) {
    return false;
  }

  *out_active = static_cast<int32_t>(value);
  return true;
}

bool EnsureCurrentClientPlayerOnMain(uint32_t* out_player_object, int32_t* out_error) {
  if (out_player_object == nullptr) {
    if (out_error != nullptr) {
      *out_error = kErrInvalidParameter;
    }
    return false;
  }
  *out_player_object = 0;

  uint32_t player_object = static_cast<uint32_t>(AtomicGet(&g_state.player_object));
  if (!IsLikelyClientPlayer(player_object)) {
    int32_t identity_error = kErrSuccess;
    RefreshCharacterIdentity(&identity_error);
    player_object = static_cast<uint32_t>(AtomicGet(&g_state.player_object));
  }
  if (!IsLikelyClientPlayer(player_object)) {
    player_object = ResolveCurrentClientPlayer();
    if (IsLikelyClientPlayer(player_object)) {
      AtomicSet(&g_state.player_object, static_cast<int32_t>(player_object));
      const uint32_t creature = ResolveCreatureFromPlayerObject(player_object);
      if (creature != 0) {
        AtomicSet(&g_state.player_creature, static_cast<int32_t>(creature));
      }
    }
  }
  if (!IsLikelyClientPlayer(player_object)) {
    if (out_error != nullptr) {
      *out_error = kErrNotFound;
    }
    return false;
  }

  *out_player_object = player_object;
  if (out_error != nullptr) {
    *out_error = kErrSuccess;
  }
  return true;
}

bool TriggerToggleModeInputOnMain(int32_t mode, uint32_t target_object_id, int32_t* out_active, int32_t* out_error) {
  if (out_active != nullptr) {
    *out_active = -1;
  }

  uint32_t player_object = 0;
  int32_t last_error = kErrSuccess;
  if (!EnsureCurrentClientPlayerOnMain(&player_object, &last_error)) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    return false;
  }

  typedef void (*ToggleModeInputFn)(void*, int32_t, uint32_t);
  const uint32_t resolved_target = target_object_id != 0 ? target_object_id : kInvalidObjectId;
  int signal_number = 0;
  uintptr_t instruction_pointer = 0;
  uintptr_t fault_address = 0;
  if (!RunWithFaultGuard(
          [&]() {
            NwnFunction<ToggleModeInputFn>(kToggleModeInput)(
                reinterpret_cast<void*>(player_object),
                mode,
                resolved_target);
          },
          &signal_number,
          &instruction_pointer,
          &fault_address)) {
    if (out_error != nullptr) {
      *out_error = kErrInvalidData;
    }
    LogMessage(
        kLogError,
        "toggle mode input failed mode=%d player=0x%08X target=0x%08X signal=%d ip=0x%08lX fault=0x%08lX",
        mode,
        player_object,
        resolved_target,
        signal_number,
        static_cast<unsigned long>(instruction_pointer),
        static_cast<unsigned long>(fault_address));
    return false;
  }

  if (out_error != nullptr) {
    *out_error = kErrSuccess;
  }
  LogMessage(
      kLogInfo,
      "toggle mode input dispatched mode=%d player=0x%08X target=0x%08X active=unknown",
      mode,
      player_object,
      resolved_target);
  return true;
}

bool SetActionModeOnMain(int32_t mode, int32_t enabled, int32_t* out_active, int32_t* out_error) {
  if (out_active != nullptr) {
    *out_active = 0;
  }
  if (mode < 0 || mode > 12) {
    if (out_error != nullptr) {
      *out_error = kErrInvalidParameter;
    }
    return false;
  }
  if (mode == kActionModeDefensiveCast) {
    if (!enabled) {
      if (out_error != nullptr) {
        *out_error = kErrNotSupported;
      }
      return false;
    }
    return TriggerToggleModeInputOnMain(mode, kInvalidObjectId, out_active, out_error);
  }
  uint32_t game_object = 0;
  uint32_t creature = ResolveCurrentServerCreature(&game_object);
  if (creature == 0) {
    if (out_error != nullptr) {
      *out_error = kErrNotFound;
    }
    return false;
  }
  typedef void (*SetModeFn)(void*, uint8_t, int32_t);
  typedef int32_t (*GetModeFn)(void*, uint8_t);
  int32_t active = 0;
  int signal_number = 0;
  uintptr_t instruction_pointer = 0;
  uintptr_t fault_address = 0;
  if (!RunWithFaultGuard(
          [&]() {
            NwnFunction<SetModeFn>(kSetActionMode)(
                reinterpret_cast<void*>(creature),
                static_cast<uint8_t>(mode),
                enabled ? 1 : 0);
            active = NwnFunction<GetModeFn>(kGetActionMode)(
                reinterpret_cast<void*>(creature),
                static_cast<uint8_t>(mode));
          },
          &signal_number,
          &instruction_pointer,
          &fault_address)) {
    if (out_error != nullptr) {
      *out_error = kErrInvalidData;
    }
    LogMessage(
        kLogError,
        "set action mode failed mode=%d enabled=%d creature=0x%08X signal=%d ip=0x%08lX fault=0x%08lX",
        mode,
        enabled ? 1 : 0,
        creature,
        signal_number,
        static_cast<unsigned long>(instruction_pointer),
        static_cast<unsigned long>(fault_address));
    return false;
  }
  if (out_active != nullptr) {
    *out_active = active;
  }
  if (out_error != nullptr) {
    *out_error = kErrSuccess;
  }
  return true;
}

int HexDigitValue(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return 10 + (value - 'a');
  }
  if (value >= 'A' && value <= 'F') {
    return 10 + (value - 'A');
  }
  return -1;
}

bool TryParseOverlayLineColor(const char* text, uint32_t* out_color) {
  if (text == nullptr || text[0] != kOverlayLineColorMarker) {
    return false;
  }
  for (int index = 1; index < kOverlayLineColorMarkerLength; ++index) {
    if (text[index] == '\0') {
      return false;
    }
  }
  if (text[7] != ';') {
    return false;
  }

  uint32_t color = 0;
  for (int index = 0; index < 6; ++index) {
    const int digit = HexDigitValue(text[index + 1]);
    if (digit < 0) {
      return false;
    }
    color = (color << 4) | static_cast<uint32_t>(digit);
  }

  if (out_color != nullptr) {
    *out_color = color & 0xFFFFFFu;
  }
  return true;
}

const char* OverlayVisibleTextAfterControls(const char* text) {
  const char prefix[] = "controls;";
  if (text == nullptr || text[0] != kOverlayControlMarker ||
      strncmp(text + 1, prefix, sizeof(prefix) - 1) != 0) {
    return text != nullptr ? text : "";
  }

  const char* line_end = text + 1 + (sizeof(prefix) - 1);
  while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
    ++line_end;
  }
  if (*line_end == '\r') {
    ++line_end;
    if (*line_end == '\n') {
      ++line_end;
    }
  } else if (*line_end == '\n') {
    ++line_end;
  }
  return line_end;
}

void ParseOverlayControls(OverlayRecord* record, const char** visible_text) {
  record->control_count = 0;
  const char* text = record->text;
  const char prefix[] = "controls;";
  if (text[0] != kOverlayControlMarker || strncmp(text + 1, prefix, sizeof(prefix) - 1) != 0) {
    *visible_text = text;
    return;
  }

  const char* cursor = text + 1 + (sizeof(prefix) - 1);
  const char* line_end = cursor;
  while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
    ++line_end;
  }

  int count = 0;
  while (cursor < line_end && count < kOverlayMaxControls) {
    const char* semi = static_cast<const char*>(memchr(cursor, ';', static_cast<size_t>(line_end - cursor)));
    const char* part_end = semi != nullptr ? semi : line_end;
    const size_t part_len = static_cast<size_t>(part_end - cursor);
    char part[128] = {};
    snprintf(part, sizeof(part), "%.*s", static_cast<int>(part_len), cursor);
    char* first = strchr(part, '|');
    char* second = first != nullptr ? strchr(first + 1, '|') : nullptr;
    if (first != nullptr && second != nullptr) {
      *first = '\0';
      *second = '\0';
      OverlayControlButton& button = record->controls[count++];
      snprintf(button.script_id, sizeof(button.script_id), "%s", part);
      snprintf(button.label, sizeof(button.label), "%s", first + 1);
      button.enabled = atoi(second + 1) ? 1 : 0;
    }
    if (semi == nullptr) {
      break;
    }
    cursor = semi + 1;
  }
  record->control_count = count;
  *visible_text = OverlayVisibleTextAfterControls(text);
}

int OverlayFontScale(int font_size) {
  if (font_size <= 0) {
    font_size = 16;
  }
  if (font_size > 72) {
    font_size = 72;
  }
  int scale = (font_size + 5) / 8;
  if (scale < 1) {
    scale = 1;
  }
  if (scale > 8) {
    scale = 8;
  }
  return scale;
}

int OverlayTextAdvance(int font_size) {
  return 6 * OverlayFontScale(font_size);
}

int OverlayGlyphHeight(int font_size) {
  return 7 * OverlayFontScale(font_size);
}

int OverlayLineHeight(int font_size) {
  return OverlayGlyphHeight(font_size) + 4;
}

int OverlayHeaderHeight(int font_size) {
  return OverlayGlyphHeight(font_size) + kOverlayTextPadding * 2;
}

int OverlayControlAreaWidth(int control_count) {
  if (control_count <= 0) {
    return 0;
  }
  return kOverlayControlPadding * 2 +
      control_count * kOverlayControlButtonSize +
      (control_count - 1) * kOverlayControlGap;
}

uint32_t DecodeOverlayUtf8Codepoint(const char** cursor, const char* end);

void MeasureOverlayText(const char* text, int font_size, int* out_width, int* out_height) {
  int longest = 0;
  int current = 0;
  int lines = 1;
  bool line_start = true;
  const int advance = OverlayTextAdvance(font_size);
  const char* cursor = text != nullptr ? text : "";
  while (*cursor != '\0') {
    if (line_start && TryParseOverlayLineColor(cursor, nullptr)) {
      cursor += kOverlayLineColorMarkerLength;
      continue;
    }
    if (*cursor == '\r' || *cursor == '\n') {
      if (current > longest) {
        longest = current;
      }
      current = 0;
      ++lines;
      if (*cursor == '\r' && cursor[1] == '\n') {
        cursor += 2;
      } else {
        ++cursor;
      }
      line_start = true;
      continue;
    }
    DecodeOverlayUtf8Codepoint(&cursor, nullptr);
    current += advance;
    line_start = false;
  }
  if (current > longest) {
    longest = current;
  }
  if (out_width != nullptr) {
    *out_width = kOverlayTextPadding * 2 + longest;
  }
  if (out_height != nullptr) {
    const int header_height = OverlayHeaderHeight(font_size);
    if (lines <= 1) {
      *out_height = header_height;
    } else {
      *out_height = header_height +
          kOverlayTextPadding +
          (lines - 1) * OverlayLineHeight(font_size) +
          kOverlayTextPadding;
    }
  }
}

bool StoreOverlayText(const OverlayTextRequestHeader& request, const char* text, int32_t* out_width, int32_t* out_height) {
  pthread_mutex_lock(&g_state.overlay_mutex);
  int index = -1;
  for (int i = 0; i < kMaxOverlays; ++i) {
    if (g_state.overlays[i].active && g_state.overlays[i].id == request.id) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    for (int i = 0; i < kMaxOverlays; ++i) {
      if (!g_state.overlays[i].active) {
        index = i;
        break;
      }
    }
  }
  if (index < 0) {
    pthread_mutex_unlock(&g_state.overlay_mutex);
    return false;
  }
  OverlayRecord& record = g_state.overlays[index];
  memset(&record, 0, sizeof(record));
  record.active = 1;
  record.id = request.id;
  record.position = request.position;
  record.offset_x = request.offset_x;
  record.offset_y = request.offset_y;
  record.font_size = request.font_size > 0 ? request.font_size : 16;
  record.color_rgb = request.color_rgb & 0xFFFFFFu;
  snprintf(record.text, sizeof(record.text), "%s", text != nullptr ? text : "");
  const char* visible = record.text;
  ParseOverlayControls(&record, &visible);
  int text_width = 0;
  int text_height = 0;
  const bool has_panel = visible != nullptr && visible[0] != '\0';
  if (has_panel) {
    MeasureOverlayText(visible, record.font_size, &text_width, &text_height);
  }
  const int control_width = OverlayControlAreaWidth(record.control_count);
  const int control_height = record.control_count > 0
      ? kOverlayControlPadding * 2 + kOverlayControlButtonSize
      : 0;
  record.width = text_width > control_width ? text_width : control_width;
  record.height = text_height + control_height;
  if (record.width < 8) {
    record.width = 8;
  }
  if (record.height < 8) {
    record.height = 8;
  }
  if (record.control_count > 0) {
    const int min_width = OverlayControlAreaWidth(record.control_count);
    if (record.width < min_width) {
      record.width = min_width;
    }
  }
  if (record.width > kOverlayMaxDimension) {
    record.width = kOverlayMaxDimension;
  }
  if (record.height > kOverlayMaxDimension) {
    record.height = kOverlayMaxDimension;
  }
  if (out_width != nullptr) {
    *out_width = record.width;
  }
  if (out_height != nullptr) {
    *out_height = record.height;
  }
  int count = 0;
  for (int i = 0; i < kMaxOverlays; ++i) {
    if (g_state.overlays[i].active) {
      ++count;
    }
  }
  AtomicSet(&g_state.overlay_count, count);
  pthread_mutex_unlock(&g_state.overlay_mutex);
  return true;
}

bool ClearOverlayById(int32_t id) {
  pthread_mutex_lock(&g_state.overlay_mutex);
  for (int i = 0; i < kMaxOverlays; ++i) {
    if (g_state.overlays[i].active && g_state.overlays[i].id == id) {
      memset(&g_state.overlays[i], 0, sizeof(g_state.overlays[i]));
    }
  }
  int count = 0;
  for (int i = 0; i < kMaxOverlays; ++i) {
    if (g_state.overlays[i].active) {
      ++count;
    }
  }
  AtomicSet(&g_state.overlay_count, count);
  pthread_mutex_unlock(&g_state.overlay_mutex);
  return true;
}

bool ClearAllOverlays() {
  pthread_mutex_lock(&g_state.overlay_mutex);
  memset(g_state.overlays, 0, sizeof(g_state.overlays));
  AtomicSet(&g_state.overlay_count, 0);
  pthread_mutex_unlock(&g_state.overlay_mutex);
  return true;
}

template <typename T>
bool ResolveGraphicsSymbol(T* out, const char* name) {
  void* symbol = dlsym(RTLD_DEFAULT, name);
  if (symbol == nullptr) {
    if (g_libgl_handle == nullptr) {
      g_libgl_handle = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (g_libgl_handle != nullptr) {
      symbol = dlsym(g_libgl_handle, name);
    }
  }
  *out = reinterpret_cast<T>(symbol);
  return *out != nullptr;
}

bool ResolveGraphicsApi() {
  if (g_graphics_ready) {
    return true;
  }
  if (g_graphics_failed) {
    return false;
  }

  bool ok = true;
  ok = ResolveGraphicsSymbol(&g_graphics.glGetIntegerv, "glGetIntegerv") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glIsEnabled, "glIsEnabled") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glDisable, "glDisable") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glEnable, "glEnable") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glBlendFunc, "glBlendFunc") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glDepthMask, "glDepthMask") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glColor3f, "glColor3f") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glMatrixMode, "glMatrixMode") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glPushMatrix, "glPushMatrix") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glLoadIdentity, "glLoadIdentity") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glPopMatrix, "glPopMatrix") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glBindTexture, "glBindTexture") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glRasterPos2f, "glRasterPos2f") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glDrawPixels, "glDrawPixels") && ok;
  ok = ResolveGraphicsSymbol(&g_graphics.glPixelStorei, "glPixelStorei") && ok;

  if (!ok) {
    g_graphics_failed = 1;
    AtomicSet(&g_state.overlay_last_error, ENOSYS);
    LogMessage(kLogError, "could not resolve required OpenGL symbols for overlay rendering");
    return false;
  }

  g_graphics_ready = 1;
  return true;
}

void TryInitXThreads() {
  typedef int (*XInitThreadsFn)();
  XInitThreadsFn init_threads = reinterpret_cast<XInitThreadsFn>(dlsym(RTLD_DEFAULT, "XInitThreads"));
  if (init_threads == nullptr) {
    void* x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
    if (x11 != nullptr) {
      init_threads = reinterpret_cast<XInitThreadsFn>(dlsym(x11, "XInitThreads"));
    }
  }
  if (init_threads != nullptr) {
    init_threads();
  }
}


char UpperAscii(char value) {
  if (value >= 'a' && value <= 'z') {
    return static_cast<char>(value - ('a' - 'A'));
  }
  return value;
}

enum OverlayAccent {
  kOverlayAccentNone = 0,
  kOverlayAccentAcute,
  kOverlayAccentGrave,
  kOverlayAccentCircumflex,
  kOverlayAccentTilde,
  kOverlayAccentDiaeresis,
  kOverlayAccentRing,
  kOverlayAccentCedilla,
  kOverlayAccentSlash,
};

bool OverlayHasByte(const unsigned char* cursor, const unsigned char* end, int offset) {
  if (end != nullptr && cursor + offset >= end) {
    return false;
  }
  return cursor[offset] != '\0';
}

uint32_t DecodeOverlayUtf8Codepoint(const char** cursor, const char* end) {
  const unsigned char* input = reinterpret_cast<const unsigned char*>(*cursor);
  const unsigned char* input_end = reinterpret_cast<const unsigned char*>(end);
  if (input == nullptr || (input_end != nullptr && input >= input_end) || *input == '\0') {
    return 0;
  }

  const unsigned char first = input[0];
  if (first < 0x80u) {
    *cursor = reinterpret_cast<const char*>(input + 1);
    return static_cast<uint32_t>(first);
  }

  int extra = 0;
  uint32_t codepoint = 0;
  uint32_t minimum = 0;
  if (first >= 0xC2u && first <= 0xDFu) {
    extra = 1;
    codepoint = first & 0x1Fu;
    minimum = 0x80u;
  } else if (first >= 0xE0u && first <= 0xEFu) {
    extra = 2;
    codepoint = first & 0x0Fu;
    minimum = 0x800u;
  } else if (first >= 0xF0u && first <= 0xF4u) {
    extra = 3;
    codepoint = first & 0x07u;
    minimum = 0x10000u;
  } else {
    *cursor = reinterpret_cast<const char*>(input + 1);
    return '?';
  }

  for (int index = 1; index <= extra; ++index) {
    if (!OverlayHasByte(input, input_end, index) || (input[index] & 0xC0u) != 0x80u) {
      *cursor = reinterpret_cast<const char*>(input + 1);
      return '?';
    }
    codepoint = (codepoint << 6) | static_cast<uint32_t>(input[index] & 0x3Fu);
  }

  *cursor = reinterpret_cast<const char*>(input + extra + 1);
  if (codepoint < minimum ||
      codepoint > 0x10FFFFu ||
      (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
    return '?';
  }
  return codepoint;
}

void OverlayGlyphForCodepoint(uint32_t codepoint, char* out_value, int* out_accent) {
  char value = '?';
  int accent = kOverlayAccentNone;
  switch (codepoint) {
    case 0x00C0u: case 0x00E0u: value = 'A'; accent = kOverlayAccentGrave; break;
    case 0x00C1u: case 0x00E1u: value = 'A'; accent = kOverlayAccentAcute; break;
    case 0x00C2u: case 0x00E2u: value = 'A'; accent = kOverlayAccentCircumflex; break;
    case 0x00C3u: case 0x00E3u: value = 'A'; accent = kOverlayAccentTilde; break;
    case 0x00C4u: case 0x00E4u: value = 'A'; accent = kOverlayAccentDiaeresis; break;
    case 0x00C5u: case 0x00E5u: value = 'A'; accent = kOverlayAccentRing; break;
    case 0x00C7u: case 0x00E7u: value = 'C'; accent = kOverlayAccentCedilla; break;
    case 0x00C8u: case 0x00E8u: value = 'E'; accent = kOverlayAccentGrave; break;
    case 0x00C9u: case 0x00E9u: value = 'E'; accent = kOverlayAccentAcute; break;
    case 0x00CAu: case 0x00EAu: value = 'E'; accent = kOverlayAccentCircumflex; break;
    case 0x00CBu: case 0x00EBu: value = 'E'; accent = kOverlayAccentDiaeresis; break;
    case 0x00CCu: case 0x00ECu: value = 'I'; accent = kOverlayAccentGrave; break;
    case 0x00CDu: case 0x00EDu: value = 'I'; accent = kOverlayAccentAcute; break;
    case 0x00CEu: case 0x00EEu: value = 'I'; accent = kOverlayAccentCircumflex; break;
    case 0x00CFu: case 0x00EFu: value = 'I'; accent = kOverlayAccentDiaeresis; break;
    case 0x00D1u: case 0x00F1u: value = 'N'; accent = kOverlayAccentTilde; break;
    case 0x00D2u: case 0x00F2u: value = 'O'; accent = kOverlayAccentGrave; break;
    case 0x00D3u: case 0x00F3u: value = 'O'; accent = kOverlayAccentAcute; break;
    case 0x00D4u: case 0x00F4u: value = 'O'; accent = kOverlayAccentCircumflex; break;
    case 0x00D5u: case 0x00F5u: value = 'O'; accent = kOverlayAccentTilde; break;
    case 0x00D6u: case 0x00F6u: value = 'O'; accent = kOverlayAccentDiaeresis; break;
    case 0x00D8u: case 0x00F8u: value = 'O'; accent = kOverlayAccentSlash; break;
    case 0x00D9u: case 0x00F9u: value = 'U'; accent = kOverlayAccentGrave; break;
    case 0x00DAu: case 0x00FAu: value = 'U'; accent = kOverlayAccentAcute; break;
    case 0x00DBu: case 0x00FBu: value = 'U'; accent = kOverlayAccentCircumflex; break;
    case 0x00DCu: case 0x00FCu: value = 'U'; accent = kOverlayAccentDiaeresis; break;
    case 0x00DDu: case 0x00FDu: value = 'Y'; accent = kOverlayAccentAcute; break;
    case 0x00FFu: value = 'Y'; accent = kOverlayAccentDiaeresis; break;
    case 0x00C6u: case 0x00E6u: value = 'A'; break;
    case 0x00DEu: case 0x00FEu: value = 'P'; break;
    case 0x00DFu: value = 'S'; break;
    case 0x2018u: case 0x2019u: value = '\''; break;
    case 0x201Cu: case 0x201Du: value = '"'; break;
    case 0x2013u: case 0x2014u: value = '-'; break;
    default:
      if (codepoint < 0x80u) {
        value = static_cast<char>(codepoint);
      }
      break;
  }
  if (out_value != nullptr) {
    *out_value = value;
  }
  if (out_accent != nullptr) {
    *out_accent = accent;
  }
}

const uint8_t* OverlayGlyphRows(uint32_t codepoint, int* out_accent) {
  static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
  static const uint8_t question[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
  char value = '?';
  int accent = kOverlayAccentNone;
  OverlayGlyphForCodepoint(codepoint, &value, &accent);
  if (out_accent != nullptr) {
    *out_accent = accent;
  }
  const char ch = UpperAscii(value);
  switch (ch) {
    case 'A': { static const uint8_t rows[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}; return rows; }
    case 'B': { static const uint8_t rows[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}; return rows; }
    case 'C': { static const uint8_t rows[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}; return rows; }
    case 'D': { static const uint8_t rows[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}; return rows; }
    case 'E': { static const uint8_t rows[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}; return rows; }
    case 'F': { static const uint8_t rows[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}; return rows; }
    case 'G': { static const uint8_t rows[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}; return rows; }
    case 'H': { static const uint8_t rows[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}; return rows; }
    case 'I': { static const uint8_t rows[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}; return rows; }
    case 'J': { static const uint8_t rows[7] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}; return rows; }
    case 'K': { static const uint8_t rows[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}; return rows; }
    case 'L': { static const uint8_t rows[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}; return rows; }
    case 'M': { static const uint8_t rows[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}; return rows; }
    case 'N': { static const uint8_t rows[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}; return rows; }
    case 'O': { static const uint8_t rows[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}; return rows; }
    case 'P': { static const uint8_t rows[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}; return rows; }
    case 'Q': { static const uint8_t rows[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}; return rows; }
    case 'R': { static const uint8_t rows[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}; return rows; }
    case 'S': { static const uint8_t rows[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}; return rows; }
    case 'T': { static const uint8_t rows[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}; return rows; }
    case 'U': { static const uint8_t rows[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}; return rows; }
    case 'V': { static const uint8_t rows[7] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}; return rows; }
    case 'W': { static const uint8_t rows[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}; return rows; }
    case 'X': { static const uint8_t rows[7] = {0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11}; return rows; }
    case 'Y': { static const uint8_t rows[7] = {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04}; return rows; }
    case 'Z': { static const uint8_t rows[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}; return rows; }
    case '0': { static const uint8_t rows[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}; return rows; }
    case '1': { static const uint8_t rows[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}; return rows; }
    case '2': { static const uint8_t rows[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}; return rows; }
    case '3': { static const uint8_t rows[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}; return rows; }
    case '4': { static const uint8_t rows[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}; return rows; }
    case '5': { static const uint8_t rows[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}; return rows; }
    case '6': { static const uint8_t rows[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}; return rows; }
    case '7': { static const uint8_t rows[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}; return rows; }
    case '8': { static const uint8_t rows[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}; return rows; }
    case '9': { static const uint8_t rows[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}; return rows; }
    case ':': { static const uint8_t rows[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}; return rows; }
    case '.': { static const uint8_t rows[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}; return rows; }
    case ',': { static const uint8_t rows[7] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}; return rows; }
    case '-': { static const uint8_t rows[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}; return rows; }
    case '+': { static const uint8_t rows[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}; return rows; }
    case '/': { static const uint8_t rows[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}; return rows; }
    case '\\': { static const uint8_t rows[7] = {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}; return rows; }
    case '[': { static const uint8_t rows[7] = {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}; return rows; }
    case ']': { static const uint8_t rows[7] = {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}; return rows; }
    case '(': { static const uint8_t rows[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}; return rows; }
    case ')': { static const uint8_t rows[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}; return rows; }
    case '!': { static const uint8_t rows[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}; return rows; }
    case '\'': { static const uint8_t rows[7] = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}; return rows; }
    case '"': { static const uint8_t rows[7] = {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}; return rows; }
    case '#': { static const uint8_t rows[7] = {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}; return rows; }
    case '%': { static const uint8_t rows[7] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}; return rows; }
    case '&': { static const uint8_t rows[7] = {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}; return rows; }
    case '*': { static const uint8_t rows[7] = {0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00}; return rows; }
    case '=': { static const uint8_t rows[7] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}; return rows; }
    case '<': { static const uint8_t rows[7] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}; return rows; }
    case '>': { static const uint8_t rows[7] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}; return rows; }
    case ' ': return space;
    default: return question;
  }
}

void PutOverlayPixel(uint8_t* pixels, int width, int height, int x, int y, uint32_t rgb, uint8_t alpha) {
  if (pixels == nullptr || x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  const int row = height - 1 - y;
  const int offset = (row * width + x) * 4;
  pixels[offset + 0] = static_cast<uint8_t>((rgb >> 16) & 0xFF);
  pixels[offset + 1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);
  pixels[offset + 2] = static_cast<uint8_t>(rgb & 0xFF);
  pixels[offset + 3] = alpha;
}

void FillOverlayRect(uint8_t* pixels, int width, int height, int x, int y, int w, int h, uint32_t rgb, uint8_t alpha) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      PutOverlayPixel(pixels, width, height, xx, yy, rgb, alpha);
    }
  }
}

void DrawOverlayRectOutline(uint8_t* pixels, int width, int height, int x, int y, int w, int h, uint32_t rgb, uint8_t alpha) {
  FillOverlayRect(pixels, width, height, x, y, w, 1, rgb, alpha);
  FillOverlayRect(pixels, width, height, x, y + h - 1, w, 1, rgb, alpha);
  FillOverlayRect(pixels, width, height, x, y, 1, h, rgb, alpha);
  FillOverlayRect(pixels, width, height, x + w - 1, y, 1, h, rgb, alpha);
}

void DrawOverlayAccent(uint8_t* pixels, int width, int height, int x, int y, int accent, int scale, uint32_t rgb) {
  if (accent == kOverlayAccentNone) {
    return;
  }
  if (scale < 1) {
    scale = 1;
  }

  switch (accent) {
    case kOverlayAccentAcute:
      FillOverlayRect(pixels, width, height, x + 3 * scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentGrave:
      FillOverlayRect(pixels, width, height, x + scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentCircumflex:
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + scale, y - scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 3 * scale, y - scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentTilde:
      FillOverlayRect(pixels, width, height, x + scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 3 * scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 4 * scale, y - scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentDiaeresis:
      FillOverlayRect(pixels, width, height, x + scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 3 * scale, y - 2 * scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentRing:
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - 3 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 3 * scale, y - 2 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + 2 * scale, y - scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentCedilla:
      FillOverlayRect(pixels, width, height, x + 2 * scale, y + 7 * scale, scale, scale, rgb, 255);
      FillOverlayRect(pixels, width, height, x + scale, y + 8 * scale, scale, scale, rgb, 255);
      break;
    case kOverlayAccentSlash:
      for (int row = 0; row < 7; ++row) {
        const int col = 4 - (row * 5 / 7);
        FillOverlayRect(pixels, width, height, x + col * scale, y + row * scale, scale, scale, rgb, 255);
      }
      break;
    default:
      break;
  }
}

void DrawOverlayGlyph(uint8_t* pixels, int width, int height, int x, int y, uint32_t codepoint, int scale, uint32_t rgb) {
  if (scale < 1) {
    scale = 1;
  }
  int accent = kOverlayAccentNone;
  const uint8_t* rows = OverlayGlyphRows(codepoint, &accent);
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 5; ++col) {
      if ((rows[row] & (1u << (4 - col))) == 0) {
        continue;
      }
      FillOverlayRect(pixels, width, height, x + col * scale, y + row * scale, scale, scale, rgb, 255);
    }
  }
  DrawOverlayAccent(pixels, width, height, x, y, accent, scale, rgb);
}

void DrawOverlayString(uint8_t* pixels, int width, int height, int x, int y, const char* text, int length, int scale, uint32_t rgb) {
  if (text == nullptr || length <= 0) {
    return;
  }
  int draw_x = x;
  const int advance = 6 * (scale > 0 ? scale : 1);
  const char* cursor = text;
  const char* end = text + length;
  while (cursor < end && *cursor != '\0') {
    const uint32_t codepoint = DecodeOverlayUtf8Codepoint(&cursor, end);
    DrawOverlayGlyph(pixels, width, height, draw_x, y, codepoint, scale, rgb);
    draw_x += advance;
  }
}

void DrawOverlayCircle(uint8_t* pixels, int width, int height, int left, int top, int size, uint32_t fill_rgb, uint32_t border_rgb) {
  const int radius = size / 2;
  const int center_x = left + radius;
  const int center_y = top + radius;
  const int radius_sq = radius * radius;
  const int inner = radius - 2;
  const int inner_sq = inner * inner;
  for (int yy = top; yy < top + size; ++yy) {
    for (int xx = left; xx < left + size; ++xx) {
      const int dx = xx - center_x;
      const int dy = yy - center_y;
      const int dist_sq = dx * dx + dy * dy;
      if (dist_sq <= radius_sq) {
        PutOverlayPixel(pixels, width, height, xx, yy, dist_sq >= inner_sq ? border_rgb : fill_rgb, 255);
      }
    }
  }
}

void UpdateOverlayScreenPosition(OverlayRecord& record, int viewport_w, int viewport_h) {
  int x = 0;
  int y = 0;
  switch (record.position) {
    case 1:
      x = 20;
      y = 50;
      break;
    case 2:
      x = (viewport_w - record.width) / 2;
      y = 50;
      break;
    case 3:
      x = viewport_w - record.width - 90;
      y = 50;
      break;
    case 4:
      x = 20;
      y = (viewport_h - record.height) / 2;
      break;
    case 5:
      x = (viewport_w - record.width) / 2;
      y = (viewport_h - record.height) / 2;
      break;
    case 6:
      x = viewport_w - record.width - 90;
      y = (viewport_h - record.height) / 2;
      break;
    case 7:
      x = 20;
      y = viewport_h - record.height - 70;
      break;
    case 8:
      x = (viewport_w - record.width) / 2;
      y = viewport_h - record.height - 70;
      break;
    case 9:
      x = viewport_w - record.width - 90;
      y = viewport_h - record.height - 70;
      break;
    case 0:
    default:
      x = 0;
      y = 0;
      break;
  }
  record.screen_x = x + record.offset_x;
  record.screen_y = y + record.offset_y;
}

uint8_t* BuildOverlayPixels(
    OverlayRecord& record,
    int window_origin_x,
    int window_origin_y,
    uint32_t* out_pixel_bytes) {
  if (out_pixel_bytes != nullptr) {
    *out_pixel_bytes = 0;
  }
  if (record.width <= 0 || record.height <= 0 ||
      record.width > kOverlayMaxDimension || record.height > kOverlayMaxDimension) {
    return nullptr;
  }

  const uint64_t pixel_bytes64 =
      static_cast<uint64_t>(record.width) * static_cast<uint64_t>(record.height) * 4u;
  if (pixel_bytes64 == 0 || pixel_bytes64 > 0x7FFFFFFFu) {
    return nullptr;
  }

  uint8_t* pixels = static_cast<uint8_t*>(malloc(static_cast<size_t>(pixel_bytes64)));
  if (pixels == nullptr) {
    return nullptr;
  }
  memset(pixels, 0, static_cast<size_t>(pixel_bytes64));
  FillOverlayRect(pixels, record.width, record.height, 0, 0, record.width, record.height, 0x0C1012, 170);

  const int controls_width = OverlayControlAreaWidth(record.control_count);
  const int controls_height = record.control_count > 0
      ? kOverlayControlPadding * 2 + kOverlayControlButtonSize
      : 0;
  if (record.control_count > 0) {
    int button_x = record.width - controls_width + kOverlayControlPadding;
    if (button_x < kOverlayControlPadding) {
      button_x = kOverlayControlPadding;
    }
    const int button_y = kOverlayControlPadding;
    for (int index = 0; index < record.control_count; ++index) {
      OverlayControlButton& button = record.controls[index];
      button.x1 = window_origin_x + record.screen_x + button_x;
      button.y1 = window_origin_y + record.screen_y + button_y;
      button.x2 = button.x1 + kOverlayControlButtonSize;
      button.y2 = button.y1 + kOverlayControlButtonSize;
      DrawOverlayCircle(
          pixels,
          record.width,
          record.height,
          button_x,
          button_y,
          kOverlayControlButtonSize,
          button.enabled ? 0x105226 : 0x302424,
          button.enabled ? 0x60E678 : 0xD25A5A);
      const int label_len = static_cast<int>(strnlen(button.label, sizeof(button.label)));
      const int label_width = label_len * 6;
      const int label_x = button_x + (kOverlayControlButtonSize - label_width) / 2;
      const int label_y = button_y + (kOverlayControlButtonSize - 7) / 2;
      DrawOverlayString(
          pixels,
          record.width,
          record.height,
          label_x,
          label_y,
          button.label,
          label_len,
          1,
          button.enabled ? 0xF5FFF5 : 0xDCBEBE);
      button_x += kOverlayControlButtonSize + kOverlayControlGap;
    }
  }

  const char* visible = OverlayVisibleTextAfterControls(record.text);
  if (visible != nullptr && visible[0] != '\0') {
    const int text_scale = OverlayFontScale(record.font_size);
    const int line_height = OverlayLineHeight(record.font_size);
    const int header_height = OverlayHeaderHeight(record.font_size);
    const int panel_y = controls_height;
    FillOverlayRect(pixels, record.width, record.height, 0, panel_y, record.width, header_height, 0x182024, 200);
    DrawOverlayRectOutline(pixels, record.width, record.height, 0, panel_y, record.width, record.height - panel_y, 0x5A6E78, 220);
    FillOverlayRect(pixels, record.width, record.height, 0, panel_y + header_height, record.width, 1, 0x5A6E78, 220);

    int line_y = panel_y + kOverlayTextPadding;
    int line_index = 0;
    uint32_t color = record.color_rgb != 0 ? record.color_rgb : 0xFFFFFFu;
    const char* cursor = visible;
    while (*cursor != '\0' && line_y < record.height - kOverlayTextPadding) {
      if (TryParseOverlayLineColor(cursor, &color)) {
        cursor += kOverlayLineColorMarkerLength;
      }
      const char* line_end = cursor;
      while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
        ++line_end;
      }
      const int line_len = static_cast<int>(line_end - cursor);
      DrawOverlayString(
          pixels,
          record.width,
          record.height,
          kOverlayTextPadding,
          line_y,
          cursor,
          line_len,
          text_scale,
          color);
      cursor = line_end;
      if (*cursor == '\r') {
        ++cursor;
        if (*cursor == '\n') {
          ++cursor;
        }
      } else if (*cursor == '\n') {
        ++cursor;
      }
      ++line_index;
      if (line_index == 1) {
        line_y = panel_y + header_height + kOverlayTextPadding;
      } else {
        line_y += line_height;
      }
    }
  }

  if (out_pixel_bytes != nullptr) {
    *out_pixel_bytes = static_cast<uint32_t>(pixel_bytes64);
  }
  return pixels;
}

void DisableOverlayRenderingAfterFault(int signal_number, const char* phase) {
  AtomicSet(&g_state.overlay_last_error, EFAULT);
  if (__sync_lock_test_and_set(&g_overlay_render_failed, 1) == 0) {
    LogMessage(kLogError, "overlay rendering disabled after %s fault signal=%d", phase, signal_number);
  }
  ClearAllOverlays();
}

int CopyActiveOverlays(OverlayRecord* records, int* indices, int capacity) {
  int count = 0;
  pthread_mutex_lock(&g_state.overlay_mutex);
  for (int i = 0; i < kMaxOverlays && count < capacity; ++i) {
    if (g_state.overlays[i].active) {
      records[count] = g_state.overlays[i];
      indices[count] = i;
      ++count;
    }
  }
  pthread_mutex_unlock(&g_state.overlay_mutex);
  return count;
}

void UpdateRenderedOverlayBounds(const OverlayRecord& rendered, int index) {
  if (index < 0 || index >= kMaxOverlays) {
    return;
  }
  pthread_mutex_lock(&g_state.overlay_mutex);
  OverlayRecord& current = g_state.overlays[index];
  if (current.active && current.id == rendered.id) {
    current.screen_x = rendered.screen_x;
    current.screen_y = rendered.screen_y;
    for (int i = 0; i < kOverlayMaxControls; ++i) {
      current.controls[i].x1 = rendered.controls[i].x1;
      current.controls[i].y1 = rendered.controls[i].y1;
      current.controls[i].x2 = rendered.controls[i].x2;
      current.controls[i].y2 = rendered.controls[i].y2;
    }
  }
  pthread_mutex_unlock(&g_state.overlay_mutex);
}

void RestoreOverlayGlState(
    GLint matrix_mode,
    GLint texture,
    GLint depth_mask,
    GLboolean blend_enabled,
    GLboolean depth_enabled,
    GLboolean cull_enabled) {
  g_graphics.glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
  g_graphics.glPopMatrix();
  g_graphics.glMatrixMode(GL_PROJECTION);
  g_graphics.glPopMatrix();
  g_graphics.glMatrixMode(static_cast<GLenum>(matrix_mode));
  g_graphics.glDepthMask(depth_mask ? 1 : 0);
  if (blend_enabled) {
    g_graphics.glEnable(GL_BLEND);
  } else {
    g_graphics.glDisable(GL_BLEND);
  }
  if (depth_enabled) {
    g_graphics.glEnable(GL_DEPTH_TEST);
  } else {
    g_graphics.glDisable(GL_DEPTH_TEST);
  }
  if (cull_enabled) {
    g_graphics.glEnable(GL_CULL_FACE);
  } else {
    g_graphics.glDisable(GL_CULL_FACE);
  }
}

bool RenderOverlayText(
    OverlayRecord& record,
    int viewport_w,
    int viewport_h,
    int window_origin_x,
    int window_origin_y) {
  UpdateOverlayScreenPosition(record, viewport_w, viewport_h);
  uint32_t pixel_bytes = 0;
  uint8_t* pixels = BuildOverlayPixels(record, window_origin_x, window_origin_y, &pixel_bytes);
  if (pixels == nullptr || pixel_bytes == 0) {
    free(pixels);
    return false;
  }

  const float raster_x =
      (static_cast<float>(record.screen_x) * 2.0f / static_cast<float>(viewport_w)) - 1.0f;
  const float raster_y =
      1.0f - (static_cast<float>(record.screen_y + record.height) * 2.0f / static_cast<float>(viewport_h));
  g_graphics.glRasterPos2f(raster_x, raster_y);
  g_graphics.glDrawPixels(record.width, record.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  free(pixels);
  return true;
}

void RenderOverlays() {
  if (!OverlayRenderingEnabled()) {
    return;
  }
  if (AtomicGet(&g_overlay_render_failed) != 0) {
    return;
  }
  if (AtomicGet(&g_state.overlay_count) == 0) {
    return;
  }

  OverlayRecord records[kMaxOverlays] = {};
  int indices[kMaxOverlays] = {};
  const int record_count = CopyActiveOverlays(records, indices, kMaxOverlays);
  if (record_count == 0) {
    return;
  }

  int signal_number = 0;
  bool graphics_ready = false;
  if (!RunWithFaultGuard([&]() { graphics_ready = ResolveGraphicsApi(); }, &signal_number)) {
    DisableOverlayRenderingAfterFault(signal_number, "graphics resolve");
    return;
  }
  if (!graphics_ready) {
    return;
  }

  GLint viewport[4] = {};
  if (!RunWithFaultGuard([&]() { g_graphics.glGetIntegerv(GL_VIEWPORT, viewport); }, &signal_number)) {
    DisableOverlayRenderingAfterFault(signal_number, "viewport query");
    return;
  }
  const int viewport_w = viewport[2];
  const int viewport_h = viewport[3];
  if (viewport_w <= 0 || viewport_h <= 0) {
    return;
  }

  int surface_h = viewport_h;
  if (!RunWithFaultGuard(
          [&]() {
            int queried_h = 0;
            if (GetSdlWindowSize(nullptr, &queried_h) ||
                GetSdlVideoSurfaceSize(nullptr, &queried_h)) {
              surface_h = queried_h;
            }
          },
          &signal_number)) {
    DisableOverlayRenderingAfterFault(signal_number, "SDL surface query");
    return;
  }
  const int window_origin_x = viewport[0] > 0 ? viewport[0] : 0;
  int window_origin_y = surface_h - viewport_h - (viewport[1] > 0 ? viewport[1] : 0);
  if (window_origin_y < 0) {
    window_origin_y = 0;
  }

  GLint texture = 0;
  GLint matrix_mode = GL_MODELVIEW;
  GLint depth_mask = 1;
  GLboolean blend_enabled = 0;
  GLboolean depth_enabled = 0;
  GLboolean cull_enabled = 0;
  if (!RunWithFaultGuard(
          [&]() {
            g_graphics.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            g_graphics.glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
            g_graphics.glGetIntegerv(GL_DEPTH_WRITEMASK, &depth_mask);
            blend_enabled = g_graphics.glIsEnabled(GL_BLEND);
            depth_enabled = g_graphics.glIsEnabled(GL_DEPTH_TEST);
            cull_enabled = g_graphics.glIsEnabled(GL_CULL_FACE);
          },
          &signal_number)) {
    DisableOverlayRenderingAfterFault(signal_number, "state query");
    return;
  }

  bool setup_complete = false;
  if (!RunWithFaultGuard(
          [&]() {
            g_graphics.glDisable(GL_CULL_FACE);
            g_graphics.glDisable(GL_DEPTH_TEST);
            g_graphics.glEnable(GL_BLEND);
            g_graphics.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            g_graphics.glDepthMask(0);
            g_graphics.glColor3f(1.0f, 1.0f, 1.0f);
            g_graphics.glBindTexture(GL_TEXTURE_2D, 0);
            g_graphics.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            g_graphics.glMatrixMode(GL_PROJECTION);
            g_graphics.glPushMatrix();
            g_graphics.glLoadIdentity();
            g_graphics.glMatrixMode(GL_MODELVIEW);
            g_graphics.glPushMatrix();
            g_graphics.glLoadIdentity();
            setup_complete = true;
          },
          &signal_number)) {
    DisableOverlayRenderingAfterFault(signal_number, "OpenGL setup");
    return;
  }

  bool render_ok = true;
  for (int i = 0; i < record_count; ++i) {
    bool overlay_drawn = false;
    if (!RunWithFaultGuard(
            [&]() {
              overlay_drawn =
                  RenderOverlayText(records[i], viewport_w, viewport_h, window_origin_x, window_origin_y);
            },
            &signal_number) ||
        !overlay_drawn) {
      render_ok = false;
      break;
    }
  }

  bool cleanup_ok = true;
  if (setup_complete &&
      !RunWithFaultGuard(
          [&]() {
            RestoreOverlayGlState(
                matrix_mode,
                texture,
                depth_mask,
                blend_enabled,
                depth_enabled,
                cull_enabled);
          },
          &signal_number)) {
    cleanup_ok = false;
  }

  if (!render_ok) {
    DisableOverlayRenderingAfterFault(signal_number, "overlay draw");
    return;
  }
  if (!cleanup_ok) {
    DisableOverlayRenderingAfterFault(signal_number, "OpenGL cleanup");
    return;
  }

  for (int i = 0; i < record_count; ++i) {
    UpdateRenderedOverlayBounds(records[i], indices[i]);
  }

  AtomicSet(&g_state.overlay_last_error, 0);
  AtomicIncrement(&g_state.overlay_draws);
}

bool HandleOverlayMouseButton(int x, int y) {
  pthread_mutex_lock(&g_state.overlay_mutex);
  for (int overlay_index = kMaxOverlays - 1; overlay_index >= 0; --overlay_index) {
    OverlayRecord& overlay = g_state.overlays[overlay_index];
    if (!overlay.active) {
      continue;
    }
    for (int i = 0; i < overlay.control_count; ++i) {
      OverlayControlButton& button = overlay.controls[i];
      if (x >= button.x1 && x < button.x2 && y >= button.y1 && y < button.y2) {
        char event[kChatTextCapacity] = {};
        snprintf(event, sizeof(event), "%cSIMKEYS_OVERLAY_TOGGLE:%s", kOverlayEventMarker, button.script_id);
        pthread_mutex_unlock(&g_state.overlay_mutex);
        QueueChatLine(event);
        return true;
      }
    }
  }
  pthread_mutex_unlock(&g_state.overlay_mutex);
  return false;
}

void HandleSdlOverlayMouseEvent(void* event) {
  if (event == nullptr) {
    return;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(event);
  const uint8_t type = bytes[0];
  const uint8_t button_state = bytes[3];
  if (type != 5 || button_state != 1) {
    return;
  }

  uint16_t event_x = 0;
  uint16_t event_y = 0;
  memcpy(&event_x, bytes + 4, sizeof(event_x));
  memcpy(&event_y, bytes + 6, sizeof(event_y));
  int x = event_x;
  int y = event_y;
  GetSdlPointerPosition(&x, &y);
  HandleOverlayMouseButton(x, y);
}

void FillSnapshotText(const char* reason, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  int32_t pos = 0;
  char name[kCharacterNameCapacity] = {};
  pthread_mutex_lock(&g_state.overlay_mutex);
  snprintf(name, sizeof(name), "%s", g_state.character_name);
  pos = g_state.position_valid;
  x = g_state.position_x;
  y = g_state.position_y;
  z = g_state.position_z;
  pthread_mutex_unlock(&g_state.overlay_mutex);
  int32_t pending_busy = 0;
  int32_t pending_done = 0;
  int32_t pending_kind = 0;
  pthread_mutex_lock(&g_pending_mutex);
  pending_busy = g_pending.busy;
  pending_done = g_pending.done;
  pending_kind = static_cast<int32_t>(g_pending.kind);
  pthread_mutex_unlock(&g_pending_mutex);
  const uint32_t snapshot_player_object = static_cast<uint32_t>(AtomicGet(&g_state.player_object));
  const uint32_t snapshot_player_creature = static_cast<uint32_t>(AtomicGet(&g_state.player_creature));
  int32_t client_defensive_casting = -1;
  const int32_t client_defensive_casting_error =
      ReadClientDefensiveCastingModeFlag(snapshot_player_object, &client_defensive_casting)
          ? kErrSuccess
          : kErrInvalidData;
  int32_t current_hp = 0;
  int32_t max_hp = 0;
  uint32_t current_hp_address = 0;
  uint32_t max_hp_address = 0;
  int32_t hp_error = kErrNotFound;
  ReadCreatureHitPoints(
      snapshot_player_creature,
      &current_hp,
      &max_hp,
      &current_hp_address,
      &max_hp_address,
      &hp_error);

  snprintf(
      out,
      capacity,
      "reason=%s\n"
      "process: pid=%ld imageBase=0x%08X\n"
      "hook: module=SimKeysHookLinux.so\n"
      "hook: log=%s\n"
      "hook: installed=%d logLevel=%d pipeState=%d pipeErr=%d\n"
      "expected: quickbarExec=0x%08X quickbarPageSelect=0x%08X slotDispatch=0x%08X quickbarVtable=0x%08X chatSend=0x%08X chatWindowLog=0x%08X\n"
      "engine: appGlobalSlot=0x%08X appHolder=0x%08X appObject=0x%08X serverApp=0x%08X appInner=0x%08X currentObjectId=0x%08X currentPlayerGlobal=0x%08X\n"
      "pending: busy=%d kind=%d done=%d drains=%d wakeAttempts=%d wakeSuccess=%d wakeSwallowed=%d signalAttempts=%d signalSuccess=%d focusLossSwallowed=%d\n"
      "quickbar: execTrace=%d slotTrace=%d capturedThis=0x%08X page=%d capturedSlot=%d slotPtr=0x%08X slotType=%d calls=%d scanAttempts=%d scanHits=%d itemMask=0x%08X%08X equippedMask=0x%08X%08X\n"
      "chat: trace=%d queued=%d nextWrite=%d latestSeq=%d lastMode=%d lastRc=%d lastErr=%d\n"
      "overlay: hook=%d count=%d draws=%d err=%d\n"
      "movement: walkToWaypoint=0x%08X noWalkBypass=%d positionValid=%d position=(%.3f, %.3f, %.3f)\n"
      "actionMode: toggle=0x%08X set=0x%08X get=0x%08X clientPlayer=0x%08X serverCreature=0x%08X defensiveOffset=0x%X defensiveState=%d defensiveErr=%d\n"
      "health: current=%d max=%d currentAddr=0x%08X maxAddr=0x%08X err=%d\n"
      "identity: player=0x%08X creature=0x%08X name=%s refreshes=%d err=%d\n"
      "last: vk=0x%08X rc=%d err=%d\n",
      reason != nullptr ? reason : "snapshot",
      static_cast<long>(getpid()),
      kImageBase,
      g_state.log_path,
      g_state.installed,
      g_state.log_level,
      g_state.pipe_state,
      g_state.pipe_thread_error,
      kQuickbarExec,
      kQuickbarPageSelect,
      kQuickbarSlotDispatch,
      kQuickbarPanelVtable,
      kChatSend,
      kChatWindowLog,
      kAppGlobalSlotAddress,
      ReadAppHolderPointer(),
      ReadAppObjectPointer(),
      ReadServerAppObjectPointer(),
      ReadAppInnerPointer(),
      ReadCurrentPlayerObjectId(),
      ReadCurrentPlayerGlobalPointer(),
      pending_busy,
      pending_kind,
      pending_done,
      g_state.pending_drain_count,
      g_state.pending_wake_attempts,
      g_state.pending_wake_success,
      g_state.pending_wake_swallowed,
      g_state.pending_signal_wake_attempts,
      g_state.pending_signal_wake_success,
      g_state.focus_loss_swallowed,
      g_state.quickbar_trace_installed,
      g_state.quickbar_slot_trace_installed,
      g_state.quickbar_this,
      g_state.quickbar_page,
      g_state.quickbar_slot,
      g_state.quickbar_slot_ptr,
      g_state.quickbar_slot_type,
      g_state.quickbar_calls,
      g_state.quickbar_scan_attempts,
      g_state.quickbar_scan_hits,
      static_cast<uint32_t>(g_state.quickbar_item_mask_high),
      static_cast<uint32_t>(g_state.quickbar_item_mask_low),
      static_cast<uint32_t>(g_state.quickbar_equipped_mask_high),
      static_cast<uint32_t>(g_state.quickbar_equipped_mask_low),
      g_state.chat_trace_installed,
      g_state.chat_count,
      g_state.chat_write_index,
      g_state.chat_sequence,
      g_state.last_chat_mode,
      g_state.last_chat_result,
      g_state.last_chat_error,
      g_state.overlay_hook_installed,
      g_state.overlay_count,
      g_state.overlay_draws,
      g_state.overlay_last_error,
      kWalkToWaypoint,
      g_state.walk_no_walk_bypass_enabled,
      pos ? 1 : 0,
      static_cast<double>(x),
      static_cast<double>(y),
      static_cast<double>(z),
      kToggleModeInput,
      kSetActionMode,
      kGetActionMode,
      snapshot_player_object,
      snapshot_player_creature,
      kClientDefensiveCastingStateOffset,
      client_defensive_casting,
      client_defensive_casting_error,
      current_hp,
      max_hp,
      current_hp_address,
      max_hp_address,
      hp_error,
      snapshot_player_object,
      snapshot_player_creature,
      name[0] != '\0' ? name : "<unknown>",
      g_state.identity_refresh_count,
      g_state.identity_error,
      g_state.last_vk,
      g_state.last_result,
      g_state.last_error);
}

void CompleteTriggerSlotPending(PendingCommand* pending) {
  if (pending == nullptr) {
    return;
  }
  const int32_t vk = 0x70 + pending->slot - 1;
  int32_t aux_rc = -1;
  int32_t path = 0;
  pending->trigger_response.vk = vk;
  int32_t rc = 0;
  int32_t last_error = 0;

  rc = CallQuickbarPageSlotDirect(pending->page, pending->slot, &aux_rc, &path);
  last_error = rc ? 0 : errno;

  pending->trigger_response.rc = rc;
  pending->trigger_response.success = pending->trigger_response.rc ? 1 : 0;
  pending->trigger_response.aux_rc = aux_rc;
  pending->trigger_response.last_error = pending->trigger_response.success ? 0 : last_error;
  pending->trigger_response.path = path;
  if (pending->trigger_response.success) {
    UpdateQuickbarItemMasks();
  }
  AtomicSet(&g_state.last_vk, vk);
  AtomicSet(&g_state.last_result, pending->trigger_response.rc);
  AtomicSet(&g_state.last_error, pending->trigger_response.last_error);
}

bool SubmitPending(PendingKind kind, PendingCommand* template_command) {
  timespec deadline = {};
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += kDispatchTimeoutMs / 1000;
  deadline.tv_nsec += (kDispatchTimeoutMs % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += 1;
    deadline.tv_nsec -= 1000000000L;
  }

  pthread_mutex_lock(&g_pending_mutex);
  if (g_pending.busy) {
    pthread_mutex_unlock(&g_pending_mutex);
    return false;
  }
  g_pending = *template_command;
  g_pending.kind = kind;
  g_pending.busy = 1;
  g_pending.done = 0;
  pthread_mutex_unlock(&g_pending_mutex);

  WakeMainThreadForPending(kind);

  pthread_mutex_lock(&g_pending_mutex);
  while (!g_pending.done) {
    const int rc = pthread_cond_timedwait(&g_pending_cond, &g_pending_mutex, &deadline);
    if (rc == ETIMEDOUT) {
      g_pending.busy = 0;
      g_pending.kind = kPendingNone;
      pthread_mutex_unlock(&g_pending_mutex);
      return false;
    }
  }
  *template_command = g_pending;
  g_pending.busy = 0;
  g_pending.kind = kPendingNone;
  pthread_mutex_unlock(&g_pending_mutex);
  return true;
}

void DrainPendingOnMainThread() {
  if (AtomicGet(&g_state.installed) == 0 || !IsHookMainThread()) {
    return;
  }
  pthread_mutex_lock(&g_pending_mutex);
  if (!g_pending.busy || g_pending.done || g_pending.kind == kPendingNone) {
    pthread_mutex_unlock(&g_pending_mutex);
    return;
  }
  PendingKind kind = g_pending.kind;
  PendingCommand* pending = &g_pending;
  AtomicIncrement(&g_state.pending_drain_count);

  switch (kind) {
    case kPendingTriggerSlot: {
      CompleteTriggerSlotPending(pending);
      break;
    }
    case kPendingChatSend: {
      pending->chat_response.mode = pending->mode;
      pending->chat_response.rc = CallChatSendDirect(pending->text, pending->mode);
      pending->chat_response.success = pending->chat_response.rc ? 1 : 0;
      pending->chat_response.last_error = pending->chat_response.success ? 0 : errno;
      break;
    }
    case kPendingMove: {
      pending->move_response.x = pending->x;
      pending->move_response.y = pending->y;
      pending->move_response.z = pending->z;
      pending->move_response.rc = CallMoveToLocationDirect(
          pending->x,
          pending->y,
          pending->z,
          pending->client_side,
          pending->action_object_id,
          pending->bypass_no_walk);
      pending->move_response.success = pending->move_response.rc ? 1 : 0;
      pending->move_response.last_error = pending->move_response.success ? 0 : errno;
      AtomicSet(&g_state.last_result, pending->move_response.rc);
      AtomicSet(&g_state.last_error, pending->move_response.last_error);
      break;
    }
    case kPendingWalkBypass: {
      pending->walk_response.success = SetWalkBypassEnabled(pending->enabled != 0) ? 1 : 0;
      pending->walk_response.enabled = static_cast<int32_t>(AtomicGet(&g_state.walk_no_walk_bypass_enabled));
      pending->walk_response.last_error = pending->walk_response.success ? 0 : errno;
      break;
    }
    case kPendingActionMode: {
      int32_t active = 0;
      int32_t error = 0;
      pending->action_response.mode = pending->mode;
      pending->action_response.enabled = pending->enabled ? 1 : 0;
      pending->action_response.success = SetActionModeOnMain(pending->mode, pending->enabled, &active, &error) ? 1 : 0;
      pending->action_response.active = active;
      pending->action_response.rc = pending->action_response.success;
      pending->action_response.last_error = error;
      break;
    }
    case kPendingMapPin: {
      pending->map_pin_response.rc = CallAddMapPinDirect(pending->x, pending->y, pending->text);
      pending->map_pin_response.success = pending->map_pin_response.rc ? 1 : 0;
      pending->map_pin_response.last_error = pending->map_pin_response.success ? 0 : errno;
      AtomicSet(&g_state.last_result, pending->map_pin_response.rc);
      AtomicSet(&g_state.last_error, pending->map_pin_response.last_error);
      break;
    }
    case kPendingRefreshIdentity: {
      int32_t error = 0;
      RefreshCharacterIdentity(&error);
      pending->refresh_error = error;
      break;
    }
    default:
      break;
  }

  g_pending.done = 1;
  pthread_cond_broadcast(&g_pending_cond);
  pthread_mutex_unlock(&g_pending_mutex);
}

bool RunRefreshIdentity() {
  PendingCommand pending = {};
  if (!SubmitPending(kPendingRefreshIdentity, &pending)) {
    return false;
  }
  return pending.refresh_error == kErrSuccess;
}

bool CanRefreshIdentityNow() {
  return ReadAppObjectPointer() != 0 && ReadAppInnerPointer() != 0;
}

void FillQueryResponse(QueryResponse* response) {
  memset(response, 0, sizeof(*response));
  // Keep socket-thread queries passive; quickbar discovery and mask refresh run
  // on the SDL/main thread when a trigger command is drained.
  if (CanRefreshIdentityNow()) {
    RunRefreshIdentity();
  }
  response->module_base = kImageBase;
  response->installed = static_cast<uint32_t>(AtomicGet(&g_state.installed));
  response->app_global_slot = kAppGlobalSlotAddress;
  response->app_holder = ReadAppHolderPointer();
  response->app_object = ReadAppObjectPointer();
  response->app_inner = ReadAppInnerPointer();
  response->quickbar_exec = kQuickbarExec;
  response->quickbar_slot_dispatch = kQuickbarSlotDispatch;
  response->quickbar_panel_vtable = kQuickbarPanelVtable;
  response->quickbar_slot_ptr = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_slot_ptr));
  response->quickbar_this = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_this));
  response->quickbar_page = static_cast<int32_t>(AtomicGet(&g_state.quickbar_page));
  response->quickbar_slot = static_cast<int32_t>(AtomicGet(&g_state.quickbar_slot));
  response->quickbar_slot_type = static_cast<int32_t>(AtomicGet(&g_state.quickbar_slot_type));
  response->quickbar_calls = static_cast<int32_t>(AtomicGet(&g_state.quickbar_calls));
  response->quickbar_scan_attempts = static_cast<int32_t>(AtomicGet(&g_state.quickbar_scan_attempts));
  response->quickbar_scan_hits = static_cast<int32_t>(AtomicGet(&g_state.quickbar_scan_hits));
  response->last_vk = static_cast<int32_t>(AtomicGet(&g_state.last_vk));
  response->last_rc = static_cast<int32_t>(AtomicGet(&g_state.last_result));
  response->last_error = static_cast<int32_t>(AtomicGet(&g_state.last_error));
  response->log_level = static_cast<int32_t>(AtomicGet(&g_state.log_level));
  response->player_object = static_cast<uint32_t>(AtomicGet(&g_state.player_object));
  response->player_creature = static_cast<uint32_t>(AtomicGet(&g_state.player_creature));
  response->player_hp_error = kErrNotFound;
  ReadCreatureHitPoints(
      response->player_creature,
      &response->player_current_hp,
      &response->player_max_hp,
      &response->player_current_hp_address,
      &response->player_max_hp_address,
      &response->player_hp_error);
  response->identity_refresh_count = static_cast<int32_t>(AtomicGet(&g_state.identity_refresh_count));
  response->identity_error = static_cast<int32_t>(AtomicGet(&g_state.identity_error));
  response->quickbar_item_mask_low = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_item_mask_low));
  response->quickbar_item_mask_high = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_item_mask_high));
  response->quickbar_equipped_mask_low = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_equipped_mask_low));
  response->quickbar_equipped_mask_high = static_cast<uint32_t>(AtomicGet(&g_state.quickbar_equipped_mask_high));
  pthread_mutex_lock(&g_state.overlay_mutex);
  response->position_valid = g_state.position_valid;
  response->position_x = g_state.position_x;
  response->position_y = g_state.position_y;
  response->position_z = g_state.position_z;
  snprintf(response->character_name, sizeof(response->character_name), "%s", g_state.character_name);
  pthread_mutex_unlock(&g_state.overlay_mutex);
}

bool HandleClient(int fd) {
  for (;;) {
    PipeHeader header = {};
    if (!ReadExact(fd, &header, sizeof(header))) {
      LogMessage(kLogDebug, "socket read header failed fd=%d errno=%d", fd, errno);
      return false;
    }
    if (header.size > kPipeBufferSize) {
      LogMessage(kLogDebug, "socket payload too large op=%u size=%u", header.op, header.size);
      return false;
    }
    LogMessage(kLogDebug, "socket request op=%u size=%u", header.op, header.size);
    uint8_t payload[kPipeBufferSize] = {};
    if (header.size > 0 && !ReadExact(fd, payload, header.size)) {
      LogMessage(kLogDebug, "socket read payload failed op=%u size=%u errno=%d", header.op, header.size, errno);
      return false;
    }
    switch (header.op) {
      case kOpQuery: {
        QueryResponse response = {};
        FillQueryResponse(&response);
        WriteResponse(fd, kOpQuery, &response, sizeof(response));
        LogMessage(kLogDebug, "socket query response size=%u installed=%u", static_cast<uint32_t>(sizeof(response)), response.installed);
        break;
      }
      case kOpSnapshotText: {
        char snapshot[4096] = {};
        FillSnapshotText("socket-query", snapshot, sizeof(snapshot));
        WriteResponse(fd, kOpSnapshotText, snapshot, static_cast<uint32_t>(strlen(snapshot)));
        break;
      }
      case kOpChatSend: {
        ChatSendResponse response = {};
        if (header.size < 8) {
          response.last_error = kErrInvalidData;
        } else {
          int32_t mode = 0;
          int32_t len = 0;
          memcpy(&mode, payload, 4);
          memcpy(&len, payload + 4, 4);
          if (len < 0 || len >= kPendingChatCapacity || header.size != static_cast<uint32_t>(8 + len)) {
            response.last_error = kErrInvalidData;
          } else {
            PendingCommand pending = {};
            pending.mode = mode;
            memcpy(pending.text, payload + 8, len);
            pending.text[len] = '\0';
            if (SubmitPending(kPendingChatSend, &pending)) {
              response = pending.chat_response;
            } else {
              response.last_error = kErrTimeout;
            }
          }
        }
        WriteResponse(fd, kOpChatSend, &response, sizeof(response));
        break;
      }
      case kOpMoveToLocation: {
        MoveToLocationResponse response = {};
        if (header.size != sizeof(MoveToLocationRequest) && header.size != 20) {
          response.last_error = kErrInvalidData;
        } else {
          MoveToLocationRequest request = {};
          memcpy(&request, payload, header.size);
          PendingCommand pending = {};
          pending.x = request.x;
          pending.y = request.y;
          pending.z = request.z;
          pending.client_side = request.client_side;
          pending.action_object_id = request.action_object_id;
          pending.bypass_no_walk = request.bypass_no_walk;
          if (SubmitPending(kPendingMove, &pending)) {
            response = pending.move_response;
          } else {
            response.last_error = kErrTimeout;
          }
        }
        WriteResponse(fd, kOpMoveToLocation, &response, sizeof(response));
        break;
      }
      case kOpSetWalkBypass: {
        WalkBypassResponse response = {};
        if (header.size != sizeof(WalkBypassRequest)) {
          response.last_error = kErrInvalidData;
        } else {
          WalkBypassRequest request = {};
          memcpy(&request, payload, sizeof(request));
          PendingCommand pending = {};
          pending.enabled = request.enabled;
          if (SubmitPending(kPendingWalkBypass, &pending)) {
            response = pending.walk_response;
          } else {
            response.last_error = kErrTimeout;
          }
        }
        WriteResponse(fd, kOpSetWalkBypass, &response, sizeof(response));
        break;
      }
      case kOpSetActionMode: {
        SetActionModeResponse response = {};
        if (header.size != sizeof(SetActionModeRequest)) {
          response.last_error = kErrInvalidData;
        } else {
          SetActionModeRequest request = {};
          memcpy(&request, payload, sizeof(request));
          PendingCommand pending = {};
          pending.mode = request.mode;
          pending.enabled = request.enabled;
          if (SubmitPending(kPendingActionMode, &pending)) {
            response = pending.action_response;
          } else {
            response.last_error = kErrTimeout;
          }
        }
        WriteResponse(fd, kOpSetActionMode, &response, sizeof(response));
        break;
      }
      case kOpMapPin: {
        MapPinResponse response = {};
        if (header.size < sizeof(MapPinRequestHeader)) {
          response.last_error = kErrInvalidData;
        } else {
          MapPinRequestHeader request = {};
          memcpy(&request, payload, sizeof(request));
          const uint32_t expected_size = static_cast<uint32_t>(
              sizeof(MapPinRequestHeader) + (request.text_length > 0 ? request.text_length : 0));
          if (request.text_length <= 0 ||
              request.text_length >= kMapPinTextCapacity ||
              header.size != expected_size) {
            response.last_error = kErrInvalidData;
          } else {
            PendingCommand pending = {};
            pending.x = request.x;
            pending.y = request.y;
            memcpy(pending.text, payload + sizeof(MapPinRequestHeader), request.text_length);
            pending.text[request.text_length] = '\0';
            if (SubmitPending(kPendingMapPin, &pending)) {
              response = pending.map_pin_response;
            } else {
              response.last_error = kErrTimeout;
            }
          }
        }
        WriteResponse(fd, kOpMapPin, &response, sizeof(response));
        break;
      }
      case kOpChatPoll: {
        ChatPollRequest request = {};
        request.max_lines = 20;
        if (header.size == sizeof(ChatPollRequest)) {
          memcpy(&request, payload, sizeof(request));
        }
        uint8_t response[kPipeBufferSize] = {};
        uint32_t size = 0;
        BuildChatPollResponse(request, response, sizeof(response), &size);
        WriteResponse(fd, kOpChatPoll, response, size);
        break;
      }
      case kOpOverlayText: {
        OverlayResponse response = {};
        if (header.size < sizeof(OverlayTextRequestHeader)) {
          response.last_error = kErrInvalidData;
        } else {
          OverlayTextRequestHeader request = {};
          memcpy(&request, payload, sizeof(request));
          if (request.text_length < 0 ||
              request.text_length >= kOverlayTextCapacity ||
              header.size != sizeof(OverlayTextRequestHeader) + static_cast<uint32_t>(request.text_length)) {
            response.last_error = kErrInvalidData;
          } else {
            char text[kOverlayTextCapacity] = {};
            memcpy(text, payload + sizeof(OverlayTextRequestHeader), request.text_length);
            if (StoreOverlayText(request, text, &response.width, &response.height)) {
              response.success = 1;
              response.last_error = 0;
              AtomicSet(&g_state.overlay_hook_installed, 1);
            } else {
              response.last_error = kErrBusy;
            }
          }
        }
        WriteResponse(fd, kOpOverlayText, &response, sizeof(response));
        break;
      }
      case kOpOverlayClear: {
        OverlayResponse response = {};
        if (header.size != 4) {
          response.last_error = kErrInvalidData;
        } else {
          int32_t id = 0;
          memcpy(&id, payload, 4);
          response.success = ClearOverlayById(id) ? 1 : 0;
        }
        WriteResponse(fd, kOpOverlayClear, &response, sizeof(response));
        break;
      }
      case kOpOverlayClearAll: {
        OverlayResponse response = {};
        response.success = ClearAllOverlays() ? 1 : 0;
        WriteResponse(fd, kOpOverlayClearAll, &response, sizeof(response));
        break;
      }
      case kOpTriggerPageSlot:
      case kOpTriggerSlot: {
        TriggerResponse response = {};
        int32_t slot = 0;
        int32_t page = 0;
        if (header.op == kOpTriggerSlot && header.size == 4) {
          memcpy(&slot, payload, 4);
        } else if (header.op == kOpTriggerPageSlot && header.size == 8) {
          memcpy(&slot, payload, 4);
          memcpy(&page, payload + 4, 4);
        } else {
          response.last_error = kErrInvalidData;
          WriteResponse(fd, header.op, &response, sizeof(response));
          break;
        }
        PendingCommand pending = {};
        pending.slot = slot;
        pending.page = page;
        if (SubmitPending(kPendingTriggerSlot, &pending)) {
          response = pending.trigger_response;
        } else {
          response.last_error = kErrTimeout;
        }
        WriteResponse(fd, header.op, &response, sizeof(response));
        break;
      }
      case kOpTriggerVk:
      case kOpReplayLast: {
        TriggerResponse response = {};
        response.last_error = kErrNotSupported;
        WriteResponse(fd, header.op, &response, sizeof(response));
        break;
      }
      case kOpSetLog: {
        int32_t level = kLogInfo;
        if (header.size == 4) {
          memcpy(&level, payload, 4);
        }
        if (level < kLogError) {
          level = kLogError;
        }
        if (level > kLogDebug) {
          level = kLogDebug;
        }
        AtomicSet(&g_state.log_level, level);
        WriteResponse(fd, kOpSetLog, &level, sizeof(level));
        break;
      }
      default: {
        TriggerResponse response = {};
        response.last_error = kErrInvalidFunction;
        WriteResponse(fd, header.op, &response, sizeof(response));
        break;
      }
    }
  }
}

void ResolveSocketPath(char* out, size_t capacity) {
  const char* explicit_dir = getenv("SIMKEYS_LINUX_SOCKET_DIR");
  char dir[PATH_MAX] = {};
  if (explicit_dir != nullptr && explicit_dir[0] != '\0') {
    snprintf(dir, sizeof(dir), "%s", explicit_dir);
  } else {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
      snprintf(dir, sizeof(dir), "%s/hgcc", runtime_dir);
    } else {
      snprintf(dir, sizeof(dir), "/tmp/hgcc-%ld", static_cast<long>(getuid()));
    }
  }
  mkdir(dir, 0700);
  snprintf(out, capacity, "%s/simkeys_%ld.sock", dir, static_cast<long>(getpid()));
}

void* SocketThreadMain(void*) {
  ResolveSocketPath(g_state.socket_path, sizeof(g_state.socket_path));
  unlink(g_state.socket_path);

  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0) {
    AtomicSet(&g_state.pipe_state, -1);
    AtomicSet(&g_state.pipe_thread_error, errno);
    return nullptr;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_state.socket_path);
  if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(server, 8) != 0) {
    AtomicSet(&g_state.pipe_state, -1);
    AtomicSet(&g_state.pipe_thread_error, errno);
    close(server);
    return nullptr;
  }
  chmod(g_state.socket_path, 0600);
  AtomicSet(&g_state.pipe_state, 1);
  AtomicSet(&g_state.pipe_thread_error, 0);
  LogMessage(kLogInfo, "socket server ready at %s", g_state.socket_path);

  for (;;) {
    int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      usleep(250000);
      continue;
    }
    LogMessage(kLogDebug, "socket client accepted fd=%d", client);
    HandleClient(client);
    close(client);
  }
}

void InitHook() {
  if (__sync_lock_test_and_set(&g_state.initialized, 1) != 0) {
    return;
  }
  g_main_thread = pthread_self();
  AtomicSet(&g_main_thread_ready, 1);
  pthread_mutex_init(&g_state.log_mutex, nullptr);
  pthread_mutex_init(&g_state.chat_mutex, nullptr);
  pthread_mutex_init(&g_state.overlay_mutex, nullptr);
  pthread_mutex_init(&g_pending_mutex, nullptr);
  pthread_cond_init(&g_pending_cond, nullptr);
  AtomicSet(&g_state.log_level, InitialLogLevel());
  InstallFaultHandlers();
  InstallWakeSignalHandler();
  TryInitXThreads();
  EnsureLogReady();
  InstallHooks();
  DiscoverQuickbarPanel("init");
  AtomicSet(&g_state.installed, 1);
  if (pthread_create(&g_state.pipe_thread, nullptr, SocketThreadMain, nullptr) == 0) {
    pthread_detach(g_state.pipe_thread);
  } else {
    AtomicSet(&g_state.pipe_state, -1);
    AtomicSet(&g_state.pipe_thread_error, errno);
  }
  LogMessage(kLogInfo, "SimKeysHookLinux initialized");
}

}  // namespace

extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureQuickbarExec(
    int32_t panel,
    int32_t slot_index) {
  CaptureQuickbarExec(panel, slot_index);
}

extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureQuickbarSlotDispatch(
    int32_t slot_ptr) {
  CaptureQuickbarSlotDispatch(slot_ptr);
}

extern "C" __attribute__((visibility("hidden"))) void SimKeysLinuxCaptureChatWindowLog(
    int32_t chat_window,
    const void* nwn_string) {
  CaptureChatWindowLog(chat_window, nwn_string);
}

extern "C" __attribute__((constructor)) void SimKeysLinuxConstructor() {
  InitHook();
}

extern "C" void SDL_GL_SwapBuffers() {
  if (g_real_sdl_gl_swap_buffers == nullptr) {
    g_real_sdl_gl_swap_buffers = reinterpret_cast<SdlGlSwapBuffersFn>(ResolveSdl12Symbol("SDL_GL_SwapBuffers"));
  }
  DrainPendingOnMainThread();
  RenderOverlays();
  if (g_real_sdl_gl_swap_buffers != nullptr) {
    g_real_sdl_gl_swap_buffers();
  }
}

extern "C" void SDL_Delay(uint32_t ms) {
  if (g_real_sdl_delay == nullptr) {
    g_real_sdl_delay = reinterpret_cast<SdlDelayFn>(ResolveSdl12Symbol("SDL_Delay"));
  }
  if (g_real_sdl_delay != nullptr) {
    g_real_sdl_delay(ms);
  } else {
    usleep(static_cast<useconds_t>(ms) * 1000u);
  }
  DrainPendingOnMainThread();
}

extern "C" uint8_t SDL_GetAppState() {
  if (g_real_sdl_get_app_state == nullptr) {
    g_real_sdl_get_app_state = reinterpret_cast<SdlGetAppStateFn>(ResolveSdl12Symbol("SDL_GetAppState"));
  }

  uint8_t state = g_real_sdl_get_app_state != nullptr ? g_real_sdl_get_app_state() : kSdlAppActiveMask;
  if (KeepSdlActiveEnabled()) {
    state = static_cast<uint8_t>(state | kSdlAppActiveMask);
  }
  return state;
}

extern "C" int SDL_PollEvent(void* event) {
  if (g_real_sdl_poll_event == nullptr) {
    g_real_sdl_poll_event = reinterpret_cast<SdlPollEventFn>(ResolveSdl12Symbol("SDL_PollEvent"));
  }
  int rc = 0;
  while (g_real_sdl_poll_event != nullptr) {
    rc = g_real_sdl_poll_event(event);
    if (rc && FilterSdlInternalEvent(event)) {
      continue;
    }
    break;
  }
  if (rc) {
    HandleSdlOverlayMouseEvent(event);
  }
  return rc;
}

extern "C" int SDL_WaitEvent(void* event) {
  if (g_real_sdl_wait_event == nullptr) {
    g_real_sdl_wait_event = reinterpret_cast<SdlWaitEventFn>(ResolveSdl12Symbol("SDL_WaitEvent"));
  }
  int rc = 0;
  while (g_real_sdl_wait_event != nullptr) {
    rc = g_real_sdl_wait_event(event);
    if (rc && FilterSdlInternalEvent(event)) {
      continue;
    }
    break;
  }
  DrainPendingOnMainThread();
  if (rc) {
    HandleSdlOverlayMouseEvent(event);
  }
  return rc;
}

extern "C" int SDL_PeepEvents(void* events, int numevents, int action, uint32_t mask) {
  SdlPeepEventsFn peep_events = ResolveSdlPeepEvents();
  if (peep_events == nullptr) {
    return -1;
  }

  int rc = peep_events(events, numevents, action, mask);
  if (rc > 0 && action == kSdlGetEvent && events != nullptr) {
    rc = FilterSdlInternalEvents(events, rc);
  }
  return rc;
}
