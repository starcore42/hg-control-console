#include "SimKeysHook.h"

#include <strsafe.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:InitSimKeys=_InitSimKeys@4")
#elif defined(_M_X64)
#pragma comment(linker, "/EXPORT:InitSimKeys=InitSimKeys")
#endif

namespace {

static_assert(sizeof(void*) == 4, "Build the SimKeys hook as Win32 for NWN Diamond.");

constexpr UINT kOpQuery = 3000;
constexpr UINT kOpTriggerSlot = 3001;
constexpr UINT kOpTriggerVk = 3002;
constexpr UINT kOpSetLog = 3003;
constexpr UINT kOpReplayLast = 3004;
constexpr UINT kOpSnapshotText = 3005;
constexpr UINT kOpChatSend = 3006;
constexpr UINT kOpChatPoll = 3007;
constexpr UINT kOpTriggerPageSlot = 3008;
constexpr UINT kOpOverlayText = 3009;
constexpr UINT kOpOverlayClear = 3010;
constexpr UINT kOpOverlayClearAll = 3011;
constexpr UINT kOpMoveToLocation = 3012;
constexpr UINT kOpSetWalkBypass = 3013;
constexpr UINT kOpSetActionMode = 3014;
constexpr UINT kOpMapPin = 3015;
constexpr UINT kOpQuickbarWeapons = 3016;

constexpr UINT kMsgTriggerVk = WM_APP + 0x491;
constexpr UINT kMsgSendChat = WM_APP + 0x492;
constexpr UINT kMsgRefreshIdentity = WM_APP + 0x493;
constexpr UINT kMsgTriggerPageSlot = WM_APP + 0x494;
constexpr UINT kMsgMoveToLocation = WM_APP + 0x495;
constexpr UINT kMsgSetWalkBypass = WM_APP + 0x496;
constexpr UINT kMsgSetActionMode = WM_APP + 0x497;
constexpr UINT kMsgAddMapPin = WM_APP + 0x498;
constexpr DWORD kPipeBufferSize = 65536;
constexpr DWORD kDispatchTimeoutMs = 2000;
constexpr DWORD kPipeStartupTimeoutMs = 2000;
constexpr uintptr_t kPreferredImageBase = 0x00400000u;
constexpr uintptr_t kAppGlobalSlotAddress = 0x0092DC50u;
constexpr UINT kExpectedNwnWndProc = 0x00403F90;
constexpr UINT kExpectedKeyPreDispatch = 0x00403B90;
constexpr UINT kExpectedGate90Accessor = 0x00407790;
constexpr UINT kExpectedGate94Accessor = 0x004077A0;
constexpr UINT kExpectedGate98Accessor = 0x004077B0;
constexpr UINT kExpectedDispatcherAccessor = 0x004077F0;
constexpr UINT kExpectedDispatcherThunk = 0x0076AF10;
constexpr UINT kExpectedDispatcherSlot0 = 0x005905E0;
constexpr UINT kExpectedQuickbarExec = 0x0051FAA0;
constexpr UINT kExpectedQuickbarPageSelect = 0x0051FD10;
constexpr UINT kExpectedQuickbarSlotDispatch = 0x005164A0;
constexpr UINT kExpectedQuickbarVtable = 0x008AB6D0;
constexpr UINT kExpectedObjectByIdResolver = 0x004078C0;
constexpr UINT kExpectedItemEquippedOwnerResolver = 0x004E9B50;
constexpr UINT kExpectedItemDescriptionBuilder = 0x004074C0;
constexpr UINT kExpectedItemInfoPopupBranch = 0x004556B4;
constexpr UINT kExpectedItemInfoPopupBranchReturn = 0x004556BF;
constexpr UINT kExpectedItemInfoParser = 0x00451680;
constexpr UINT kExpectedItemInfoPropertyRowAppend = 0x0055EC30;
constexpr UINT kExpectedItemInfoPropertyRowCall = 0x0046715A;
constexpr UINT kExpectedItemInfoMessageHandler = 0x00466F00;
constexpr UINT kExpectedItemInfoMessageHandlerCall = 0x00452A94;
constexpr UINT kExpectedItemInfoPanelManager = 0x00931678;
constexpr UINT kExpectedItemDetailRequest = 0x004B01D0;
constexpr UINT kExpectedNwnStringInit = 0x004F85A0;
constexpr UINT kExpectedGuiSetVisible = 0x005E4650;
constexpr UINT kExpectedChatSend = 0x0057C9F0;
constexpr UINT kExpectedChatWindowLog = 0x00493BD0;
constexpr UINT kExpectedAppObjectResolver = 0x00405160;
constexpr UINT kExpectedClientGuiMessageResolver = 0x00407B70;
constexpr UINT kExpectedItemMessageResolver = 0x00407C80;
constexpr UINT kExpectedCurrentPlayerResolver = 0x00407850;
constexpr UINT kExpectedPlayerNameBuilder = 0x004CEF20;
constexpr UINT kExpectedNwnStringConstructFromCString = 0x005BA260;
constexpr UINT kExpectedNwnStringDestroy = 0x005BA420;
constexpr UINT kExpectedWalkToWaypoint = 0x00407D70;
constexpr UINT kExpectedToggleModeInput = 0x004D00B0;
constexpr UINT kExpectedWalkNoWalkBlock = 0x0042A7AB;
constexpr UINT kExpectedWalkNoWalkBypassTarget = 0x0042A7D2;
constexpr UINT kExpectedServerObjectByIdResolver = 0x005FFAA0;
constexpr UINT kExpectedSetActionMode = 0x00658BD0;
constexpr UINT kExpectedGetActionMode = 0x00658A50;
constexpr uint32_t kCurrentPlayerPositionOffset = 0x2Cu;
constexpr uint32_t kClientCreatureDefensiveCastingStateOffset = 0x184u;
constexpr uint32_t kCreatureDefensiveCastingModeOffset = 0x4AAu;
constexpr uint32_t kCreatureCurrentCombatModeOffset = 0x4ABu;
constexpr uint32_t kObjectAsCreatureVtableOffset = 0x30u;
constexpr uint32_t kQuickbarPanelSlotsOffset = 0x68u;
constexpr uint32_t kQuickbarCurrentPageOffset = 0x2BB8u;
constexpr uint32_t kQuickbarPageStride = 0xE70u;
constexpr uint32_t kQuickbarSlotStride = 0x134u;
constexpr uint32_t kQuickbarSlotPrimaryItemOffset = 0x50u;
constexpr uint32_t kQuickbarSlotSecondaryItemOffset = 0x54u;
constexpr uint32_t kQuickbarSlotTypeOffset = 0x84u;
constexpr uint32_t kInvalidObjectId = 0x7F000000u;
constexpr BYTE kQuickbarItemSlotType = 1;
constexpr int kQuickbarPageCount = 3;
constexpr int kQuickbarSlotCount = 12;
constexpr int kQuickbarWeaponEntryCount = kQuickbarPageCount * kQuickbarSlotCount;
constexpr int kQuickbarWeaponDamageTypeCount = 16;
constexpr int kQuickbarWeaponMaxDamageRows = 16;
constexpr int kQuickbarWeaponDescriptionCapacity = 8192;
constexpr int kItemPropertyMaxRows = 256;
constexpr int kQuickbarWeaponDetailBackscanBytes = 160;
constexpr int kQuickbarWeaponDetailMaxUniqueItems = kQuickbarWeaponEntryCount * 2;
constexpr int kQuickbarWeaponDetailCacheCapacity = kQuickbarWeaponEntryCount * 2;
constexpr int kQuickbarWeaponDetailList = 3;
constexpr DWORD kQuickbarWeaponDetailPendingTimeoutMs = 60000u;
constexpr uintptr_t kQuickbarWeaponDetailNearbyBytes = 0x01000000u;
constexpr uintptr_t kQuickbarWeaponDetailAppNearbyBytes = 0x04000000u;
constexpr int kQuietItemInfoParserNotHandled = static_cast<int>(0x80000000u);
constexpr uint32_t kClientItemDetailObjectMarker = 0xFFFFFFF7u;
constexpr uint32_t kClientItemQuickbarObjectMarker = 0x00FF00FFu;
constexpr uint32_t kClientItemActivePropertyCountOffset = 0x11Cu;
constexpr uint32_t kClientItemActivePropertyDataOffset = 0x120u;
constexpr uint32_t kClientItemPropertyStride = 0x14u;
constexpr uint32_t kItemInfoPanelItemIdOffset = 0x2CCu;
constexpr uint16_t kItemPropertyDamageBonus = 16;
constexpr uint16_t kItemPropertyDamageBonusVsAlignmentGroup = 17;
constexpr uint16_t kItemPropertyDamageBonusVsRacialGroup = 18;
constexpr uint16_t kItemPropertyDamageBonusVsSpecificAlignment = 19;
constexpr int kActionModeDefensiveCast = 10;
constexpr int kPendingChatCapacity = 1024;
constexpr int kMapPinTextCapacity = 512;
constexpr int kMapPinXmlCapacity = 1024;
constexpr int kChatQueueCapacity = 1024;
constexpr int kChatTextCapacity = 768;
constexpr int kCharacterNameCapacity = 128;
constexpr int kMaxOverlays = 32;
constexpr int kOverlayTextCapacity = 4096;
constexpr int kOverlayMaxDimension = 1024;
constexpr int kOverlayTextPadding = 6;
constexpr char kOverlayLineColorMarker = '\x1F';
constexpr int kOverlayLineColorMarkerLength = 8;
constexpr int kOverlayMaxParsedLines = 128;
constexpr char kOverlayControlMarker = '\x1D';
constexpr char kOverlayEventMarker = '\x1E';
constexpr int kOverlayMaxControls = 16;
constexpr int kOverlayControlIdCapacity = 32;
constexpr int kOverlayControlLabelCapacity = 8;
constexpr int kOverlayControlButtonSize = 22;
constexpr int kOverlayControlGap = 4;
constexpr int kOverlayControlPadding = 3;
constexpr UINT kGlCullFace = 0x0B44;
constexpr UINT kGlDepthTest = 0x0B71;
constexpr UINT kGlTexture2D = 0x0DE1;
constexpr UINT kGlTextureBinding2D = 0x8069;
constexpr UINT kGlViewport = 0x0BA2;
constexpr UINT kGlMatrixMode = 0x0BA0;
constexpr UINT kGlProjection = 0x1701;
constexpr UINT kGlModelview = 0x1700;
constexpr UINT kGlUnsignedByte = 0x1401;
constexpr UINT kGlBgraExt = 0x80E1;
constexpr UINT kGlBlend = 0x0BE2;
constexpr UINT kGlSrcAlpha = 0x0302;
constexpr UINT kGlOneMinusSrcAlpha = 0x0303;

enum LogLevel {
  kLogError = 0,
  kLogInfo = 1,
  kLogDebug = 2,
};

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

struct QuickbarWeaponPropertyRow {
  int32_t hand;
  int32_t list;
  int32_t property_name;
  int32_t subtype;
  int32_t cost_table;
  int32_t cost_value;
  int32_t param1;
  int32_t param1_value;
};

struct QuickbarWeaponInfoEntry {
  int32_t page;
  int32_t slot;
  int32_t bit_index;
  int32_t slot_type;
  uint32_t slot_ptr;
  uint32_t primary_item_id;
  uint32_t secondary_item_id;
  uint32_t primary_item_ptr;
  uint32_t secondary_item_ptr;
  int32_t primary_equipped;
  int32_t secondary_equipped;
  int32_t primary_active_property_count;
  int32_t primary_passive_property_count;
  int32_t secondary_active_property_count;
  int32_t secondary_passive_property_count;
  int32_t primary_detail_requested;
  int32_t secondary_detail_requested;
  int32_t damage_row_count;
  uint32_t primary_damage_mask;
  uint32_t secondary_damage_mask;
  int32_t primary_damage_amounts[kQuickbarWeaponDamageTypeCount];
  int32_t secondary_damage_amounts[kQuickbarWeaponDamageTypeCount];
  int32_t error;
  QuickbarWeaponPropertyRow damage_rows[kQuickbarWeaponMaxDamageRows];
};

struct QuickbarWeaponInfoResponse {
  int32_t success;
  int32_t count;
  int32_t last_error;
  QuickbarWeaponInfoEntry entries[kQuickbarWeaponEntryCount];
};

struct QuickbarWeaponInfoRequest {
  uint32_t detail_slot_mask_low;
  uint32_t detail_slot_mask_high;
};

static_assert(sizeof(QuickbarWeaponPropertyRow) == 32, "quickbar weapon row pipe ABI changed");
static_assert(sizeof(QuickbarWeaponInfoEntry) == 724, "quickbar weapon entry pipe ABI changed");
static_assert(sizeof(QuickbarWeaponInfoRequest) == 8, "quickbar weapon request pipe ABI changed");

struct QuickbarWeaponDetailScanResult {
  uint32_t item_id;
  int32_t found;
  int32_t row_count;
  QuickbarWeaponPropertyRow rows[kQuickbarWeaponMaxDamageRows];
};

struct QuickbarWeaponDetailCacheEntry {
  uint32_t item_id;
  int32_t pending;
  int32_t valid;
  int32_t row_count;
  DWORD last_error;
  DWORD pending_since;
  QuickbarWeaponPropertyRow rows[kQuickbarWeaponMaxDamageRows];
};

struct MoveToLocationRequest {
  float x;
  float y;
  float z;
  int32_t client_side;
  uint32_t action_object_id;
  int32_t bypass_no_walk;
};

constexpr DWORD kMoveToLocationRequestLegacySize =
    static_cast<DWORD>(sizeof(float) * 3 + sizeof(int32_t) + sizeof(uint32_t));

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

struct PendingChatDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG mode;
  volatile LONG result;
  volatile LONG last_error;
  char text[kPendingChatCapacity];
};

struct PendingMoveDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG result;
  volatile LONG last_error;
  volatile LONG client_side;
  volatile LONG bypass_no_walk;
  uint32_t action_object_id;
  float x;
  float y;
  float z;
};

struct PendingWalkBypassDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG enabled;
  volatile LONG result;
  volatile LONG last_error;
};

struct PendingCombatModeDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG mode;
  volatile LONG enabled;
  volatile LONG result;
  volatile LONG active;
  volatile LONG last_error;
};

struct PendingMapPinDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG result;
  volatile LONG last_error;
  float x;
  float y;
  char text[kMapPinTextCapacity];
};

struct ChatLineEntry {
  int32_t sequence;
  char text[kChatTextCapacity];
};

struct OverlayControlButton {
  char script_id[kOverlayControlIdCapacity];
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
};

struct OverlayRecord {
  int32_t id;
  int32_t position;
  int32_t offset_x;
  int32_t offset_y;
  int32_t width;
  int32_t height;
  int32_t pixel_x;
  int32_t pixel_y;
  int32_t viewport_width;
  int32_t viewport_height;
  float raster_x;
  float raster_y;
  DWORD pixel_bytes;
  BYTE* pixels;
  int32_t control_count;
  OverlayControlButton controls[kOverlayMaxControls];
  bool enabled;
};

struct PendingDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG vk;
  volatile LONG result;
  volatile LONG aux_result;
  volatile LONG dispatch_path;
  volatile LONG last_error;
};

struct PendingIdentityDispatch {
  HANDLE event;
  volatile LONG busy;
  volatile LONG sequence_seed;
  volatile LONG request_id;
  volatile LONG last_error;
};

struct SimKeysState {
  HMODULE module;
  HWND hwnd;
  WNDPROC original_wndproc;
  HANDLE pipe_thread;
  HANDLE pipe_ready_event;
  CRITICAL_SECTION lock;
  bool lock_ready;
  CRITICAL_SECTION chat_lock;
  bool chat_lock_ready;
  CRITICAL_SECTION log_lock;
  bool log_lock_ready;
  CRITICAL_SECTION overlay_lock;
  bool overlay_lock_ready;
  DWORD window_thread_id;
  PendingDispatch pending;
  PendingChatDispatch pending_chat;
  PendingIdentityDispatch pending_identity;
  PendingMoveDispatch pending_move;
  PendingWalkBypassDispatch pending_walk_bypass;
  PendingCombatModeDispatch pending_combat_mode;
  PendingMapPinDispatch pending_map_pin;
  HANDLE log_file;
  char module_path[MAX_PATH];
  char log_path[MAX_PATH];
  char character_name[kCharacterNameCapacity];
  volatile LONG initialized;
  volatile LONG installed;
  volatile LONG pipe_state;
  volatile LONG pipe_thread_error;
  volatile LONG quickbar_trace_installed;
  volatile LONG quickbar_slot_trace_installed;
  volatile LONG chat_trace_installed;
  volatile LONG item_info_parser_hook_installed;
  volatile LONG overlay_hook_installed;
  volatile LONG overlay_count;
  volatile LONG overlay_draws;
  volatile LONG overlay_last_error;
  volatile LONG chat_write_index;
  volatile LONG chat_count;
  volatile LONG chat_sequence;
  volatile LONG last_chat_mode;
  volatile LONG last_chat_result;
  volatile LONG last_chat_error;
  volatile LONG quickbar_this;
  volatile LONG quickbar_page;
  volatile LONG quickbar_slot;
  volatile LONG quickbar_slot_type;
  volatile LONG quickbar_slot_ptr;
  volatile LONG quickbar_calls;
  volatile LONG quickbar_scan_attempts;
  volatile LONG quickbar_scan_hits;
  volatile LONG quickbar_item_mask_low;
  volatile LONG quickbar_item_mask_high;
  volatile LONG quickbar_equipped_mask_low;
  volatile LONG quickbar_equipped_mask_high;
  volatile LONG quickbar_weapon_count;
  volatile LONG quickbar_weapon_error;
  volatile LONG quickbar_weapon_refresh_requested;
  volatile LONG quickbar_weapon_detail_slot_mask_low;
  volatile LONG quickbar_weapon_detail_slot_mask_high;
  volatile LONG quickbar_weapon_detail_pending_count;
  volatile LONG quickbar_weapon_detail_cache_hits;
  volatile LONG log_level;
  volatile LONG player_object;
  volatile LONG player_creature;
  volatile LONG identity_refresh_count;
  volatile LONG identity_error;
  volatile LONG walk_no_walk_bypass_enabled;
  volatile LONG key_message_count;
  volatile LONG key_down_count;
  volatile LONG key_up_count;
  volatile LONG last_key_message;
  volatile LONG last_key_wparam;
  volatile LONG last_key_lparam;
  volatile LONG last_vk;
  volatile LONG last_result;
  volatile LONG last_error;
  ChatLineEntry chat_lines[kChatQueueCapacity];
  OverlayRecord overlays[kMaxOverlays];
  QuickbarWeaponInfoEntry quickbar_weapon_work[kQuickbarWeaponEntryCount];
  QuickbarWeaponDetailScanResult quickbar_weapon_detail_results[kQuickbarWeaponDetailMaxUniqueItems];
  QuickbarWeaponInfoEntry quickbar_weapons[kQuickbarWeaponEntryCount];
};

SimKeysState g_state = {};

BYTE g_quickbar_exec_original[16] = {};
void* g_quickbar_exec_gateway = nullptr;
size_t g_quickbar_exec_stolen = 0;
BYTE g_quickbar_slot_original[16] = {};
void* g_quickbar_slot_gateway = nullptr;
size_t g_quickbar_slot_stolen = 0;
BYTE g_chat_log_original[32] = {};
void* g_chat_log_gateway = nullptr;
size_t g_chat_log_stolen = 0;
BYTE g_item_info_parser_original[16] = {};
void* g_item_info_parser_gateway = nullptr;
size_t g_item_info_parser_stolen = 0;
BYTE g_item_info_property_row_original[16] = {};
void* g_item_info_property_row_gateway = nullptr;
size_t g_item_info_property_row_stolen = 0;
BYTE g_item_info_message_handler_original[8] = {};
void* g_item_info_message_handler_gateway = nullptr;
size_t g_item_info_message_handler_stolen = 0;
uint32_t g_item_info_message_handler_return_address = 0;
BYTE g_wgl_swap_original[8] = {};
void* g_wgl_swap_gateway = nullptr;
size_t g_wgl_swap_stolen = 0;
BYTE g_item_info_popup_original[16] = {};
size_t g_item_info_popup_stolen = 0;
uint32_t g_item_info_parser_address = 0;
uint32_t g_item_info_popup_return_address = 0;
BYTE g_walk_no_walk_original[5] = {};
bool g_walk_no_walk_bypass_installed = false;
CRITICAL_SECTION g_quickbar_weapon_detail_lock = {};
bool g_quickbar_weapon_detail_lock_ready = false;
QuickbarWeaponDetailCacheEntry g_quickbar_weapon_detail_cache[kQuickbarWeaponDetailCacheCapacity] = {};

typedef BOOL (WINAPI* WglSwapLayerBuffersFn)(HDC hdc, UINT planes);
typedef void (APIENTRY* GlDisableFn)(UINT cap);
typedef void (APIENTRY* GlEnableFn)(UINT cap);
typedef void (APIENTRY* GlDepthMaskFn)(BYTE flag);
typedef void (APIENTRY* GlColor3fFn)(float red, float green, float blue);
typedef void (APIENTRY* GlMatrixModeFn)(UINT mode);
typedef void (APIENTRY* GlPushMatrixFn)();
typedef void (APIENTRY* GlPopMatrixFn)();
typedef void (APIENTRY* GlLoadIdentityFn)();
typedef void (APIENTRY* GlGetIntegervFn)(UINT pname, int* params);
typedef void (APIENTRY* GlBindTextureFn)(UINT target, UINT texture);
typedef void (APIENTRY* GlRasterPos2fFn)(float x, float y);
typedef void (APIENTRY* GlDrawPixelsFn)(int width, int height, UINT format, UINT type, const void* pixels);
typedef BYTE (APIENTRY* GlIsEnabledFn)(UINT cap);
typedef void (APIENTRY* GlBlendFuncFn)(UINT sfactor, UINT dfactor);

GlDisableFn g_glDisable = nullptr;
GlEnableFn g_glEnable = nullptr;
GlDepthMaskFn g_glDepthMask = nullptr;
GlColor3fFn g_glColor3f = nullptr;
GlMatrixModeFn g_glMatrixMode = nullptr;
GlPushMatrixFn g_glPushMatrix = nullptr;
GlPopMatrixFn g_glPopMatrix = nullptr;
GlLoadIdentityFn g_glLoadIdentity = nullptr;
GlGetIntegervFn g_glGetIntegerv = nullptr;
GlBindTextureFn g_glBindTexture = nullptr;
GlRasterPos2fFn g_glRasterPos2f = nullptr;
GlDrawPixelsFn g_glDrawPixels = nullptr;
GlIsEnabledFn g_glIsEnabled = nullptr;
GlBlendFuncFn g_glBlendFunc = nullptr;

LRESULT CALLBACK SimKeysWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
void LogMessage(int level, const char* format, ...);
void WriteExecutableMemory(void* destination, const void* source, SIZE_T size);
void* MakeJmpGateway(BYTE* target, size_t stolen);
BOOL DiscoverQuickbarPanelByScan(const char* reason);
BOOL InstallChatWindowLogHook();
BOOL InstallOverlayHook();
BOOL InstallItemInfoParserHook();
BOOL InstallItemInfoPopupHook();
BOOL InstallItemInfoPropertyRowHook();
BOOL InstallItemInfoMessageHandlerHook();
BOOL UninstallItemInfoPropertyRowHook();
BOOL UninstallItemInfoMessageHandlerHook();
BOOL EnsureQuickbarWeaponDetailHooksInstalled();
void MaybeUninstallQuickbarWeaponDetailHooks();
BOOL RefreshCharacterIdentity(DWORD* out_error);
BOOL SetWalkNoWalkBypassEnabledOnWindowThread(BOOL enabled);
BOOL SetActionModeOnWindowThread(LONG mode, BOOL enabled, LONG* out_active, DWORD* out_error);
bool IsReadableWritableProtection(DWORD protect);
void UpdateQuickbarItemMasksOnWindowThread();
void UpdateQuickbarWeaponInfoOnWindowThread();
#if defined(_M_IX86)
extern "C" void ItemInfoParserEntryHook();
extern "C" void ItemInfoPopupBranchHook();
extern "C" void ItemInfoPropertyRowAppendHook();
extern "C" void ItemInfoMessageHandlerCallHook();
extern "C" void ItemInfoMessageHandlerAfterOriginal();
#endif

uintptr_t GetProcessImageBase() {
  return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
}

uint32_t RebaseAddress(uint32_t preferred_absolute) {
  const uintptr_t image_base = GetProcessImageBase();
  if (image_base == 0 || preferred_absolute < kPreferredImageBase) {
    return preferred_absolute;
  }
  return static_cast<uint32_t>(image_base + (preferred_absolute - kPreferredImageBase));
}

template <typename T>
bool SafeReadValue(uintptr_t address, T* out) {
  if (out == nullptr) {
    return false;
  }
  __try {
    *out = *reinterpret_cast<const T*>(address);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ZeroMemory(out, sizeof(T));
    return false;
  }
}

uint32_t SafeReadPointer32(uintptr_t address) {
  uint32_t value = 0;
  SafeReadValue(address, &value);
  return value;
}

LONG QuickbarSlotTypeToCaseIndex(LONG raw_slot_type) {
  return raw_slot_type > 0 ? (raw_slot_type - 1) : -1;
}

uint32_t ReadAppHolderPointer() {
  return SafeReadPointer32(kAppGlobalSlotAddress);
}

uint32_t ReadAppObjectPointer() {
  const uint32_t holder = ReadAppHolderPointer();
  return holder != 0 ? SafeReadPointer32(holder) : 0;
}

uint32_t ReadAppInnerPointer() {
  const uint32_t app_object = ReadAppObjectPointer();
  return app_object != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_object) + 4) : 0;
}

uint32_t ReadCurrentPlayerObjectId() {
  const uint32_t app_inner = ReadAppInnerPointer();
  return app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x20u) : 0;
}

bool IsPlausibleCoordinate(float value) {
  return value == value && value > -1000000.0f && value < 1000000.0f;
}

bool IsPlausiblePosition(float x, float y, float z) {
  return IsPlausibleCoordinate(x) && IsPlausibleCoordinate(y) && IsPlausibleCoordinate(z);
}

BOOL TryReadCurrentPlayerPosition(float* out_x, float* out_y, float* out_z) {
  if (out_x == nullptr || out_y == nullptr || out_z == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  const uint32_t player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
  if (player_object == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  const uintptr_t position_base = static_cast<uintptr_t>(player_object) + kCurrentPlayerPositionOffset;
  if (!SafeReadValue(position_base + 0u, &x) ||
      !SafeReadValue(position_base + 4u, &y) ||
      !SafeReadValue(position_base + 8u, &z) ||
      !IsPlausiblePosition(x, y, z)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  *out_x = x;
  *out_y = y;
  *out_z = z;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ReadDefensiveCastingModeByte(uint32_t creature, LONG* out_active) {
  if (out_active == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out_active = 0;

  if (creature == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  BYTE value = 0;
  if (!SafeReadValue(static_cast<uintptr_t>(creature) + kCreatureDefensiveCastingModeOffset, &value)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  if (value > 1) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  *out_active = static_cast<LONG>(value);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ReadClientDefensiveCastingModeFlag(uint32_t client_creature, LONG* out_active) {
  if (out_active == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out_active = 0;

  if (client_creature == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  DWORD value = 0;
  if (!SafeReadValue(
          static_cast<uintptr_t>(client_creature) + kClientCreatureDefensiveCastingStateOffset,
          &value)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  if (value > 1) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  *out_active = static_cast<LONG>(value);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ResolveCreatureFromObjectPointer(uint32_t game_object, uint32_t* out_creature, DWORD* out_error) {
  if (out_creature == nullptr) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out_creature = 0;

  if (game_object == 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_NOT_FOUND;
    }
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  uint32_t vtable = 0;
  uint32_t method = 0;
  if (!SafeReadValue(static_cast<uintptr_t>(game_object), &vtable) ||
      vtable == 0 ||
      !SafeReadValue(static_cast<uintptr_t>(vtable) + kObjectAsCreatureVtableOffset, &method) ||
      method == 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_DATA;
    }
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  typedef void* (__thiscall* AsCreatureFn)(void* game_object);
  const AsCreatureFn as_creature = reinterpret_cast<AsCreatureFn>(method);
  void* creature = nullptr;
  DWORD last_error = ERROR_SUCCESS;
  __try {
    creature = as_creature(reinterpret_cast<void*>(game_object));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = static_cast<DWORD>(GetExceptionCode());
  }

  if (last_error == ERROR_SUCCESS && creature == nullptr) {
    last_error = ERROR_NOT_FOUND;
  }
  if (last_error != ERROR_SUCCESS) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    SetLastError(last_error);
    return FALSE;
  }

  *out_creature = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(creature));
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ResolveCurrentCreatureFromObjectId(uint32_t* out_game_object, uint32_t* out_creature, DWORD* out_error) {
  if (out_game_object == nullptr || out_creature == nullptr) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out_game_object = 0;
  *out_creature = 0;

  const uint32_t object_id = ReadCurrentPlayerObjectId();
  const uint32_t app_holder = ReadAppHolderPointer();
  const uint32_t server_app = app_holder != 0 ? SafeReadPointer32(static_cast<uintptr_t>(app_holder) + 4u) : 0;
  if (object_id == 0 || object_id == kInvalidObjectId || server_app == 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_NOT_FOUND;
    }
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  typedef void* (__thiscall* ResolveServerObjectByIdFn)(void* server_app, uint32_t object_id);
  const ResolveServerObjectByIdFn resolve_server_object =
      reinterpret_cast<ResolveServerObjectByIdFn>(RebaseAddress(kExpectedServerObjectByIdResolver));

  void* game_object = nullptr;
  DWORD last_error = ERROR_SUCCESS;
  __try {
    game_object = resolve_server_object(reinterpret_cast<void*>(server_app), object_id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = static_cast<DWORD>(GetExceptionCode());
  }

  if (last_error == ERROR_SUCCESS && game_object == nullptr) {
    last_error = ERROR_NOT_FOUND;
  }
  if (last_error != ERROR_SUCCESS) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    SetLastError(last_error);
    return FALSE;
  }

  uint32_t creature = 0;
  if (!ResolveCreatureFromObjectPointer(
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game_object)),
          &creature,
          &last_error)) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    SetLastError(last_error);
    return FALSE;
  }

  *out_game_object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game_object));
  *out_creature = creature;
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

bool IsValidObjectId(uint32_t object_id) {
  return object_id != 0 && object_id != kInvalidObjectId;
}

void SetQuickbarMaskBit(uint32_t* low, uint32_t* high, int bit_index) {
  if (low == nullptr || high == nullptr || bit_index < 0) {
    return;
  }

  if (bit_index < 32) {
    *low |= (1u << bit_index);
  } else if (bit_index < kQuickbarPageCount * kQuickbarSlotCount) {
    *high |= (1u << (bit_index - 32));
  }
}

void StoreQuickbarItemMasks(uint32_t item_low, uint32_t item_high, uint32_t equipped_low, uint32_t equipped_high) {
  InterlockedExchange(&g_state.quickbar_item_mask_low, static_cast<LONG>(item_low));
  InterlockedExchange(&g_state.quickbar_item_mask_high, static_cast<LONG>(item_high));
  InterlockedExchange(&g_state.quickbar_equipped_mask_low, static_cast<LONG>(equipped_low));
  InterlockedExchange(&g_state.quickbar_equipped_mask_high, static_cast<LONG>(equipped_high));
}

void UpdateQuickbarItemMasksOnWindowThread() {
  typedef void* (__thiscall* ResolveObjectByIdFn)(void* app_object, uint32_t object_id);
  typedef void* (__thiscall* ItemEquippedOwnerFn)(void* item_object);

  uint32_t item_low = 0;
  uint32_t item_high = 0;
  uint32_t equipped_low = 0;
  uint32_t equipped_high = 0;

  __try {
    const uint32_t panel = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_this, 0, 0));
    if (panel == 0 || SafeReadPointer32(panel) != RebaseAddress(kExpectedQuickbarVtable)) {
      StoreQuickbarItemMasks(0, 0, 0, 0);
      return;
    }

    const uint32_t app_object = ReadAppObjectPointer();
    const uint32_t current_player_object_id = ReadCurrentPlayerObjectId();
    if (app_object == 0 || current_player_object_id == 0) {
      StoreQuickbarItemMasks(0, 0, 0, 0);
      return;
    }

    const ResolveObjectByIdFn resolve_object =
        reinterpret_cast<ResolveObjectByIdFn>(RebaseAddress(kExpectedObjectByIdResolver));
    const ItemEquippedOwnerFn item_equipped_owner =
        reinterpret_cast<ItemEquippedOwnerFn>(RebaseAddress(kExpectedItemEquippedOwnerResolver));

    for (int page = 0; page < kQuickbarPageCount; ++page) {
      for (int slot = 0; slot < kQuickbarSlotCount; ++slot) {
        const int bit_index = page * kQuickbarSlotCount + slot;
        const uint32_t slot_ptr = panel +
            kQuickbarPanelSlotsOffset +
            static_cast<uint32_t>(page) * kQuickbarPageStride +
            static_cast<uint32_t>(slot) * kQuickbarSlotStride;

        BYTE slot_type = 0;
        if (!SafeReadValue(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotTypeOffset, &slot_type) ||
            slot_type != kQuickbarItemSlotType) {
          continue;
        }

        const uint32_t primary_item_id = SafeReadPointer32(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotPrimaryItemOffset);
        if (!IsValidObjectId(primary_item_id)) {
          continue;
        }

        void* primary_item = resolve_object(reinterpret_cast<void*>(app_object), primary_item_id);
        if (primary_item == nullptr) {
          continue;
        }

        SetQuickbarMaskBit(&item_low, &item_high, bit_index);

        void* primary_owner = item_equipped_owner(primary_item);
        if (primary_owner == nullptr ||
            SafeReadPointer32(reinterpret_cast<uintptr_t>(primary_owner) + 4u) != current_player_object_id) {
          continue;
        }

        const uint32_t secondary_item_id = SafeReadPointer32(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotSecondaryItemOffset);
        if (IsValidObjectId(secondary_item_id)) {
          void* secondary_item = resolve_object(reinterpret_cast<void*>(app_object), secondary_item_id);
          if (secondary_item == nullptr) {
            continue;
          }
          void* secondary_owner = item_equipped_owner(secondary_item);
          if (secondary_owner == nullptr ||
              SafeReadPointer32(reinterpret_cast<uintptr_t>(secondary_owner) + 4u) != current_player_object_id) {
            continue;
          }
        }

        SetQuickbarMaskBit(&equipped_low, &equipped_high, bit_index);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    StoreQuickbarItemMasks(0, 0, 0, 0);
    LogMessage(
        kLogError,
        "quickbar item mask refresh raised exception code=0x%08lX",
        static_cast<unsigned long>(GetExceptionCode()));
    return;
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

void InitializeQuickbarWeaponInfoEntry(QuickbarWeaponInfoEntry* entry, int page, int slot_index) {
  if (entry == nullptr) {
    return;
  }

  ZeroMemory(entry, sizeof(*entry));
  entry->page = page;
  entry->slot = slot_index + 1;
  entry->bit_index = page * kQuickbarSlotCount + slot_index;
  entry->slot_type = -1;
  entry->primary_active_property_count = -1;
  entry->primary_passive_property_count = -1;
  entry->secondary_active_property_count = -1;
  entry->secondary_passive_property_count = -1;
}

void SetQuickbarWeaponEntryError(QuickbarWeaponInfoEntry* entry, DWORD error) {
  if (entry == nullptr || error == ERROR_SUCCESS) {
    return;
  }

  if (entry->error == ERROR_SUCCESS) {
    entry->error = static_cast<int32_t>(error);
  }
}

void SetQuickbarWeaponPropertyCount(QuickbarWeaponInfoEntry* entry, int hand, int list, int32_t count) {
  if (entry == nullptr) {
    return;
  }

  if (hand == 1 && list == 1) {
    entry->primary_active_property_count = count;
  } else if (hand == 1 && list == 2) {
    entry->primary_passive_property_count = count;
  } else if (hand == 2 && list == 1) {
    entry->secondary_active_property_count = count;
  } else if (hand == 2 && list == 2) {
    entry->secondary_passive_property_count = count;
  }
}

bool IsDamageBonusPropertyName(int32_t property_name) {
  return property_name == kItemPropertyDamageBonus ||
      property_name == kItemPropertyDamageBonusVsAlignmentGroup ||
      property_name == kItemPropertyDamageBonusVsRacialGroup ||
      property_name == kItemPropertyDamageBonusVsSpecificAlignment;
}

int NormalizeClientDamageTypeValue(int value) {
  if (value > 2 && value <= 11) {
    return value + 2;
  }
  return value;
}

int NormalizeDamagePropertyMaskBit(const QuickbarWeaponPropertyRow& row) {
  int value = row.property_name == kItemPropertyDamageBonus ? row.subtype : row.param1_value;
  if (value < 0 || value >= kQuickbarWeaponDamageTypeCount) {
    return -1;
  }
  return value;
}

void AddQuickbarWeaponDamageRow(QuickbarWeaponInfoEntry* entry, const QuickbarWeaponPropertyRow& row) {
  if (entry == nullptr || !IsDamageBonusPropertyName(row.property_name)) {
    return;
  }

  if (entry->damage_row_count < kQuickbarWeaponMaxDamageRows) {
    entry->damage_rows[entry->damage_row_count] = row;
  }
  entry->damage_row_count += 1;

  const int mask_bit = NormalizeDamagePropertyMaskBit(row);
  if (mask_bit < 0) {
    return;
  }

  uint32_t* mask = row.hand == 2 ? &entry->secondary_damage_mask : &entry->primary_damage_mask;
  int32_t* amounts = row.hand == 2 ? entry->secondary_damage_amounts : entry->primary_damage_amounts;
  *mask |= (1u << mask_bit);
  if (row.cost_value > amounts[mask_bit]) {
    amounts[mask_bit] = row.cost_value;
  }
}

void AddQuickbarWeaponParsedDamageRow(QuickbarWeaponInfoEntry* entry, const QuickbarWeaponPropertyRow& row, int mask_bit) {
  if (entry == nullptr || !IsDamageBonusPropertyName(row.property_name)) {
    return;
  }

  if (entry->damage_row_count < kQuickbarWeaponMaxDamageRows) {
    entry->damage_rows[entry->damage_row_count] = row;
  }
  entry->damage_row_count += 1;

  if (mask_bit < 0 || mask_bit >= kQuickbarWeaponDamageTypeCount) {
    return;
  }

  uint32_t* mask = row.hand == 2 ? &entry->secondary_damage_mask : &entry->primary_damage_mask;
  int32_t* amounts = row.hand == 2 ? entry->secondary_damage_amounts : entry->primary_damage_amounts;
  *mask |= (1u << mask_bit);
  if (row.cost_value > amounts[mask_bit]) {
    amounts[mask_bit] = row.cost_value;
  }
}

const char* SkipAsciiWhitespace(const char* text) {
  while (text != nullptr && (*text == ' ' || *text == '\t')) {
    ++text;
  }
  return text;
}

bool StartsWithNoCase(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  const size_t prefix_length = strlen(prefix);
  return _strnicmp(text, prefix, prefix_length) == 0;
}

bool ParseQuickbarWeaponDamageType(const char* text, int* out_damage_type, size_t* out_label_length) {
  struct DamageTypeName {
    const char* label;
    int type;
  };

  static const DamageTypeName kDamageTypes[] = {
      {"Bludgeoning", 0},
      {"Piercing", 1},
      {"Slashing", 2},
      {"Subdual", 3},
      {"Physical", 4},
      {"Magical", 5},
      {"Magic", 5},
      {"Acid", 6},
      {"Cold", 7},
      {"Divine", 8},
      {"Electrical", 9},
      {"Electricity", 9},
      {"Fire", 10},
      {"Negative", 11},
      {"Positive", 12},
      {"Sonic", 13},
      {"Ectoplasmic", 14},
      {"Psionic", 15},
      {"Sacred", 16},
      {"Vile", 17},
  };

  if (text == nullptr || out_damage_type == nullptr || out_label_length == nullptr) {
    return false;
  }

  for (int index = 0; index < static_cast<int>(_countof(kDamageTypes)); ++index) {
    const size_t label_length = strlen(kDamageTypes[index].label);
    if (_strnicmp(text, kDamageTypes[index].label, label_length) != 0) {
      continue;
    }
    const char next = text[label_length];
    if (next != '\0' && next != ' ' && next != '\t') {
      continue;
    }
    *out_damage_type = kDamageTypes[index].type;
    *out_label_length = label_length;
    return true;
  }

  return false;
}

int ParseQuickbarWeaponDamageCostValue(const char* token) {
  struct DamageCostName {
    const char* label;
    int value;
  };

  static const DamageCostName kDamageCosts[] = {
      {"1d4", 6},
      {"1d6", 7},
      {"1d8", 8},
      {"1d10", 9},
      {"2d6", 10},
      {"2d8", 11},
      {"2d4", 12},
      {"2d10", 13},
      {"1d12", 14},
      {"2d12", 15},
      {"3d12", 78},
      {"4d12", 79},
      {"5d12", 80},
      {"6d12", 81},
      {"7d12", 82},
      {"8d12", 83},
      {"9d12", 84},
      {"10d12", 85},
      {"11d12", 124},
      {"12d12", 125},
      {"13d12", 126},
      {"14d12", 127},
      {"15d12", 128},
      {"16d12", 129},
      {"17d12", 130},
      {"18d12", 131},
      {"19d12", 132},
      {"20d12", 133},
      {"1d20", 48},
      {"2d20", 49},
      {"3d20", 50},
      {"4d20", 51},
      {"5d20", 52},
      {"6d20", 53},
      {"7d20", 54},
      {"8d20", 55},
      {"9d20", 56},
      {"10d20", 57},
  };

  if (token == nullptr || token[0] == '\0') {
    return 0;
  }

  for (int index = 0; index < static_cast<int>(_countof(kDamageCosts)); ++index) {
    if (_stricmp(token, kDamageCosts[index].label) == 0) {
      return kDamageCosts[index].value;
    }
  }

  const char* cursor = token;
  if (*cursor == '+') {
    ++cursor;
  }
  int value = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    value = value * 10 + (*cursor - '0');
    ++cursor;
  }
  if (value <= 0 || *cursor != '\0') {
    return 0;
  }
  return value <= 5 ? value : value + 10;
}

bool ParseQuickbarWeaponDescriptionDamageLine(const char* line, int hand, QuickbarWeaponPropertyRow* out, int* out_mask_bit) {
  static const char* kPrefix = "Damage Bonus:";
  if (line == nullptr || out == nullptr || out_mask_bit == nullptr || !StartsWithNoCase(line, kPrefix)) {
    return false;
  }

  const char* cursor = SkipAsciiWhitespace(line + strlen(kPrefix));
  int damage_type = -1;
  size_t damage_type_length = 0;
  if (!ParseQuickbarWeaponDamageType(cursor, &damage_type, &damage_type_length)) {
    return false;
  }

  cursor = SkipAsciiWhitespace(cursor + damage_type_length);
  char amount_token[32] = {};
  size_t amount_length = 0;
  while (cursor[amount_length] != '\0' &&
      cursor[amount_length] != ' ' &&
      cursor[amount_length] != '\t' &&
      amount_length + 1 < sizeof(amount_token)) {
    amount_token[amount_length] = cursor[amount_length];
    ++amount_length;
  }
  amount_token[amount_length] = '\0';

  const int cost_value = ParseQuickbarWeaponDamageCostValue(amount_token);
  if (cost_value <= 0) {
    return false;
  }

  cursor = SkipAsciiWhitespace(cursor + amount_length);
  if (!StartsWithNoCase(cursor, "Damage")) {
    return false;
  }

  ZeroMemory(out, sizeof(*out));
  out->hand = hand;
  out->list = kQuickbarWeaponDetailList;
  out->property_name = kItemPropertyDamageBonus;
  out->subtype = damage_type;
  out->cost_table = 0;
  out->cost_value = cost_value;
  out->param1 = 0;
  out->param1_value = damage_type;
  *out_mask_bit = damage_type;
  return true;
}

int ScanQuickbarWeaponDescriptionText(const char* text, int hand, QuickbarWeaponInfoEntry* entry) {
  if (text == nullptr || entry == nullptr) {
    return 0;
  }

  int added = 0;
  const char* cursor = text;
  while (*cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    const char* line_start = cursor;
    while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
      ++cursor;
    }

    char line[512] = {};
    size_t line_length = static_cast<size_t>(cursor - line_start);
    if (line_length >= sizeof(line)) {
      line_length = sizeof(line) - 1;
    }
    memcpy(line, line_start, line_length);
    line[line_length] = '\0';

    QuickbarWeaponPropertyRow row = {};
    int mask_bit = -1;
    if (ParseQuickbarWeaponDescriptionDamageLine(SkipAsciiWhitespace(line), hand, &row, &mask_bit)) {
      AddQuickbarWeaponParsedDamageRow(entry, row, mask_bit);
      ++added;
    }
  }

  return added;
}

int ScanQuickbarWeaponItemDescription(uint32_t item_id, int hand, QuickbarWeaponInfoEntry* entry) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };

  if (!IsValidObjectId(item_id) || entry == nullptr) {
    return 0;
  }

  if ((hand == 2 ? entry->secondary_damage_mask : entry->primary_damage_mask) != 0) {
    return 0;
  }

  const uint32_t app_object = ReadAppObjectPointer();
  if (app_object == 0) {
    return 0;
  }

  typedef void (__thiscall* InitNwnStringFn)(NwnStringRef* text_object);
  typedef void (__thiscall* DestroyNwnStringFn)(NwnStringRef* text_object);
  typedef void (__thiscall* BuildItemDescriptionFn)(void* app_object, uint32_t object_id, NwnStringRef* out_text);
  const InitNwnStringFn init_string =
      reinterpret_cast<InitNwnStringFn>(RebaseAddress(kExpectedNwnStringInit));
  const DestroyNwnStringFn destroy_string =
      reinterpret_cast<DestroyNwnStringFn>(RebaseAddress(kExpectedNwnStringDestroy));
  const BuildItemDescriptionFn build_description =
      reinterpret_cast<BuildItemDescriptionFn>(RebaseAddress(kExpectedItemDescriptionBuilder));

  NwnStringRef description = {};
  char text[kQuickbarWeaponDescriptionCapacity] = {};
  DWORD last_error = ERROR_SUCCESS;
  __try {
    init_string(&description);
    build_description(reinterpret_cast<void*>(app_object), item_id, &description);
    if (description.text != nullptr && description.text[0] != '\0') {
      strncpy_s(text, sizeof(text), description.text, _TRUNCATE);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = GetExceptionCode();
  }

  __try {
    destroy_string(&description);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (last_error == ERROR_SUCCESS) {
      last_error = GetExceptionCode();
    }
  }

  if (last_error != ERROR_SUCCESS) {
    SetQuickbarWeaponEntryError(entry, last_error);
    LogMessage(
        kLogDebug,
        "quickbar weapon item description failed item=0x%08X hand=%d code=0x%08lX",
        item_id,
        hand,
        static_cast<unsigned long>(last_error));
    return 0;
  }

  const int added = ScanQuickbarWeaponDescriptionText(text, hand, entry);
  char preview[160] = {};
  strncpy_s(preview, sizeof(preview), text, _TRUNCATE);
  for (size_t index = 0; preview[index] != '\0'; ++index) {
    if (preview[index] == '\r' || preview[index] == '\n' || static_cast<unsigned char>(preview[index]) < 0x20u) {
      preview[index] = ' ';
    }
  }
  LogMessage(
      kLogDebug,
      "quickbar weapon item description item=0x%08X hand=%d len=%u rows=%d text=%s",
      item_id,
      hand,
      static_cast<unsigned int>(strnlen(text, sizeof(text))),
      added,
      preview[0] != '\0' ? preview : "<empty>");
  return added;
}

bool ReadClientItemPropertyRow(uint32_t row_ptr, int hand, QuickbarWeaponPropertyRow* out) {
  if (out == nullptr || row_ptr == 0) {
    return false;
  }

  uint16_t property_name = 0;
  uint16_t subtype = 0;
  uint16_t cost_value = 0;
  BYTE param = 0;
  if (!SafeReadValue(static_cast<uintptr_t>(row_ptr) + 0u, &property_name) ||
      !SafeReadValue(static_cast<uintptr_t>(row_ptr) + 2u, &subtype) ||
      !SafeReadValue(static_cast<uintptr_t>(row_ptr) + 6u, &cost_value) ||
      !SafeReadValue(static_cast<uintptr_t>(row_ptr) + 8u, &param)) {
    return false;
  }

  out->hand = hand;
  out->list = 1;
  out->property_name = static_cast<int32_t>(property_name);
  out->subtype = static_cast<int32_t>(subtype);
  out->cost_table = 0;
  out->cost_value = static_cast<int32_t>(cost_value);
  out->param1 = 0;
  out->param1_value = static_cast<int32_t>(param);
  return true;
}

bool ReadCompactItemPropertyRow(uintptr_t row_ptr, int hand, QuickbarWeaponPropertyRow* out) {
  if (out == nullptr || row_ptr == 0) {
    return false;
  }

  uint16_t property_name = 0;
  uint16_t subtype = 0;
  uint16_t cost_value = 0;
  BYTE param = 0;
  if (!SafeReadValue(row_ptr + 0u, &property_name) ||
      !SafeReadValue(row_ptr + 2u, &subtype) ||
      !SafeReadValue(row_ptr + 4u, &cost_value) ||
      !SafeReadValue(row_ptr + 6u, &param)) {
    return false;
  }

  if (!IsDamageBonusPropertyName(property_name) ||
      subtype > 0x3Fu ||
      cost_value == 0 ||
      cost_value > 0x1FFu ||
      (param != 0u && param != 1u && param != 0xFFu)) {
    return false;
  }

  out->hand = hand;
  out->list = kQuickbarWeaponDetailList;
  out->property_name = static_cast<int32_t>(property_name);
  out->subtype = static_cast<int32_t>(subtype);
  out->cost_table = 0;
  out->cost_value = static_cast<int32_t>(cost_value);
  out->param1 = 0;
  out->param1_value = static_cast<int32_t>(param);
  return true;
}

bool ScanClientItemActiveProperties(uint32_t item_ptr, int hand, QuickbarWeaponInfoEntry* entry) {
  if (item_ptr == 0 || entry == nullptr) {
    return false;
  }

  int32_t count = 0;
  if (!SafeReadValue(static_cast<uintptr_t>(item_ptr) + kClientItemActivePropertyCountOffset, &count)) {
    SetQuickbarWeaponEntryError(entry, ERROR_INVALID_DATA);
    SetQuickbarWeaponPropertyCount(entry, hand, 1, -1);
    return false;
  }

  if (count < 0 || count > kItemPropertyMaxRows) {
    SetQuickbarWeaponPropertyCount(entry, hand, 1, -1);
    SetQuickbarWeaponEntryError(entry, ERROR_INVALID_DATA);
    return false;
  }

  SetQuickbarWeaponPropertyCount(entry, hand, 1, count);
  if (count == 0) {
    return true;
  }

  const uint32_t data = SafeReadPointer32(static_cast<uintptr_t>(item_ptr) + kClientItemActivePropertyDataOffset);
  if (data == 0) {
    SetQuickbarWeaponEntryError(entry, ERROR_INVALID_DATA);
    return false;
  }

  for (int32_t index = 0; index < count; ++index) {
    QuickbarWeaponPropertyRow row = {};
    const uint32_t row_ptr = data + static_cast<uint32_t>(index) * kClientItemPropertyStride;
    if (!ReadClientItemPropertyRow(row_ptr, hand, &row)) {
      SetQuickbarWeaponEntryError(entry, ERROR_INVALID_DATA);
      return false;
    }
    AddQuickbarWeaponDamageRow(entry, row);
  }

  return true;
}

void ScanQuickbarWeaponItemProperties(uint32_t item_ptr, int hand, QuickbarWeaponInfoEntry* entry) {
  if (item_ptr == 0 || entry == nullptr) {
    return;
  }

  ScanClientItemActiveProperties(item_ptr, hand, entry);
}

int FindQuickbarWeaponDetailCacheIndexLocked(uint32_t item_id, bool create) {
  if (!IsValidObjectId(item_id)) {
    return -1;
  }

  int empty_index = -1;
  for (int index = 0; index < kQuickbarWeaponDetailCacheCapacity; ++index) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[index];
    if (entry->item_id == item_id) {
      return index;
    }
    if (empty_index < 0 && entry->item_id == 0) {
      empty_index = index;
    }
  }

  if (!create || empty_index < 0) {
    return -1;
  }

  QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[empty_index];
  ZeroMemory(entry, sizeof(*entry));
  entry->item_id = item_id;
  return empty_index;
}

void ClearQuickbarWeaponDetailPendingLocked(QuickbarWeaponDetailCacheEntry* entry) {
  if (entry == nullptr || entry->pending == 0) {
    return;
  }
  entry->pending = 0;
  entry->pending_since = 0;
  const LONG pending_count = InterlockedDecrement(&g_state.quickbar_weapon_detail_pending_count);
  if (pending_count < 0) {
    InterlockedExchange(&g_state.quickbar_weapon_detail_pending_count, 0);
  }
}

BOOL HasPendingQuickbarWeaponDetailRequestsAtLeast(DWORD minimum_age_ms) {
  if (!g_quickbar_weapon_detail_lock_ready) {
    return FALSE;
  }

  BOOL has_pending = FALSE;
  const DWORD now = GetTickCount();
  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  for (int index = 0; index < kQuickbarWeaponDetailCacheCapacity; ++index) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[index];
    if (entry->pending == 0) {
      continue;
    }
    if (now - entry->pending_since > kQuickbarWeaponDetailPendingTimeoutMs) {
      ClearQuickbarWeaponDetailPendingLocked(entry);
      continue;
    }
    if (now - entry->pending_since < minimum_age_ms) {
      continue;
    }
    has_pending = TRUE;
    break;
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
  return has_pending;
}

BOOL HasPendingQuickbarWeaponDetailRequests() {
  return HasPendingQuickbarWeaponDetailRequestsAtLeast(0u);
}

bool IsQuickbarWeaponDetailItemPending(uint32_t item_id) {
  if (!g_quickbar_weapon_detail_lock_ready || !IsValidObjectId(item_id)) {
    return false;
  }

  bool pending = false;
  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
  if (index >= 0) {
    const QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[index];
    pending = entry->pending != 0;
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
  return pending;
}

void CompleteQuickbarWeaponDetailCacheFromItemInfoResponse(uint32_t item_id) {
  if (!g_quickbar_weapon_detail_lock_ready || !IsValidObjectId(item_id)) {
    return;
  }

  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
  if (index >= 0) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[index];
    if (entry->pending != 0) {
      if (!entry->valid) {
        entry->valid = 1;
        entry->row_count = 0;
        entry->last_error = ERROR_SUCCESS;
        ZeroMemory(entry->rows, sizeof(entry->rows));
      }
      ClearQuickbarWeaponDetailPendingLocked(entry);
    }
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
}

uint32_t ResolveCurrentItemInfoPanel() {
  const uint32_t manager = SafeReadPointer32(RebaseAddress(kExpectedItemInfoPanelManager));
  if (manager == 0) {
    return 0;
  }

  const uint32_t vtable = SafeReadPointer32(manager);
  const uint32_t getter = vtable != 0 ? SafeReadPointer32(static_cast<uintptr_t>(vtable) + 0xA4u) : 0;
  if (getter == 0) {
    return 0;
  }

  typedef void* (__thiscall* GetItemInfoPanelFn)(void* self);
  void* panel = nullptr;
  __try {
    panel = reinterpret_cast<GetItemInfoPanelFn>(getter)(reinterpret_cast<void*>(manager));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(panel));
}

extern "C" void HideQuietItemInfoPanelIfPending() {
  if (!HasPendingQuickbarWeaponDetailRequests()) {
    return;
  }

  const uint32_t panel = ResolveCurrentItemInfoPanel();
  if (panel == 0) {
    return;
  }

  uint32_t item_id = 0;
  const bool has_item_id =
      SafeReadValue(static_cast<uintptr_t>(panel) + kItemInfoPanelItemIdOffset, &item_id) &&
      IsValidObjectId(item_id);
  const bool completes_pending_item =
      has_item_id && IsQuickbarWeaponDetailItemPending(item_id);
  if (!has_item_id) {
    return;
  }

  if (completes_pending_item) {
    CompleteQuickbarWeaponDetailCacheFromItemInfoResponse(item_id);
  }

  typedef void (__thiscall* GuiSetVisibleFn)(void* self, int visible);
  const GuiSetVisibleFn set_visible =
      reinterpret_cast<GuiSetVisibleFn>(RebaseAddress(kExpectedGuiSetVisible));
  __try {
    set_visible(reinterpret_cast<void*>(panel), 0);
    LogMessage(
        kLogDebug,
        "quickbar weapon detail hid item info panel item=0x%08X panel=0x%08X matched=%d",
        item_id,
        panel,
        completes_pending_item ? 1 : 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    LogMessage(
        kLogError,
        "quickbar weapon detail hide pending item info panel failed item=0x%08X panel=0x%08X code=0x%08lX",
        item_id,
        panel,
        static_cast<unsigned long>(GetExceptionCode()));
  }

  MaybeUninstallQuickbarWeaponDetailHooks();
}

bool IsQuickbarWeaponDetailSlotRequested(int bit_index, uint32_t mask_low, uint32_t mask_high) {
  if (bit_index < 0 || bit_index >= kQuickbarWeaponEntryCount) {
    return false;
  }
  if (bit_index < 32) {
    return (mask_low & (1u << bit_index)) != 0;
  }
  return (mask_high & (1u << (bit_index - 32))) != 0;
}

bool MarkQuickbarWeaponDetailPending(uint32_t item_id, bool* out_already_valid) {
  if (out_already_valid != nullptr) {
    *out_already_valid = false;
  }
  if (!g_quickbar_weapon_detail_lock_ready || !IsValidObjectId(item_id)) {
    return false;
  }

  bool should_request = false;
  const DWORD now = GetTickCount();
  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, true);
  if (index >= 0) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[index];
    if (entry->valid) {
      if (out_already_valid != nullptr) {
        *out_already_valid = true;
      }
    } else if (entry->pending == 0) {
      entry->pending = 1;
      entry->pending_since = now;
      InterlockedIncrement(&g_state.quickbar_weapon_detail_pending_count);
      should_request = true;
    } else if (now - entry->pending_since > 2000u) {
      entry->pending_since = now;
      should_request = true;
    }
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
  return should_request;
}

int ApplyQuickbarWeaponDetailCacheRows(uint32_t item_id, int hand, QuickbarWeaponInfoEntry* entry) {
  if (!g_quickbar_weapon_detail_lock_ready || entry == nullptr || !IsValidObjectId(item_id)) {
    return 0;
  }

  QuickbarWeaponPropertyRow rows[kQuickbarWeaponMaxDamageRows] = {};
  int row_count = -1;
  DWORD last_error = ERROR_SUCCESS;
  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
  if (index >= 0) {
    QuickbarWeaponDetailCacheEntry* cached = &g_quickbar_weapon_detail_cache[index];
    if (cached->valid) {
      ClearQuickbarWeaponDetailPendingLocked(cached);
      row_count = cached->row_count;
      last_error = cached->last_error;
      const int copy_count = row_count < kQuickbarWeaponMaxDamageRows ? row_count : kQuickbarWeaponMaxDamageRows;
      for (int row_index = 0; row_index < copy_count; ++row_index) {
        rows[row_index] = cached->rows[row_index];
      }
    }
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);

  if (row_count < 0) {
    return 0;
  }
  if (last_error != ERROR_SUCCESS) {
    SetQuickbarWeaponEntryError(entry, last_error);
  }
  if (hand == 2) {
    entry->secondary_passive_property_count = row_count;
  } else {
    entry->primary_passive_property_count = row_count;
  }

  const int stored = row_count < kQuickbarWeaponMaxDamageRows ? row_count : kQuickbarWeaponMaxDamageRows;
  for (int row_index = 0; row_index < stored; ++row_index) {
    QuickbarWeaponPropertyRow row = rows[row_index];
    row.hand = hand;
    row.list = kQuickbarWeaponDetailList;
    AddQuickbarWeaponDamageRow(entry, row);
  }
  InterlockedIncrement(&g_state.quickbar_weapon_detail_cache_hits);
  return stored;
}

void StoreQuickbarWeaponDetailCacheRows(uint32_t item_ptr, DWORD last_error) {
  if (!g_quickbar_weapon_detail_lock_ready || item_ptr == 0) {
    return;
  }

  uint32_t item_id = 0;
  int32_t property_count = 0;
  uint32_t property_data = 0;
  QuickbarWeaponPropertyRow rows[kQuickbarWeaponMaxDamageRows] = {};
  int stored_rows = 0;
  if (!SafeReadValue(static_cast<uintptr_t>(item_ptr) + 0x14u, &item_id) ||
      !IsValidObjectId(item_id)) {
    return;
  }

  if (last_error == ERROR_SUCCESS) {
    if (!SafeReadValue(static_cast<uintptr_t>(item_ptr) + kClientItemActivePropertyCountOffset, &property_count) ||
        property_count < 0 ||
        property_count > kItemPropertyMaxRows) {
      last_error = ERROR_INVALID_DATA;
      property_count = 0;
    }
  }

  if (last_error == ERROR_SUCCESS && property_count > 0) {
    property_data = SafeReadPointer32(static_cast<uintptr_t>(item_ptr) + kClientItemActivePropertyDataOffset);
    if (property_data == 0) {
      last_error = ERROR_INVALID_DATA;
    }
  }

  if (last_error == ERROR_SUCCESS && property_data != 0) {
    for (int32_t index = 0; index < property_count; ++index) {
      QuickbarWeaponPropertyRow row = {};
      const uint32_t row_ptr = property_data + static_cast<uint32_t>(index) * kClientItemPropertyStride;
      if (!ReadClientItemPropertyRow(row_ptr, 0, &row)) {
        last_error = ERROR_INVALID_DATA;
        break;
      }
      row.list = kQuickbarWeaponDetailList;
      if (!IsDamageBonusPropertyName(row.property_name)) {
        continue;
      }
      if (stored_rows < kQuickbarWeaponMaxDamageRows) {
        rows[stored_rows] = row;
      }
      ++stored_rows;
    }
  }

  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int cache_index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
  if (cache_index >= 0) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[cache_index];
    ClearQuickbarWeaponDetailPendingLocked(entry);
    entry->valid = 1;
    entry->row_count = stored_rows;
    entry->last_error = last_error;
    ZeroMemory(entry->rows, sizeof(entry->rows));
    const int copy_count = stored_rows < kQuickbarWeaponMaxDamageRows ? stored_rows : kQuickbarWeaponMaxDamageRows;
    for (int index = 0; index < copy_count; ++index) {
      entry->rows[index] = rows[index];
    }
  } else {
    LogMessage(
        kLogDebug,
        "quickbar weapon detail cache ignored unrequested item=0x%08X ptr=0x%08X props=%ld damageRows=%d err=%lu",
        item_id,
        item_ptr,
        static_cast<long>(property_count),
        stored_rows,
        static_cast<unsigned long>(last_error));
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);

  LogMessage(
      kLogDebug,
      "quickbar weapon detail cache item=0x%08X ptr=0x%08X props=%ld damageRows=%d err=%lu",
      item_id,
      item_ptr,
      static_cast<long>(property_count),
      stored_rows,
      static_cast<unsigned long>(last_error));
}

void StoreQuickbarWeaponDetailCacheScanResult(const QuickbarWeaponDetailScanResult& result) {
  if (!g_quickbar_weapon_detail_lock_ready || !IsValidObjectId(result.item_id) || result.found == 0) {
    return;
  }

  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int cache_index = FindQuickbarWeaponDetailCacheIndexLocked(result.item_id, false);
  if (cache_index >= 0) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[cache_index];
    ClearQuickbarWeaponDetailPendingLocked(entry);
    entry->valid = 1;
    entry->row_count = result.row_count;
    entry->last_error = ERROR_SUCCESS;
    ZeroMemory(entry->rows, sizeof(entry->rows));
    const int copy_count = result.row_count < kQuickbarWeaponMaxDamageRows
        ? result.row_count
        : kQuickbarWeaponMaxDamageRows;
    for (int index = 0; index < copy_count; ++index) {
      entry->rows[index] = result.rows[index];
    }
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);

  LogMessage(
      kLogDebug,
      "quickbar weapon detail scan cache item=0x%08X damageRows=%d",
      result.item_id,
      result.row_count);
}

void CaptureQuietItemDetailResult(uint32_t item_ptr, int parse_result) {
  StoreQuickbarWeaponDetailCacheRows(
      item_ptr,
      parse_result != 0 && item_ptr != 0 ? ERROR_SUCCESS : ERROR_NOT_FOUND);
}

void StoreQuickbarWeaponDetailCacheNetworkRow(uint32_t item_id, const QuickbarWeaponPropertyRow& row) {
  if (!g_quickbar_weapon_detail_lock_ready || !IsValidObjectId(item_id)) {
    return;
  }

  bool accepted = false;
  bool stored_damage = false;
  int stored_rows = 0;
  EnterCriticalSection(&g_quickbar_weapon_detail_lock);
  const int cache_index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
  if (cache_index >= 0) {
    QuickbarWeaponDetailCacheEntry* entry = &g_quickbar_weapon_detail_cache[cache_index];
    if (entry->pending != 0 || !entry->valid) {
      accepted = true;
      if (!entry->valid) {
        entry->valid = 1;
        entry->row_count = 0;
        entry->last_error = ERROR_SUCCESS;
        ZeroMemory(entry->rows, sizeof(entry->rows));
      }

      if (IsDamageBonusPropertyName(row.property_name)) {
        bool duplicate = false;
        const int compare_count = entry->row_count < kQuickbarWeaponMaxDamageRows
            ? entry->row_count
            : kQuickbarWeaponMaxDamageRows;
        for (int index = 0; index < compare_count; ++index) {
          const QuickbarWeaponPropertyRow& existing = entry->rows[index];
          if (existing.property_name == row.property_name &&
              existing.subtype == row.subtype &&
              existing.cost_value == row.cost_value &&
              existing.param1_value == row.param1_value) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          if (entry->row_count < kQuickbarWeaponMaxDamageRows) {
            entry->rows[entry->row_count] = row;
          }
          entry->row_count += 1;
          stored_damage = true;
        }
      }
      stored_rows = entry->row_count;
    }
  }
  LeaveCriticalSection(&g_quickbar_weapon_detail_lock);

  if (accepted && (stored_damage || IsDamageBonusPropertyName(row.property_name))) {
    LogMessage(
        kLogDebug,
        "quickbar weapon detail network row item=0x%08X prop=%ld subtype=%ld cost=%ld param=%ld active=%ld damageRows=%d stored=%d",
        item_id,
        static_cast<long>(row.property_name),
        static_cast<long>(row.subtype),
        static_cast<long>(row.cost_value),
        static_cast<long>(row.param1_value),
        static_cast<long>(row.param1),
        stored_rows,
        stored_damage ? 1 : 0);
  }
}

extern "C" void CaptureItemInfoPropertyRow(
    void* panel,
    uint32_t property_name,
    uint32_t subtype,
    uint32_t cost_value,
    uint32_t param1_value,
    uint32_t active) {
  if (panel == nullptr) {
    return;
  }

  uint32_t item_id = 0;
  if (!SafeReadValue(reinterpret_cast<uintptr_t>(panel) + kItemInfoPanelItemIdOffset, &item_id) ||
      !IsValidObjectId(item_id)) {
    return;
  }

  QuickbarWeaponPropertyRow row = {};
  row.hand = 0;
  row.list = kQuickbarWeaponDetailList;
  row.property_name = static_cast<int32_t>(property_name & 0xFFFFu);
  row.subtype = static_cast<int32_t>(subtype & 0xFFFFu);
  row.cost_table = 0;
  row.cost_value = static_cast<int32_t>(cost_value & 0xFFFFu);
  row.param1 = active != 0 ? 1 : 0;
  row.param1_value = static_cast<int32_t>(param1_value & 0xFFu);

  StoreQuickbarWeaponDetailCacheNetworkRow(item_id, row);
}

extern "C" int TryQuietItemInfoParser(void* message, uint32_t arg0, uint32_t arg4) {
  const BOOL has_pending = HasPendingQuickbarWeaponDetailRequests();
  const bool caller_supplied_output = arg0 == 1 && arg4 != 0;
  if (message == nullptr || g_item_info_parser_gateway == nullptr ||
      (!has_pending && !caller_supplied_output)) {
    return kQuietItemInfoParserNotHandled;
  }

  typedef int (__thiscall* ItemInfoParserFn)(void* self, uint32_t quiet_mode, uint32_t* out_item);
  uint32_t out_item = 0;
  int parse_result = 0;
  __try {
    const ItemInfoParserFn parser =
        reinterpret_cast<ItemInfoParserFn>(g_item_info_parser_gateway);
    if (arg0 == 1 && arg4 != 0) {
      uint32_t* caller_out_item = reinterpret_cast<uint32_t*>(arg4);
      parse_result = parser(message, arg0, caller_out_item);
      out_item = *caller_out_item;
    } else if (arg0 == 0) {
      parse_result = parser(message, 1, &out_item);
    } else {
      return kQuietItemInfoParserNotHandled;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    parse_result = 0;
    out_item = 0;
  }

  CaptureQuietItemDetailResult(out_item, parse_result);
  LogMessage(
      kLogDebug,
      "item info parser quiet capture mode=%lu pending=%ld callerOut=%d result=%d itemPtr=0x%08X arg4=0x%08X",
      static_cast<unsigned long>(arg0),
      static_cast<long>(has_pending),
      caller_supplied_output ? 1 : 0,
      parse_result,
      out_item,
      arg4);
  return parse_result;
}

void RequestQuickbarWeaponItemDetailOnWindowThread(uint32_t item_id, int hand, QuickbarWeaponInfoEntry* entry) {
  if (!IsValidObjectId(item_id) || entry == nullptr) {
    return;
  }

  if (ApplyQuickbarWeaponDetailCacheRows(item_id, hand, entry) > 0 ||
      (hand == 2 ? entry->secondary_passive_property_count : entry->primary_passive_property_count) >= 0) {
    return;
  }

  bool already_valid = false;
  const bool should_request = MarkQuickbarWeaponDetailPending(item_id, &already_valid);
  if (already_valid) {
    ApplyQuickbarWeaponDetailCacheRows(item_id, hand, entry);
    return;
  }

  if (hand == 2) {
    entry->secondary_detail_requested = 1;
  } else {
    entry->primary_detail_requested = 1;
  }
  if (!should_request) {
    return;
  }

  if (!EnsureQuickbarWeaponDetailHooksInstalled()) {
    const DWORD hook_error = GetLastError();
    SetQuickbarWeaponEntryError(entry, hook_error);
    EnterCriticalSection(&g_quickbar_weapon_detail_lock);
    const int cache_index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
    if (cache_index >= 0) {
      ClearQuickbarWeaponDetailPendingLocked(&g_quickbar_weapon_detail_cache[cache_index]);
    }
    LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
    MaybeUninstallQuickbarWeaponDetailHooks();
    LogMessage(
        kLogError,
        "quickbar weapon detail hook install failed item=0x%08X hand=%d err=%lu",
        item_id,
        hand,
        static_cast<unsigned long>(hook_error));
    return;
  }

  typedef void* (__thiscall* ResolveItemMessageFn)(void* app_object);
  typedef void (__thiscall* RequestItemDetailFn)(void* message, uint32_t item_id);
  const uint32_t app_object = ReadAppObjectPointer();
  if (app_object == 0) {
    SetQuickbarWeaponEntryError(entry, ERROR_NOT_FOUND);
    return;
  }

  DWORD last_error = ERROR_SUCCESS;
  __try {
    const ResolveItemMessageFn resolve_item_message =
        reinterpret_cast<ResolveItemMessageFn>(RebaseAddress(kExpectedItemMessageResolver));
    const RequestItemDetailFn request_detail =
        reinterpret_cast<RequestItemDetailFn>(RebaseAddress(kExpectedItemDetailRequest));
    void* item_message = resolve_item_message(reinterpret_cast<void*>(app_object));
    if (item_message == nullptr) {
      last_error = ERROR_NOT_FOUND;
    } else {
      request_detail(item_message, item_id);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = GetExceptionCode();
  }

  if (last_error != ERROR_SUCCESS) {
    SetQuickbarWeaponEntryError(entry, last_error);
    EnterCriticalSection(&g_quickbar_weapon_detail_lock);
    const int cache_index = FindQuickbarWeaponDetailCacheIndexLocked(item_id, false);
    if (cache_index >= 0) {
      ClearQuickbarWeaponDetailPendingLocked(&g_quickbar_weapon_detail_cache[cache_index]);
    }
    LeaveCriticalSection(&g_quickbar_weapon_detail_lock);
    MaybeUninstallQuickbarWeaponDetailHooks();
  }

  LogMessage(
      kLogDebug,
      "quickbar weapon detail request item=0x%08X hand=%d err=%lu",
      item_id,
      hand,
      static_cast<unsigned long>(last_error));
}

LONG ResolveQuickbarWeaponEquippedState(void* item_object, void* item_equipped_owner_fn, uint32_t current_player_object_id) {
  if (item_object == nullptr || item_equipped_owner_fn == nullptr || current_player_object_id == 0) {
    return 0;
  }

  typedef void* (__thiscall* ItemEquippedOwnerFn)(void* item_object);
  const ItemEquippedOwnerFn item_equipped_owner = reinterpret_cast<ItemEquippedOwnerFn>(item_equipped_owner_fn);
  __try {
    void* owner = item_equipped_owner(item_object);
    if (owner == nullptr) {
      return 0;
    }
    return SafeReadPointer32(reinterpret_cast<uintptr_t>(owner) + 4u) == current_player_object_id ? 1 : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -1;
  }
}

int FindQuickbarWeaponDetailResult(
    QuickbarWeaponDetailScanResult* results,
    int count,
    uint32_t item_id) {
  if (results == nullptr || !IsValidObjectId(item_id)) {
    return -1;
  }

  for (int index = 0; index < count; ++index) {
    if (results[index].item_id == item_id) {
      return index;
    }
  }
  return -1;
}

int CollectQuickbarWeaponDetailTargets(
    const QuickbarWeaponInfoEntry* entries,
    int count,
    QuickbarWeaponDetailScanResult* results,
    int capacity,
    bool pending_only) {
  if (entries == nullptr || results == nullptr || capacity <= 0) {
    return 0;
  }

  int result_count = 0;
  for (int index = 0; index < count; ++index) {
    const QuickbarWeaponInfoEntry& entry = entries[index];
    const uint32_t ids[] = {entry.primary_item_id, entry.secondary_item_id};
    for (int id_index = 0; id_index < 2; ++id_index) {
      const uint32_t item_id = ids[id_index];
      if (!IsValidObjectId(item_id) ||
          (pending_only && !IsQuickbarWeaponDetailItemPending(item_id)) ||
          FindQuickbarWeaponDetailResult(results, result_count, item_id) >= 0) {
        continue;
      }
      if (result_count >= capacity) {
        return result_count;
      }
      ZeroMemory(&results[result_count], sizeof(results[result_count]));
      results[result_count].item_id = item_id;
      ++result_count;
    }
  }
  return result_count;
}

bool IsMemoryRangeNearAddress(uintptr_t range_base, uintptr_t range_end, uintptr_t address, uintptr_t distance) {
  if (address == 0 || range_end <= range_base) {
    return false;
  }

  const uintptr_t low = address > distance ? address - distance : 0;
  const uintptr_t high = address > UINTPTR_MAX - distance ? UINTPTR_MAX : address + distance;
  return range_base <= high && range_end >= low;
}

bool LooksLikeClientItemDetailRecord(uintptr_t object_id_address, uintptr_t region_end) {
  if (object_id_address + 16u >= region_end) {
    return false;
  }

  uint32_t marker = 0;
  uint32_t text_length = 0;
  BYTE first_text_byte = 0;
  if (!SafeReadValue(object_id_address + 4u, &marker) ||
      !SafeReadValue(object_id_address + 8u, &text_length) ||
      !SafeReadValue(object_id_address + 12u, &first_text_byte)) {
    return false;
  }

  if (marker == kClientItemQuickbarObjectMarker) {
    return text_length > 0u && text_length < 0x4000u;
  }

  return marker == kClientItemDetailObjectMarker &&
      text_length > 0u &&
      text_length < 0x4000u &&
      (first_text_byte == '-' || first_text_byte == '\r' || first_text_byte == '\n' ||
          (first_text_byte >= 0x20u && first_text_byte < 0x7Fu));
}

int ExtractQuickbarWeaponDetailRowsBeforeObjectId(
    uintptr_t object_id_address,
    uintptr_t region_base,
    int hand,
    QuickbarWeaponPropertyRow* rows,
    int capacity) {
  if (rows == nullptr || capacity <= 0 || object_id_address <= region_base) {
    return 0;
  }

  uintptr_t scan_start = object_id_address > static_cast<uintptr_t>(kQuickbarWeaponDetailBackscanBytes)
      ? object_id_address - static_cast<uintptr_t>(kQuickbarWeaponDetailBackscanBytes)
      : region_base;
  if (scan_start < region_base) {
    scan_start = region_base;
  }

  int row_count = 0;
  for (uintptr_t cursor = scan_start; cursor + 7u <= object_id_address; ++cursor) {
    QuickbarWeaponPropertyRow row = {};
    if (!ReadCompactItemPropertyRow(cursor, hand, &row)) {
      continue;
    }
    if (row_count < capacity) {
      rows[row_count] = row;
    }
    ++row_count;
  }
  return row_count;
}

void ScanQuickbarWeaponDetailRegion(
    uintptr_t region_base,
    uintptr_t region_end,
    QuickbarWeaponDetailScanResult* results,
    int result_count,
    bool single_row_results_only) {
  if (results == nullptr || result_count <= 0 || region_end <= region_base + sizeof(uint32_t)) {
    return;
  }

  __try {
    const BYTE* const bytes = reinterpret_cast<const BYTE*>(region_base);
    const size_t region_size = static_cast<size_t>(region_end - region_base);
    if (region_size < sizeof(uint32_t)) {
      return;
    }

    for (int result_index = 0; result_index < result_count; ++result_index) {
      QuickbarWeaponDetailScanResult* result = &results[result_index];
      if (!IsValidObjectId(result->item_id)) {
        continue;
      }
      if (single_row_results_only && result->row_count != 1) {
        continue;
      }

      const BYTE* const needle = reinterpret_cast<const BYTE*>(&result->item_id);
      size_t offset = 0;
      while (offset + sizeof(uint32_t) <= region_size) {
        const void* found = memchr(bytes + offset, needle[0], region_size - offset - sizeof(uint32_t) + 1u);
        if (found == nullptr) {
          break;
        }

        const BYTE* const match = static_cast<const BYTE*>(found);
        offset = static_cast<size_t>(match - bytes) + 1u;
        const uintptr_t object_id_address = region_base + static_cast<uintptr_t>(match - bytes);
        uint32_t value = 0;
        memcpy(&value, match, sizeof(value));
        if (value != result->item_id ||
            !LooksLikeClientItemDetailRecord(object_id_address, region_end)) {
          continue;
        }

        QuickbarWeaponPropertyRow rows[kQuickbarWeaponMaxDamageRows] = {};
        const int row_count = ExtractQuickbarWeaponDetailRowsBeforeObjectId(
            object_id_address,
            region_base,
            1,
            rows,
            kQuickbarWeaponMaxDamageRows);
        result->found = 1;
        if (row_count <= result->row_count) {
          continue;
        }

        result->row_count = row_count;
        ZeroMemory(result->rows, sizeof(result->rows));
        const int stored = row_count < kQuickbarWeaponMaxDamageRows ? row_count : kQuickbarWeaponMaxDamageRows;
        for (int row_index = 0; row_index < stored; ++row_index) {
          result->rows[row_index] = rows[row_index];
        }
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    LogMessage(
        kLogDebug,
        "quickbar weapon detail region scan skipped base=0x%08X code=0x%08lX",
        static_cast<unsigned int>(region_base),
        static_cast<unsigned long>(GetExceptionCode()));
  }
}

void ApplyQuickbarWeaponDetailRows(
    QuickbarWeaponInfoEntry* entry,
    int hand,
    const QuickbarWeaponDetailScanResult& result) {
  if (entry == nullptr || result.found == 0) {
    return;
  }

  if (hand == 2) {
    entry->secondary_passive_property_count = result.row_count;
  } else {
    entry->primary_passive_property_count = result.row_count;
  }
  if (result.row_count <= 0) {
    return;
  }

  const int stored = result.row_count < kQuickbarWeaponMaxDamageRows
      ? result.row_count
      : kQuickbarWeaponMaxDamageRows;
  for (int row_index = 0; row_index < stored; ++row_index) {
    QuickbarWeaponPropertyRow row = result.rows[row_index];
    row.hand = hand;
    AddQuickbarWeaponDamageRow(entry, row);
  }
}

void ScanQuickbarWeaponDetailBlobs(QuickbarWeaponInfoEntry* entries, int count) {
  if (entries == nullptr || count <= 0) {
    return;
  }

  QuickbarWeaponDetailScanResult* results = g_state.quickbar_weapon_detail_results;
  ZeroMemory(g_state.quickbar_weapon_detail_results, sizeof(g_state.quickbar_weapon_detail_results));
  const int result_count = CollectQuickbarWeaponDetailTargets(
      entries,
      count,
      results,
      kQuickbarWeaponDetailMaxUniqueItems,
      true);
  if (result_count <= 0) {
    return;
  }

  SYSTEM_INFO system_info = {};
  GetSystemInfo(&system_info);
  uintptr_t cursor = reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress);
  const uintptr_t maximum = reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress);
  const uintptr_t quickbar_panel =
      static_cast<uintptr_t>(InterlockedCompareExchange(&g_state.quickbar_this, 0, 0));
  const uintptr_t app_object = static_cast<uintptr_t>(ReadAppObjectPointer());

  while (cursor < maximum) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
      break;
    }

    const uintptr_t region_base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t region_end = region_base + mbi.RegionSize;
    if (region_end < region_base) {
      break;
    }

    if (mbi.State == MEM_COMMIT &&
        mbi.Type == MEM_PRIVATE &&
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
        IsReadableWritableProtection(mbi.Protect) &&
        IsMemoryRangeNearAddress(
            region_base,
            region_end,
            quickbar_panel,
            kQuickbarWeaponDetailNearbyBytes) &&
        region_end > region_base + sizeof(uint32_t)) {
      ScanQuickbarWeaponDetailRegion(region_base, region_end, results, result_count, false);
    }

    cursor = region_end;
  }

  bool needs_single_row_fallback = false;
  for (int result_index = 0; result_index < result_count; ++result_index) {
    if (results[result_index].row_count == 1) {
      needs_single_row_fallback = true;
      break;
    }
  }
  if (needs_single_row_fallback && app_object != 0) {
    cursor = reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress);
    while (cursor < maximum) {
      MEMORY_BASIC_INFORMATION mbi = {};
      if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        break;
      }

      const uintptr_t region_base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      const uintptr_t region_end = region_base + mbi.RegionSize;
      if (region_end < region_base) {
        break;
      }

      if (mbi.State == MEM_COMMIT &&
          mbi.Type == MEM_PRIVATE &&
          (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
          IsReadableWritableProtection(mbi.Protect) &&
          IsMemoryRangeNearAddress(
              region_base,
              region_end,
              app_object,
              kQuickbarWeaponDetailAppNearbyBytes) &&
          region_end > region_base + sizeof(uint32_t)) {
        ScanQuickbarWeaponDetailRegion(region_base, region_end, results, result_count, true);
      }

      cursor = region_end;
    }
  }

  for (int result_index = 0; result_index < result_count; ++result_index) {
    if (results[result_index].found != 0) {
      StoreQuickbarWeaponDetailCacheScanResult(results[result_index]);
    }
  }

  for (int entry_index = 0; entry_index < count; ++entry_index) {
    QuickbarWeaponInfoEntry* entry = &entries[entry_index];
    const int primary_index = FindQuickbarWeaponDetailResult(results, result_count, entry->primary_item_id);
    if (primary_index >= 0 && results[primary_index].found != 0) {
      ApplyQuickbarWeaponDetailRows(entry, 1, results[primary_index]);
    }

    const int secondary_index = FindQuickbarWeaponDetailResult(results, result_count, entry->secondary_item_id);
    if (secondary_index >= 0 && results[secondary_index].found != 0) {
      ApplyQuickbarWeaponDetailRows(entry, 2, results[secondary_index]);
    }
  }
}

void StoreQuickbarWeaponInfo(const QuickbarWeaponInfoEntry* entries, int count, DWORD last_error) {
  if (entries == nullptr) {
    count = 0;
  }
  if (count < 0) {
    count = 0;
  }
  if (count > kQuickbarWeaponEntryCount) {
    count = kQuickbarWeaponEntryCount;
  }

  if (g_state.lock_ready) {
    EnterCriticalSection(&g_state.lock);
  }
  if (entries != nullptr && count > 0) {
    memcpy(g_state.quickbar_weapons, entries, static_cast<size_t>(count) * sizeof(QuickbarWeaponInfoEntry));
  }
  if (count < kQuickbarWeaponEntryCount) {
    ZeroMemory(
        g_state.quickbar_weapons + count,
        static_cast<size_t>(kQuickbarWeaponEntryCount - count) * sizeof(QuickbarWeaponInfoEntry));
  }
  InterlockedExchange(&g_state.quickbar_weapon_count, count);
  InterlockedExchange(&g_state.quickbar_weapon_error, static_cast<LONG>(last_error));
  if (g_state.lock_ready) {
    LeaveCriticalSection(&g_state.lock);
  }
}

void CopyStoredQuickbarWeaponInfo(QuickbarWeaponInfoResponse* response) {
  if (response == nullptr) {
    return;
  }

  ZeroMemory(response, sizeof(*response));
  if (g_state.lock_ready) {
    EnterCriticalSection(&g_state.lock);
  }
  LONG count = InterlockedCompareExchange(&g_state.quickbar_weapon_count, 0, 0);
  if (count < 0) {
    count = 0;
  }
  if (count > kQuickbarWeaponEntryCount) {
    count = kQuickbarWeaponEntryCount;
  }
  response->count = count;
  response->last_error = InterlockedCompareExchange(&g_state.quickbar_weapon_error, 0, 0);
  response->success = response->last_error == ERROR_SUCCESS ? 1 : 0;
  if (count > 0) {
    memcpy(response->entries, g_state.quickbar_weapons, static_cast<size_t>(count) * sizeof(QuickbarWeaponInfoEntry));
  }
  if (g_state.lock_ready) {
    LeaveCriticalSection(&g_state.lock);
  }
}

void UpdateQuickbarWeaponInfoOnWindowThread() {
  typedef void* (__thiscall* ResolveObjectByIdFn)(void* app_object, uint32_t object_id);

  QuickbarWeaponInfoEntry* entries = g_state.quickbar_weapon_work;
  ZeroMemory(g_state.quickbar_weapon_work, sizeof(g_state.quickbar_weapon_work));
  for (int page = 0; page < kQuickbarPageCount; ++page) {
    for (int slot = 0; slot < kQuickbarSlotCount; ++slot) {
      InitializeQuickbarWeaponInfoEntry(
          &entries[page * kQuickbarSlotCount + slot],
          page,
          slot);
    }
  }

  DWORD last_error = ERROR_SUCCESS;
  __try {
    const uint32_t panel = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_this, 0, 0));
    if (panel == 0 || SafeReadPointer32(panel) != RebaseAddress(kExpectedQuickbarVtable)) {
      StoreQuickbarWeaponInfo(entries, kQuickbarWeaponEntryCount, ERROR_NOT_FOUND);
      return;
    }

    const uint32_t app_object = ReadAppObjectPointer();
    const uint32_t current_player_object_id = ReadCurrentPlayerObjectId();
    if (app_object == 0 || current_player_object_id == 0) {
      StoreQuickbarWeaponInfo(entries, kQuickbarWeaponEntryCount, ERROR_NOT_FOUND);
      return;
    }

    const ResolveObjectByIdFn resolve_object =
        reinterpret_cast<ResolveObjectByIdFn>(RebaseAddress(kExpectedObjectByIdResolver));
    void* item_equipped_owner_fn = reinterpret_cast<void*>(RebaseAddress(kExpectedItemEquippedOwnerResolver));
    const uint32_t detail_slot_mask_low =
        static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_weapon_detail_slot_mask_low, 0, 0));
    const uint32_t detail_slot_mask_high =
        static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_weapon_detail_slot_mask_high, 0, 0));

    for (int page = 0; page < kQuickbarPageCount; ++page) {
      for (int slot = 0; slot < kQuickbarSlotCount; ++slot) {
        QuickbarWeaponInfoEntry* entry = &entries[page * kQuickbarSlotCount + slot];
        const bool request_detail = IsQuickbarWeaponDetailSlotRequested(
            entry->bit_index,
            detail_slot_mask_low,
            detail_slot_mask_high);
        const uint32_t slot_ptr = panel +
            kQuickbarPanelSlotsOffset +
            static_cast<uint32_t>(page) * kQuickbarPageStride +
            static_cast<uint32_t>(slot) * kQuickbarSlotStride;
        entry->slot_ptr = slot_ptr;

        BYTE slot_type = 0;
        if (!SafeReadValue(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotTypeOffset, &slot_type)) {
          entry->slot_type = -1;
          SetQuickbarWeaponEntryError(entry, ERROR_INVALID_DATA);
          continue;
        }
        entry->slot_type = static_cast<int32_t>(slot_type);
        if (slot_type != kQuickbarItemSlotType) {
          continue;
        }

        const uint32_t primary_item_id =
            SafeReadPointer32(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotPrimaryItemOffset);
        const uint32_t secondary_item_id =
            SafeReadPointer32(static_cast<uintptr_t>(slot_ptr) + kQuickbarSlotSecondaryItemOffset);
        entry->primary_item_id = primary_item_id;
        entry->secondary_item_id = secondary_item_id;

        if (IsValidObjectId(primary_item_id)) {
          void* primary_item = resolve_object(reinterpret_cast<void*>(app_object), primary_item_id);
          entry->primary_item_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(primary_item));
          entry->primary_equipped = ResolveQuickbarWeaponEquippedState(
              primary_item,
              item_equipped_owner_fn,
              current_player_object_id);
          if (primary_item != nullptr) {
            ScanQuickbarWeaponItemProperties(entry->primary_item_ptr, 1, entry);
          } else {
            SetQuickbarWeaponEntryError(entry, ERROR_NOT_FOUND);
          }
          ScanQuickbarWeaponItemDescription(primary_item_id, 1, entry);
          ApplyQuickbarWeaponDetailCacheRows(primary_item_id, 1, entry);
          if (request_detail) {
            RequestQuickbarWeaponItemDetailOnWindowThread(primary_item_id, 1, entry);
          }
        }

        if (IsValidObjectId(secondary_item_id)) {
          void* secondary_item = resolve_object(reinterpret_cast<void*>(app_object), secondary_item_id);
          entry->secondary_item_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(secondary_item));
          entry->secondary_equipped = ResolveQuickbarWeaponEquippedState(
              secondary_item,
              item_equipped_owner_fn,
              current_player_object_id);
          if (secondary_item != nullptr) {
            ScanQuickbarWeaponItemProperties(entry->secondary_item_ptr, 2, entry);
          } else {
            SetQuickbarWeaponEntryError(entry, ERROR_NOT_FOUND);
          }
          ScanQuickbarWeaponItemDescription(secondary_item_id, 2, entry);
          ApplyQuickbarWeaponDetailCacheRows(secondary_item_id, 2, entry);
          if (request_detail) {
            RequestQuickbarWeaponItemDetailOnWindowThread(secondary_item_id, 2, entry);
          }
        }
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = GetExceptionCode();
    LogMessage(
        kLogError,
        "quickbar weapon info refresh raised exception code=0x%08lX",
        static_cast<unsigned long>(last_error));
  }

  StoreQuickbarWeaponInfo(entries, kQuickbarWeaponEntryCount, last_error);
  LogMessage(kLogDebug, "quickbar weapon info refreshed err=%lu", static_cast<unsigned long>(last_error));
}

void CopyStoredCharacterName(char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }

  out[0] = '\0';
  if (g_state.lock_ready) {
    EnterCriticalSection(&g_state.lock);
    StringCchCopyA(out, capacity, g_state.character_name);
    LeaveCriticalSection(&g_state.lock);
    return;
  }

  StringCchCopyA(out, capacity, g_state.character_name);
}

void StoreCharacterName(const char* text) {
  const char* resolved = text != nullptr ? text : "";
  if (g_state.lock_ready) {
    EnterCriticalSection(&g_state.lock);
    strncpy_s(g_state.character_name, sizeof(g_state.character_name), resolved, _TRUNCATE);
    LeaveCriticalSection(&g_state.lock);
    return;
  }

  strncpy_s(g_state.character_name, sizeof(g_state.character_name), resolved, _TRUNCATE);
}

bool IsReadableWritableProtection(DWORD protect) {
  const DWORD basic = protect & 0xFFu;
  return basic == PAGE_READWRITE ||
      basic == PAGE_WRITECOPY ||
      basic == PAGE_EXECUTE_READWRITE ||
      basic == PAGE_EXECUTE_WRITECOPY;
}

BOOL TryAdoptQuickbarPanel(uint32_t panel_ptr, LONG slot_index, LONG page_index, const char* source) {
  if (panel_ptr == 0) {
    return FALSE;
  }

  const uint32_t expected_vtable = RebaseAddress(kExpectedQuickbarVtable);
  const uint32_t expected_slot_dispatch = RebaseAddress(kExpectedQuickbarSlotDispatch);
  if (SafeReadPointer32(panel_ptr) != expected_vtable) {
    return FALSE;
  }

  const uint32_t current_page_base = SafeReadPointer32(static_cast<uintptr_t>(panel_ptr) + kQuickbarCurrentPageOffset);
  LONG resolved_page = page_index;
  bool page_matches = false;
  for (LONG page = 0; page < kQuickbarPageCount; ++page) {
    const uint32_t expected_page_base = panel_ptr + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(page) * kQuickbarPageStride;
    if (current_page_base == expected_page_base) {
      if (resolved_page < 0) {
        resolved_page = page;
      }
      page_matches = page == resolved_page;
      if (page_matches) {
        break;
      }
    }
  }
  if (!page_matches) {
    return FALSE;
  }

  if (SafeReadPointer32(static_cast<uintptr_t>(current_page_base) + 0x2Cu) != expected_slot_dispatch) {
    return FALSE;
  }

  const LONG previous_this = InterlockedExchange(&g_state.quickbar_this, static_cast<LONG>(panel_ptr));
  InterlockedExchange(&g_state.quickbar_page, resolved_page);
  if (slot_index >= 0) {
    InterlockedExchange(&g_state.quickbar_slot, slot_index);
  }
  if (previous_this != static_cast<LONG>(panel_ptr)) {
    LogMessage(
        kLogInfo,
        "quickbar panel captured via %s panel=0x%08X page=%ld slot=%ld currentPageBase=0x%08X",
        source != nullptr ? source : "unknown",
        panel_ptr,
        resolved_page,
        slot_index,
        current_page_base);
  }
  return TRUE;
}

BOOL TryDeriveQuickbarPanelFromSlot(uint32_t slot_ptr, LONG* out_panel, LONG* out_slot_index, LONG* out_page_index) {
  if (slot_ptr == 0) {
    return FALSE;
  }

  for (LONG page = 0; page < kQuickbarPageCount; ++page) {
    for (LONG slot = 0; slot < kQuickbarSlotCount; ++slot) {
      const uint32_t delta = kQuickbarPanelSlotsOffset +
          static_cast<uint32_t>(page) * kQuickbarPageStride +
          static_cast<uint32_t>(slot) * kQuickbarSlotStride;
      if (slot_ptr < delta) {
        continue;
      }

      const uint32_t panel_ptr = slot_ptr - delta;
      if (!TryAdoptQuickbarPanel(panel_ptr, slot, page, "slot-trace")) {
        continue;
      }

      if (out_panel != nullptr) {
        *out_panel = static_cast<LONG>(panel_ptr);
      }
      if (out_slot_index != nullptr) {
        *out_slot_index = slot;
      }
      if (out_page_index != nullptr) {
        *out_page_index = page;
      }
      return TRUE;
    }
  }

  return FALSE;
}

BOOL DiscoverQuickbarPanelByScan(const char* reason) {
  const LONG attempt = InterlockedIncrement(&g_state.quickbar_scan_attempts);
  const uint32_t expected_vtable = RebaseAddress(kExpectedQuickbarVtable);
  const uint32_t expected_slot_dispatch = RebaseAddress(kExpectedQuickbarSlotDispatch);
  SYSTEM_INFO system_info = {};
  GetSystemInfo(&system_info);

  LONG matches = 0;
  uint32_t found_panel = 0;
  LONG found_page = -1;

  LogMessage(
      kLogDebug,
      "quickbar scan starting attempt=%ld reason=%s min=0x%08X max=0x%08X",
      attempt,
      reason != nullptr ? reason : "scan",
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress)),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress)));

  __try {
    uintptr_t cursor = reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress);
    while (cursor < maximum) {
      MEMORY_BASIC_INFORMATION mbi = {};
      if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        break;
      }

      const uintptr_t region_base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
      const uintptr_t region_end = region_base + mbi.RegionSize;
      if (region_end < region_base) {
        break;
      }

      if (mbi.State == MEM_COMMIT &&
          (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
          IsReadableWritableProtection(mbi.Protect) &&
          region_end > region_base + kQuickbarCurrentPageOffset + sizeof(uint32_t)) {
        const uintptr_t limit = region_end - (kQuickbarCurrentPageOffset + sizeof(uint32_t));
        for (uintptr_t candidate = region_base; candidate <= limit; candidate += sizeof(uint32_t)) {
          const uint32_t candidate_vtable = SafeReadPointer32(candidate);
          if (candidate_vtable != expected_vtable) {
            continue;
          }

          const uint32_t current_page_base = SafeReadPointer32(candidate + kQuickbarCurrentPageOffset);
          if (current_page_base == 0) {
            continue;
          }
          if (SafeReadPointer32(static_cast<uintptr_t>(current_page_base) + 0x2Cu) != expected_slot_dispatch) {
            continue;
          }

          LONG page_match = -1;
          for (LONG page = 0; page < kQuickbarPageCount; ++page) {
            const uint32_t expected_page_base =
                static_cast<uint32_t>(candidate) + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(page) * kQuickbarPageStride;
            if (current_page_base == expected_page_base) {
              page_match = page;
              break;
            }
          }
          if (page_match < 0) {
            continue;
          }

          found_panel = static_cast<uint32_t>(candidate);
          found_page = page_match;
          ++matches;
          if (matches > 1) {
            break;
          }
        }
      }

      if (matches > 1) {
        break;
      }
      cursor = region_end;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    LogMessage(
        kLogError,
        "quickbar scan raised SEH exception attempt=%ld reason=%s",
        attempt,
        reason != nullptr ? reason : "scan");
    return FALSE;
  }

  if (matches == 1 && TryAdoptQuickbarPanel(found_panel, -1, found_page, "memory-scan")) {
    const LONG hits = InterlockedIncrement(&g_state.quickbar_scan_hits);
    LogMessage(
        kLogInfo,
        "quickbar scan found panel=0x%08X page=%ld attempt=%ld hits=%ld reason=%s",
        found_panel,
        found_page,
        attempt,
        hits,
        reason != nullptr ? reason : "scan");
    return TRUE;
  }

  LogMessage(
      kLogDebug,
      "quickbar scan found no unique panel attempt=%ld matches=%ld reason=%s",
      attempt,
      matches,
      reason != nullptr ? reason : "scan");
  return FALSE;
}

void __stdcall CaptureQuickbarExec(LONG quickbar_this, LONG slot_index) {
  TryAdoptQuickbarPanel(static_cast<uint32_t>(quickbar_this), slot_index, -1, "quickbar-exec");
  const LONG count = InterlockedIncrement(&g_state.quickbar_calls);
  if (count <= 5) {
    LogMessage(kLogDebug, "quickbar exec trace this=0x%08X slot=%ld calls=%ld", quickbar_this, slot_index, count);
  }
}

void __stdcall CaptureQuickbarSlotDispatch(LONG slot_ptr) {
  InterlockedExchange(&g_state.quickbar_slot_ptr, slot_ptr);
  BYTE slot_type = 0;
  SafeReadValue(static_cast<uintptr_t>(slot_ptr) + 0x84u, &slot_type);
  const LONG raw_slot_type = static_cast<LONG>(slot_type);
  const LONG slot_case = QuickbarSlotTypeToCaseIndex(raw_slot_type);
  InterlockedExchange(&g_state.quickbar_slot_type, raw_slot_type);

  LONG panel = 0;
  LONG slot_index = -1;
  LONG page_index = -1;
  if (TryDeriveQuickbarPanelFromSlot(static_cast<uint32_t>(slot_ptr), &panel, &slot_index, &page_index)) {
    LogMessage(
        kLogDebug,
        "quickbar slot trace slotPtr=0x%08X rawType=%u case=%ld panel=0x%08X page=%ld slot=%ld",
        slot_ptr,
        static_cast<unsigned int>(slot_type),
        slot_case,
        static_cast<uint32_t>(panel),
        page_index,
        slot_index);
  } else {
    LogMessage(
        kLogDebug,
        "quickbar slot trace slotPtr=0x%08X rawType=%u case=%ld (panel unresolved)",
        slot_ptr,
        static_cast<unsigned int>(slot_type),
        slot_case);
  }
}

bool ExtractNwnStringText(const void* nwn_string_object, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return false;
  }
  out[0] = '\0';

  if (nwn_string_object == nullptr) {
    return false;
  }

  uint32_t text_ptr = 0;
  int32_t text_length = 0;
  __try {
    text_ptr = *reinterpret_cast<const uint32_t*>(nwn_string_object);
    text_length = *reinterpret_cast<const int32_t*>(static_cast<const BYTE*>(nwn_string_object) + sizeof(uint32_t));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }

  if (text_ptr == 0 || text_length <= 0) {
    return false;
  }

  size_t copy_length = static_cast<size_t>(text_length);
  if (copy_length >= capacity) {
    copy_length = capacity - 1;
  }

  __try {
    memcpy(out, reinterpret_cast<const void*>(text_ptr), copy_length);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    out[0] = '\0';
    return false;
  }

  out[copy_length] = '\0';
  return true;
}

void QueueChatLine(const char* text) {
  if (text == nullptr || text[0] == '\0' || !g_state.chat_lock_ready) {
    return;
  }

  EnterCriticalSection(&g_state.chat_lock);

  const LONG sequence = InterlockedIncrement(&g_state.chat_sequence);
  const LONG write_index = InterlockedCompareExchange(&g_state.chat_write_index, 0, 0);
  ChatLineEntry* entry = &g_state.chat_lines[write_index % kChatQueueCapacity];
  entry->sequence = sequence;
  strncpy_s(entry->text, sizeof(entry->text), text, _TRUNCATE);

  const LONG next_index = (write_index + 1) % kChatQueueCapacity;
  InterlockedExchange(&g_state.chat_write_index, next_index);
  const LONG existing_count = InterlockedCompareExchange(&g_state.chat_count, 0, 0);
  if (existing_count < kChatQueueCapacity) {
    InterlockedExchange(&g_state.chat_count, existing_count + 1);
  }

  LeaveCriticalSection(&g_state.chat_lock);

  LogMessage(kLogDebug, "chat line captured seq=%ld text=%s", sequence, text);
}

void __stdcall CaptureChatWindowLog(LONG chat_this, const void* nwn_string_object) {
  char text[kChatTextCapacity] = {};
  if (!ExtractNwnStringText(nwn_string_object, text, sizeof(text))) {
    LogMessage(kLogDebug, "chat log hook saw an unreadable string this=0x%08X", static_cast<uint32_t>(chat_this));
    return;
  }

  QueueChatLine(text);
}

BOOL BuildChatPollResponse(const ChatPollRequest& request, BYTE* out_payload, DWORD capacity, DWORD* out_size) {
  if (out_payload == nullptr || out_size == nullptr || capacity < sizeof(ChatPollResponseHeader) || !g_state.chat_lock_ready) {
    return FALSE;
  }

  ChatPollResponseHeader* response = reinterpret_cast<ChatPollResponseHeader*>(out_payload);
  response->latest_sequence = InterlockedCompareExchange(&g_state.chat_sequence, 0, 0);
  response->line_count = 0;
  DWORD offset = sizeof(ChatPollResponseHeader);

  EnterCriticalSection(&g_state.chat_lock);

  const LONG latest_sequence = InterlockedCompareExchange(&g_state.chat_sequence, 0, 0);
  const LONG chat_count = InterlockedCompareExchange(&g_state.chat_count, 0, 0);
  const LONG write_index = InterlockedCompareExchange(&g_state.chat_write_index, 0, 0);
  response->latest_sequence = latest_sequence;

  if (chat_count > 0 && latest_sequence > request.after_sequence) {
    LONG oldest_sequence = latest_sequence - chat_count + 1;
    if (oldest_sequence < 1) {
      oldest_sequence = 1;
    }

    LONG first_sequence = request.after_sequence + 1;
    if (first_sequence < oldest_sequence) {
      first_sequence = oldest_sequence;
    }

    LONG max_lines = request.max_lines;
    if (max_lines <= 0 || max_lines > kChatQueueCapacity) {
      max_lines = kChatQueueCapacity;
    }

    if (latest_sequence >= first_sequence && latest_sequence - first_sequence + 1 > max_lines) {
      first_sequence = latest_sequence - max_lines + 1;
    }

    const LONG oldest_index = (write_index - chat_count + kChatQueueCapacity) % kChatQueueCapacity;
    for (LONG sequence = first_sequence; sequence <= latest_sequence; ++sequence) {
      const LONG index = (oldest_index + (sequence - oldest_sequence)) % kChatQueueCapacity;
      const ChatLineEntry& entry = g_state.chat_lines[index];
      if (entry.sequence != sequence) {
        continue;
      }

      const size_t text_length = strnlen(entry.text, sizeof(entry.text));
      const DWORD needed = static_cast<DWORD>(sizeof(ChatPollLineHeader) + text_length);
      if (offset + needed > capacity) {
        break;
      }

      ChatPollLineHeader* line = reinterpret_cast<ChatPollLineHeader*>(out_payload + offset);
      line->sequence = entry.sequence;
      line->text_length = static_cast<int32_t>(text_length);
      offset += sizeof(ChatPollLineHeader);
      if (text_length > 0) {
        memcpy(out_payload + offset, entry.text, text_length);
        offset += static_cast<DWORD>(text_length);
      }
      ++response->line_count;
    }
  }

  LeaveCriticalSection(&g_state.chat_lock);

  *out_size = offset;
  return TRUE;
}

#if defined(_M_IX86)
extern "C" __declspec(naked) void QuickbarExecTraceThunk() {
  __asm {
    pushad
    mov  eax, dword ptr [esp + 32 + 4]
    push eax
    mov  eax, ecx
    push eax
    call CaptureQuickbarExec
    popad
    mov  eax, dword ptr [g_quickbar_exec_gateway]
    jmp  eax
  }
}

extern "C" __declspec(naked) void QuickbarSlotDispatchTraceThunk() {
  __asm {
    pushad
    mov  eax, ecx
    push eax
    call CaptureQuickbarSlotDispatch
    popad
    mov  eax, dword ptr [g_quickbar_slot_gateway]
    jmp  eax
  }
}

extern "C" __declspec(naked) void ChatWindowLogTraceThunk() {
  __asm {
    pushad
    lea  eax, [esp + 32 + 4]
    push eax
    mov  eax, ecx
    push eax
    call CaptureChatWindowLog
    popad
    mov  eax, dword ptr [g_chat_log_gateway]
    jmp  eax
  }
}

extern "C" __declspec(naked) void ItemInfoParserEntryHook() {
  __asm {
    push dword ptr [esp + 8]
    push dword ptr [esp + 8]
    push ecx
    call TryQuietItemInfoParser
    add  esp, 12
    cmp  eax, 080000000h
    jne  handled
    mov  eax, dword ptr [g_item_info_parser_gateway]
    jmp  eax

handled:
    retn 8
  }
}

extern "C" __declspec(naked) void ItemInfoPopupBranchHook() {
  __asm {
    pushad
    call HasPendingQuickbarWeaponDetailRequests
    test eax, eax
    popad
    jnz capture_quiet

    push 0
    push 0
    mov  ecx, esi
    mov  eax, dword ptr [g_item_info_parser_address]
    call eax
    mov  eax, dword ptr [g_item_info_popup_return_address]
    jmp  eax

capture_quiet:
    push 0
    mov  eax, esp
    push eax
    push 1
    mov  ecx, esi
    mov  eax, dword ptr [g_item_info_parser_address]
    call eax
    mov  edx, [esp]
    add  esp, 4
    push eax
    push eax
    push edx
    call CaptureQuietItemDetailResult
    add  esp, 8
    pop  eax
    mov  eax, dword ptr [g_item_info_popup_return_address]
    jmp  eax
  }
}

extern "C" __declspec(naked) void ItemInfoPropertyRowAppendHook() {
  __asm {
    pushfd
    pushad
    mov  edi, ecx
    mov  esi, dword ptr [esp + 36 + 4]
    mov  ebp, dword ptr [esp + 36 + 8]
    mov  ebx, dword ptr [esp + 36 + 12]
    mov  edx, dword ptr [esp + 36 + 16]
    mov  eax, dword ptr [esp + 36 + 20]
    push eax
    push edx
    push ebx
    push ebp
    push esi
    push edi
    call CaptureItemInfoPropertyRow
    add  esp, 24
    popad
    popfd
    mov  eax, dword ptr [g_item_info_property_row_gateway]
    jmp  eax
  }
}

extern "C" __declspec(naked) void ItemInfoMessageHandlerCallHook() {
  __asm {
    mov  eax, dword ptr [esp]
    mov  dword ptr [g_item_info_message_handler_return_address], eax
    mov  dword ptr [esp], offset ItemInfoMessageHandlerAfterOriginal
    mov  eax, dword ptr [g_item_info_message_handler_gateway]
    jmp  eax
  }
}

extern "C" __declspec(naked) void ItemInfoMessageHandlerAfterOriginal() {
  __asm {
    pushfd
    pushad
    call HideQuietItemInfoPanelIfPending
    popad
    popfd
    mov  eax, dword ptr [g_item_info_message_handler_return_address]
    jmp  eax
  }
}
#endif

BOOL InstallQuickbarTraceHook() {
  if (InterlockedCompareExchange(&g_state.quickbar_trace_installed, 0, 0) != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedQuickbarExec));
  const size_t stolen = 10;
  memcpy(g_quickbar_exec_original, target, stolen);
  g_quickbar_exec_gateway = MakeJmpGateway(target, stolen);
  if (g_quickbar_exec_gateway == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  BYTE patch[10] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&QuickbarExecTraceThunk) - (target + 5));
  for (size_t i = 5; i < stolen; ++i) {
    patch[i] = 0x90;
  }
  WriteExecutableMemory(target, patch, stolen);
  g_quickbar_exec_stolen = stolen;
  InterlockedExchange(&g_state.quickbar_trace_installed, 1);
  LogMessage(
      kLogInfo,
      "installed quickbar exec trace hook at 0x%08X stolen=%u gateway=0x%08X",
      RebaseAddress(kExpectedQuickbarExec),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_quickbar_exec_gateway)));
  return TRUE;
}

BOOL InstallQuickbarSlotTraceHook() {
  if (InterlockedCompareExchange(&g_state.quickbar_slot_trace_installed, 0, 0) != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedQuickbarSlotDispatch));
  const size_t stolen = 6;
  memcpy(g_quickbar_slot_original, target, stolen);
  g_quickbar_slot_gateway = MakeJmpGateway(target, stolen);
  if (g_quickbar_slot_gateway == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  BYTE patch[6] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&QuickbarSlotDispatchTraceThunk) - (target + 5));
  patch[5] = 0x90;
  WriteExecutableMemory(target, patch, stolen);
  g_quickbar_slot_stolen = stolen;
  InterlockedExchange(&g_state.quickbar_slot_trace_installed, 1);
  LogMessage(
      kLogInfo,
      "installed quickbar slot trace hook at 0x%08X stolen=%u gateway=0x%08X",
      RebaseAddress(kExpectedQuickbarSlotDispatch),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_quickbar_slot_gateway)));
  return TRUE;
}

BOOL InstallChatWindowLogHook() {
  if (InterlockedCompareExchange(&g_state.chat_trace_installed, 0, 0) != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedChatWindowLog));
  const size_t stolen = 21;
  memcpy(g_chat_log_original, target, stolen);
  g_chat_log_gateway = MakeJmpGateway(target, stolen);
  if (g_chat_log_gateway == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  BYTE patch[21] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&ChatWindowLogTraceThunk) - (target + 5));
  for (size_t i = 5; i < stolen; ++i) {
    patch[i] = 0x90;
  }
  WriteExecutableMemory(target, patch, stolen);
  g_chat_log_stolen = stolen;
  InterlockedExchange(&g_state.chat_trace_installed, 1);
  LogMessage(
      kLogInfo,
      "installed chat window log hook at 0x%08X stolen=%u gateway=0x%08X",
      RebaseAddress(kExpectedChatWindowLog),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_chat_log_gateway)));
  return TRUE;
}

BOOL InstallItemInfoParserHook() {
  if (InterlockedCompareExchange(&g_state.item_info_parser_hook_installed, 0, 0) != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoParser));
  const size_t stolen = 9;
  BYTE original[16] = {};
  memcpy(original, target, stolen);
  if (original[0] != 0x83 || original[1] != 0xEC || original[2] != 0x40 ||
      original[3] != 0x53 || original[4] != 0x55 || original[5] != 0x56 ||
      original[6] != 0x8B || original[7] != 0xE9 || original[8] != 0x57) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  memcpy(g_item_info_parser_original, original, stolen);
  g_item_info_parser_gateway = MakeJmpGateway(target, stolen);
  if (g_item_info_parser_gateway == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  BYTE patch[16] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&ItemInfoParserEntryHook) - (target + 5));
  for (size_t index = 5; index < stolen; ++index) {
    patch[index] = 0x90;
  }
  WriteExecutableMemory(target, patch, stolen);
  g_item_info_parser_stolen = stolen;
  InterlockedExchange(&g_state.item_info_parser_hook_installed, 1);
  LogMessage(
      kLogInfo,
      "installed item info parser quiet hook at 0x%08X stolen=%u gateway=0x%08X",
      RebaseAddress(kExpectedItemInfoParser),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_item_info_parser_gateway)));
  return TRUE;
}

BOOL InstallItemInfoPopupHook() {
  if (g_item_info_popup_stolen != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoPopupBranch));
  const size_t stolen = 11;
  BYTE original[16] = {};
  memcpy(original, target, stolen);
  if (original[0] != 0x6A || original[1] != 0x00 ||
      original[2] != 0x6A || original[3] != 0x00 ||
      original[4] != 0x8B || original[5] != 0xCE ||
      original[6] != 0xE8) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  const int32_t call_rel = *reinterpret_cast<int32_t*>(original + 7);
  const BYTE* call_target = target + stolen + call_rel;
  if (reinterpret_cast<uint32_t>(call_target) != RebaseAddress(kExpectedItemInfoParser)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  memcpy(g_item_info_popup_original, original, stolen);
  g_item_info_parser_address = RebaseAddress(kExpectedItemInfoParser);
  g_item_info_popup_return_address = RebaseAddress(kExpectedItemInfoPopupBranchReturn);

  BYTE patch[16] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&ItemInfoPopupBranchHook) - (target + 5));
  for (size_t index = 5; index < stolen; ++index) {
    patch[index] = 0x90;
  }
  WriteExecutableMemory(target, patch, stolen);
  g_item_info_popup_stolen = stolen;
  LogMessage(
      kLogInfo,
      "installed item info quiet detail hook at 0x%08X stolen=%u return=0x%08X",
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(target)),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(g_item_info_popup_return_address));
  return TRUE;
}

BOOL InstallItemInfoPropertyRowHook() {
  if (g_item_info_property_row_stolen != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoPropertyRowCall));
  const size_t stolen = 5;
  BYTE original[16] = {};
  memcpy(original, target, stolen);
  if (original[0] != 0xE8) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  const int32_t call_rel = *reinterpret_cast<int32_t*>(original + 1);
  const BYTE* call_target = target + stolen + call_rel;
  if (reinterpret_cast<uint32_t>(call_target) != RebaseAddress(kExpectedItemInfoPropertyRowAppend)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  memcpy(g_item_info_property_row_original, original, stolen);
  g_item_info_property_row_gateway =
      reinterpret_cast<void*>(RebaseAddress(kExpectedItemInfoPropertyRowAppend));

  BYTE patch[5] = {};
  patch[0] = 0xE8;
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&ItemInfoPropertyRowAppendHook) - (target + 5));
  WriteExecutableMemory(target, patch, stolen);
  g_item_info_property_row_stolen = stolen;
  LogMessage(
      kLogInfo,
      "installed item info property row call hook at 0x%08X stolen=%u target=0x%08X",
      RebaseAddress(kExpectedItemInfoPropertyRowCall),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_item_info_property_row_gateway)));
  return TRUE;
}

BOOL UninstallItemInfoPropertyRowHook() {
  if (g_item_info_property_row_stolen == 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoPropertyRowCall));
  WriteExecutableMemory(target, g_item_info_property_row_original, g_item_info_property_row_stolen);
  LogMessage(
      kLogInfo,
      "uninstalled item info property row call hook at 0x%08X restored=%u",
      RebaseAddress(kExpectedItemInfoPropertyRowCall),
      static_cast<unsigned int>(g_item_info_property_row_stolen));
  g_item_info_property_row_stolen = 0;
  return TRUE;
}

BOOL InstallItemInfoMessageHandlerHook() {
  if (g_item_info_message_handler_stolen != 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoMessageHandlerCall));
  const size_t stolen = 5;
  BYTE original[8] = {};
  memcpy(original, target, stolen);
  if (original[0] != 0xE8) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  const int32_t call_rel = *reinterpret_cast<int32_t*>(original + 1);
  const BYTE* call_target = target + stolen + call_rel;
  if (reinterpret_cast<uint32_t>(call_target) != RebaseAddress(kExpectedItemInfoMessageHandler)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  memcpy(g_item_info_message_handler_original, original, stolen);
  g_item_info_message_handler_gateway =
      reinterpret_cast<void*>(RebaseAddress(kExpectedItemInfoMessageHandler));

  BYTE patch[5] = {};
  patch[0] = 0xE8;
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&ItemInfoMessageHandlerCallHook) - (target + 5));
  WriteExecutableMemory(target, patch, stolen);
  g_item_info_message_handler_stolen = stolen;
  LogMessage(
      kLogInfo,
      "installed item info message handler call hook at 0x%08X stolen=%u target=0x%08X",
      RebaseAddress(kExpectedItemInfoMessageHandlerCall),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_item_info_message_handler_gateway)));
  return TRUE;
}

BOOL UninstallItemInfoMessageHandlerHook() {
  if (g_item_info_message_handler_stolen == 0) {
    return TRUE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedItemInfoMessageHandlerCall));
  WriteExecutableMemory(target, g_item_info_message_handler_original, g_item_info_message_handler_stolen);
  LogMessage(
      kLogInfo,
      "uninstalled item info message handler call hook at 0x%08X restored=%u",
      RebaseAddress(kExpectedItemInfoMessageHandlerCall),
      static_cast<unsigned int>(g_item_info_message_handler_stolen));
  g_item_info_message_handler_stolen = 0;
  return TRUE;
}

BOOL EnsureQuickbarWeaponDetailHooksInstalled() {
  if (!InstallItemInfoPropertyRowHook()) {
    return FALSE;
  }
  if (!InstallItemInfoMessageHandlerHook()) {
    const DWORD error = GetLastError();
    UninstallItemInfoPropertyRowHook();
    SetLastError(error);
    return FALSE;
  }
  return TRUE;
}

void MaybeUninstallQuickbarWeaponDetailHooks() {
  if (HasPendingQuickbarWeaponDetailRequests()) {
    return;
  }
  UninstallItemInfoMessageHandlerHook();
  UninstallItemInfoPropertyRowHook();
}

LRESULT CallQuickbarExecDirect(int slot_index) {
  typedef void (__thiscall* QuickbarExecFn)(void* self, int slot_index);
  LONG quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
  if (quickbar_this == 0) {
    DiscoverQuickbarPanelByScan("direct-call");
    quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
  }
  if (quickbar_this == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return 0;
  }

  QuickbarExecFn fn = reinterpret_cast<QuickbarExecFn>(RebaseAddress(kExpectedQuickbarExec));
  fn(reinterpret_cast<void*>(quickbar_this), slot_index);
  InterlockedExchange(&g_state.quickbar_slot, slot_index);
  return 1;
}

LONG ResolveQuickbarPageIndex(uint32_t panel_ptr) {
  if (panel_ptr == 0) {
    return -1;
  }

  const uint32_t current_page_base = SafeReadPointer32(static_cast<uintptr_t>(panel_ptr) + kQuickbarCurrentPageOffset);
  if (current_page_base == 0) {
    return -1;
  }

  for (LONG page = 0; page < kQuickbarPageCount; ++page) {
    const uint32_t expected_page_base =
        panel_ptr + kQuickbarPanelSlotsOffset + static_cast<uint32_t>(page) * kQuickbarPageStride;
    if (current_page_base == expected_page_base) {
      return page;
    }
  }

  return -1;
}

BOOL CallQuickbarPageSelectDirect(int page_index, LONG* out_resolved_page) {
  typedef void (__thiscall* QuickbarPageSelectFn)(void* self, int page_index);

  if (page_index < 0 || page_index >= kQuickbarPageCount) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  LONG quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
  if (quickbar_this == 0) {
    DiscoverQuickbarPanelByScan("direct-page-select");
    quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
  }
  if (quickbar_this == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
  }

  const LONG previous_page = ResolveQuickbarPageIndex(static_cast<uint32_t>(quickbar_this));
  QuickbarPageSelectFn fn = reinterpret_cast<QuickbarPageSelectFn>(RebaseAddress(kExpectedQuickbarPageSelect));
  fn(reinterpret_cast<void*>(quickbar_this), page_index);

  const LONG resolved_page = ResolveQuickbarPageIndex(static_cast<uint32_t>(quickbar_this));
  if (out_resolved_page != nullptr) {
    *out_resolved_page = resolved_page;
  }

  if (resolved_page >= 0) {
    TryAdoptQuickbarPanel(static_cast<uint32_t>(quickbar_this), -1, resolved_page, "direct-page-select");
  }

  LogMessage(
      kLogDebug,
      "quickbar page select direct panel=0x%08X request=%d previous=%ld resolved=%ld",
      static_cast<unsigned int>(quickbar_this),
      page_index,
      previous_page,
      resolved_page);

  if (resolved_page != page_index) {
    SetLastError(ERROR_INVALID_STATE);
    return FALSE;
  }

  return TRUE;
}

void AppendFormat(char* buffer, size_t capacity, size_t* offset, const char* format, ...) {
  if (buffer == nullptr || offset == nullptr || *offset >= capacity) {
    return;
  }

  va_list args;
  va_start(args, format);
  const int written = _vsnprintf_s(buffer + *offset, capacity - *offset, _TRUNCATE, format, args);
  va_end(args);

  if (written < 0) {
    *offset = strlen(buffer);
  } else {
    *offset += static_cast<size_t>(written);
  }
}

void WriteExecutableMemory(void* destination, const void* source, SIZE_T size) {
  DWORD old_protect = 0;
  if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
    return;
  }
  memcpy(destination, source, size);
  DWORD ignored = 0;
  VirtualProtect(destination, size, old_protect, &ignored);
  FlushInstructionCache(GetCurrentProcess(), destination, size);
}

void* MakeJmpGateway(BYTE* target, size_t stolen) {
  BYTE* gateway = static_cast<BYTE*>(VirtualAlloc(nullptr, stolen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (gateway == nullptr) {
    return nullptr;
  }

  memcpy(gateway, target, stolen);
  const intptr_t return_rel = reinterpret_cast<intptr_t>(target + stolen) - reinterpret_cast<intptr_t>(gateway + stolen + 5);
  gateway[stolen] = 0xE9;
  *reinterpret_cast<int32_t*>(gateway + stolen + 1) = static_cast<int32_t>(return_rel);
  return gateway;
}

HMODULE ResolveOpenGlModule() {
  HMODULE opengl = GetModuleHandleA("OPENGL32.DLL");
  if (opengl == nullptr) {
    opengl = LoadLibraryA("OPENGL32.DLL");
  }
  return opengl;
}

BOOL ResolveOpenGlFunctions() {
  if (g_glDisable != nullptr && g_glDrawPixels != nullptr) {
    return TRUE;
  }

  HMODULE opengl = ResolveOpenGlModule();
  if (opengl == nullptr) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return FALSE;
  }

  g_glDisable = reinterpret_cast<GlDisableFn>(GetProcAddress(opengl, "glDisable"));
  g_glEnable = reinterpret_cast<GlEnableFn>(GetProcAddress(opengl, "glEnable"));
  g_glDepthMask = reinterpret_cast<GlDepthMaskFn>(GetProcAddress(opengl, "glDepthMask"));
  g_glColor3f = reinterpret_cast<GlColor3fFn>(GetProcAddress(opengl, "glColor3f"));
  g_glMatrixMode = reinterpret_cast<GlMatrixModeFn>(GetProcAddress(opengl, "glMatrixMode"));
  g_glPushMatrix = reinterpret_cast<GlPushMatrixFn>(GetProcAddress(opengl, "glPushMatrix"));
  g_glPopMatrix = reinterpret_cast<GlPopMatrixFn>(GetProcAddress(opengl, "glPopMatrix"));
  g_glLoadIdentity = reinterpret_cast<GlLoadIdentityFn>(GetProcAddress(opengl, "glLoadIdentity"));
  g_glGetIntegerv = reinterpret_cast<GlGetIntegervFn>(GetProcAddress(opengl, "glGetIntegerv"));
  g_glBindTexture = reinterpret_cast<GlBindTextureFn>(GetProcAddress(opengl, "glBindTexture"));
  g_glRasterPos2f = reinterpret_cast<GlRasterPos2fFn>(GetProcAddress(opengl, "glRasterPos2f"));
  g_glDrawPixels = reinterpret_cast<GlDrawPixelsFn>(GetProcAddress(opengl, "glDrawPixels"));
  g_glIsEnabled = reinterpret_cast<GlIsEnabledFn>(GetProcAddress(opengl, "glIsEnabled"));
  g_glBlendFunc = reinterpret_cast<GlBlendFuncFn>(GetProcAddress(opengl, "glBlendFunc"));

  if (g_glDisable == nullptr || g_glEnable == nullptr || g_glDepthMask == nullptr ||
      g_glColor3f == nullptr || g_glMatrixMode == nullptr || g_glPushMatrix == nullptr ||
      g_glPopMatrix == nullptr || g_glLoadIdentity == nullptr || g_glGetIntegerv == nullptr ||
      g_glBindTexture == nullptr || g_glRasterPos2f == nullptr || g_glDrawPixels == nullptr ||
      g_glIsEnabled == nullptr || g_glBlendFunc == nullptr) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return FALSE;
  }
  return TRUE;
}

void FreeOverlayPixels(OverlayRecord* record) {
  if (record == nullptr) {
    return;
  }
  if (record->pixels != nullptr) {
    HeapFree(GetProcessHeap(), 0, record->pixels);
  }
  ZeroMemory(record, sizeof(*record));
}

int FindOverlayIndexLocked(int32_t id) {
  for (int index = 0; index < kMaxOverlays; ++index) {
    if (g_state.overlays[index].enabled && g_state.overlays[index].id == id) {
      return index;
    }
  }
  return -1;
}

int FindFreeOverlayIndexLocked() {
  for (int index = 0; index < kMaxOverlays; ++index) {
    if (!g_state.overlays[index].enabled) {
      return index;
    }
  }
  return -1;
}

LONG CountOverlaysLocked() {
  LONG count = 0;
  for (int index = 0; index < kMaxOverlays; ++index) {
    if (g_state.overlays[index].enabled) {
      ++count;
    }
  }
  return count;
}

void UpdateOverlayRasterPosition(OverlayRecord* record, int viewport_width, int viewport_height) {
  if (record == nullptr || viewport_width <= 0 || viewport_height <= 0) {
    return;
  }

  int x = 0;
  int y = 0;
  switch (record->position) {
    case 1:
      x = 20;
      y = 50;
      break;
    case 2:
      x = (viewport_width - record->width) / 2;
      y = 50;
      break;
    case 3:
      x = viewport_width - record->width - 90;
      y = 50;
      break;
    case 4:
      x = 20;
      y = (viewport_height - record->height) / 2;
      break;
    case 5:
      x = (viewport_width - record->width) / 2;
      y = (viewport_height - record->height) / 2;
      break;
    case 6:
      x = viewport_width - record->width - 90;
      y = (viewport_height - record->height) / 2;
      break;
    case 7:
      x = 20;
      y = viewport_height - record->height - 70;
      break;
    case 8:
      x = (viewport_width - record->width) / 2;
      y = viewport_height - record->height - 70;
      break;
    case 9:
      x = viewport_width - record->width - 90;
      y = viewport_height - record->height - 70;
      break;
    case 0:
    default:
      x = 0;
      y = 0;
      break;
  }

  x += record->offset_x;
  y += record->offset_y;
  record->pixel_x = x;
  record->pixel_y = y;
  record->raster_x = (static_cast<float>(x) * 2.0f / static_cast<float>(viewport_width)) - 1.0f;
  record->raster_y = 1.0f - (static_cast<float>(y + record->height) * 2.0f / static_cast<float>(viewport_height));
  record->viewport_width = viewport_width;
  record->viewport_height = viewport_height;
}

BOOL StoreOverlayBitmap(
    int32_t id,
    int32_t position,
    int32_t offset_x,
    int32_t offset_y,
    int32_t width,
    int32_t height,
    const void* pixels,
    DWORD pixel_bytes,
    const OverlayControlButton* controls,
    int32_t control_count) {
  if (!g_state.overlay_lock_ready || id < 0 || width <= 0 || height <= 0 ||
      width > kOverlayMaxDimension || height > kOverlayMaxDimension || pixels == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  const uint64_t expected_bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
  if (expected_bytes == 0 || expected_bytes > 0x7FFFFFFFu || pixel_bytes != static_cast<DWORD>(expected_bytes)) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  BYTE* copy = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(pixel_bytes)));
  if (copy == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }
  memcpy(copy, pixels, pixel_bytes);

  EnterCriticalSection(&g_state.overlay_lock);
  int index = FindOverlayIndexLocked(id);
  if (index < 0) {
    index = FindFreeOverlayIndexLocked();
  }
  if (index < 0) {
    LeaveCriticalSection(&g_state.overlay_lock);
    HeapFree(GetProcessHeap(), 0, copy);
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  OverlayRecord* record = &g_state.overlays[index];
  if (record->pixels != nullptr) {
    HeapFree(GetProcessHeap(), 0, record->pixels);
  }
  record->id = id;
  record->position = position;
  record->offset_x = offset_x;
  record->offset_y = offset_y;
  record->width = width;
  record->height = height;
  record->viewport_width = 0;
  record->viewport_height = 0;
  record->pixel_x = 0;
  record->pixel_y = 0;
  record->raster_x = 0.0f;
  record->raster_y = 0.0f;
  record->pixel_bytes = pixel_bytes;
  record->pixels = copy;
  record->control_count = 0;
  if (controls != nullptr && control_count > 0) {
    const int32_t count = control_count > kOverlayMaxControls ? kOverlayMaxControls : control_count;
    CopyMemory(record->controls, controls, sizeof(OverlayControlButton) * count);
    record->control_count = count;
  }
  record->enabled = true;
  InterlockedExchange(&g_state.overlay_count, CountOverlaysLocked());
  LeaveCriticalSection(&g_state.overlay_lock);
  return TRUE;
}

BOOL ClearOverlayById(int32_t id) {
  if (!g_state.overlay_lock_ready) {
    SetLastError(ERROR_NOT_READY);
    return FALSE;
  }
  EnterCriticalSection(&g_state.overlay_lock);
  const int index = FindOverlayIndexLocked(id);
  if (index >= 0) {
    FreeOverlayPixels(&g_state.overlays[index]);
  }
  InterlockedExchange(&g_state.overlay_count, CountOverlaysLocked());
  LeaveCriticalSection(&g_state.overlay_lock);
  return TRUE;
}

BOOL ClearAllOverlays() {
  if (!g_state.overlay_lock_ready) {
    SetLastError(ERROR_NOT_READY);
    return FALSE;
  }
  EnterCriticalSection(&g_state.overlay_lock);
  for (int index = 0; index < kMaxOverlays; ++index) {
    FreeOverlayPixels(&g_state.overlays[index]);
  }
  InterlockedExchange(&g_state.overlay_count, 0);
  LeaveCriticalSection(&g_state.overlay_lock);
  return TRUE;
}

struct ParsedOverlayLine {
  int start;
  int length;
  uint32_t color_rgb;
};

struct ParsedOverlayControl {
  char script_id[kOverlayControlIdCapacity];
  char label[kOverlayControlLabelCapacity];
  bool active;
};

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
  if (text == nullptr || out_color == nullptr || text[0] != kOverlayLineColorMarker) {
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

  if (text[7] != ';') {
    return false;
  }

  *out_color = color & 0xFFFFFFu;
  return true;
}

const char* ParseOverlayControls(
    const char* text,
    ParsedOverlayControl* controls,
    int control_capacity,
    int* out_control_count) {
  if (out_control_count != nullptr) {
    *out_control_count = 0;
  }
  if (text == nullptr || controls == nullptr || control_capacity <= 0 || out_control_count == nullptr) {
    return text != nullptr ? text : "";
  }

  const char prefix[] = "controls;";
  if (text[0] != kOverlayControlMarker || strncmp(text + 1, prefix, sizeof(prefix) - 1) != 0) {
    return text;
  }

  const char* cursor = text + 1 + (sizeof(prefix) - 1);
  const char* line_end = cursor;
  while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
    ++line_end;
  }

  char buffer[1024] = {};
  size_t length = static_cast<size_t>(line_end - cursor);
  if (length >= sizeof(buffer)) {
    length = sizeof(buffer) - 1;
  }
  CopyMemory(buffer, cursor, length);
  buffer[length] = '\0';

  int count = 0;
  char* context = nullptr;
  char* token = strtok_s(buffer, ";", &context);
  while (token != nullptr && count < control_capacity) {
    char* first = strchr(token, '|');
    char* second = first != nullptr ? strchr(first + 1, '|') : nullptr;
    if (first != nullptr && second != nullptr) {
      *first = '\0';
      *second = '\0';
      const char* script_id = token;
      const char* label = first + 1;
      const char* state = second + 1;
      if (script_id[0] != '\0' && label[0] != '\0') {
        StringCchCopyA(controls[count].script_id, _countof(controls[count].script_id), script_id);
        StringCchCopyA(controls[count].label, _countof(controls[count].label), label);
        controls[count].active = state[0] == '1' || state[0] == 't' || state[0] == 'T';
        ++count;
      }
    }
    token = strtok_s(nullptr, ";", &context);
  }

  *out_control_count = count;

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

int ParseOverlayLines(
    const char* text,
    uint32_t default_color_rgb,
    char* visible_text,
    int visible_capacity,
    ParsedOverlayLine* lines,
    int line_capacity) {
  if (visible_text == nullptr || visible_capacity <= 0 || lines == nullptr || line_capacity <= 0) {
    return 0;
  }

  visible_text[0] = '\0';
  if (text == nullptr) {
    text = "";
  }

  int src = 0;
  int dst = 0;
  int line_count = 0;
  while (text[src] != '\0' && line_count < line_capacity) {
    uint32_t line_color = default_color_rgb & 0xFFFFFFu;
    uint32_t parsed_color = 0;
    if (TryParseOverlayLineColor(text + src, &parsed_color)) {
      line_color = parsed_color;
      src += kOverlayLineColorMarkerLength;
    }

    const int line_start = dst;
    while (text[src] != '\0' && text[src] != '\r' && text[src] != '\n') {
      if (dst < visible_capacity - 1) {
        visible_text[dst++] = text[src];
      }
      ++src;
    }

    lines[line_count].start = line_start;
    lines[line_count].length = dst - line_start;
    lines[line_count].color_rgb = line_color;
    ++line_count;

    bool consumed_newline = false;
    if (text[src] == '\r') {
      consumed_newline = true;
      ++src;
      if (text[src] == '\n') {
        ++src;
      }
    } else if (text[src] == '\n') {
      consumed_newline = true;
      ++src;
    }

    if (consumed_newline && text[src] != '\0' && dst < visible_capacity - 1) {
      visible_text[dst++] = '\n';
    }
  }

  if (line_count == 0) {
    lines[0].start = 0;
    lines[0].length = 0;
    lines[0].color_rgb = default_color_rgb & 0xFFFFFFu;
    line_count = 1;
  }

  visible_text[dst < visible_capacity ? dst : visible_capacity - 1] = '\0';
  return line_count;
}

bool FindOverlayControlAt(int mouse_x, int mouse_y, char* out_script_id, size_t out_script_id_count) {
  if (!g_state.overlay_lock_ready || out_script_id == nullptr || out_script_id_count == 0) {
    return false;
  }

  out_script_id[0] = '\0';
  EnterCriticalSection(&g_state.overlay_lock);
  for (int overlay_index = kMaxOverlays - 1; overlay_index >= 0; --overlay_index) {
    OverlayRecord* record = &g_state.overlays[overlay_index];
    if (!record->enabled || record->control_count <= 0) {
      continue;
    }

    for (int control_index = record->control_count - 1; control_index >= 0; --control_index) {
      OverlayControlButton* control = &record->controls[control_index];
      const int left = record->pixel_x + control->x;
      const int top = record->pixel_y + control->y;
      const int right = left + control->width;
      const int bottom = top + control->height;
      if (mouse_x >= left && mouse_x < right && mouse_y >= top && mouse_y < bottom) {
        StringCchCopyA(out_script_id, out_script_id_count, control->script_id);
        LeaveCriticalSection(&g_state.overlay_lock);
        return true;
      }
    }
  }
  LeaveCriticalSection(&g_state.overlay_lock);
  return false;
}

bool HandleOverlayMouseButton(int mouse_x, int mouse_y, bool queue_click_event) {
  char script_id[kOverlayControlIdCapacity] = {};
  if (!FindOverlayControlAt(mouse_x, mouse_y, script_id, _countof(script_id))) {
    return false;
  }

  if (queue_click_event) {
    char event_text[kChatTextCapacity] = {};
    StringCchPrintfA(
        event_text,
        _countof(event_text),
        "%cSIMKEYS_OVERLAY_TOGGLE:%s",
        kOverlayEventMarker,
        script_id);
    QueueChatLine(event_text);
    LogMessage(kLogInfo, "overlay control clicked script=%s", script_id);
  }
  return true;
}

BOOL RenderTextOverlay(
    int32_t id,
    int32_t position,
    int32_t offset_x,
    int32_t offset_y,
    int32_t font_size,
    uint32_t color_rgb,
    const char* text,
    int* out_width,
    int* out_height) {
  if (text == nullptr) {
    text = "";
  }
  if (font_size <= 0) {
    font_size = 16;
  }
  if (font_size > 72) {
    font_size = 72;
  }

  HDC dc = CreateCompatibleDC(nullptr);
  if (dc == nullptr) {
    return FALSE;
  }

  HFONT font = CreateFontA(
      -font_size,
      0,
      0,
      0,
      FW_SEMIBOLD,
      FALSE,
      FALSE,
      FALSE,
      DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE,
      "Segoe UI");
  if (font == nullptr) {
    DeleteDC(dc);
    return FALSE;
  }

  HGDIOBJ old_font = SelectObject(dc, font);
  ParsedOverlayControl parsed_controls[kOverlayMaxControls] = {};
  int parsed_control_count = 0;
  const char* visible_source = ParseOverlayControls(text, parsed_controls, kOverlayMaxControls, &parsed_control_count);
  char visible_text[kOverlayTextCapacity] = {};
  ParsedOverlayLine parsed_lines[kOverlayMaxParsedLines] = {};
  const int parsed_line_count = ParseOverlayLines(
      visible_source,
      color_rgb,
      visible_text,
      kOverlayTextCapacity,
      parsed_lines,
      kOverlayMaxParsedLines);
  RECT measure = {0, 0, kOverlayMaxDimension - (kOverlayTextPadding * 2), 0};
  DrawTextA(dc, visible_text, -1, &measure, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);

  const bool has_panel = visible_text[0] != '\0';
  const int control_area_height = parsed_control_count > 0
      ? kOverlayControlButtonSize + (kOverlayControlPadding * 2)
      : 0;
  const int controls_width = parsed_control_count > 0
      ? (kOverlayControlPadding * 2) +
          (parsed_control_count * kOverlayControlButtonSize) +
          ((parsed_control_count - 1) * kOverlayControlGap)
      : 0;
  int panel_width = has_panel ? (measure.right - measure.left) + (kOverlayTextPadding * 2) : 0;
  int panel_height = has_panel ? (measure.bottom - measure.top) + (kOverlayTextPadding * 2) : 0;
  int width = panel_width > controls_width ? panel_width : controls_width;
  int height = panel_height + control_area_height;
  if (width < 8) {
    width = 8;
  }
  if (height < 8) {
    height = 8;
  }
  if (width > kOverlayMaxDimension) {
    width = kOverlayMaxDimension;
  }
  if (height > kOverlayMaxDimension) {
    height = kOverlayMaxDimension;
  }

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap == nullptr || bits == nullptr) {
    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteDC(dc);
    return FALSE;
  }

  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  RECT background = {0, 0, width, height};
  const COLORREF panel_color = RGB(12, 16, 18);
  const COLORREF header_color = RGB(24, 32, 36);
  HBRUSH background_brush = CreateSolidBrush(panel_color);
  FillRect(dc, &background, background_brush);
  DeleteObject(background_brush);

  OverlayControlButton stored_controls[kOverlayMaxControls] = {};
  if (parsed_control_count > 0) {
    int button_x = width - controls_width + kOverlayControlPadding;
    if (button_x < kOverlayControlPadding) {
      button_x = kOverlayControlPadding;
    }
    const int button_y = kOverlayControlPadding;
    for (int index = 0; index < parsed_control_count; ++index) {
      ParsedOverlayControl& control = parsed_controls[index];
      RECT button_rect = {
          button_x,
          button_y,
          button_x + kOverlayControlButtonSize,
          button_y + kOverlayControlButtonSize
      };

      const COLORREF fill_color = control.active ? RGB(16, 82, 38) : RGB(48, 36, 36);
      const COLORREF border_color = control.active ? RGB(96, 230, 120) : RGB(210, 90, 90);
      HBRUSH fill_brush = CreateSolidBrush(fill_color);
      HGDIOBJ old_button_brush = SelectObject(dc, fill_brush);
      HPEN border_pen = CreatePen(PS_SOLID, 2, border_color);
      HGDIOBJ old_button_pen = SelectObject(dc, border_pen);
      Ellipse(dc, button_rect.left, button_rect.top, button_rect.right, button_rect.bottom);
      SelectObject(dc, old_button_pen);
      SelectObject(dc, old_button_brush);
      DeleteObject(border_pen);
      DeleteObject(fill_brush);

      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, control.active ? RGB(245, 255, 245) : RGB(220, 190, 190));
      DrawTextA(dc, control.label, -1, &button_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      StringCchCopyA(stored_controls[index].script_id, _countof(stored_controls[index].script_id), control.script_id);
      stored_controls[index].x = button_rect.left;
      stored_controls[index].y = button_rect.top;
      stored_controls[index].width = button_rect.right - button_rect.left;
      stored_controls[index].height = button_rect.bottom - button_rect.top;

      button_x += kOverlayControlButtonSize + kOverlayControlGap;
    }
  }

  const int header_height = font_size + (kOverlayTextPadding * 2);
  const int panel_y = control_area_height;
  if (has_panel) {
    RECT header_rect = {0, panel_y, width, panel_y + header_height};
    HBRUSH header_brush = CreateSolidBrush(header_color);
    FillRect(dc, &header_rect, header_brush);
    DeleteObject(header_brush);

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(90, 110, 120));
    HGDIOBJ old_pen = SelectObject(dc, border_pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, panel_y, width, height);
    MoveToEx(dc, 0, panel_y + header_height, nullptr);
    LineTo(dc, width, panel_y + header_height);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border_pen);

    SetBkMode(dc, TRANSPARENT);
    TEXTMETRICA text_metrics = {};
    GetTextMetricsA(dc, &text_metrics);
    int line_height = text_metrics.tmHeight + text_metrics.tmExternalLeading;
    if (line_height <= 0) {
      line_height = font_size + 4;
    }
    int line_top = panel_y + kOverlayTextPadding;
    for (int line_index = 0; line_index < parsed_line_count; ++line_index) {
      const ParsedOverlayLine& line = parsed_lines[line_index];
      const uint32_t line_color = line.color_rgb & 0xFFFFFFu;
      SetTextColor(dc, RGB((line_color >> 16) & 0xFF, (line_color >> 8) & 0xFF, line_color & 0xFF));

      RECT line_rect = {
          kOverlayTextPadding,
          line_top,
          width - kOverlayTextPadding,
          line_top + line_height
      };

      const int end = line.start + line.length;
      if (line.start >= 0 && end >= line.start && end < kOverlayTextCapacity) {
        const char saved = visible_text[end];
        visible_text[end] = '\0';
        DrawTextA(dc, visible_text + line.start, -1, &line_rect, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
        visible_text[end] = saved;
      }
      line_top += line_height;
    }
  }
  GdiFlush();

  BYTE* pixel = static_cast<BYTE*>(bits);
  for (int i = 0; i < width * height; ++i) {
    const BYTE blue = pixel[i * 4 + 0];
    const BYTE green = pixel[i * 4 + 1];
    const BYTE red = pixel[i * 4 + 2];
    if ((red <= 30 && green <= 36 && blue <= 40) || (red == 90 && green == 110 && blue == 120)) {
      pixel[i * 4 + 3] = 170;
    } else {
      pixel[i * 4 + 3] = 255;
    }
  }

  const DWORD pixel_bytes = static_cast<DWORD>(width * height * 4);
  const BOOL stored = StoreOverlayBitmap(
      id,
      position,
      offset_x,
      offset_y,
      width,
      height,
      bits,
      pixel_bytes,
      stored_controls,
      parsed_control_count);
  if (stored) {
    if (out_width != nullptr) {
      *out_width = width;
    }
    if (out_height != nullptr) {
      *out_height = height;
    }
  }

  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  SelectObject(dc, old_font);
  DeleteObject(font);
  DeleteDC(dc);
  return stored;
}

void RenderOverlays() {
  if (!g_state.overlay_lock_ready || InterlockedCompareExchange(&g_state.overlay_count, 0, 0) <= 0) {
    return;
  }
  if (!ResolveOpenGlFunctions()) {
    InterlockedExchange(&g_state.overlay_last_error, static_cast<LONG>(GetLastError()));
    return;
  }

  int viewport[4] = {};
  int texture = 0;
  int matrix_mode = kGlModelview;
  g_glGetIntegerv(kGlViewport, viewport);
  g_glGetIntegerv(kGlTextureBinding2D, &texture);
  g_glGetIntegerv(kGlMatrixMode, &matrix_mode);
  const BYTE blend_enabled = g_glIsEnabled(kGlBlend);
  const int viewport_width = viewport[2];
  const int viewport_height = viewport[3];
  if (viewport_width <= 0 || viewport_height <= 0) {
    return;
  }

  g_glDisable(kGlCullFace);
  g_glDisable(kGlDepthTest);
  g_glEnable(kGlBlend);
  g_glBlendFunc(kGlSrcAlpha, kGlOneMinusSrcAlpha);
  g_glDepthMask(FALSE);
  g_glColor3f(1.0f, 1.0f, 1.0f);
  g_glMatrixMode(kGlProjection);
  g_glPushMatrix();
  g_glLoadIdentity();
  g_glMatrixMode(kGlModelview);
  g_glPushMatrix();
  g_glLoadIdentity();
  g_glBindTexture(kGlTexture2D, 0);

  EnterCriticalSection(&g_state.overlay_lock);
  for (int index = 0; index < kMaxOverlays; ++index) {
    OverlayRecord* record = &g_state.overlays[index];
    if (!record->enabled || record->pixels == nullptr || record->width <= 0 || record->height <= 0) {
      continue;
    }
    if (record->viewport_width != viewport_width || record->viewport_height != viewport_height) {
      UpdateOverlayRasterPosition(record, viewport_width, viewport_height);
    }
    g_glRasterPos2f(record->raster_x, record->raster_y);
    g_glDrawPixels(record->width, record->height, kGlBgraExt, kGlUnsignedByte, record->pixels);
  }
  LeaveCriticalSection(&g_state.overlay_lock);

  g_glBindTexture(kGlTexture2D, static_cast<UINT>(texture));
  g_glPopMatrix();
  g_glMatrixMode(kGlProjection);
  g_glPopMatrix();
  g_glMatrixMode(static_cast<UINT>(matrix_mode));
  g_glDepthMask(TRUE);
  if (!blend_enabled) {
    g_glDisable(kGlBlend);
  }
  g_glEnable(kGlDepthTest);
  g_glEnable(kGlCullFace);
  InterlockedIncrement(&g_state.overlay_draws);
}

BOOL WINAPI WglSwapLayerBuffersHook(HDC hdc, UINT planes) {
  RenderOverlays();
  WglSwapLayerBuffersFn original = reinterpret_cast<WglSwapLayerBuffersFn>(g_wgl_swap_gateway);
  if (original == nullptr) {
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
  }
  return original(hdc, planes);
}

BOOL InstallOverlayHook() {
  if (InterlockedCompareExchange(&g_state.overlay_hook_installed, 0, 0) != 0) {
    return TRUE;
  }
  if (!ResolveOpenGlFunctions()) {
    return FALSE;
  }

  HMODULE opengl = ResolveOpenGlModule();
  FARPROC proc = opengl != nullptr ? GetProcAddress(opengl, "wglSwapLayerBuffers") : nullptr;
  if (proc == nullptr) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return FALSE;
  }

  BYTE* target = reinterpret_cast<BYTE*>(proc);
  const size_t stolen = 5;
  memcpy(g_wgl_swap_original, target, stolen);
  g_wgl_swap_gateway = MakeJmpGateway(target, stolen);
  if (g_wgl_swap_gateway == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  BYTE patch[5] = {};
  patch[0] = 0xE9;
  *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(
      reinterpret_cast<BYTE*>(&WglSwapLayerBuffersHook) - (target + 5));
  WriteExecutableMemory(target, patch, stolen);
  g_wgl_swap_stolen = stolen;
  InterlockedExchange(&g_state.overlay_hook_installed, 1);
  LogMessage(
      kLogInfo,
      "installed overlay draw hook at 0x%08X stolen=%u gateway=0x%08X",
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(target)),
      static_cast<unsigned int>(stolen),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_wgl_swap_gateway)));
  return TRUE;
}

bool BuildLogDirectory(const char* module_path, char* log_dir, size_t capacity) {
  if (module_path == nullptr || log_dir == nullptr || capacity == 0) {
    return false;
  }

  HRESULT hr = StringCchCopyA(log_dir, capacity, module_path);
  if (FAILED(hr)) {
    return false;
  }

  char* last_slash = strrchr(log_dir, '\\');
  if (last_slash == nullptr) {
    return false;
  }
  *last_slash = '\0';

  char* leaf = strrchr(log_dir, '\\');
  if (leaf != nullptr) {
    const char* leaf_name = leaf + 1;
    if (_stricmp(leaf_name, "Release") == 0 || _stricmp(leaf_name, "Debug") == 0 || _stricmp(leaf_name, "bin") == 0) {
      *leaf = '\0';
    }
  }

  hr = StringCchCatA(log_dir, capacity, "\\logs");
  return SUCCEEDED(hr);
}

void EnsureLogFileReady() {
  if (!g_state.log_lock_ready || g_state.log_file != nullptr) {
    return;
  }

  char module_path[MAX_PATH] = {};
  if (GetModuleFileNameA(g_state.module, module_path, ARRAYSIZE(module_path)) == 0) {
    return;
  }

  StringCchCopyA(g_state.module_path, ARRAYSIZE(g_state.module_path), module_path);

  char log_dir[MAX_PATH] = {};
  if (!BuildLogDirectory(module_path, log_dir, ARRAYSIZE(log_dir))) {
    return;
  }
  CreateDirectoryA(log_dir, nullptr);

  StringCchPrintfA(g_state.log_path, ARRAYSIZE(g_state.log_path), "%s\\simkeys_%lu.log", log_dir, GetCurrentProcessId());
  g_state.log_file = CreateFileA(
      g_state.log_path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (g_state.log_file == INVALID_HANDLE_VALUE) {
    g_state.log_file = nullptr;
    g_state.log_path[0] = '\0';
  }
}

void LogMessage(int level, const char* format, ...) {
  if (level > InterlockedCompareExchange(&g_state.log_level, 0, 0)) {
    return;
  }

  char buffer[512] = {};
  va_list args;
  va_start(args, format);
  vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
  va_end(args);

  SYSTEMTIME now = {};
  GetLocalTime(&now);

  char final_buffer[896] = {};
  _snprintf_s(
      final_buffer,
      sizeof(final_buffer),
      _TRUNCATE,
      "[simkeys][%04u-%02u-%02u %02u:%02u:%02u.%03u][pid=%lu][tid=%lu][L%d] %s\r\n",
      now.wYear,
      now.wMonth,
      now.wDay,
      now.wHour,
      now.wMinute,
      now.wSecond,
      now.wMilliseconds,
      GetCurrentProcessId(),
      GetCurrentThreadId(),
      level,
      buffer);
  OutputDebugStringA(final_buffer);

  if (!g_state.log_lock_ready) {
    return;
  }

  EnsureLogFileReady();
  if (g_state.log_file == nullptr) {
    return;
  }

  EnterCriticalSection(&g_state.log_lock);
  DWORD written = 0;
  WriteFile(g_state.log_file, final_buffer, static_cast<DWORD>(strlen(final_buffer)), &written, nullptr);
  FlushFileBuffers(g_state.log_file);
  LeaveCriticalSection(&g_state.log_lock);
}

void UpdateLastOperation(UINT vk, LONG rc, DWORD last_error) {
  InterlockedExchange(&g_state.last_vk, static_cast<LONG>(vk));
  InterlockedExchange(&g_state.last_result, rc);
  InterlockedExchange(&g_state.last_error, static_cast<LONG>(last_error));
}

void BuildSnapshotText(const char* reason, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) {
    return;
  }

  out[0] = '\0';
  size_t offset = 0;

  const uint32_t module_base = static_cast<uint32_t>(GetProcessImageBase());
  const uint32_t runtime_nwn_wndproc = RebaseAddress(kExpectedNwnWndProc);
  const uint32_t runtime_key_pre_dispatch = RebaseAddress(kExpectedKeyPreDispatch);
  const uint32_t runtime_gate90_accessor = RebaseAddress(kExpectedGate90Accessor);
  const uint32_t runtime_gate94_accessor = RebaseAddress(kExpectedGate94Accessor);
  const uint32_t runtime_gate98_accessor = RebaseAddress(kExpectedGate98Accessor);
  const uint32_t runtime_dispatcher_accessor = RebaseAddress(kExpectedDispatcherAccessor);
  const uint32_t runtime_dispatcher_thunk = RebaseAddress(kExpectedDispatcherThunk);
  const uint32_t runtime_dispatcher_slot0 = RebaseAddress(kExpectedDispatcherSlot0);
  const uint32_t runtime_quickbar_exec = RebaseAddress(kExpectedQuickbarExec);
  const uint32_t runtime_quickbar_page_select = RebaseAddress(kExpectedQuickbarPageSelect);
  const uint32_t runtime_quickbar_slot_dispatch = RebaseAddress(kExpectedQuickbarSlotDispatch);
  const uint32_t runtime_quickbar_vtable = RebaseAddress(kExpectedQuickbarVtable);
  const uint32_t runtime_object_by_id_resolver = RebaseAddress(kExpectedObjectByIdResolver);
  const uint32_t runtime_item_equipped_owner_resolver = RebaseAddress(kExpectedItemEquippedOwnerResolver);
  const uint32_t runtime_item_description_builder = RebaseAddress(kExpectedItemDescriptionBuilder);
  const uint32_t runtime_nwn_string_init = RebaseAddress(kExpectedNwnStringInit);
  const uint32_t runtime_chat_send = RebaseAddress(kExpectedChatSend);
  const uint32_t runtime_chat_window_log = RebaseAddress(kExpectedChatWindowLog);
  const uint32_t runtime_app_object_resolver = RebaseAddress(kExpectedAppObjectResolver);
  const uint32_t runtime_current_player_resolver = RebaseAddress(kExpectedCurrentPlayerResolver);
  const uint32_t runtime_player_name_builder = RebaseAddress(kExpectedPlayerNameBuilder);
  const uint32_t runtime_nwn_string_destroy = RebaseAddress(kExpectedNwnStringDestroy);
  const uint32_t runtime_walk_to_waypoint = RebaseAddress(kExpectedWalkToWaypoint);
  const uint32_t runtime_server_object_by_id = RebaseAddress(kExpectedServerObjectByIdResolver);
  const uint32_t runtime_set_action_mode = RebaseAddress(kExpectedSetActionMode);
  const uint32_t runtime_get_action_mode = RebaseAddress(kExpectedGetActionMode);
  char character_name[kCharacterNameCapacity] = {};
  CopyStoredCharacterName(character_name, ARRAYSIZE(character_name));
  const LONG quickbar_slot_type = InterlockedCompareExchange(&g_state.quickbar_slot_type, 0, 0);
  const LONG quickbar_slot_case = QuickbarSlotTypeToCaseIndex(quickbar_slot_type);
  const uint32_t quickbar_item_mask_low =
      static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_item_mask_low, 0, 0));
  const uint32_t quickbar_item_mask_high =
      static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_item_mask_high, 0, 0));
  const uint32_t quickbar_equipped_mask_low =
      static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_equipped_mask_low, 0, 0));
  const uint32_t quickbar_equipped_mask_high =
      static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_equipped_mask_high, 0, 0));

  const HWND hwnd = g_state.hwnd;
  const uint32_t current_wndproc = (hwnd != nullptr && IsWindow(hwnd))
      ? static_cast<uint32_t>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC))
      : 0;
  const uint32_t original_wndproc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_state.original_wndproc));

  char class_name[128] = {};
  char window_title[256] = {};
  if (hwnd != nullptr && IsWindow(hwnd)) {
    GetClassNameA(hwnd, class_name, ARRAYSIZE(class_name));
    GetWindowTextA(hwnd, window_title, ARRAYSIZE(window_title));
  }

  const HWND foreground = GetForegroundWindow();
  float position_x = 0.0f;
  float position_y = 0.0f;
  float position_z = 0.0f;
  const BOOL position_valid = TryReadCurrentPlayerPosition(&position_x, &position_y, &position_z);

  const uint32_t app_holder = ReadAppHolderPointer();
  const uint32_t app_object = ReadAppObjectPointer();
  uint32_t app_inner = 0;
  uint32_t dispatcher_ptr = 0;
  uint32_t gate_90 = 0;
  uint32_t gate_94 = 0;
  uint32_t gate_98 = 0;
  if (app_object != 0) {
    app_inner = SafeReadPointer32(static_cast<uintptr_t>(app_object) + 4);
  }
  if (app_inner != 0) {
    dispatcher_ptr = SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x24);
    gate_90 = SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x90);
    gate_94 = SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x94);
    gate_98 = SafeReadPointer32(static_cast<uintptr_t>(app_inner) + 0x98);
  }

  AppendFormat(out, capacity, &offset, "reason=%s\r\n", reason != nullptr ? reason : "snapshot");
  AppendFormat(out, capacity, &offset, "process: pid=%lu tid=%lu imageBase=0x%08X\r\n", GetCurrentProcessId(), GetCurrentThreadId(), module_base);
  AppendFormat(out, capacity, &offset, "hook: module=%s\r\n", g_state.module_path[0] != '\0' ? g_state.module_path : "<unavailable>");
  AppendFormat(out, capacity, &offset, "hook: log=%s\r\n", g_state.log_path[0] != '\0' ? g_state.log_path : "<unavailable>");
  AppendFormat(out, capacity, &offset, "hook: installed=%ld logLevel=%ld pendingBusy=%ld pipeState=%ld pipeErr=%ld\r\n",
      InterlockedCompareExchange(&g_state.installed, 0, 0),
      InterlockedCompareExchange(&g_state.log_level, 0, 0),
      InterlockedCompareExchange(&g_state.pending.busy, 0, 0),
      InterlockedCompareExchange(&g_state.pipe_state, 0, 0),
      InterlockedCompareExchange(&g_state.pipe_thread_error, 0, 0));
  AppendFormat(out, capacity, &offset, "window: hwnd=0x%08X thread=%lu visible=%d class=%s title=%s\r\n",
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hwnd)),
      g_state.window_thread_id,
      hwnd != nullptr && IsWindowVisible(hwnd),
      class_name[0] != '\0' ? class_name : "<unknown>",
      window_title[0] != '\0' ? window_title : "<untitled>");
  AppendFormat(out, capacity, &offset, "window: foreground=0x%08X matches=%d currentWndProc=0x%08X hookWndProc=0x%08X originalWndProc=0x%08X\r\n",
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(foreground)),
      foreground == hwnd,
      current_wndproc,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SimKeysWndProc)),
      original_wndproc);
  AppendFormat(out, capacity, &offset, "expected: wndProc=0x%08X keyPreDispatch=0x%08X gate90=0x%08X gate94=0x%08X gate98=0x%08X dispatcherAccessor=0x%08X dispatcherThunk=0x%08X dispatcherSlot0=0x%08X\r\n",
      runtime_nwn_wndproc,
      runtime_key_pre_dispatch,
      runtime_gate90_accessor,
      runtime_gate94_accessor,
      runtime_gate98_accessor,
      runtime_dispatcher_accessor,
      runtime_dispatcher_thunk,
      runtime_dispatcher_slot0);
  AppendFormat(out, capacity, &offset, "engine: appGlobalSlot=0x%08X appHolder=0x%08X appObject=0x%08X appInner=0x%08X dispatcher=0x%08X gate90Value=0x%08X gate94Value=0x%08X gate98Value=0x%08X\r\n",
      static_cast<uint32_t>(kAppGlobalSlotAddress),
      app_holder,
      app_object,
      app_inner,
      dispatcher_ptr,
      gate_90,
      gate_94,
      gate_98);
  AppendFormat(out, capacity, &offset, "quickbar: exec=0x%08X pageSelect=0x%08X slotDispatch=0x%08X panelVtable=0x%08X objectById=0x%08X equipOwner=0x%08X execTrace=%ld slotTrace=%ld capturedThis=0x%08X page=%ld capturedSlot=%ld slotPtr=0x%08X slotType=%ld slotCase=%ld calls=%ld scanAttempts=%ld scanHits=%ld itemMask=0x%08X%08X equippedMask=0x%08X%08X\r\n",
      runtime_quickbar_exec,
      runtime_quickbar_page_select,
      runtime_quickbar_slot_dispatch,
      runtime_quickbar_vtable,
      runtime_object_by_id_resolver,
      runtime_item_equipped_owner_resolver,
      InterlockedCompareExchange(&g_state.quickbar_trace_installed, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_slot_trace_installed, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_this, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_page, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_slot, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_slot_ptr, 0, 0),
      quickbar_slot_type,
      quickbar_slot_case,
      InterlockedCompareExchange(&g_state.quickbar_calls, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_scan_attempts, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_scan_hits, 0, 0),
      quickbar_item_mask_high,
      quickbar_item_mask_low,
      quickbar_equipped_mask_high,
      quickbar_equipped_mask_low);
  AppendFormat(out, capacity, &offset, "quickbarWeapon: itemDescription=0x%08X stringInit=0x%08X itemInfoParser=0x%08X parserHook=%ld itemInfoBranch=0x%08X branchHook=%u itemInfoPropertyRow=0x%08X itemInfoPropertyRowCall=0x%08X rowHook=%u itemInfoMessage=0x%08X itemInfoMessageCall=0x%08X messageHook=%u itemDetailRequest=0x%08X detailPending=%ld detailCacheHits=%ld\r\n",
      runtime_item_description_builder,
      runtime_nwn_string_init,
      RebaseAddress(kExpectedItemInfoParser),
      InterlockedCompareExchange(&g_state.item_info_parser_hook_installed, 0, 0),
      RebaseAddress(kExpectedItemInfoPopupBranch),
      static_cast<unsigned int>(g_item_info_popup_stolen != 0),
      RebaseAddress(kExpectedItemInfoPropertyRowAppend),
      RebaseAddress(kExpectedItemInfoPropertyRowCall),
      static_cast<unsigned int>(g_item_info_property_row_stolen != 0),
      RebaseAddress(kExpectedItemInfoMessageHandler),
      RebaseAddress(kExpectedItemInfoMessageHandlerCall),
      static_cast<unsigned int>(g_item_info_message_handler_stolen != 0),
      RebaseAddress(kExpectedItemDetailRequest),
      InterlockedCompareExchange(&g_state.quickbar_weapon_detail_pending_count, 0, 0),
      InterlockedCompareExchange(&g_state.quickbar_weapon_detail_cache_hits, 0, 0));
  AppendFormat(out, capacity, &offset, "chat: send=0x%08X windowLog=0x%08X trace=%ld queued=%ld nextWrite=%ld latestSeq=%ld lastMode=%ld lastRc=%ld lastErr=%ld\r\n",
      runtime_chat_send,
      runtime_chat_window_log,
      InterlockedCompareExchange(&g_state.chat_trace_installed, 0, 0),
      InterlockedCompareExchange(&g_state.chat_count, 0, 0),
      InterlockedCompareExchange(&g_state.chat_write_index, 0, 0),
      InterlockedCompareExchange(&g_state.chat_sequence, 0, 0),
      InterlockedCompareExchange(&g_state.last_chat_mode, 0, 0),
      InterlockedCompareExchange(&g_state.last_chat_result, 0, 0),
      InterlockedCompareExchange(&g_state.last_chat_error, 0, 0));
  AppendFormat(out, capacity, &offset, "keyboard: messages=%ld down=%ld up=%ld lastMsg=0x%04lX lastWParam=0x%08lX lastLParam=0x%08lX\r\n",
      InterlockedCompareExchange(&g_state.key_message_count, 0, 0),
      InterlockedCompareExchange(&g_state.key_down_count, 0, 0),
      InterlockedCompareExchange(&g_state.key_up_count, 0, 0),
      InterlockedCompareExchange(&g_state.last_key_message, 0, 0),
      InterlockedCompareExchange(&g_state.last_key_wparam, 0, 0),
      InterlockedCompareExchange(&g_state.last_key_lparam, 0, 0));
  AppendFormat(out, capacity, &offset, "overlay: hook=%ld count=%ld draws=%ld err=%ld\r\n",
      InterlockedCompareExchange(&g_state.overlay_hook_installed, 0, 0),
      InterlockedCompareExchange(&g_state.overlay_count, 0, 0),
      InterlockedCompareExchange(&g_state.overlay_draws, 0, 0),
      InterlockedCompareExchange(&g_state.overlay_last_error, 0, 0));
  AppendFormat(out, capacity, &offset, "identityPath: appObjectResolver=0x%08X currentPlayerResolver=0x%08X nameBuilder=0x%08X stringDestroy=0x%08X\r\n",
      runtime_app_object_resolver,
      runtime_current_player_resolver,
      runtime_player_name_builder,
      runtime_nwn_string_destroy);
  AppendFormat(out, capacity, &offset, "movement: walkToWaypoint=0x%08X pendingBusy=%ld noWalkBypass=%ld positionValid=%d position=(%.3f, %.3f, %.3f)\r\n",
      runtime_walk_to_waypoint,
      InterlockedCompareExchange(&g_state.pending_move.busy, 0, 0),
      InterlockedCompareExchange(&g_state.walk_no_walk_bypass_enabled, 0, 0),
      position_valid ? 1 : 0,
      static_cast<double>(position_x),
      static_cast<double>(position_y),
      static_cast<double>(position_z));
  uint32_t snapshot_player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
  uint32_t snapshot_creature = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_creature, 0, 0));
  LONG client_defensive_casting = -1;
  if (!ReadClientDefensiveCastingModeFlag(snapshot_player_object, &client_defensive_casting)) {
    client_defensive_casting = -1;
  }
  LONG server_defensive_casting = -1;
  if (!ReadDefensiveCastingModeByte(snapshot_creature, &server_defensive_casting)) {
    server_defensive_casting = -1;
  }
  BYTE current_combat_mode = 0xFF;
  if (snapshot_creature != 0) {
    SafeReadValue(static_cast<uintptr_t>(snapshot_creature) + kCreatureCurrentCombatModeOffset, &current_combat_mode);
  }
  AppendFormat(out, capacity, &offset, "actionMode: serverObjectById=0x%08X set=0x%08X get=0x%08X clientCreature=0x%08X dcmClient=%ld serverCreature=0x%08X dcmServer=%ld currentCombatMode=%u\r\n",
      runtime_server_object_by_id,
      runtime_set_action_mode,
      runtime_get_action_mode,
      snapshot_player_object,
      client_defensive_casting,
      snapshot_creature,
      server_defensive_casting,
      static_cast<unsigned int>(current_combat_mode));
  AppendFormat(out, capacity, &offset, "identity: player=0x%08X creature=0x%08X name=%s refreshes=%ld err=%ld\r\n",
      snapshot_player_object,
      snapshot_creature,
      character_name[0] != '\0' ? character_name : "<unknown>",
      InterlockedCompareExchange(&g_state.identity_refresh_count, 0, 0),
      InterlockedCompareExchange(&g_state.identity_error, 0, 0));
  AppendFormat(out, capacity, &offset, "last: vk=0x%08X rc=%ld err=%ld requestId=%ld\r\n",
      InterlockedCompareExchange(&g_state.last_vk, 0, 0),
      InterlockedCompareExchange(&g_state.last_result, 0, 0),
      InterlockedCompareExchange(&g_state.last_error, 0, 0),
      InterlockedCompareExchange(&g_state.pending.request_id, 0, 0));
}

void LogSnapshot(int level, const char* reason) {
  if (level > InterlockedCompareExchange(&g_state.log_level, 0, 0)) {
    return;
  }

  char snapshot[4096] = {};
  BuildSnapshotText(reason, snapshot, sizeof(snapshot));
  LogMessage(level, "%s", snapshot);
}

LPARAM BuildKeyDownLParam(UINT vk) {
  const UINT scan_code = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
  return static_cast<LPARAM>(1u | (scan_code << 16));
}

LPARAM BuildKeyUpLParam(UINT vk) {
  const UINT scan_code = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
  return static_cast<LPARAM>(1u | (scan_code << 16) | (1u << 30) | (1u << 31));
}

LRESULT CallKeyPreDispatch(HWND hwnd, UINT vk) {
#if defined(_M_IX86)
  typedef LRESULT (WINAPI* KeyPreDispatchFn)(HWND hwnd, WPARAM wparam, LPARAM lparam);
#else
  typedef LRESULT (WINAPI* KeyPreDispatchFn)(HWND hwnd, WPARAM wparam, LPARAM lparam);
#endif
  const KeyPreDispatchFn fn = reinterpret_cast<KeyPreDispatchFn>(RebaseAddress(kExpectedKeyPreDispatch));
  return fn(hwnd, static_cast<WPARAM>(vk), BuildKeyDownLParam(vk));
}

UINT SlotToVirtualKey(int slot) {
  if (slot < 1 || slot > 12) {
    return 0;
  }
  return static_cast<UINT>(VK_F1 + (slot - 1));
}

LONG CallChatSendDirect(const char* text, int mode) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };

  if (text == nullptr || text[0] == '\0') {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  typedef void (__cdecl* ChatSendFn)(const void* text_object, int mode);
  const ChatSendFn fn = reinterpret_cast<ChatSendFn>(RebaseAddress(kExpectedChatSend));

  NwnStringRef message = {};
  message.text = const_cast<char*>(text);
  message.length = static_cast<int32_t>(strnlen(text, kPendingChatCapacity));
  fn(&message, mode);
  return 1;
}

BOOL AppendMapPinXmlText(char* out, size_t capacity, size_t* offset, const char* text) {
  if (out == nullptr || offset == nullptr || *offset >= capacity) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
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
        replacement = (*cursor < 0x20u && *cursor != '\t') ? "?" : single;
        break;
    }
    const size_t length = strlen(replacement);
    if (*offset + length >= capacity) {
      SetLastError(ERROR_BUFFER_OVERFLOW);
      return FALSE;
    }
    CopyMemory(out + *offset, replacement, length);
    *offset += length;
    ++cursor;
  }
  out[*offset] = '\0';
  return TRUE;
}

BOOL AppendMapPinXmlLiteral(char* out, size_t capacity, size_t* offset, const char* text) {
  if (out == nullptr || offset == nullptr || text == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const size_t length = strlen(text);
  if (*offset + length >= capacity) {
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  CopyMemory(out + *offset, text, length);
  *offset += length;
  out[*offset] = '\0';
  return TRUE;
}

BOOL BuildMapPinXml(float x, float y, const char* text, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0 || text == nullptr || text[0] == '\0' ||
      !IsPlausibleCoordinate(x) || !IsPlausibleCoordinate(y)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const HRESULT hr = StringCchPrintfA(
      out,
      capacity,
      "<pin x=\"%.6f\" y=\"%.6f\" text=\"",
      static_cast<double>(x),
      static_cast<double>(y));
  if (FAILED(hr)) {
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  size_t offset = strlen(out);
  if (!AppendMapPinXmlText(out, capacity, &offset, text) ||
      !AppendMapPinXmlLiteral(out, capacity, &offset, "\" />")) {
    return FALSE;
  }
  return TRUE;
}

void DispatchClientGuiMessageRaw(
    void* target,
    const void* message,
    int32_t opcode,
    int32_t arg0,
    int32_t arg1,
    const void* extra) {
#if defined(_M_IX86)
  void* dispatch_fn = reinterpret_cast<void*>(RebaseAddress(kExpectedChatWindowLog));
  __asm {
    mov esi, extra
    push dword ptr [esi+12]
    push dword ptr [esi+8]
    push dword ptr [esi+4]
    push dword ptr [esi]
    push arg1
    push arg0
    push opcode
    mov esi, message
    push dword ptr [esi+4]
    push dword ptr [esi]
    mov ecx, target
    mov eax, dispatch_fn
    call eax
  }
#else
  (void)target;
  (void)message;
  (void)opcode;
  (void)arg0;
  (void)arg1;
  (void)extra;
#endif
}

LONG CallAddMapPinDirect(float x, float y, const char* text) {
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

  const uint32_t app_object = ReadAppObjectPointer();
  if (app_object == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return 0;
  }

  typedef void* (__thiscall* ResolveClientGuiMessageFn)(void* app_object);
  typedef NwnStringRef* (__thiscall* ConstructNwnStringFn)(NwnStringRef* text_object, const char* text);
  const ResolveClientGuiMessageFn resolve_gui =
      reinterpret_cast<ResolveClientGuiMessageFn>(RebaseAddress(kExpectedClientGuiMessageResolver));
  const ConstructNwnStringFn construct_string =
      reinterpret_cast<ConstructNwnStringFn>(RebaseAddress(kExpectedNwnStringConstructFromCString));

  void* target = resolve_gui(reinterpret_cast<void*>(app_object));
  if (target == nullptr) {
    SetLastError(ERROR_NOT_FOUND);
    return 0;
  }

  NwnStringRef message = {};
  construct_string(&message, xml);
  if (message.text == nullptr) {
    SetLastError(ERROR_OUTOFMEMORY);
    return 0;
  }

  ClientMessageExtra extra = {};
  DispatchClientGuiMessageRaw(target, &message, 0x80, 0, 0, &extra);
  LogMessage(kLogInfo, "map pin dispatched x=%.3f y=%.3f text=%s", static_cast<double>(x), static_cast<double>(y), text);
  SetLastError(ERROR_SUCCESS);
  return 1;
}

BOOL InstallWalkNoWalkBypassPatch(BYTE* original, SIZE_T original_size) {
  if (original == nullptr || original_size < 5) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  BYTE* source = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedWalkNoWalkBlock));
  BYTE* target = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedWalkNoWalkBypassTarget));
  const BYTE expected[] = {0x6A, 0x00, 0x83, 0xEC, 0x10};

  __try {
    memcpy(original, source, sizeof(expected));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    SetLastError(static_cast<DWORD>(GetExceptionCode()));
    return FALSE;
  }

  if (memcmp(original, expected, sizeof(expected)) != 0) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  BYTE patch[5] = {};
  patch[0] = 0xE9;
  const intptr_t rel = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(source + sizeof(patch));
  *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(rel);
  WriteExecutableMemory(source, patch, sizeof(patch));
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

void RestoreWalkNoWalkBypassPatch(const BYTE* original, SIZE_T original_size) {
  if (original == nullptr || original_size < 5) {
    return;
  }
  BYTE* source = reinterpret_cast<BYTE*>(RebaseAddress(kExpectedWalkNoWalkBlock));
  WriteExecutableMemory(source, original, 5);
}

BOOL SetWalkNoWalkBypassEnabledOnWindowThread(BOOL enabled) {
  if (enabled) {
    if (g_walk_no_walk_bypass_installed) {
      InterlockedExchange(&g_state.walk_no_walk_bypass_enabled, 1);
      SetLastError(ERROR_SUCCESS);
      return TRUE;
    }

    if (!InstallWalkNoWalkBypassPatch(g_walk_no_walk_original, sizeof(g_walk_no_walk_original))) {
      return FALSE;
    }

    g_walk_no_walk_bypass_installed = true;
    InterlockedExchange(&g_state.walk_no_walk_bypass_enabled, 1);
    SetLastError(ERROR_SUCCESS);
    LogMessage(kLogInfo, "walk no-walk bypass enabled");
    return TRUE;
  }

  if (g_walk_no_walk_bypass_installed) {
    RestoreWalkNoWalkBypassPatch(g_walk_no_walk_original, sizeof(g_walk_no_walk_original));
    ZeroMemory(g_walk_no_walk_original, sizeof(g_walk_no_walk_original));
    g_walk_no_walk_bypass_installed = false;
    LogMessage(kLogInfo, "walk no-walk bypass disabled");
  }

  InterlockedExchange(&g_state.walk_no_walk_bypass_enabled, 0);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

LONG CallMoveToLocationDirect(float x, float y, float z, int client_side, uint32_t action_object_id, int bypass_no_walk) {
  if (!IsPlausiblePosition(x, y, z)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  typedef int (__thiscall* WalkToWaypointFn)(
      void* app_object,
      float x,
      float y,
      float z,
      int client_side,
      uint32_t action_object_id);

  const uint32_t app_object = ReadAppObjectPointer();
  if (app_object == 0) {
    SetLastError(ERROR_NOT_FOUND);
    return 0;
  }

  const WalkToWaypointFn fn = reinterpret_cast<WalkToWaypointFn>(RebaseAddress(kExpectedWalkToWaypoint));
  const uint32_t resolved_action_object_id = action_object_id != 0 ? action_object_id : kInvalidObjectId;
  BYTE no_walk_original[5] = {};
  const bool bypass_already_installed = g_walk_no_walk_bypass_installed;
  const BOOL bypass_installed = (bypass_no_walk && !bypass_already_installed)
      ? InstallWalkNoWalkBypassPatch(no_walk_original, sizeof(no_walk_original))
      : FALSE;
  if (bypass_no_walk && !bypass_already_installed && !bypass_installed) {
    return 0;
  }

  SetLastError(ERROR_SUCCESS);
  const LONG rc = static_cast<LONG>(fn(
      reinterpret_cast<void*>(app_object),
      x,
      y,
      z,
      client_side ? 1 : 0,
      resolved_action_object_id));
  const DWORD call_error = GetLastError();
  if (bypass_installed) {
    RestoreWalkNoWalkBypassPatch(no_walk_original, sizeof(no_walk_original));
  }
  SetLastError(call_error);
  return rc;
}

BOOL ResolveCurrentCharacterIdentityOnWindowThread(DWORD* out_error) {
  struct NwnStringRef {
    char* text;
    int32_t length;
  };

  typedef void* (__thiscall* ResolveAppObjectFn)(void* app_holder);
  typedef void* (__thiscall* ResolveCurrentPlayerFn)(void* app_object);
  typedef NwnStringRef* (__thiscall* BuildPlayerNameFn)(void* player_object, NwnStringRef* out_name);
  typedef void (__thiscall* DestroyNwnStringFn)(NwnStringRef* text_object);

  DWORD last_error = ERROR_SUCCESS;
  DWORD creature_error = ERROR_SUCCESS;
  void* app_object = nullptr;
  void* player_object = nullptr;
  uint32_t player_server_object = 0;
  uint32_t player_creature = 0;
  NwnStringRef name = {};
  char local_name[kCharacterNameCapacity] = {};

  const uint32_t app_holder = ReadAppHolderPointer();
  if (app_holder == 0) {
    last_error = ERROR_NOT_FOUND;
  } else {
    const ResolveAppObjectFn resolve_app_object =
        reinterpret_cast<ResolveAppObjectFn>(RebaseAddress(kExpectedAppObjectResolver));
    const ResolveCurrentPlayerFn resolve_current_player =
        reinterpret_cast<ResolveCurrentPlayerFn>(RebaseAddress(kExpectedCurrentPlayerResolver));
    const BuildPlayerNameFn build_player_name =
        reinterpret_cast<BuildPlayerNameFn>(RebaseAddress(kExpectedPlayerNameBuilder));
    const DestroyNwnStringFn destroy_nwn_string =
        reinterpret_cast<DestroyNwnStringFn>(RebaseAddress(kExpectedNwnStringDestroy));

    __try {
      app_object = resolve_app_object(reinterpret_cast<void*>(app_holder));
      if (app_object != nullptr) {
        player_object = resolve_current_player(app_object);
      }
      if (player_object != nullptr) {
        build_player_name(player_object, &name);
      } else {
        last_error = ERROR_NOT_FOUND;
      }
      if (last_error == ERROR_SUCCESS && (name.text == nullptr || name.text[0] == '\0')) {
        last_error = ERROR_NOT_FOUND;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
    }

    if (last_error == ERROR_SUCCESS) {
      if (name.text != nullptr && name.text[0] != '\0') {
        strncpy_s(local_name, sizeof(local_name), name.text, _TRUNCATE);
      } else {
        last_error = ERROR_NOT_FOUND;
      }
    }

    if (name.text != nullptr) {
      __try {
        destroy_nwn_string(&name);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (last_error == ERROR_SUCCESS) {
          last_error = static_cast<DWORD>(GetExceptionCode());
        }
      }
    }
  }

  if (!ResolveCurrentCreatureFromObjectId(&player_server_object, &player_creature, &creature_error)) {
    player_creature = 0;
    if (player_object != nullptr) {
      DWORD fallback_error = ERROR_SUCCESS;
      if (ResolveCreatureFromObjectPointer(
              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player_object)),
              &player_creature,
              &fallback_error)) {
        creature_error = ERROR_SUCCESS;
      } else if (creature_error == ERROR_SUCCESS) {
        creature_error = fallback_error;
      }
    }
  }

  StoreCharacterName(last_error == ERROR_SUCCESS ? local_name : "");
  InterlockedExchange(&g_state.player_object, static_cast<LONG>(reinterpret_cast<uintptr_t>(player_object)));
  InterlockedExchange(&g_state.player_creature, static_cast<LONG>(player_creature));
  InterlockedExchange(&g_state.identity_error, static_cast<LONG>(last_error));
  InterlockedIncrement(&g_state.identity_refresh_count);
  UpdateQuickbarItemMasksOnWindowThread();
  if (InterlockedExchange(&g_state.quickbar_weapon_refresh_requested, 0) != 0) {
    UpdateQuickbarWeaponInfoOnWindowThread();
  }

  if (last_error == ERROR_SUCCESS) {
    LogMessage(
        kLogDebug,
        "identity refresh resolved holder=0x%08X app=0x%08X player=0x%08X serverObject=0x%08X creature=0x%08X creatureErr=%lu namePtr=0x%08X nameLen=%ld name=%s",
        app_holder,
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(app_object)),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(player_object)),
        player_server_object,
        player_creature,
        static_cast<unsigned long>(creature_error),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(name.text)),
        static_cast<long>(name.length),
        local_name);
  } else {
    LogMessage(
        kLogDebug,
        "identity refresh failed holder=0x%08X app=0x%08X player=0x%08X serverObject=0x%08X creature=0x%08X creatureErr=%lu namePtr=0x%08X nameLen=%ld err=%lu",
        app_holder,
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(app_object)),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(player_object)),
        player_server_object,
        player_creature,
        static_cast<unsigned long>(creature_error),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(name.text)),
        static_cast<long>(name.length),
        static_cast<unsigned long>(last_error));
  }

  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS;
}

BOOL EnsureCurrentPlayerObjectOnWindowThread(uint32_t* out_player_object, DWORD* out_error) {
  if (out_player_object == nullptr) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out_player_object = 0;

  uint32_t player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
  if (player_object == 0) {
    DWORD identity_error = ERROR_SUCCESS;
    ResolveCurrentCharacterIdentityOnWindowThread(&identity_error);
    player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
  }

  if (player_object == 0) {
    typedef void* (__thiscall* ResolveAppObjectFn)(void* app_holder);
    typedef void* (__thiscall* ResolveCurrentPlayerFn)(void* app_object);

    const uint32_t app_holder = ReadAppHolderPointer();
    if (app_holder == 0) {
      if (out_error != nullptr) {
        *out_error = ERROR_NOT_FOUND;
      }
      SetLastError(ERROR_NOT_FOUND);
      return FALSE;
    }

    const ResolveAppObjectFn resolve_app_object =
        reinterpret_cast<ResolveAppObjectFn>(RebaseAddress(kExpectedAppObjectResolver));
    const ResolveCurrentPlayerFn resolve_current_player =
        reinterpret_cast<ResolveCurrentPlayerFn>(RebaseAddress(kExpectedCurrentPlayerResolver));

    void* app_object = nullptr;
    void* resolved_player = nullptr;
    DWORD last_error = ERROR_SUCCESS;
    __try {
      app_object = resolve_app_object(reinterpret_cast<void*>(app_holder));
      if (app_object != nullptr) {
        resolved_player = resolve_current_player(app_object);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
    }

    if (last_error == ERROR_SUCCESS && resolved_player == nullptr) {
      last_error = ERROR_NOT_FOUND;
    }
    if (last_error != ERROR_SUCCESS) {
      if (out_error != nullptr) {
        *out_error = last_error;
      }
      SetLastError(last_error);
      return FALSE;
    }

    player_object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(resolved_player));
    InterlockedExchange(&g_state.player_object, static_cast<LONG>(player_object));
  }

  *out_player_object = player_object;
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL TriggerToggleModeInputOnWindowThread(LONG mode, uint32_t target_object_id, LONG* out_active, DWORD* out_error) {
  if (out_active != nullptr) {
    *out_active = -1;
  }

  uint32_t player_object = 0;
  DWORD last_error = ERROR_SUCCESS;
  if (!EnsureCurrentPlayerObjectOnWindowThread(&player_object, &last_error)) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    return FALSE;
  }

  typedef void (__thiscall* ToggleModeInputFn)(void* player_object, int mode, uint32_t target_object_id);
  const ToggleModeInputFn toggle_mode_input =
      reinterpret_cast<ToggleModeInputFn>(RebaseAddress(kExpectedToggleModeInput));

  const uint32_t resolved_target = target_object_id != 0 ? target_object_id : kInvalidObjectId;
  __try {
    toggle_mode_input(reinterpret_cast<void*>(player_object), static_cast<int>(mode), resolved_target);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = static_cast<DWORD>(GetExceptionCode());
  }

  if (last_error != ERROR_SUCCESS) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    SetLastError(last_error);
    LogMessage(
        kLogError,
        "toggle mode input failed mode=%ld player=0x%08X target=0x%08X err=0x%08lX",
        mode,
        player_object,
        resolved_target,
        static_cast<unsigned long>(last_error));
    return FALSE;
  }

  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  LogMessage(
      kLogInfo,
      "toggle mode input dispatched mode=%ld player=0x%08X target=0x%08X active=unknown",
      mode,
      player_object,
      resolved_target);
  return TRUE;
}

BOOL EnsureCurrentPlayerCreatureOnWindowThread(uint32_t* out_player_object, uint32_t* out_creature, DWORD* out_error) {
  if (out_player_object == nullptr || out_creature == nullptr) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  uint32_t player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
  uint32_t creature = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_creature, 0, 0));
  if (player_object == 0 || creature == 0) {
    DWORD identity_error = ERROR_SUCCESS;
    ResolveCurrentCharacterIdentityOnWindowThread(&identity_error);
    player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
    creature = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_creature, 0, 0));
  }

  if (creature == 0) {
    DWORD creature_error = ERROR_SUCCESS;
    uint32_t server_object = 0;
    if (!ResolveCurrentCreatureFromObjectId(&server_object, &creature, &creature_error) &&
        (player_object == 0 ||
         !ResolveCreatureFromObjectPointer(player_object, &creature, &creature_error))) {
      if (out_error != nullptr) {
        *out_error = creature_error;
      }
      SetLastError(creature_error);
      return FALSE;
    }
    InterlockedExchange(&g_state.player_creature, static_cast<LONG>(creature));
  }

  *out_player_object = player_object;
  *out_creature = creature;
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL SetActionModeOnWindowThread(LONG mode, BOOL enabled, LONG* out_active, DWORD* out_error) {
  if (out_active != nullptr) {
    *out_active = 0;
  }

  if (mode < 0 || mode > 12) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (mode == kActionModeDefensiveCast) {
    if (!enabled) {
      if (out_error != nullptr) {
        *out_error = ERROR_NOT_SUPPORTED;
      }
      SetLastError(ERROR_NOT_SUPPORTED);
      return FALSE;
    }
    return TriggerToggleModeInputOnWindowThread(mode, kInvalidObjectId, out_active, out_error);
  }

  uint32_t player_object = 0;
  uint32_t creature = 0;
  DWORD last_error = ERROR_SUCCESS;
  if (!EnsureCurrentPlayerCreatureOnWindowThread(&player_object, &creature, &last_error)) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    return FALSE;
  }

  typedef void (__thiscall* SetActionModeFn)(void* creature, unsigned char mode, int enabled);
  typedef int (__thiscall* GetActionModeFn)(void* creature, unsigned char mode);
  const SetActionModeFn set_action_mode =
      reinterpret_cast<SetActionModeFn>(RebaseAddress(kExpectedSetActionMode));
  const GetActionModeFn get_action_mode =
      reinterpret_cast<GetActionModeFn>(RebaseAddress(kExpectedGetActionMode));

  LONG active = 0;
  __try {
    set_action_mode(
        reinterpret_cast<void*>(creature),
        static_cast<unsigned char>(mode),
        enabled ? 1 : 0);
    active = get_action_mode(reinterpret_cast<void*>(creature), static_cast<unsigned char>(mode)) ? 1 : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    last_error = static_cast<DWORD>(GetExceptionCode());
  }

  if (last_error != ERROR_SUCCESS) {
    if (out_error != nullptr) {
      *out_error = last_error;
    }
    SetLastError(last_error);
    LogMessage(
        kLogError,
        "set action mode failed mode=%ld enabled=%ld player=0x%08X creature=0x%08X err=0x%08lX",
        mode,
        enabled ? 1L : 0L,
        player_object,
        creature,
        static_cast<unsigned long>(last_error));
    return FALSE;
  }

  if (mode == kActionModeDefensiveCast) {
    LONG defensive_casting = 0;
    if (ReadDefensiveCastingModeByte(creature, &defensive_casting)) {
      active = defensive_casting;
    }
  }

  if (out_active != nullptr) {
    *out_active = active;
  }
  if ((enabled && active == 0) || (!enabled && active != 0)) {
    if (out_error != nullptr) {
      *out_error = ERROR_GEN_FAILURE;
    }
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
  }
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ReadExact(HANDLE handle, void* buffer, DWORD size) {
  BYTE* out = static_cast<BYTE*>(buffer);
  DWORD total = 0;
  while (total < size) {
    DWORD read_now = 0;
    if (!ReadFile(handle, out + total, size - total, &read_now, nullptr) || read_now == 0) {
      return FALSE;
    }
    total += read_now;
  }
  return TRUE;
}

BOOL WriteExact(HANDLE handle, const void* buffer, DWORD size) {
  const BYTE* in = static_cast<const BYTE*>(buffer);
  DWORD total = 0;
  while (total < size) {
    DWORD wrote_now = 0;
    if (!WriteFile(handle, in + total, size - total, &wrote_now, nullptr) || wrote_now == 0) {
      return FALSE;
    }
    total += wrote_now;
  }
  return TRUE;
}

BOOL WriteResponse(HANDLE pipe, uint32_t op, const void* payload, uint32_t payload_size) {
  PipeHeader header = {op, payload_size};
  if (!WriteExact(pipe, &header, sizeof(header))) {
    return FALSE;
  }
  if (payload_size == 0) {
    return TRUE;
  }
  return WriteExact(pipe, payload, payload_size);
}

struct FindWindowContext {
  DWORD process_id;
  HWND visible_hwnd;
  HWND fallback_hwnd;
  DWORD visible_thread_id;
  DWORD fallback_thread_id;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
  FindWindowContext* ctx = reinterpret_cast<FindWindowContext*>(lparam);

  DWORD process_id = 0;
  DWORD thread_id = GetWindowThreadProcessId(hwnd, &process_id);
  if (process_id != ctx->process_id) {
    return TRUE;
  }
  if (GetWindow(hwnd, GW_OWNER) != nullptr) {
    return TRUE;
  }

  if (ctx->fallback_hwnd == nullptr) {
    ctx->fallback_hwnd = hwnd;
    ctx->fallback_thread_id = thread_id;
  }

  if (IsWindowVisible(hwnd)) {
    ctx->visible_hwnd = hwnd;
    ctx->visible_thread_id = thread_id;
    return FALSE;
  }

  return TRUE;
}

BOOL FindGameWindow(HWND* out_hwnd, DWORD* out_thread_id) {
  FindWindowContext ctx = {};
  ctx.process_id = GetCurrentProcessId();

  if (!EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx))) {
    // Enumeration stopped early after finding a visible window.
  }

  if (ctx.visible_hwnd != nullptr) {
    *out_hwnd = ctx.visible_hwnd;
    *out_thread_id = ctx.visible_thread_id;
    return TRUE;
  }
  if (ctx.fallback_hwnd != nullptr) {
    *out_hwnd = ctx.fallback_hwnd;
    *out_thread_id = ctx.fallback_thread_id;
    return TRUE;
  }

  *out_hwnd = nullptr;
  *out_thread_id = 0;
  return FALSE;
}

LRESULT CALLBACK SimKeysWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_KEYDOWN || message == WM_KEYUP ||
      message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
    InterlockedIncrement(&g_state.key_message_count);
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
      InterlockedIncrement(&g_state.key_down_count);
    } else {
      InterlockedIncrement(&g_state.key_up_count);
    }
    InterlockedExchange(&g_state.last_key_message, static_cast<LONG>(message));
    InterlockedExchange(&g_state.last_key_wparam, static_cast<LONG>(wparam));
    InterlockedExchange(&g_state.last_key_lparam, static_cast<LONG>(lparam));
  }

  if (message == kMsgTriggerVk) {
    const LONG request_id = static_cast<LONG>(lparam);
    const UINT vk = static_cast<UINT>(wparam);

    if (request_id != InterlockedCompareExchange(&g_state.pending.request_id, 0, 0)) {
      return 0;
    }

    DWORD last_error = ERROR_SUCCESS;
    LRESULT rc = 0;
    LRESULT aux_rc = 0;
    LONG dispatch_path = 0;

    __try {
      const int slot_index = static_cast<int>(vk) - static_cast<int>(VK_F1);
      if (slot_index >= 0 && slot_index < 12 && InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) != 0) {
        rc = CallQuickbarExecDirect(slot_index);
        dispatch_path = 2;
        LogMessage(
            kLogDebug,
            "dispatched vk=0x%02X through quickbar exec slot=%d capturedThis=0x%08X rc=%ld",
            vk,
            slot_index,
            InterlockedCompareExchange(&g_state.quickbar_this, 0, 0),
            static_cast<long>(rc));
      } else {
        rc = CallKeyPreDispatch(hwnd, vk);
        if (g_state.original_wndproc != nullptr) {
          aux_rc = CallWindowProcA(g_state.original_wndproc, hwnd, WM_KEYUP, static_cast<WPARAM>(vk), BuildKeyUpLParam(vk));
        }
        dispatch_path = 1;
        LogMessage(
            kLogDebug,
            "dispatched vk=0x%02X through keyPreDispatch rc=%ld and keyUp rc=%ld",
            vk,
            static_cast<long>(rc),
            static_cast<long>(aux_rc));
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      aux_rc = 0;
      dispatch_path = 0;
      LogMessage(kLogError, "keyPreDispatch raised exception vk=0x%02X code=0x%08lX", vk, static_cast<unsigned long>(last_error));
    }

    InterlockedExchange(&g_state.pending.vk, static_cast<LONG>(vk));
    InterlockedExchange(&g_state.pending.result, static_cast<LONG>(rc));
    InterlockedExchange(&g_state.pending.aux_result, static_cast<LONG>(aux_rc));
    InterlockedExchange(&g_state.pending.dispatch_path, dispatch_path);
    InterlockedExchange(&g_state.pending.last_error, static_cast<LONG>(last_error));
    UpdateLastOperation(vk, static_cast<LONG>(rc), last_error);
    SetEvent(g_state.pending.event);
    return 0;
  }

  if (message == kMsgTriggerPageSlot) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending.request_id, 0, 0)) {
      return 0;
    }

    const int requested_page = static_cast<int>(LOWORD(static_cast<DWORD_PTR>(wparam)));
    const int slot_index = static_cast<int>(HIWORD(static_cast<DWORD_PTR>(wparam)));
    const UINT vk = SlotToVirtualKey(slot_index + 1);

    DWORD last_error = ERROR_SUCCESS;
    LRESULT rc = 0;
    LRESULT aux_rc = -1;
    LONG dispatch_path = 0;

    __try {
      if (requested_page < 0 || requested_page >= kQuickbarPageCount || slot_index < 0 || slot_index >= kQuickbarSlotCount) {
        last_error = ERROR_INVALID_PARAMETER;
      } else {
        LONG quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
        if (quickbar_this == 0) {
          DiscoverQuickbarPanelByScan("page-slot-trigger");
          quickbar_this = InterlockedCompareExchange(&g_state.quickbar_this, 0, 0);
        }

        if (quickbar_this == 0) {
          last_error = ERROR_NOT_FOUND;
        } else {
          LONG original_page = ResolveQuickbarPageIndex(static_cast<uint32_t>(quickbar_this));
          if (original_page < 0) {
            const LONG cached_page = InterlockedCompareExchange(&g_state.quickbar_page, 0, 0);
            if (cached_page >= 0 && cached_page < kQuickbarPageCount) {
              original_page = cached_page;
            }
          }
          aux_rc = original_page;

          LONG resolved_target_page = original_page;
          LONG page_after_exec = original_page;
          LONG final_page = original_page;
          const bool restore_needed = original_page >= 0 && original_page != requested_page;

          if (original_page != requested_page) {
            if (!CallQuickbarPageSelectDirect(requested_page, &resolved_target_page)) {
              last_error = GetLastError();
              LogMessage(
                  kLogError,
                  "quickbar page-slot request could not switch to page=%d slot=%d err=%lu",
                  requested_page,
                  slot_index,
                  static_cast<unsigned long>(last_error));
            }
          }

          if (last_error == ERROR_SUCCESS) {
            rc = CallQuickbarExecDirect(slot_index);
            if (rc == 0) {
              last_error = GetLastError();
            } else {
              dispatch_path = 3;
            }
            page_after_exec = ResolveQuickbarPageIndex(static_cast<uint32_t>(quickbar_this));
            final_page = page_after_exec;
          }

          if (dispatch_path == 3 && restore_needed) {
            LONG restored_page = -1;
            if (!CallQuickbarPageSelectDirect(original_page, &restored_page)) {
              const DWORD restore_error = GetLastError();
              if (last_error == ERROR_SUCCESS) {
                last_error = restore_error;
              }
              LogMessage(
                  kLogError,
                  "quickbar page-slot request restore failed original=%ld requested=%d slot=%d err=%lu",
                  original_page,
                  requested_page,
                  slot_index,
                  static_cast<unsigned long>(restore_error));
            } else {
              final_page = restored_page;
            }
          }

          LogMessage(
              kLogDebug,
              "dispatched quickbar page-slot requestPage=%d slot=%d vk=0x%02X panel=0x%08X originalPage=%ld targetPage=%ld pageAfterExec=%ld finalPage=%ld rc=%ld err=%lu",
              requested_page,
              slot_index,
              vk,
              static_cast<unsigned int>(quickbar_this),
              original_page,
              resolved_target_page,
              page_after_exec,
              final_page,
              static_cast<long>(rc),
              static_cast<unsigned long>(last_error));
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      aux_rc = -1;
      dispatch_path = 0;
      LogMessage(
          kLogError,
          "quickbar page-slot dispatch raised exception page=%d slot=%d code=0x%08lX",
          requested_page,
          slot_index,
          static_cast<unsigned long>(last_error));
    }

    InterlockedExchange(&g_state.pending.vk, static_cast<LONG>(vk));
    InterlockedExchange(&g_state.pending.result, static_cast<LONG>(rc));
    InterlockedExchange(&g_state.pending.aux_result, static_cast<LONG>(aux_rc));
    InterlockedExchange(&g_state.pending.dispatch_path, dispatch_path);
    InterlockedExchange(&g_state.pending.last_error, static_cast<LONG>(last_error));
    UpdateLastOperation(vk, static_cast<LONG>(rc), last_error);
    SetEvent(g_state.pending.event);
    return 0;
  }

  if (message == kMsgSendChat) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_chat.request_id, 0, 0)) {
      return 0;
    }

    const LONG mode = InterlockedCompareExchange(&g_state.pending_chat.mode, 0, 0);
    const char* text = g_state.pending_chat.text;
    DWORD last_error = ERROR_SUCCESS;
    LONG rc = 0;

    __try {
      rc = CallChatSendDirect(text, static_cast<int>(mode));
      LogMessage(kLogInfo, "chat send dispatched mode=%ld text=%s", mode, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      LogMessage(kLogError, "chat send raised exception mode=%ld code=0x%08lX text=%s", mode, static_cast<unsigned long>(last_error), text);
    }

    InterlockedExchange(&g_state.pending_chat.result, rc);
    InterlockedExchange(&g_state.pending_chat.last_error, static_cast<LONG>(last_error));
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, rc);
    InterlockedExchange(&g_state.last_chat_error, static_cast<LONG>(last_error));
    SetEvent(g_state.pending_chat.event);
    return 0;
  }

  if (message == kMsgAddMapPin) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_map_pin.request_id, 0, 0)) {
      return 0;
    }

    const float x = g_state.pending_map_pin.x;
    const float y = g_state.pending_map_pin.y;
    const char* text = g_state.pending_map_pin.text;
    DWORD last_error = ERROR_SUCCESS;
    LONG rc = 0;

    __try {
      rc = CallAddMapPinDirect(x, y, text);
      if (rc == 0) {
        last_error = GetLastError();
        if (last_error == ERROR_SUCCESS) {
          last_error = ERROR_GEN_FAILURE;
        }
      }
      LogMessage(
          last_error == ERROR_SUCCESS ? kLogInfo : kLogError,
          "map pin dispatched x=%.3f y=%.3f success=%ld rc=%ld err=%lu text=%s",
          static_cast<double>(x),
          static_cast<double>(y),
          rc ? 1L : 0L,
          rc,
          static_cast<unsigned long>(last_error),
          text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      LogMessage(
          kLogError,
          "map pin raised exception x=%.3f y=%.3f code=0x%08lX text=%s",
          static_cast<double>(x),
          static_cast<double>(y),
          static_cast<unsigned long>(last_error),
          text);
    }

    InterlockedExchange(&g_state.pending_map_pin.result, rc);
    InterlockedExchange(&g_state.pending_map_pin.last_error, static_cast<LONG>(last_error));
    UpdateLastOperation(0, rc, last_error);
    SetEvent(g_state.pending_map_pin.event);
    return 0;
  }

  if (message == kMsgMoveToLocation) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_move.request_id, 0, 0)) {
      return 0;
    }

    const float x = g_state.pending_move.x;
    const float y = g_state.pending_move.y;
    const float z = g_state.pending_move.z;
    const LONG client_side = InterlockedCompareExchange(&g_state.pending_move.client_side, 0, 0);
    const LONG bypass_no_walk = InterlockedCompareExchange(&g_state.pending_move.bypass_no_walk, 0, 0);
    const uint32_t action_object_id = g_state.pending_move.action_object_id;
    DWORD last_error = ERROR_SUCCESS;
    LONG rc = 0;

    __try {
      rc = CallMoveToLocationDirect(x, y, z, static_cast<int>(client_side), action_object_id, static_cast<int>(bypass_no_walk));
      if (rc == 0) {
        last_error = GetLastError();
        if (last_error == ERROR_SUCCESS) {
          last_error = ERROR_GEN_FAILURE;
        }
      }
      LogMessage(
          last_error == ERROR_SUCCESS ? kLogInfo : kLogError,
          "move-to-location dispatched x=%.3f y=%.3f z=%.3f clientSide=%ld bypassNoWalk=%ld actionObject=0x%08X rc=%ld err=%lu",
          static_cast<double>(x),
          static_cast<double>(y),
          static_cast<double>(z),
          client_side,
          bypass_no_walk,
          action_object_id,
          rc,
          static_cast<unsigned long>(last_error));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      LogMessage(
          kLogError,
          "move-to-location raised exception x=%.3f y=%.3f z=%.3f code=0x%08lX",
          static_cast<double>(x),
          static_cast<double>(y),
          static_cast<double>(z),
          static_cast<unsigned long>(last_error));
    }

    InterlockedExchange(&g_state.pending_move.result, rc);
    InterlockedExchange(&g_state.pending_move.last_error, static_cast<LONG>(last_error));
    UpdateLastOperation(0, rc, last_error);
    SetEvent(g_state.pending_move.event);
    return 0;
  }

  if (message == kMsgSetWalkBypass) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_walk_bypass.request_id, 0, 0)) {
      return 0;
    }

    const LONG enabled = InterlockedCompareExchange(&g_state.pending_walk_bypass.enabled, 0, 0);
    DWORD last_error = ERROR_SUCCESS;
    LONG rc = 0;

    __try {
      rc = SetWalkNoWalkBypassEnabledOnWindowThread(enabled ? TRUE : FALSE) ? 1 : 0;
      if (rc == 0) {
        last_error = GetLastError();
        if (last_error == ERROR_SUCCESS) {
          last_error = ERROR_GEN_FAILURE;
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      LogMessage(
          kLogError,
          "set walk no-walk bypass raised exception enabled=%ld code=0x%08lX",
          enabled,
          static_cast<unsigned long>(last_error));
    }

    InterlockedExchange(&g_state.pending_walk_bypass.result, rc);
    InterlockedExchange(&g_state.pending_walk_bypass.last_error, static_cast<LONG>(last_error));
    SetEvent(g_state.pending_walk_bypass.event);
    return 0;
  }

  if (message == kMsgSetActionMode) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_combat_mode.request_id, 0, 0)) {
      return 0;
    }

    const LONG mode = InterlockedCompareExchange(&g_state.pending_combat_mode.mode, 0, 0);
    const LONG enabled = InterlockedCompareExchange(&g_state.pending_combat_mode.enabled, 0, 0);
    DWORD last_error = ERROR_SUCCESS;
    LONG rc = 0;
    LONG active = 0;

    __try {
      rc = SetActionModeOnWindowThread(mode, enabled ? TRUE : FALSE, &active, &last_error) ? 1 : 0;
      if (rc == 0 && last_error == ERROR_SUCCESS) {
        last_error = GetLastError();
        if (last_error == ERROR_SUCCESS) {
          last_error = ERROR_GEN_FAILURE;
        }
      }
      LogMessage(
          last_error == ERROR_SUCCESS ? kLogInfo : kLogError,
          "set action mode dispatched mode=%ld enabled=%ld active=%ld rc=%ld err=%lu",
          mode,
          enabled,
          active,
          rc,
          static_cast<unsigned long>(last_error));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      last_error = static_cast<DWORD>(GetExceptionCode());
      rc = 0;
      active = 0;
      LogMessage(
          kLogError,
          "set action mode raised exception mode=%ld enabled=%ld code=0x%08lX",
          mode,
          enabled,
          static_cast<unsigned long>(last_error));
    }

    InterlockedExchange(&g_state.pending_combat_mode.result, rc);
    InterlockedExchange(&g_state.pending_combat_mode.active, active);
    InterlockedExchange(&g_state.pending_combat_mode.last_error, static_cast<LONG>(last_error));
    UpdateLastOperation(0, rc, last_error);
    SetEvent(g_state.pending_combat_mode.event);
    return 0;
  }

  if (message == kMsgRefreshIdentity) {
    const LONG request_id = static_cast<LONG>(lparam);
    if (request_id != InterlockedCompareExchange(&g_state.pending_identity.request_id, 0, 0)) {
      return 0;
    }

    DWORD last_error = ERROR_SUCCESS;
    ResolveCurrentCharacterIdentityOnWindowThread(&last_error);
    InterlockedExchange(&g_state.pending_identity.last_error, static_cast<LONG>(last_error));
    SetEvent(g_state.pending_identity.event);
    return 0;
  }

  if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK) {
    const int mouse_x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
    const int mouse_y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
    const bool queue_click = message == WM_LBUTTONUP;
    if (HandleOverlayMouseButton(mouse_x, mouse_y, queue_click)) {
      return 0;
    }
  }

  if (g_state.original_wndproc != nullptr) {
    return CallWindowProcA(g_state.original_wndproc, hwnd, message, wparam, lparam);
  }
  return DefWindowProcA(hwnd, message, wparam, lparam);
}

BOOL EnsureHookInstalled() {
  if (!g_state.lock_ready) {
    SetLastError(ERROR_INVALID_STATE);
    return FALSE;
  }

  EnterCriticalSection(&g_state.lock);

  if (g_state.hwnd != nullptr && IsWindow(g_state.hwnd)) {
    const LONG_PTR current_hook = GetWindowLongPtrA(g_state.hwnd, GWLP_WNDPROC);
    if (InterlockedCompareExchange(&g_state.installed, 0, 0) != 0 &&
        current_hook == reinterpret_cast<LONG_PTR>(&SimKeysWndProc) &&
        g_state.original_wndproc != nullptr) {
      LeaveCriticalSection(&g_state.lock);
      return TRUE;
    }
  }

  HWND hwnd = nullptr;
  DWORD thread_id = 0;
  if (!FindGameWindow(&hwnd, &thread_id)) {
    LeaveCriticalSection(&g_state.lock);
    SetLastError(ERROR_FILE_NOT_FOUND);
    UpdateLastOperation(0, 0, ERROR_FILE_NOT_FOUND);
    LogMessage(kLogError, "could not find the NWN game window in pid=%lu", GetCurrentProcessId());
    return FALSE;
  }

  const LONG_PTR current_proc = GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
  if (current_proc == 0) {
    const DWORD gle = GetLastError();
    LeaveCriticalSection(&g_state.lock);
    SetLastError(gle);
    UpdateLastOperation(0, 0, gle);
    LogMessage(kLogError, "GetWindowLongPtrA(GWLP_WNDPROC) failed gle=%lu", gle);
    return FALSE;
  }

  SetLastError(ERROR_SUCCESS);
  const LONG_PTR previous_proc = SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SimKeysWndProc));
  const DWORD set_wndproc_error = GetLastError();
  if (previous_proc == 0 && set_wndproc_error != ERROR_SUCCESS) {
    LeaveCriticalSection(&g_state.lock);
    SetLastError(set_wndproc_error);
    UpdateLastOperation(0, 0, set_wndproc_error);
    LogMessage(kLogError, "SetWindowLongPtrA(GWLP_WNDPROC) failed gle=%lu", set_wndproc_error);
    return FALSE;
  }

  g_state.hwnd = hwnd;
  g_state.window_thread_id = thread_id;
  g_state.original_wndproc = reinterpret_cast<WNDPROC>(previous_proc == 0 ? current_proc : previous_proc);
  InterlockedExchange(&g_state.installed, 1);

  LeaveCriticalSection(&g_state.lock);

  LogMessage(
      kLogInfo,
      "installed window hook hwnd=0x%08X current=0x%08X original=0x%08X thread=%lu expected_nwn_wndproc=0x%08X",
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_state.hwnd)),
      static_cast<unsigned int>(current_proc),
      static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_state.original_wndproc)),
      g_state.window_thread_id,
      RebaseAddress(kExpectedNwnWndProc));
  LogSnapshot(kLogDebug, "after-hook-install");

  return TRUE;
}

BOOL TriggerVirtualKey(UINT vk, LONG* out_rc, DWORD* out_error) {
  if (vk == 0) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    UpdateLastOperation(0, 0, ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (vk >= VK_F1 && vk <= VK_F12 && InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) == 0) {
    DiscoverQuickbarPanelByScan("trigger");
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending.busy, 1, 0) != 0) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    UpdateLastOperation(vk, 0, ERROR_BUSY);
    LogMessage(kLogError, "trigger rejected for vk=0x%02X because a previous dispatch is still in flight", vk);
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending.sequence_seed);
  InterlockedExchange(&g_state.pending.request_id, request_id);
  InterlockedExchange(&g_state.pending.vk, static_cast<LONG>(vk));
  InterlockedExchange(&g_state.pending.result, 0);
  InterlockedExchange(&g_state.pending.aux_result, 0);
  InterlockedExchange(&g_state.pending.dispatch_path, 0);
  InterlockedExchange(&g_state.pending.last_error, static_cast<LONG>(ERROR_IO_PENDING));
  ResetEvent(g_state.pending.event);
  LogSnapshot(kLogDebug, "before-trigger");

  if (!PostMessageA(g_state.hwnd, kMsgTriggerVk, static_cast<WPARAM>(vk), static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending.busy, 0);
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(vk, 0, gle);
    LogMessage(kLogError, "PostMessageA(custom trigger) failed vk=0x%02X gle=%lu", vk, gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending.result, 0, 0);
  const LONG aux_result = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
  const LONG dispatch_path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending.last_error, 0, 0));
  InterlockedExchange(&g_state.pending.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(vk, result, gle);
    LogMessage(kLogError, "dispatch wait failed vk=0x%02X wait_rc=%lu gle=%lu path=%ld rc=%ld aux=%ld", vk, wait_rc, gle, dispatch_path, result, aux_result);
    LogSnapshot(kLogDebug, "after-trigger-wait-failure");
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }
  LogMessage(kLogDebug, "dispatch completed vk=0x%02X path=%ld rc=%ld aux=%ld err=%lu", vk, dispatch_path, result, aux_result, static_cast<unsigned long>(last_error));
  LogSnapshot(kLogDebug, "after-trigger");

  return last_error == ERROR_SUCCESS;
}

BOOL TriggerQuickbarPageSlot(int page_index, int slot, LONG* out_rc, DWORD* out_error) {
  if (page_index < 0 || page_index >= kQuickbarPageCount || slot < 1 || slot > kQuickbarSlotCount) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    UpdateLastOperation(0, 0, ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) == 0) {
    DiscoverQuickbarPanelByScan("page-slot-trigger");
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending.busy, 1, 0) != 0) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    UpdateLastOperation(0, 0, ERROR_BUSY);
    LogMessage(
        kLogError,
        "page-slot trigger rejected for page=%d slot=%d because a previous dispatch is still in flight",
        page_index,
        slot);
    return FALSE;
  }

  const UINT vk = SlotToVirtualKey(slot);
  const LONG request_id = InterlockedIncrement(&g_state.pending.sequence_seed);
  InterlockedExchange(&g_state.pending.request_id, request_id);
  InterlockedExchange(&g_state.pending.vk, static_cast<LONG>(vk));
  InterlockedExchange(&g_state.pending.result, 0);
  InterlockedExchange(&g_state.pending.aux_result, 0);
  InterlockedExchange(&g_state.pending.dispatch_path, 0);
  InterlockedExchange(&g_state.pending.last_error, static_cast<LONG>(ERROR_IO_PENDING));
  ResetEvent(g_state.pending.event);
  LogSnapshot(kLogDebug, "before-page-slot-trigger");

  const WPARAM packed = static_cast<WPARAM>(MAKELONG(page_index, slot - 1));
  if (!PostMessageA(g_state.hwnd, kMsgTriggerPageSlot, packed, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending.busy, 0);
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(vk, 0, gle);
    LogMessage(
        kLogError,
        "PostMessageA(page-slot trigger) failed page=%d slot=%d vk=0x%02X gle=%lu",
        page_index,
        slot,
        vk,
        static_cast<unsigned long>(gle));
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending.result, 0, 0);
  const LONG aux_result = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
  const LONG dispatch_path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending.last_error, 0, 0));
  InterlockedExchange(&g_state.pending.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(vk, result, gle);
    LogMessage(
        kLogError,
        "page-slot dispatch wait failed page=%d slot=%d vk=0x%02X wait_rc=%lu gle=%lu path=%ld rc=%ld aux=%ld",
        page_index,
        slot,
        vk,
        static_cast<unsigned long>(wait_rc),
        static_cast<unsigned long>(gle),
        dispatch_path,
        result,
        aux_result);
    LogSnapshot(kLogDebug, "after-page-slot-trigger-wait-failure");
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }
  LogMessage(
      kLogDebug,
      "page-slot dispatch completed page=%d slot=%d vk=0x%02X path=%ld rc=%ld aux=%ld err=%lu",
      page_index,
      slot,
      vk,
      dispatch_path,
      result,
      aux_result,
      static_cast<unsigned long>(last_error));
  LogSnapshot(kLogDebug, "after-page-slot-trigger");

  return dispatch_path == 3;
}

BOOL TriggerChatMessage(const char* text, int mode, LONG* out_rc, DWORD* out_error) {
  if (text == nullptr || text[0] == '\0') {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, 0);
    InterlockedExchange(&g_state.last_chat_error, ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  const size_t text_length = strnlen(text, kPendingChatCapacity);
  if (text_length >= kPendingChatCapacity - 1) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_BUFFER_OVERFLOW;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, 0);
    InterlockedExchange(&g_state.last_chat_error, ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, 0);
    InterlockedExchange(&g_state.last_chat_error, static_cast<LONG>(gle));
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_chat.busy, 1, 0) != 0) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, 0);
    InterlockedExchange(&g_state.last_chat_error, ERROR_BUSY);
    LogMessage(kLogError, "chat send rejected because a previous chat dispatch is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_chat.sequence_seed);
  strncpy_s(g_state.pending_chat.text, sizeof(g_state.pending_chat.text), text, _TRUNCATE);
  InterlockedExchange(&g_state.pending_chat.request_id, request_id);
  InterlockedExchange(&g_state.pending_chat.mode, mode);
  InterlockedExchange(&g_state.pending_chat.result, 0);
  InterlockedExchange(&g_state.pending_chat.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_chat.event);
  LogMessage(kLogDebug, "queueing chat send request mode=%d text=%s", mode, text);

  if (!PostMessageA(g_state.hwnd, kMsgSendChat, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_chat.busy, 0);
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, 0);
    InterlockedExchange(&g_state.last_chat_error, static_cast<LONG>(gle));
    LogMessage(kLogError, "PostMessageA(chat trigger) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_chat.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending_chat.result, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_chat.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_chat.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    InterlockedExchange(&g_state.last_chat_mode, mode);
    InterlockedExchange(&g_state.last_chat_result, result);
    InterlockedExchange(&g_state.last_chat_error, static_cast<LONG>(gle));
    LogMessage(kLogError, "chat dispatch wait failed wait_rc=%lu gle=%lu mode=%d result=%ld", wait_rc, gle, mode, result);
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }

  return last_error == ERROR_SUCCESS;
}

BOOL TriggerAddMapPin(float x, float y, const char* text, LONG* out_rc, DWORD* out_error) {
  if (out_rc != nullptr) {
    *out_rc = 0;
  }
  if (out_error != nullptr) {
    *out_error = ERROR_SUCCESS;
  }

  if (text == nullptr || text[0] == '\0' || !IsPlausibleCoordinate(x) || !IsPlausibleCoordinate(y)) {
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    UpdateLastOperation(0, 0, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (strnlen(text, kMapPinTextCapacity) >= kMapPinTextCapacity - 1) {
    if (out_error != nullptr) {
      *out_error = ERROR_BUFFER_OVERFLOW;
    }
    UpdateLastOperation(0, 0, ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_map_pin.busy, 1, 0) != 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    UpdateLastOperation(0, 0, ERROR_BUSY);
    LogMessage(kLogError, "map pin rejected because a previous map pin dispatch is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_map_pin.sequence_seed);
  g_state.pending_map_pin.x = x;
  g_state.pending_map_pin.y = y;
  strncpy_s(g_state.pending_map_pin.text, sizeof(g_state.pending_map_pin.text), text, _TRUNCATE);
  InterlockedExchange(&g_state.pending_map_pin.request_id, request_id);
  InterlockedExchange(&g_state.pending_map_pin.result, 0);
  InterlockedExchange(&g_state.pending_map_pin.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_map_pin.event);
  LogMessage(kLogDebug, "queueing map pin request x=%.3f y=%.3f text=%s", static_cast<double>(x), static_cast<double>(y), text);

  if (!PostMessageA(g_state.hwnd, kMsgAddMapPin, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_map_pin.busy, 0);
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(0, 0, gle);
    LogMessage(kLogError, "PostMessageA(map pin trigger) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_map_pin.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending_map_pin.result, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_map_pin.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_map_pin.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(0, result, gle);
    LogMessage(
        kLogError,
        "map pin dispatch wait failed wait_rc=%lu gle=%lu result=%ld",
        static_cast<unsigned long>(wait_rc),
        static_cast<unsigned long>(gle),
        result);
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS && result != 0;
}

BOOL TriggerMoveToLocation(
    float x,
    float y,
    float z,
    int client_side,
    uint32_t action_object_id,
    int bypass_no_walk,
    LONG* out_rc,
    DWORD* out_error) {
  if (!IsPlausiblePosition(x, y, z)) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_INVALID_PARAMETER;
    }
    UpdateLastOperation(0, 0, ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_move.busy, 1, 0) != 0) {
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    UpdateLastOperation(0, 0, ERROR_BUSY);
    LogMessage(kLogError, "move-to-location rejected because a previous movement dispatch is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_move.sequence_seed);
  g_state.pending_move.x = x;
  g_state.pending_move.y = y;
  g_state.pending_move.z = z;
  g_state.pending_move.action_object_id = action_object_id != 0 ? action_object_id : kInvalidObjectId;
  InterlockedExchange(&g_state.pending_move.request_id, request_id);
  InterlockedExchange(&g_state.pending_move.client_side, client_side ? 1 : 0);
  InterlockedExchange(&g_state.pending_move.bypass_no_walk, bypass_no_walk ? 1 : 0);
  InterlockedExchange(&g_state.pending_move.result, 0);
  InterlockedExchange(&g_state.pending_move.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_move.event);
  LogMessage(
      kLogDebug,
      "queueing move-to-location request x=%.3f y=%.3f z=%.3f clientSide=%d bypassNoWalk=%d actionObject=0x%08X",
      static_cast<double>(x),
      static_cast<double>(y),
      static_cast<double>(z),
      client_side ? 1 : 0,
      bypass_no_walk ? 1 : 0,
      g_state.pending_move.action_object_id);

  if (!PostMessageA(g_state.hwnd, kMsgMoveToLocation, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_move.busy, 0);
    if (out_rc != nullptr) {
      *out_rc = 0;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(0, 0, gle);
    LogMessage(kLogError, "PostMessageA(move-to-location trigger) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_move.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending_move.result, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_move.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_move.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    UpdateLastOperation(0, result, gle);
    LogMessage(
        kLogError,
        "move-to-location dispatch wait failed wait_rc=%lu gle=%lu result=%ld",
        static_cast<unsigned long>(wait_rc),
        static_cast<unsigned long>(gle),
        result);
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS && result != 0;
}

BOOL TriggerSetWalkNoWalkBypass(BOOL enabled, DWORD* out_error) {
  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_walk_bypass.busy, 1, 0) != 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    LogMessage(kLogError, "set walk no-walk bypass rejected because a previous request is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_walk_bypass.sequence_seed);
  InterlockedExchange(&g_state.pending_walk_bypass.request_id, request_id);
  InterlockedExchange(&g_state.pending_walk_bypass.enabled, enabled ? 1 : 0);
  InterlockedExchange(&g_state.pending_walk_bypass.result, 0);
  InterlockedExchange(&g_state.pending_walk_bypass.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_walk_bypass.event);

  if (!PostMessageA(g_state.hwnd, kMsgSetWalkBypass, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_walk_bypass.busy, 0);
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(kLogError, "PostMessageA(set walk no-walk bypass) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_walk_bypass.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending_walk_bypass.result, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_walk_bypass.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_walk_bypass.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(
        kLogError,
        "set walk no-walk bypass wait failed wait_rc=%lu gle=%lu result=%ld",
        static_cast<unsigned long>(wait_rc),
        static_cast<unsigned long>(gle),
        result);
    return FALSE;
  }

  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS && result != 0;
}

BOOL TriggerSetActionMode(LONG mode, BOOL enabled, LONG* out_rc, LONG* out_active, DWORD* out_error) {
  if (out_rc != nullptr) {
    *out_rc = 0;
  }
  if (out_active != nullptr) {
    *out_active = 0;
  }

  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_error != nullptr) {
      *out_error = gle;
    }
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_combat_mode.busy, 1, 0) != 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    LogMessage(kLogError, "set action mode rejected because a previous request is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_combat_mode.sequence_seed);
  InterlockedExchange(&g_state.pending_combat_mode.request_id, request_id);
  InterlockedExchange(&g_state.pending_combat_mode.mode, mode);
  InterlockedExchange(&g_state.pending_combat_mode.enabled, enabled ? 1 : 0);
  InterlockedExchange(&g_state.pending_combat_mode.result, 0);
  InterlockedExchange(&g_state.pending_combat_mode.active, 0);
  InterlockedExchange(&g_state.pending_combat_mode.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_combat_mode.event);

  if (!PostMessageA(g_state.hwnd, kMsgSetActionMode, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_combat_mode.busy, 0);
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(kLogError, "PostMessageA(set action mode) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_combat_mode.event, kDispatchTimeoutMs);
  const LONG result = InterlockedCompareExchange(&g_state.pending_combat_mode.result, 0, 0);
  const LONG active = InterlockedCompareExchange(&g_state.pending_combat_mode.active, 0, 0);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_combat_mode.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_combat_mode.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    if (out_rc != nullptr) {
      *out_rc = result;
    }
    if (out_active != nullptr) {
      *out_active = active;
    }
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(
        kLogError,
        "set action mode wait failed wait_rc=%lu gle=%lu result=%ld",
        static_cast<unsigned long>(wait_rc),
        static_cast<unsigned long>(gle),
        result);
    return FALSE;
  }

  if (out_rc != nullptr) {
    *out_rc = result;
  }
  if (out_active != nullptr) {
    *out_active = active;
  }
  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS && result != 0;
}

BOOL RefreshCharacterIdentity(DWORD* out_error) {
  if (!EnsureHookInstalled()) {
    const DWORD gle = GetLastError();
    if (out_error != nullptr) {
      *out_error = gle;
    }
    InterlockedExchange(&g_state.identity_error, static_cast<LONG>(gle));
    return FALSE;
  }

  if (InterlockedCompareExchange(&g_state.pending_identity.busy, 1, 0) != 0) {
    if (out_error != nullptr) {
      *out_error = ERROR_BUSY;
    }
    InterlockedExchange(&g_state.identity_error, ERROR_BUSY);
    LogMessage(kLogDebug, "identity refresh rejected because a previous refresh is still in flight");
    return FALSE;
  }

  const LONG request_id = InterlockedIncrement(&g_state.pending_identity.sequence_seed);
  InterlockedExchange(&g_state.pending_identity.request_id, request_id);
  InterlockedExchange(&g_state.pending_identity.last_error, ERROR_IO_PENDING);
  ResetEvent(g_state.pending_identity.event);

  if (!PostMessageA(g_state.hwnd, kMsgRefreshIdentity, 0, static_cast<LPARAM>(request_id))) {
    const DWORD gle = GetLastError();
    InterlockedExchange(&g_state.pending_identity.busy, 0);
    InterlockedExchange(&g_state.identity_error, static_cast<LONG>(gle));
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(kLogError, "PostMessageA(identity refresh) failed gle=%lu", gle);
    return FALSE;
  }

  const DWORD wait_rc = WaitForSingleObject(g_state.pending_identity.event, kDispatchTimeoutMs);
  const DWORD last_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pending_identity.last_error, 0, 0));
  InterlockedExchange(&g_state.pending_identity.busy, 0);

  if (wait_rc != WAIT_OBJECT_0) {
    const DWORD gle = (wait_rc == WAIT_TIMEOUT) ? WAIT_TIMEOUT : GetLastError();
    InterlockedExchange(&g_state.identity_error, static_cast<LONG>(gle));
    if (out_error != nullptr) {
      *out_error = gle;
    }
    LogMessage(kLogError, "identity refresh wait failed wait_rc=%lu gle=%lu", wait_rc, gle);
    return FALSE;
  }

  if (out_error != nullptr) {
    *out_error = last_error;
  }
  return last_error == ERROR_SUCCESS;
}

BOOL HandlePipeClient(HANDLE pipe) {
  for (;;) {
    PipeHeader header = {};
    if (!ReadExact(pipe, &header, sizeof(header))) {
      return FALSE;
    }

    if (header.size > kPipeBufferSize) {
      LogMessage(kLogError, "rejecting oversized pipe payload op=%u size=%u", header.op, header.size);
      return FALSE;
    }

    BYTE payload[kPipeBufferSize] = {};
    if (header.size > 0 && !ReadExact(pipe, payload, header.size)) {
      return FALSE;
    }

    switch (header.op) {
      case kOpQuery: {
        EnsureHookInstalled();
        if (InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) == 0) {
          DiscoverQuickbarPanelByScan("query");
        }
        RefreshCharacterIdentity(nullptr);
        QueryResponse response = {};
        response.module_base = static_cast<uint32_t>(GetProcessImageBase());
        response.hook_wndproc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SimKeysWndProc));
        response.hwnd = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_state.hwnd));
        response.current_wndproc = (g_state.hwnd != nullptr)
            ? static_cast<uint32_t>(GetWindowLongPtrA(g_state.hwnd, GWLP_WNDPROC))
            : 0;
        response.original_wndproc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_state.original_wndproc));
        response.window_thread_id = g_state.window_thread_id;
        response.installed = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.installed, 0, 0));
        response.expected_runtime_nwn_wndproc = RebaseAddress(kExpectedNwnWndProc);
        response.expected_runtime_key_pre_dispatch = RebaseAddress(kExpectedKeyPreDispatch);
        response.expected_runtime_dispatcher_thunk = RebaseAddress(kExpectedDispatcherThunk);
        response.expected_runtime_dispatcher_slot0 = RebaseAddress(kExpectedDispatcherSlot0);
        response.app_global_slot = static_cast<uint32_t>(kAppGlobalSlotAddress);
        response.app_holder = ReadAppHolderPointer();
        response.app_object = ReadAppObjectPointer();
        response.app_inner = response.app_object != 0 ? SafeReadPointer32(static_cast<uintptr_t>(response.app_object) + 4) : 0;
        response.dispatcher_ptr = response.app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(response.app_inner) + 0x24) : 0;
        response.gate_90 = response.app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(response.app_inner) + 0x90) : 0;
        response.gate_94 = response.app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(response.app_inner) + 0x94) : 0;
        response.gate_98 = response.app_inner != 0 ? SafeReadPointer32(static_cast<uintptr_t>(response.app_inner) + 0x98) : 0;
        response.quickbar_exec = RebaseAddress(kExpectedQuickbarExec);
        response.quickbar_slot_dispatch = RebaseAddress(kExpectedQuickbarSlotDispatch);
        response.quickbar_panel_vtable = RebaseAddress(kExpectedQuickbarVtable);
        response.quickbar_slot_ptr = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_slot_ptr, 0, 0));
        response.quickbar_this = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_this, 0, 0));
        response.quickbar_page = InterlockedCompareExchange(&g_state.quickbar_page, 0, 0);
        response.quickbar_slot = InterlockedCompareExchange(&g_state.quickbar_slot, 0, 0);
        response.quickbar_slot_type = InterlockedCompareExchange(&g_state.quickbar_slot_type, 0, 0);
        response.quickbar_calls = InterlockedCompareExchange(&g_state.quickbar_calls, 0, 0);
        response.quickbar_scan_attempts = InterlockedCompareExchange(&g_state.quickbar_scan_attempts, 0, 0);
        response.quickbar_scan_hits = InterlockedCompareExchange(&g_state.quickbar_scan_hits, 0, 0);
        response.last_vk = InterlockedCompareExchange(&g_state.last_vk, 0, 0);
        response.last_rc = InterlockedCompareExchange(&g_state.last_result, 0, 0);
        response.last_error = InterlockedCompareExchange(&g_state.last_error, 0, 0);
        response.log_level = InterlockedCompareExchange(&g_state.log_level, 0, 0);
        response.player_object = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_object, 0, 0));
        response.player_creature = static_cast<uint32_t>(InterlockedCompareExchange(&g_state.player_creature, 0, 0));
        response.identity_refresh_count = InterlockedCompareExchange(&g_state.identity_refresh_count, 0, 0);
        response.identity_error = InterlockedCompareExchange(&g_state.identity_error, 0, 0);
        response.quickbar_item_mask_low =
            static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_item_mask_low, 0, 0));
        response.quickbar_item_mask_high =
            static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_item_mask_high, 0, 0));
        response.quickbar_equipped_mask_low =
            static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_equipped_mask_low, 0, 0));
        response.quickbar_equipped_mask_high =
            static_cast<uint32_t>(InterlockedCompareExchange(&g_state.quickbar_equipped_mask_high, 0, 0));
        response.position_valid = TryReadCurrentPlayerPosition(
            &response.position_x,
            &response.position_y,
            &response.position_z) ? 1 : 0;
        CopyStoredCharacterName(response.character_name, ARRAYSIZE(response.character_name));

        if (!WriteResponse(pipe, kOpQuery, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpQuickbarWeapons: {
        EnsureHookInstalled();
        if (InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) == 0) {
          DiscoverQuickbarPanelByScan("quickbar-weapons");
        }
        QuickbarWeaponInfoRequest request = {};
        if (header.size >= sizeof(request)) {
          memcpy(&request, payload, sizeof(request));
        }
        InterlockedExchange(&g_state.quickbar_weapon_detail_slot_mask_low, static_cast<LONG>(request.detail_slot_mask_low));
        InterlockedExchange(&g_state.quickbar_weapon_detail_slot_mask_high, static_cast<LONG>(request.detail_slot_mask_high));
        InterlockedExchange(&g_state.quickbar_weapon_refresh_requested, 1);
        RefreshCharacterIdentity(nullptr);
        QuickbarWeaponInfoResponse response = {};
        CopyStoredQuickbarWeaponInfo(&response);
        if (!WriteResponse(pipe, kOpQuickbarWeapons, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpSnapshotText: {
        if (InterlockedCompareExchange(&g_state.quickbar_this, 0, 0) == 0) {
          DiscoverQuickbarPanelByScan("snapshot");
        }
        RefreshCharacterIdentity(nullptr);
        char snapshot[4096] = {};
        BuildSnapshotText("pipe-query", snapshot, sizeof(snapshot));
        if (!WriteResponse(pipe, kOpSnapshotText, snapshot, static_cast<uint32_t>(strlen(snapshot)))) {
          return FALSE;
        }
        break;
      }

      case kOpChatSend: {
        ChatSendResponse response = {};
        if (header.size < sizeof(int32_t) * 2) {
          response.last_error = ERROR_INVALID_DATA;
          InterlockedExchange(&g_state.last_chat_result, 0);
          InterlockedExchange(&g_state.last_chat_error, ERROR_INVALID_DATA);
        } else {
          const int32_t mode = *reinterpret_cast<const int32_t*>(payload + 0);
          const int32_t text_length = *reinterpret_cast<const int32_t*>(payload + sizeof(int32_t));
          const DWORD expected_size = static_cast<DWORD>(sizeof(int32_t) * 2 + (text_length > 0 ? text_length : 0));
          if (text_length < 0 || header.size != expected_size || text_length >= kPendingChatCapacity) {
            response.last_error = ERROR_INVALID_DATA;
            InterlockedExchange(&g_state.last_chat_mode, mode);
            InterlockedExchange(&g_state.last_chat_result, 0);
            InterlockedExchange(&g_state.last_chat_error, ERROR_INVALID_DATA);
          } else {
            char text[kPendingChatCapacity] = {};
            if (text_length > 0) {
              memcpy(text, payload + sizeof(int32_t) * 2, static_cast<size_t>(text_length));
            }
            text[text_length] = '\0';

            LONG rc = 0;
            DWORD last_error = ERROR_SUCCESS;
            response.success = TriggerChatMessage(text, mode, &rc, &last_error) ? 1 : 0;
            response.mode = mode;
            response.rc = rc;
            response.last_error = static_cast<int32_t>(last_error);
            LogMessage(kLogInfo, "chat request mode=%ld success=%ld rc=%ld err=%ld text=%s", mode, response.success, response.rc, response.last_error, text);
          }
        }

        if (!WriteResponse(pipe, kOpChatSend, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpMoveToLocation: {
        MoveToLocationResponse response = {};
        if (header.size != kMoveToLocationRequestLegacySize && header.size != sizeof(MoveToLocationRequest)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          MoveToLocationRequest request = {};
          memcpy(&request, payload, header.size);
          LONG rc = 0;
          DWORD last_error = ERROR_SUCCESS;
          response.x = request.x;
          response.y = request.y;
          response.z = request.z;
          response.success = TriggerMoveToLocation(
              request.x,
              request.y,
              request.z,
              request.client_side,
              request.action_object_id,
              request.bypass_no_walk,
              &rc,
              &last_error) ? 1 : 0;
          response.rc = rc;
          response.last_error = static_cast<int32_t>(last_error);
          LogMessage(
              response.success ? kLogInfo : kLogError,
              "move-to-location request x=%.3f y=%.3f z=%.3f bypassNoWalk=%ld success=%ld rc=%ld err=%ld",
              static_cast<double>(response.x),
              static_cast<double>(response.y),
              static_cast<double>(response.z),
              request.bypass_no_walk ? 1L : 0L,
              response.success,
              response.rc,
              response.last_error);
        }

        if (!WriteResponse(pipe, kOpMoveToLocation, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpSetWalkBypass: {
        WalkBypassResponse response = {};
        if (header.size != sizeof(WalkBypassRequest)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          WalkBypassRequest request = {};
          memcpy(&request, payload, sizeof(request));
          DWORD last_error = ERROR_SUCCESS;
          response.success = TriggerSetWalkNoWalkBypass(request.enabled ? TRUE : FALSE, &last_error) ? 1 : 0;
          response.enabled = static_cast<int32_t>(InterlockedCompareExchange(&g_state.walk_no_walk_bypass_enabled, 0, 0));
          response.last_error = static_cast<int32_t>(last_error);
          LogMessage(
              response.success ? kLogInfo : kLogError,
              "set walk no-walk bypass request enabled=%ld active=%ld success=%ld err=%ld",
              request.enabled ? 1L : 0L,
              response.enabled,
              response.success,
              response.last_error);
        }

        if (!WriteResponse(pipe, kOpSetWalkBypass, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpSetActionMode: {
        SetActionModeResponse response = {};
        if (header.size != sizeof(SetActionModeRequest)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          SetActionModeRequest request = {};
          memcpy(&request, payload, sizeof(request));
          LONG rc = 0;
          LONG active = 0;
          DWORD last_error = ERROR_SUCCESS;
          response.success = TriggerSetActionMode(
              request.mode,
              request.enabled ? TRUE : FALSE,
              &rc,
              &active,
              &last_error) ? 1 : 0;
          response.mode = request.mode;
          response.enabled = request.enabled ? 1 : 0;
          response.active = active;
          response.rc = rc;
          response.last_error = static_cast<int32_t>(last_error);
          LogMessage(
              response.success ? kLogInfo : kLogError,
              "set action mode request mode=%ld enabled=%ld active=%ld success=%ld rc=%ld err=%ld",
              response.mode,
              response.enabled,
              response.active,
              response.success,
              response.rc,
              response.last_error);
        }

        if (!WriteResponse(pipe, kOpSetActionMode, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpMapPin: {
        MapPinResponse response = {};
        if (header.size < sizeof(MapPinRequestHeader)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          MapPinRequestHeader request = {};
          memcpy(&request, payload, sizeof(request));
          const DWORD expected_size = static_cast<DWORD>(
              sizeof(MapPinRequestHeader) + (request.text_length > 0 ? request.text_length : 0));
          if (request.text_length <= 0 ||
              request.text_length >= kMapPinTextCapacity ||
              header.size != expected_size) {
            response.last_error = ERROR_INVALID_DATA;
            UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
          } else {
            char text[kMapPinTextCapacity] = {};
            memcpy(text, payload + sizeof(MapPinRequestHeader), static_cast<size_t>(request.text_length));
            text[request.text_length] = '\0';
            LONG rc = 0;
            DWORD last_error = ERROR_SUCCESS;
            response.success = TriggerAddMapPin(request.x, request.y, text, &rc, &last_error) ? 1 : 0;
            response.rc = rc;
            response.last_error = static_cast<int32_t>(last_error);
            LogMessage(
                response.success ? kLogInfo : kLogError,
                "map pin request x=%.3f y=%.3f success=%ld rc=%ld err=%ld text=%s",
                static_cast<double>(request.x),
                static_cast<double>(request.y),
                response.success,
                response.rc,
                response.last_error,
                text);
          }
        }

        if (!WriteResponse(pipe, kOpMapPin, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpChatPoll: {
        ChatPollRequest request = {};
        if (header.size == sizeof(ChatPollRequest)) {
          memcpy(&request, payload, sizeof(request));
        } else {
          request.after_sequence = 0;
          request.max_lines = 20;
        }

        BYTE response[kPipeBufferSize] = {};
        DWORD response_size = 0;
        if (!BuildChatPollResponse(request, response, sizeof(response), &response_size)) {
          return FALSE;
        }
        if (!WriteResponse(pipe, kOpChatPoll, response, response_size)) {
          return FALSE;
        }
        break;
      }

      case kOpOverlayText: {
        OverlayResponse response = {};
        if (header.size < sizeof(OverlayTextRequestHeader)) {
          response.last_error = ERROR_INVALID_DATA;
        } else {
          OverlayTextRequestHeader request = {};
          memcpy(&request, payload, sizeof(request));
          const DWORD expected_size = static_cast<DWORD>(sizeof(OverlayTextRequestHeader) + (request.text_length > 0 ? request.text_length : 0));
          if (request.text_length < 0 || request.text_length >= kOverlayTextCapacity || header.size != expected_size) {
            response.last_error = ERROR_INVALID_DATA;
          } else if (!InstallOverlayHook()) {
            response.last_error = static_cast<int32_t>(GetLastError());
          } else {
            char text[kOverlayTextCapacity] = {};
            if (request.text_length > 0) {
              memcpy(text, payload + sizeof(OverlayTextRequestHeader), static_cast<size_t>(request.text_length));
            }
            text[request.text_length] = '\0';

            int width = 0;
            int height = 0;
            response.success = RenderTextOverlay(
                request.id,
                request.position,
                request.offset_x,
                request.offset_y,
                request.font_size,
                request.color_rgb,
                text,
                &width,
                &height) ? 1 : 0;
            response.width = width;
            response.height = height;
            response.last_error = response.success ? ERROR_SUCCESS : static_cast<int32_t>(GetLastError());
            InterlockedExchange(&g_state.overlay_last_error, response.last_error);
            LogMessage(
                kLogDebug,
                "overlay text request id=%ld pos=%ld size=%ldx%ld success=%ld err=%ld text=%s",
                request.id,
                request.position,
                response.width,
                response.height,
                response.success,
                response.last_error,
                text);
          }
        }

        if (!WriteResponse(pipe, kOpOverlayText, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpOverlayClear: {
        OverlayResponse response = {};
        if (header.size != sizeof(int32_t)) {
          response.last_error = ERROR_INVALID_DATA;
        } else {
          const int32_t id = *reinterpret_cast<const int32_t*>(payload);
          response.success = ClearOverlayById(id) ? 1 : 0;
          response.last_error = response.success ? ERROR_SUCCESS : static_cast<int32_t>(GetLastError());
          InterlockedExchange(&g_state.overlay_last_error, response.last_error);
        }

        if (!WriteResponse(pipe, kOpOverlayClear, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpOverlayClearAll: {
        OverlayResponse response = {};
        response.success = ClearAllOverlays() ? 1 : 0;
        response.last_error = response.success ? ERROR_SUCCESS : static_cast<int32_t>(GetLastError());
        InterlockedExchange(&g_state.overlay_last_error, response.last_error);
        if (!WriteResponse(pipe, kOpOverlayClearAll, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpTriggerPageSlot: {
        TriggerResponse response = {};
        if (header.size != sizeof(int32_t) * 2) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          const int32_t slot = *reinterpret_cast<const int32_t*>(payload + 0);
          const int32_t page = *reinterpret_cast<const int32_t*>(payload + sizeof(int32_t));
          const UINT vk = SlotToVirtualKey(slot);
          LONG rc = 0;
          DWORD last_error = ERROR_SUCCESS;
          response.success = TriggerQuickbarPageSlot(page, slot, &rc, &last_error) ? 1 : 0;
          response.vk = static_cast<int32_t>(vk);
          response.rc = rc;
          response.aux_rc = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
          response.last_error = static_cast<int32_t>(last_error);
          response.path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
          LogMessage(
              kLogInfo,
              "page-slot request page=%ld slot=%ld vk=0x%02X success=%ld rc=%ld aux=%ld path=%ld err=%ld",
              page,
              slot,
              vk,
              response.success,
              response.rc,
              response.aux_rc,
              response.path,
              response.last_error);
        }

        if (!WriteResponse(pipe, kOpTriggerPageSlot, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpTriggerSlot: {
        TriggerResponse response = {};
        if (header.size != sizeof(int32_t)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          const int32_t slot = *reinterpret_cast<int32_t*>(payload);
          const UINT vk = SlotToVirtualKey(slot);
          LONG rc = 0;
          DWORD last_error = ERROR_SUCCESS;
          response.success = TriggerVirtualKey(vk, &rc, &last_error) ? 1 : 0;
          response.vk = static_cast<int32_t>(vk);
          response.rc = rc;
          response.aux_rc = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
          response.last_error = static_cast<int32_t>(last_error);
          response.path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
          LogMessage(kLogInfo, "slot request slot=%ld vk=0x%02X success=%ld rc=%ld aux=%ld path=%ld err=%ld", slot, vk, response.success, response.rc, response.aux_rc, response.path, response.last_error);
        }

        if (!WriteResponse(pipe, kOpTriggerSlot, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpTriggerVk: {
        TriggerResponse response = {};
        if (header.size != sizeof(int32_t)) {
          response.last_error = ERROR_INVALID_DATA;
          UpdateLastOperation(0, 0, ERROR_INVALID_DATA);
        } else {
          const UINT vk = static_cast<UINT>(*reinterpret_cast<int32_t*>(payload));
          LONG rc = 0;
          DWORD last_error = ERROR_SUCCESS;
          response.success = TriggerVirtualKey(vk, &rc, &last_error) ? 1 : 0;
          response.vk = static_cast<int32_t>(vk);
          response.rc = rc;
          response.aux_rc = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
          response.last_error = static_cast<int32_t>(last_error);
          response.path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
          LogMessage(kLogInfo, "vk request vk=0x%02X success=%ld rc=%ld aux=%ld path=%ld err=%ld", vk, response.success, response.rc, response.aux_rc, response.path, response.last_error);
        }

        if (!WriteResponse(pipe, kOpTriggerVk, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      case kOpSetLog: {
        int32_t new_level = kLogInfo;
        if (header.size == sizeof(int32_t)) {
          new_level = *reinterpret_cast<int32_t*>(payload);
        }
        if (new_level < kLogError) {
          new_level = kLogError;
        }
        if (new_level > kLogDebug) {
          new_level = kLogDebug;
        }
        InterlockedExchange(&g_state.log_level, new_level);
        if (!WriteResponse(pipe, kOpSetLog, &new_level, sizeof(new_level))) {
          return FALSE;
        }
        break;
      }

      case kOpReplayLast: {
        TriggerResponse response = {};
        const UINT vk = static_cast<UINT>(InterlockedCompareExchange(&g_state.last_vk, 0, 0));
        LONG rc = 0;
        DWORD last_error = ERROR_SUCCESS;
        response.success = TriggerVirtualKey(vk, &rc, &last_error) ? 1 : 0;
        response.vk = static_cast<int32_t>(vk);
        response.rc = rc;
        response.aux_rc = InterlockedCompareExchange(&g_state.pending.aux_result, 0, 0);
        response.last_error = static_cast<int32_t>(last_error);
        response.path = InterlockedCompareExchange(&g_state.pending.dispatch_path, 0, 0);
        if (!WriteResponse(pipe, kOpReplayLast, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }

      default: {
        TriggerResponse response = {};
        response.last_error = ERROR_INVALID_FUNCTION;
        UpdateLastOperation(0, 0, ERROR_INVALID_FUNCTION);
        if (!WriteResponse(pipe, header.op, &response, sizeof(response))) {
          return FALSE;
        }
        break;
      }
    }
  }
}

DWORD WINAPI PipeThreadMain(LPVOID) {
  char pipe_name[64] = {};
  _snprintf_s(pipe_name, sizeof(pipe_name), _TRUNCATE, "\\\\.\\pipe\\simkeys_%lu", GetCurrentProcessId());

  LogMessage(kLogInfo, "pipe thread starting on %s", pipe_name);

  for (;;) {
    HANDLE pipe = CreateNamedPipeA(
        pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        kPipeBufferSize,
        kPipeBufferSize,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
      const DWORD gle = GetLastError();
      InterlockedExchange(&g_state.pipe_state, -1);
      InterlockedExchange(&g_state.pipe_thread_error, static_cast<LONG>(gle));
      if (g_state.pipe_ready_event != nullptr) {
        SetEvent(g_state.pipe_ready_event);
      }
      UpdateLastOperation(0, 0, gle);
      LogMessage(kLogError, "CreateNamedPipeA failed gle=%lu", gle);
      return gle == ERROR_SUCCESS ? 1 : gle;
    }

    if (InterlockedCompareExchange(&g_state.pipe_state, 1, 0) == 0) {
      InterlockedExchange(&g_state.pipe_thread_error, ERROR_SUCCESS);
      if (g_state.pipe_ready_event != nullptr) {
        SetEvent(g_state.pipe_ready_event);
      }
      LogMessage(kLogInfo, "pipe server is ready on %s", pipe_name);
    }

    BOOL connected = ConnectNamedPipe(pipe, nullptr);
    if (!connected) {
      const DWORD gle = GetLastError();
      if (gle != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipe);
        LogMessage(kLogError, "ConnectNamedPipe failed gle=%lu", gle);
        Sleep(250);
        continue;
      }
    }

    LogMessage(kLogDebug, "pipe client connected");
    HandlePipeClient(pipe);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    LogMessage(kLogDebug, "pipe client disconnected");
  }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_state.module = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

SIMKEYS_API DWORD WINAPI InitSimKeys(LPVOID) {
  const LONG previous = InterlockedCompareExchange(&g_state.initialized, 1, 0);
  if (previous != 0) {
    const LONG pipe_state = InterlockedCompareExchange(&g_state.pipe_state, 0, 0);
    if (pipe_state == 1) {
      return 2;
    }
    return ERROR_PIPE_NOT_CONNECTED;
  }

  InitializeCriticalSection(&g_state.lock);
  g_state.lock_ready = true;
  InitializeCriticalSection(&g_state.chat_lock);
  g_state.chat_lock_ready = true;
  InitializeCriticalSection(&g_state.log_lock);
  g_state.log_lock_ready = true;
  InitializeCriticalSection(&g_state.overlay_lock);
  g_state.overlay_lock_ready = true;
  g_state.log_file = nullptr;
  g_state.module_path[0] = '\0';
  g_state.log_path[0] = '\0';
  g_state.character_name[0] = '\0';
  ZeroMemory(g_state.overlays, sizeof(g_state.overlays));
  ZeroMemory(g_state.chat_lines, sizeof(g_state.chat_lines));
  InterlockedExchange(&g_state.pipe_state, 0);
  InterlockedExchange(&g_state.pipe_thread_error, ERROR_IO_PENDING);
  EnsureLogFileReady();
  g_state.pending.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending.event == nullptr) {
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_chat.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_chat.event == nullptr) {
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_identity.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_identity.event == nullptr) {
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_move.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_move.event == nullptr) {
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_walk_bypass.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_walk_bypass.event == nullptr) {
    CloseHandle(g_state.pending_move.event);
    g_state.pending_move.event = nullptr;
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_combat_mode.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_combat_mode.event == nullptr) {
    CloseHandle(g_state.pending_walk_bypass.event);
    g_state.pending_walk_bypass.event = nullptr;
    CloseHandle(g_state.pending_move.event);
    g_state.pending_move.event = nullptr;
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pending_map_pin.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_state.pending_map_pin.event == nullptr) {
    CloseHandle(g_state.pending_combat_mode.event);
    g_state.pending_combat_mode.event = nullptr;
    CloseHandle(g_state.pending_walk_bypass.event);
    g_state.pending_walk_bypass.event = nullptr;
    CloseHandle(g_state.pending_move.event);
    g_state.pending_move.event = nullptr;
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  g_state.pipe_ready_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (g_state.pipe_ready_event == nullptr) {
    CloseHandle(g_state.pending_map_pin.event);
    g_state.pending_map_pin.event = nullptr;
    CloseHandle(g_state.pending_combat_mode.event);
    g_state.pending_combat_mode.event = nullptr;
    CloseHandle(g_state.pending_walk_bypass.event);
    g_state.pending_walk_bypass.event = nullptr;
    CloseHandle(g_state.pending_move.event);
    g_state.pending_move.event = nullptr;
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  InitializeCriticalSection(&g_quickbar_weapon_detail_lock);
  g_quickbar_weapon_detail_lock_ready = true;
  ZeroMemory(g_quickbar_weapon_detail_cache, sizeof(g_quickbar_weapon_detail_cache));
  InterlockedExchange(&g_state.quickbar_weapon_detail_slot_mask_low, 0);
  InterlockedExchange(&g_state.quickbar_weapon_detail_slot_mask_high, 0);
  InterlockedExchange(&g_state.quickbar_weapon_detail_pending_count, 0);
  InterlockedExchange(&g_state.quickbar_weapon_detail_cache_hits, 0);

  InterlockedExchange(&g_state.log_level, kLogInfo);
  g_state.pipe_thread = CreateThread(nullptr, 0, PipeThreadMain, nullptr, 0, nullptr);
  if (g_state.pipe_thread == nullptr) {
    g_quickbar_weapon_detail_lock_ready = false;
    DeleteCriticalSection(&g_quickbar_weapon_detail_lock);
    CloseHandle(g_state.pipe_ready_event);
    g_state.pipe_ready_event = nullptr;
    CloseHandle(g_state.pending_map_pin.event);
    g_state.pending_map_pin.event = nullptr;
    CloseHandle(g_state.pending_combat_mode.event);
    g_state.pending_combat_mode.event = nullptr;
    CloseHandle(g_state.pending_walk_bypass.event);
    g_state.pending_walk_bypass.event = nullptr;
    CloseHandle(g_state.pending_move.event);
    g_state.pending_move.event = nullptr;
    CloseHandle(g_state.pending_identity.event);
    g_state.pending_identity.event = nullptr;
    CloseHandle(g_state.pending_chat.event);
    g_state.pending_chat.event = nullptr;
    CloseHandle(g_state.pending.event);
    g_state.pending.event = nullptr;
    if (g_state.log_file != nullptr) {
      CloseHandle(g_state.log_file);
      g_state.log_file = nullptr;
    }
    g_state.overlay_lock_ready = false;
    DeleteCriticalSection(&g_state.overlay_lock);
    g_state.log_lock_ready = false;
    DeleteCriticalSection(&g_state.log_lock);
    g_state.chat_lock_ready = false;
    DeleteCriticalSection(&g_state.chat_lock);
    g_state.lock_ready = false;
    DeleteCriticalSection(&g_state.lock);
    g_state.initialized = 0;
    return 0;
  }

  const DWORD pipe_wait = WaitForSingleObject(g_state.pipe_ready_event, kPipeStartupTimeoutMs);
  const LONG pipe_state = InterlockedCompareExchange(&g_state.pipe_state, 0, 0);
  const DWORD pipe_error = static_cast<DWORD>(InterlockedCompareExchange(&g_state.pipe_thread_error, 0, 0));
  if (pipe_wait != WAIT_OBJECT_0 || pipe_state != 1) {
    const DWORD failure = pipe_error != ERROR_IO_PENDING && pipe_error != ERROR_SUCCESS
        ? pipe_error
        : (pipe_wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_PIPE_NOT_CONNECTED);
    UpdateLastOperation(0, 0, failure);
    LogMessage(kLogError, "pipe startup failed wait=%lu pipeState=%ld pipeErr=%lu", pipe_wait, pipe_state, pipe_error);
    return failure;
  }

  EnsureHookInstalled();
  InstallQuickbarTraceHook();
  InstallQuickbarSlotTraceHook();
  InstallChatWindowLogHook();
  if (!InstallOverlayHook()) {
    LogMessage(kLogError, "overlay hook install failed gle=%lu", GetLastError());
  }
  DiscoverQuickbarPanelByScan("init");
  LogMessage(kLogInfo, "InitSimKeys complete");
  return 1;
}
