/* DRM / framebuffer ioctl handlers */
/* framebuffer_t defined in kernel.c */
typedef struct {
    uint8_t *address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos, red_size;
    uint8_t  green_pos, green_size;
    uint8_t  blue_pos, blue_size;
    bool     ready;
} compat4_fb_t;
extern compat4_fb_t g_fb;
extern void drm_queue_page_flip_event(int fd,uint64_t user_data,uint32_t crtc_id);
extern bool drm_page_flip_inflight(int fd);
extern bool drm_fd_is_render_node(int fd);
extern bool kvfs_exists(const char *path);
extern bool ridux_fb_ensure_current_mapping(void);

static bool c4_drm_scanout_probe_enabled(void);

#define C4_DRM_ID_FB 1u
#define C4_DRM_ID_CRTC 42u
#define C4_DRM_ID_CONNECTOR 43u
#define C4_DRM_ID_ENCODER 44u
#define C4_DRM_ID_PLANE 45u
#define C4_DRM_ID_PLANE_CURSOR 46u
#define C4_DRM_ID_PLANE_OVERLAY 47u
#define C4_DRM_ID_MODE_BLOB_BASE 900u
#define C4_DRM_ID_BLOB_EDID       901u
#define C4_DRM_ID_BLOB_IN_FORMATS 902u
#define C4_DRM_PROP_PLANE_TYPE 101u
#define C4_DRM_PROP_CRTC_ID 102u
#define C4_DRM_PROP_FB_ID 103u
#define C4_DRM_PROP_CRTC_X 104u
#define C4_DRM_PROP_CRTC_Y 105u
#define C4_DRM_PROP_CRTC_W 106u
#define C4_DRM_PROP_CRTC_H 107u
#define C4_DRM_PROP_SRC_X 108u
#define C4_DRM_PROP_SRC_Y 109u
#define C4_DRM_PROP_SRC_W 110u
#define C4_DRM_PROP_SRC_H 111u
#define C4_DRM_PROP_ACTIVE 112u
#define C4_DRM_PROP_MODE_ID 113u
#define C4_DRM_PROP_IN_FENCE_FD 114u
#define C4_DRM_PROP_OUT_FENCE_PTR 115u
#define C4_DRM_PROP_EDID         116u
#define C4_DRM_PROP_DPMS         117u
#define C4_DRM_PROP_LINK_STATUS  118u
#define C4_DRM_PROP_NON_DESKTOP  119u
#define C4_DRM_PROP_TILE         120u
#define C4_DRM_PROP_GAMMA_LUT      121u
#define C4_DRM_PROP_GAMMA_LUT_SIZE 122u
#define C4_DRM_PROP_DEGAMMA_LUT      123u
#define C4_DRM_PROP_DEGAMMA_LUT_SIZE 124u
#define C4_DRM_PROP_CTM              125u
#define C4_DRM_PROP_VRR_ENABLED      126u
#define C4_DRM_PROP_IN_FORMATS       127u
#define C4_DRM_PROP_FB_DAMAGE_CLIPS  128u
#define C4_DRM_PROP_HOTSPOT_X        129u
#define C4_DRM_PROP_HOTSPOT_Y        130u
#define C4_DRM_PROP_ZPOS             131u
#define C4_DRM_PROP_ROTATION         132u
#define C4_DRM_PROP_ALPHA            133u
/* HDR / color management / output extras (Linux KMS UAPI). */
#define C4_DRM_PROP_HDR_OUTPUT_METADATA 140u
#define C4_DRM_PROP_COLORSPACE          141u
#define C4_DRM_PROP_MAX_BPC             142u
#define C4_DRM_PROP_BROADCAST_RGB       143u
#define C4_DRM_PROP_PANEL_ORIENTATION   144u
#define C4_DRM_PROP_CONTENT_TYPE        145u
#define C4_DRM_PROP_SCALING_MODE        146u
#define C4_DRM_PROP_HDCP_CP             147u  /* "Content Protection" */
#define C4_DRM_PROP_HDCP_CP_TYPE        148u  /* "HDCP Content Type" */
#define C4_DRM_PROP_BACKGROUND_COLOR    149u
#define C4_DRM_PROP_ICC_PROFILE         150u
#define C4_DRM_PROP_PRIVACY_SCREEN_HW   151u
#define C4_DRM_PROP_PRIVACY_SCREEN_SW   152u
#define C4_DRM_PROP_DITHERING_MODE      153u
#define C4_DRM_PROP_OUTPUT_BPP          154u

/* Additional static blob IDs for the new connector/CRTC props. */
#define C4_DRM_ID_BLOB_HDR_METADATA     0xB1000003u
#define C4_DRM_ID_BLOB_ICC_PROFILE      0xB1000004u
#define C4_DRM_ID_BLOB_TILE             0xB1000005u
#define C4_DRM_ID_BLOB_PATH             0xB1000006u

/* Linux ABI struct hdr_output_metadata { u32 metadata_type; union {
 *   struct hdr_metadata_infoframe { u8 eotf, metadata_type; struct {
 *      u16 x,y; } display_primaries[3], white_point; u16 max_dl,min_dl,
 *      max_cll,max_fall; } hdmi_metadata_type1; }; }
 * We expose a single static metadata blob describing PQ EOTF + BT.2020
 * primaries so HDR-aware clients see a sane defaul. */
typedef struct __attribute__((packed)) {
    uint8_t  eotf;
    uint8_t  metadata_type;
    uint16_t prim_red_x;
    uint16_t prim_red_y;
    uint16_t prim_green_x;
    uint16_t prim_green_y;
    uint16_t prim_blue_x;
    uint16_t prim_blue_y;
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint16_t max_display_mastering_luminance;
    uint16_t min_display_mastering_luminance;
    uint16_t max_cll;
    uint16_t max_fall;
} c4_hdr_metadata_infoframe_t;

typedef struct __attribute__((packed)) {
    uint32_t metadata_type;
    c4_hdr_metadata_infoframe_t hdmi_metadata_type1;
} c4_hdr_output_metadata_t;
#define C4_DRM_PROP_PIXEL_BLEND_MODE 134u
#define C4_DRM_PROP_COLOR_ENCODING   135u
#define C4_DRM_PROP_COLOR_RANGE      136u
#define C4_DRM_PROP_VRR_CAPABLE      137u
#define C4_DRM_PROP_PATH             138u
#define C4_DRM_PROP_SUBCONNECTOR     139u
#define C4_DRM_PLANE_TYPE_OVERLAY 0u
#define C4_DRM_PLANE_TYPE_CURSOR  2u
#define C4_DRM_DPMS_ON      0u
#define C4_DRM_DPMS_STANDBY 1u
#define C4_DRM_DPMS_SUSPEND 2u
#define C4_DRM_DPMS_OFF     3u
#define C4_DRM_LINK_STATUS_GOOD 0u
#define C4_DRM_LINK_STATUS_BAD  1u
#define C4_DRM_FORMAT_MOD_LINEAR  0ULL
#define C4_DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
/* DRM_VBLANK_RELATIVE/ABSOLUTE/EVENT bits used by drm_wait_vblank */
#define C4_DRM_VBLANK_ABSOLUTE  0x00000000u
#define C4_DRM_VBLANK_RELATIVE  0x00000001u
#define C4_DRM_VBLANK_EVENT     0x4000000u
#define C4_DRM_VBLANK_HIGH_CRTC_MASK 0x3eu
#define C4_DRM_EVENT_VBLANK         0x01u
#define C4_DRM_EVENT_FLIP_COMPLETE  0x02u
#define C4_DRM_GAMMA_LUT_SIZE 256u

#define C4_DRM_MODE_OBJECT_ANY 0u
#define C4_DRM_MODE_OBJECT_CRTC 0xccccccccu
#define C4_DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0u
#define C4_DRM_MODE_OBJECT_ENCODER 0xe0e0e0e0u
#define C4_DRM_MODE_OBJECT_MODE 0xdedededeu
#define C4_DRM_MODE_OBJECT_PROPERTY 0xb0b0b0b0u
#define C4_DRM_MODE_OBJECT_FB 0xfbfbfbfbu
#define C4_DRM_MODE_OBJECT_BLOB 0xbbbbbbbbu
#define C4_DRM_MODE_OBJECT_PLANE 0xeeeeeeeeu
#define C4_DRM_MODE_PROP_RANGE 0x00000002u
#define C4_DRM_MODE_PROP_ENUM 0x00000008u
#define C4_DRM_MODE_PROP_BLOB 0x00000010u
#define C4_DRM_MODE_PROP_OBJECT 0x00000020u
#define C4_DRM_PLANE_TYPE_PRIMARY 1u
#define C4_DRM_FORMAT_XRGB8888 0x34325258u
#define C4_DRM_FORMAT_ARGB8888 0x34325241u
#define C4_DRM_FORMAT_XBGR8888 0x34324258u
#define C4_DRM_FORMAT_ABGR8888 0x34324241u
#define C4_DRM_FORMAT_RGB565   0x36314752u
#define C4_DRM_FORMAT_BGR565   0x36314742u
#define C4_DRM_FORMAT_RGB888   0x34324752u
#define C4_DRM_FORMAT_BGR888   0x34324742u
#define C4_DRM_FORMAT_XRGB2101010 0x30335258u
#define C4_DRM_FORMAT_ARGB2101010 0x30335241u
#define C4_DRM_FORMAT_XBGR2101010 0x30334258u
#define C4_DRM_FORMAT_ABGR2101010 0x30334241u
#define C4_DRM_FORMAT_NV12        0x3231564Eu
#define C4_DRM_FORMAT_NV21        0x3132564Eu
#define C4_DRM_FORMAT_YUV420      0x32315559u
#define C4_DRM_FORMAT_YVU420      0x32315659u
#define C4_DRM_FORMAT_XBGR16161616F 0x48344258u
#define C4_DRM_FORMAT_ABGR16161616F 0x48344241u
#define C4_DRM_CAP_DUMB_BUFFER 0x1ULL
#define C4_DRM_CAP_PRIME 0x5ULL
#define C4_DRM_CAP_TIMESTAMP_MONOTONIC 0x6ULL
#define C4_DRM_CAP_CURSOR_WIDTH 0x8ULL
#define C4_DRM_CAP_CURSOR_HEIGHT 0x9ULL
#define C4_DRM_CAP_ADDFB2_MODIFIERS 0x10ULL
#define C4_DRM_CAP_CRTC_IN_VBLANK_EVENT 0x12ULL
#define C4_DRM_CAP_SYNCOBJ 0x13ULL
#define C4_DRM_CAP_SYNCOBJ_TIMELINE 0x14ULL
#define C4_DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP 0x15ULL
#define C4_DRM_PRIME_CAP_IMPORT 0x1ULL
#define C4_DRM_PRIME_CAP_EXPORT 0x2ULL
#define C4_DRM_MODE_PAGE_FLIP_ASYNC 0x02u
#define C4_DRM_MODE_ATOMIC_TEST_ONLY 0x0100u
#define C4_DRM_MODE_ATOMIC_NONBLOCK 0x0200u
#define C4_DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400u
#define C4_DRM_CLIENT_CAP_STEREO_3D 1ULL
#define C4_DRM_CLIENT_CAP_UNIVERSAL_PLANES 2ULL
#define C4_DRM_CLIENT_CAP_ATOMIC 3ULL
#define C4_DRM_CLIENT_CAP_ASPECT_RATIO 4ULL
#define C4_DRM_CLIENT_CAP_WRITEBACK_CONNECTORS 5ULL
#define C4_DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT 6ULL
#define C4_DRM_MAX_BO 64
#define C4_DRM_MAX_PRIME 128
#define C4_DRM_MAX_SYNCOBJ 128
#define C4_DRM_MAX_PROP_BLOB 64
#define C4_DRM_MAX_FILES 64
#define C4_DRM_MAX_FILE_HANDLES 256
#define C4_DRM_MAX_BO_BYTES (64ULL*1024ULL*1024ULL)
#define C4_DRM_MMAP_BASE 0x70000000ULL
#define C4_DRM_MMAP_STRIDE 0x08000000ULL
#define C4_PAGE_WRITETHROUGH 0x008ULL
#define C4_PAGE_CACHE_DISABLE 0x010ULL
#define C4_DRM_MODE_PAGE_FLIP_EVENT 0x01u
#define C4_DRM_COMMAND_BASE 0x40u
#define C4_DRM_VIRTGPU_MAP (C4_DRM_COMMAND_BASE+0x01u)
#define C4_DRM_VIRTGPU_EXECBUFFER (C4_DRM_COMMAND_BASE+0x02u)
#define C4_DRM_VIRTGPU_GETPARAM (C4_DRM_COMMAND_BASE+0x03u)
#define C4_DRM_VIRTGPU_RESOURCE_CREATE (C4_DRM_COMMAND_BASE+0x04u)
#define C4_DRM_VIRTGPU_RESOURCE_INFO (C4_DRM_COMMAND_BASE+0x05u)
#define C4_DRM_VIRTGPU_TRANSFER_FROM_HOST (C4_DRM_COMMAND_BASE+0x06u)
#define C4_DRM_VIRTGPU_TRANSFER_TO_HOST (C4_DRM_COMMAND_BASE+0x07u)
#define C4_DRM_VIRTGPU_WAIT (C4_DRM_COMMAND_BASE+0x08u)
#define C4_DRM_VIRTGPU_GET_CAPS (C4_DRM_COMMAND_BASE+0x09u)
#define C4_DRM_VIRTGPU_RESOURCE_CREATE_BLOB (C4_DRM_COMMAND_BASE+0x0au)
#define C4_DRM_VIRTGPU_CONTEXT_INIT (C4_DRM_COMMAND_BASE+0x0bu)
#define C4_VIRTGPU_EXECBUF_FENCE_FD_IN  0x01u
#define C4_VIRTGPU_EXECBUF_FENCE_FD_OUT 0x02u
#define C4_DRM_VMW_GET_PARAM             (C4_DRM_COMMAND_BASE+0u)
#define C4_DRM_VMW_ALLOC_BO              (C4_DRM_COMMAND_BASE+1u)
#define C4_DRM_VMW_HANDLE_CLOSE          (C4_DRM_COMMAND_BASE+2u)
#define C4_DRM_VMW_CURSOR_BYPASS         (C4_DRM_COMMAND_BASE+3u)
#define C4_DRM_VMW_CONTROL_STREAM        (C4_DRM_COMMAND_BASE+4u)
#define C4_DRM_VMW_CLAIM_STREAM          (C4_DRM_COMMAND_BASE+5u)
#define C4_DRM_VMW_UNREF_STREAM          (C4_DRM_COMMAND_BASE+6u)
#define C4_DRM_VMW_CREATE_CONTEXT        (C4_DRM_COMMAND_BASE+7u)
#define C4_DRM_VMW_UNREF_CONTEXT         (C4_DRM_COMMAND_BASE+8u)
#define C4_DRM_VMW_CREATE_SURFACE        (C4_DRM_COMMAND_BASE+9u)
#define C4_DRM_VMW_UNREF_SURFACE         (C4_DRM_COMMAND_BASE+10u)
#define C4_DRM_VMW_REF_SURFACE           (C4_DRM_COMMAND_BASE+11u)
#define C4_DRM_VMW_EXECBUF               (C4_DRM_COMMAND_BASE+12u)
#define C4_DRM_VMW_GET_3D_CAP            (C4_DRM_COMMAND_BASE+13u)
#define C4_DRM_VMW_FENCE_WAIT            (C4_DRM_COMMAND_BASE+14u)
#define C4_DRM_VMW_FENCE_SIGNALED        (C4_DRM_COMMAND_BASE+15u)
#define C4_DRM_VMW_FENCE_UNREF           (C4_DRM_COMMAND_BASE+16u)
#define C4_DRM_VMW_FENCE_EVENT           (C4_DRM_COMMAND_BASE+17u)
#define C4_DRM_VMW_PRESENT               (C4_DRM_COMMAND_BASE+18u)
#define C4_DRM_VMW_PRESENT_READBACK      (C4_DRM_COMMAND_BASE+19u)
#define C4_DRM_VMW_UPDATE_LAYOUT         (C4_DRM_COMMAND_BASE+20u)
#define C4_DRM_VMW_CREATE_SHADER         (C4_DRM_COMMAND_BASE+21u)
#define C4_DRM_VMW_UNREF_SHADER          (C4_DRM_COMMAND_BASE+22u)
#define C4_DRM_VMW_GB_SURFACE_CREATE     (C4_DRM_COMMAND_BASE+23u)
#define C4_DRM_VMW_GB_SURFACE_REF        (C4_DRM_COMMAND_BASE+24u)
#define C4_DRM_VMW_SYNCCPU               (C4_DRM_COMMAND_BASE+25u)
#define C4_DRM_VMW_CREATE_EXTENDED_CONTEXT (C4_DRM_COMMAND_BASE+26u)
#define C4_DRM_VMW_GB_SURFACE_CREATE_EXT (C4_DRM_COMMAND_BASE+27u)
#define C4_DRM_VMW_GB_SURFACE_REF_EXT    (C4_DRM_COMMAND_BASE+28u)
#define C4_DRM_VMW_MSG                   (C4_DRM_COMMAND_BASE+29u)
#define C4_DRM_VMW_MKSSTAT_RESET         (C4_DRM_COMMAND_BASE+30u)
#define C4_DRM_VMW_MKSSTAT_ADD           (C4_DRM_COMMAND_BASE+31u)
#define C4_DRM_VMW_MKSSTAT_REMOVE        (C4_DRM_COMMAND_BASE+32u)
#define C4_VMW_HANDLE_LEGACY 0u
#define C4_VMW_HANDLE_PRIME  1u
#define C4_VMW_MAX_SURFACES  128
#define C4_VMW_PARAM_NUM_STREAMS      0u
#define C4_VMW_PARAM_NUM_FREE_STREAMS 1u
#define C4_VMW_PARAM_3D               2u
#define C4_VMW_PARAM_HW_CAPS          3u
#define C4_VMW_PARAM_FIFO_CAPS        4u
#define C4_VMW_PARAM_MAX_FB_SIZE      5u
#define C4_VMW_PARAM_FIFO_HW_VERSION  6u
#define C4_VMW_PARAM_MAX_SURF_MEMORY  7u
#define C4_VMW_PARAM_3D_CAPS_SIZE     8u
#define C4_VMW_PARAM_MAX_MOB_MEMORY   9u
#define C4_VMW_PARAM_MAX_MOB_SIZE     10u
#define C4_VMW_PARAM_SCREEN_TARGET    11u
#define C4_VMW_PARAM_DX               12u
#define C4_VMW_PARAM_HW_CAPS2         13u
#define C4_VMW_PARAM_SM4_1            14u
#define C4_VMW_PARAM_SM5              15u
#define C4_VMW_PARAM_GL43             16u
#define C4_VMW_PARAM_DEVICE_ID        17u
#define C4_SVGA3D_DEVCAP_3D                         0u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURES              2u
#define C4_SVGA3D_DEVCAP_VERTEX_SHADER_VERSION     4u
#define C4_SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION   6u
#define C4_SVGA3D_DEVCAP_MAX_RENDER_TARGETS        8u
#define C4_SVGA3D_DEVCAP_MAX_POINT_SIZE            17u
#define C4_SVGA3D_DEVCAP_MAX_SHADER_TEXTURES       18u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH         19u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT        20u
#define C4_SVGA3D_DEVCAP_MAX_VOLUME_EXTENT         21u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT        22u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO  23u
#define C4_SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY    24u
#define C4_SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT       25u
#define C4_SVGA3D_DEVCAP_MAX_VERTEX_INDEX          26u
#define C4_SVGA3D_DEVCAP_MAX_VS_INSTR              27u
#define C4_SVGA3D_DEVCAP_MAX_FS_INSTR              28u
#define C4_SVGA3D_DEVCAP_MAX_VS_TEMPS              29u
#define C4_SVGA3D_DEVCAP_MAX_FS_TEMPS              30u
#define C4_SVGA3D_DEVCAP_MAX_CONTEXT_IDS           77u
#define C4_SVGA3D_DEVCAP_MAX_SURFACE_IDS           78u
#define C4_SVGA3D_DEVCAP_LINE_AA                   87u
#define C4_SVGA3D_DEVCAP_LINE_STIPPLE              88u
#define C4_SVGA3D_DEVCAP_MAX_LINE_WIDTH            89u
#define C4_SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH         90u
#define C4_SVGA3D_DEVCAP_DXCONTEXT                 95u
#define C4_SVGA3D_DEVCAP_DX_MAX_VERTEXBUFFERS      97u
#define C4_SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS   98u
#define C4_SVGA3D_DEVCAP_DX_PROVOKING_VERTEX       99u
#define C4_SVGA3D_DEVCAP_DXFMT_FIRST              100u
#define C4_SVGA3D_DEVCAP_DXFMT_LAST               243u
#define C4_SVGA3D_DEVCAP_SM41                     244u
#define C4_SVGA3D_DEVCAP_MULTISAMPLE_2X           245u
#define C4_SVGA3D_DEVCAP_MULTISAMPLE_4X           246u
#define C4_SVGA3D_DEVCAP_LOGIC_BLENDOPS           249u
#define C4_SVGA3D_DEVCAP_SM5                      258u
#define C4_SVGA3D_DEVCAP_MULTISAMPLE_8X           259u
#define C4_SVGA3D_DEVCAP_MAX_FORCED_SAMPLE_COUNT  260u
#define C4_SVGA3D_DEVCAP_GL43                     261u
#define C4_SVGA3D_DEVCAP_MAX                      262u
#define C4_SVGA3D_DXFMT_SUPPORTED          (1u<<0)
#define C4_SVGA3D_DXFMT_SHADER_SAMPLE      (1u<<1)
#define C4_SVGA3D_DXFMT_COLOR_RENDERTARGET (1u<<2)
#define C4_SVGA3D_DXFMT_DEPTH_RENDERTARGET (1u<<3)
#define C4_SVGA3D_DXFMT_BLENDABLE          (1u<<4)
#define C4_SVGA3D_DXFMT_MIPS               (1u<<5)
#define C4_SVGA3D_DXFMT_ARRAY              (1u<<6)
#define C4_SVGA3D_DXFMT_VOLUME             (1u<<7)
#define C4_SVGA3D_DXFMT_DX_VERTEX_BUFFER   (1u<<8)
#define C4_SVGA3D_DXFMT_MULTISAMPLE        (1u<<9)
#define C4_SVGA_ID_2                       0x90000002u
#define C4_SVGA_ID_1                       0x90000001u
#define C4_SVGA_ID_INVALID                 0xFFFFFFFFu
#define C4_SVGA_REG_ID                     0u
#define C4_SVGA_REG_ENABLE                 1u
#define C4_SVGA_REG_WIDTH                  2u
#define C4_SVGA_REG_HEIGHT                 3u
#define C4_SVGA_REG_BITS_PER_PIXEL         7u
#define C4_SVGA_REG_BYTES_PER_LINE         12u
#define C4_SVGA_REG_FB_START               13u
#define C4_SVGA_REG_FB_OFFSET              14u
#define C4_SVGA_REG_VRAM_SIZE              15u
#define C4_SVGA_REG_FB_SIZE                16u
#define C4_SVGA_REG_CAPABILITIES           17u
#define C4_SVGA_REG_MEM_START              18u
#define C4_SVGA_REG_MEM_SIZE               19u
#define C4_SVGA_REG_CONFIG_DONE            20u
#define C4_SVGA_REG_SYNC                   21u
#define C4_SVGA_REG_BUSY                   22u
#define C4_SVGA_REG_GUEST_ID               23u
#define C4_SVGA_REG_NUM_GUEST_DISPLAYS     34u
#define C4_SVGA_REG_DISPLAY_ID             35u
#define C4_SVGA_REG_DISPLAY_IS_PRIMARY     36u
#define C4_SVGA_REG_DISPLAY_POSITION_X     37u
#define C4_SVGA_REG_DISPLAY_POSITION_Y     38u
#define C4_SVGA_REG_DISPLAY_WIDTH          39u
#define C4_SVGA_REG_DISPLAY_HEIGHT         40u
#define C4_SVGA_REG_COMMAND_LOW            48u
#define C4_SVGA_REG_COMMAND_HIGH           49u
#define C4_SVGA_REG_BLANK_SCREEN_TARGETS   58u
#define C4_SVGA_REG_FIFO_CAPS              68u
#define C4_SVGA_REG_FENCE                  69u
#define C4_SVGA_FIFO_MIN                   0u
#define C4_SVGA_FIFO_MAX                   1u
#define C4_SVGA_FIFO_NEXT_CMD              2u
#define C4_SVGA_FIFO_STOP                  3u
#define C4_SVGA_FIFO_CAPABILITIES          4u
#define C4_SVGA_FIFO_NUM_REGS              497u
#define C4_SVGA_FIFO_MIN_BYTES             4096u
#define C4_SVGA_CAP_3D                     0x00004000u
#define C4_SVGA_CAP_EXTENDED_FIFO          0x00008000u
#define C4_SVGA_CAP_COMMAND_BUFFERS        0x01000000u
#define C4_SVGA_CAP_CMD_BUFFERS_2          0x04000000u
#define C4_SVGA_3D_CMD_SURFACE_COPY        1042u
#define C4_SVGA_VENDOR_VBOX                0x80EEu
#define C4_SVGA_DEVICE_VBOX                0xBEEFu
#define C4_SVGA_VENDOR_VMWARE              0x15ADu
#define C4_SVGA_DEVICE_VMWARE_SVGA2        0x0405u
#define C4_SVGA_GMR_NULL                   0xFFFFFFFFu
#define C4_SVGA_GMR_FRAMEBUFFER            0xFFFFFFFEu
#define C4_SVGA_CMD_UPDATE                 1u
#define C4_SVGA_CMD_FENCE                  30u
#define C4_SVGA_CMD_DEFINE_SCREEN          34u
#define C4_SVGA_CMD_DEFINE_GMRFB           36u
#define C4_SVGA_CMD_BLIT_GMRFB_TO_SCREEN   37u
#define C4_SVGA_CMD_NOP                    45u
#define C4_SVGA_DC_CMD_START_STOP_CONTEXT  1u
#define C4_SVGA_CB_STATUS_NONE             0u
#define C4_SVGA_CB_STATUS_COMPLETED        1u
#define C4_SVGA_CB_STATUS_QUEUE_FULL       2u
#define C4_SVGA_CB_STATUS_COMMAND_ERROR    3u
#define C4_SVGA_CB_STATUS_CB_HEADER_ERROR  4u
#define C4_SVGA_CB_STATUS_PREEMPTED        5u
#define C4_SVGA_CB_STATUS_SUBMISSION_ERROR 6u
#define C4_SVGA_CB_FLAG_DX_CONTEXT         (1u<<1)
#define C4_SVGA_CB_CONTEXT_0               0u
#define C4_SVGA_CB_CONTEXT_DEVICE          0x3Fu
#define C4_SVGA_CB_CONTEXT_MASK            0x3Fu
#define C4_SVGA_CB_MAX_COMMAND_BYTES       (4u*1024u*1024u)
#define C4_SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN 1069u
#define C4_SVGA_3D_CMD_SET_OTABLE_BASE64   1115u
#define C4_SVGA_3D_CMD_DEFINE_GB_SURFACE   1097u
#define C4_SVGA_3D_CMD_DEFINE_GB_SURFACE_V4 1267u
#define C4_SVGA_3D_CMD_BIND_GB_SURFACE     1099u
#define C4_SVGA_3D_CMD_UPDATE_GB_SURFACE   1102u
#define C4_SVGA_3D_CMD_READBACK_GB_SURFACE 1104u
#define C4_SVGA_3D_CMD_INVALIDATE_GB_SURFACE 1106u
#define C4_SVGA_3D_CMD_DEFINE_GB_CONTEXT   1107u
#define C4_SVGA_3D_CMD_BIND_GB_CONTEXT     1109u
#define C4_SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH 1132u
#define C4_SVGA_3D_CMD_GB_MOB_FENCE        1133u
#define C4_SVGA_3D_CMD_DEFINE_GB_MOB64     1135u
#define C4_SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING 1096u
#define C4_SVGA_3D_CMD_DX_DEFINE_CONTEXT   1143u
#define C4_SVGA_3D_CMD_DX_BIND_CONTEXT     1145u
#define C4_SVGA_3D_CMD_DEFINE_GB_SCREENTARGET 1124u
#define C4_SVGA_3D_CMD_BIND_GB_SCREENTARGET   1126u
#define C4_SVGA_3D_CMD_UPDATE_GB_SCREENTARGET 1127u
#define C4_SVGA_3D_CMD_UPDATE_GB_SCREENTARGET_V2 1266u
#define C4_SVGA3D_INVALID_ID              0xffffffffu
#define C4_SVGA3D_MOBFMT_PT64_0            4u
#define C4_SVGA3D_MOBFMT_PT64_1            5u
#define C4_SVGA3D_MOBFMT_PT64_2            6u
#define C4_SVGA_STFLAG_PRIMARY             1u
#define C4_SVGA_SCREEN_HAS_ROOT            (1u<<0)
#define C4_SVGA_SCREEN_IS_PRIMARY          (1u<<1)
#define C4_SVGA_OTABLE_MOB                 0u
#define C4_SVGA_OTABLE_SURFACE             1u
#define C4_SVGA_OTABLE_CONTEXT             2u
#define C4_SVGA_OTABLE_SHADER              3u
#define C4_SVGA_OTABLE_SCREENTARGET        4u
#define C4_SVGA_OTABLE_DXCONTEXT           5u
#define C4_SVGA_OTABLE_COUNT               6u
#define C4_SVGA_OTABLE_MOB_ENTRIES         4096u
#define C4_SVGA_OTABLE_SURFACE_ENTRIES     2048u
#define C4_SVGA_OTABLE_CONTEXT_ENTRIES     1024u
#define C4_SVGA_OTABLE_SHADER_ENTRIES      4096u
#define C4_SVGA_OTABLE_SCREENTARGET_ENTRIES 16u
#define C4_SVGA_CONTEXT_MAX                64u
#define C4_SVGA_CONTEXT_MOB_BASE           256u
#define C4_SVGA_CONTEXT_MOB_SIZE           (64u*1024u)
#define C4_SVGA_COTABLE_RTVIEW             0u
#define C4_SVGA_COTABLE_DSVIEW             1u
#define C4_SVGA_COTABLE_SRVIEW             2u
#define C4_SVGA_COTABLE_ELEMENTLAYOUT      3u
#define C4_SVGA_COTABLE_BLENDSTATE         4u
#define C4_SVGA_COTABLE_DEPTHSTENCIL       5u
#define C4_SVGA_COTABLE_RASTERIZERSTATE    6u
#define C4_SVGA_COTABLE_SAMPLER            7u
#define C4_SVGA_COTABLE_STREAMOUTPUT       8u
#define C4_SVGA_COTABLE_DXQUERY            9u
#define C4_SVGA_COTABLE_DXSHADER           10u
#define C4_SVGA_COTABLE_UAVIEW             11u
#define C4_SVGA_COTABLE_COUNT              12u
#define C4_SVGA_COTABLE_MOB_BASE           1024u
#define C4_SVGA_COTABLE_BYTES              (256u*1024u)
#define C4_SVGA_SYNC_MOB_ID                4095u
#define C4_SVGA3D_SURFACE_MOB_PITCH        (1u<<12)
#define C4_SVGA3D_SURFACE_SCREENTARGET     (1u<<16)
#define C4_SVGA3D_SURFACE_BIND_SHADER_RESOURCE (1u<<23)
#define C4_SVGA3D_SURFACE_BIND_RENDER_TARGET   (1u<<24)
#define C4_SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER 1148u
#define C4_SVGA_3D_CMD_DX_SET_SHADER_RESOURCES       1149u
#define C4_SVGA_3D_CMD_DX_SET_SHADER                 1150u
#define C4_SVGA_3D_CMD_DX_SET_SAMPLERS               1151u
#define C4_SVGA_3D_CMD_DX_DRAW                     1152u
#define C4_SVGA_3D_CMD_DX_DRAW_INDEXED             1153u
#define C4_SVGA_3D_CMD_DX_SET_INPUT_LAYOUT          1157u
#define C4_SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS        1158u
#define C4_SVGA_3D_CMD_DX_SET_INDEX_BUFFER          1159u
#define C4_SVGA_3D_CMD_DX_SET_TOPOLOGY              1160u
#define C4_SVGA_3D_CMD_DX_SET_RENDERTARGETS        1161u
#define C4_SVGA_3D_CMD_DX_SET_BLEND_STATE           1162u
#define C4_SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE    1163u
#define C4_SVGA_3D_CMD_DX_SET_RASTERIZER_STATE      1164u
#define C4_SVGA_3D_CMD_DX_SET_VIEWPORTS             1174u
#define C4_SVGA_3D_CMD_DX_SET_SCISSORRECTS          1175u
#define C4_SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW  1176u
#define C4_SVGA_3D_CMD_DX_PRED_COPY_REGION         1178u
#define C4_SVGA_3D_CMD_DX_PRED_COPY                1179u
#define C4_SVGA_3D_CMD_DX_PRESENTBLT               1180u
#define C4_SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE       1182u
#define C4_SVGA_3D_CMD_DX_READBACK_SUBRESOURCE     1183u
#define C4_SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW 1185u
#define C4_SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW 1187u
#define C4_SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT     1191u
#define C4_SVGA_3D_CMD_DX_DEFINE_BLEND_STATE       1193u
#define C4_SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE 1195u
#define C4_SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE  1197u
#define C4_SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE     1199u
#define C4_SVGA_3D_CMD_DX_DEFINE_SHADER            1201u
#define C4_SVGA_3D_CMD_DX_BIND_SHADER              1203u
#define C4_SVGA_3D_CMD_DX_SET_COTABLE              1207u
#define C4_SVGA_3D_CMD_DX_BUFFER_COPY              1209u
#define C4_SVGA_3D_CMD_DX_MOB_FENCE_64             1216u
#define C4_VMW_MAX_VIEWS 512
#define C4_VIRTGPU_PARAM_3D_FEATURES 1ULL
#define C4_VIRTGPU_PARAM_CAPSET_QUERY_FIX 2ULL
#define C4_VIRTGPU_PARAM_RESOURCE_BLOB 3ULL
#define C4_VIRTGPU_PARAM_HOST_VISIBLE 4ULL
#define C4_VIRTGPU_PARAM_CROSS_DEVICE 5ULL
#define C4_VIRTGPU_PARAM_CONTEXT_INIT 6ULL
#define C4_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS 7ULL
#define C4_VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME 8ULL
#define C4_VIRTGPU_PARAM_CREATE_GUEST_HANDLE 9ULL
#define C4_VIRTGPU_CAPSET_VIRGL 1u
#define C4_VIRTGPU_CAPSET_VIRGL2 2u
#define C4_VIRTGPU_CAPSET_VENUS 4u
#define C4_VIRTGPU_CONTEXT_PARAM_CAPSET_ID 1ULL
#define C4_VIRTIO_GPU_VENDOR 0x1AF4u
#define C4_VIRTIO_GPU_DEVICE_MODERN 0x1050u
#define C4_VIRTIO_PCI_CAP_COMMON_CFG 1u
#define C4_VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define C4_VIRTIO_PCI_CAP_ISR_CFG 3u
#define C4_VIRTIO_PCI_CAP_DEVICE_CFG 4u
#define C4_VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8u
#define C4_VIRTIO_GPU_SHM_ID_HOST_VISIBLE 1u
#define C4_VIRTIO_STATUS_ACKNOWLEDGE 1u
#define C4_VIRTIO_STATUS_DRIVER 2u
#define C4_VIRTIO_STATUS_DRIVER_OK 4u
#define C4_VIRTIO_STATUS_FEATURES_OK 8u
#define C4_VIRTIO_STATUS_FAILED 128u
#define C4_VIRTIO_F_VERSION_1 32u
#define C4_VIRTIO_GPU_F_VIRGL 0u
#define C4_VIRTIO_GPU_F_RESOURCE_BLOB 3u
#define C4_VIRTIO_GPU_F_CONTEXT_INIT 4u
#define C4_VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100u
#define C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101u
#define C4_VIRTIO_GPU_CMD_SET_SCANOUT 0x0103u
#define C4_VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104u
#define C4_VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105u
#define C4_VIRTIO_GPU_CMD_GET_CAPSET 0x0109u
#define C4_VIRTIO_GPU_CMD_CTX_CREATE 0x0200u
#define C4_VIRTIO_GPU_CMD_CTX_DESTROY 0x0201u
#define C4_VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE 0x0202u
#define C4_VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE 0x0203u
#define C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0204u
#define C4_VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D 0x0205u
#define C4_VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D 0x0206u
#define C4_VIRTIO_GPU_CMD_SUBMIT_3D 0x0207u
#define C4_VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB 0x0208u
#define C4_VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB 0x0209u
#define C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB 0x010cu
#define C4_VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define C4_VIRTIO_GPU_RESP_OK_NODATA 0x1100u
#define C4_VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101u
#define C4_VIRTIO_GPU_RESP_OK_CAPSET 0x1103u
#define C4_VIRTIO_GPU_RESP_OK_MAP_INFO 0x1106u
#define C4_VIRTIO_GPU_FLAG_FENCE 1u
#define C4_VIRTQ_DESC_F_NEXT 1u
#define C4_VIRTQ_DESC_F_WRITE 2u
#define C4_VIRTGPU_QUEUE_SIZE 64u
#define C4_VIRTGPU_CAPSET_RESP_MAX 16384u

typedef struct {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
} c4_drm_mode_modeinfo_t;

typedef struct {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
} c4_drm_mode_card_res_t;

typedef struct {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    c4_drm_mode_modeinfo_t mode;
} c4_drm_mode_crtc_t;

typedef struct {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} c4_drm_mode_get_encoder_t;

typedef struct {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
} c4_drm_mode_get_connector_t;

typedef struct {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
} c4_drm_mode_fb_cmd_t;

typedef struct {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
} c4_drm_mode_create_dumb_t;

typedef struct {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
} c4_drm_mode_map_dumb_t;

typedef struct {
    uint64_t capability;
    uint64_t value;
} c4_drm_get_cap_t;

typedef struct {
    int32_t version_major;
    int32_t version_minor;
    int32_t version_patchlevel;
    uint64_t name_len;
    uint64_t name;
    uint64_t date_len;
    uint64_t date;
    uint64_t desc_len;
    uint64_t desc;
} c4_drm_version_t;

typedef struct {
    uint64_t capability;
    uint64_t value;
} c4_drm_set_client_cap_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
    int32_t  fd;
} c4_drm_prime_handle_t;

typedef struct {
    uint64_t offset;
    uint32_t handle;
    uint32_t pad;
} c4_virtgpu_map_t;

typedef struct {
    uint64_t param;
    uint64_t value;
} c4_virtgpu_getparam_t;

typedef struct {
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
    uint32_t bo_handle;
    uint32_t res_handle;
    uint32_t size;
    uint32_t stride;
} c4_virtgpu_resource_create_t;

typedef struct {
    uint32_t bo_handle;
    uint32_t res_handle;
    uint32_t size;
    uint32_t blob_mem;
} c4_virtgpu_resource_info_t;

typedef struct {
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint32_t bo_handle;
    uint32_t res_handle;
    uint64_t size;
    uint32_t pad;
    uint32_t cmd_size;
    uint64_t cmd;
    uint64_t blob_id;
} c4_virtgpu_resource_create_blob_t;

typedef struct {
    uint32_t cap_set_id;
    uint32_t cap_set_ver;
    uint64_t addr;
    uint32_t size;
    uint32_t pad;
} c4_virtgpu_get_caps_t;

typedef struct {
    uint64_t param;
    uint64_t value;
} c4_virtgpu_context_set_param_t;

typedef struct {
    uint32_t num_params;
    uint32_t pad;
    uint64_t ctx_set_params;
} c4_virtgpu_context_init_t;

typedef struct {
    uint32_t flags;
    uint32_t size;
    uint64_t command;
    uint64_t bo_handles;
    uint32_t num_bo_handles;
    int32_t  fence_fd;
    uint32_t ring_idx;
    uint32_t pad;
    uint64_t extra[3];
} c4_virtgpu_execbuffer_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
} c4_virtgpu_3d_box_t;

typedef struct {
    uint32_t bo_handle;
    c4_virtgpu_3d_box_t box;
    uint32_t level;
    uint32_t offset;
    uint32_t stride;
    uint32_t layer_stride;
} c4_virtgpu_3d_transfer_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
} c4_virtgpu_3d_wait_t;

#define C4_VIRTGPU_WAIT_NOWAIT 1u

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} c4_virtio_gpu_ctrl_hdr_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) c4_virtio_gpu_rect_t;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) c4_virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[C4_VIRTGPU_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed)) c4_virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) c4_virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    c4_virtq_used_elem_t ring[C4_VIRTGPU_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed)) c4_virtq_used_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t nlen;
    uint32_t context_init;
    char debug_name[64];
} __attribute__((packed)) c4_virtio_gpu_ctx_create_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_ctx_resource_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) c4_virtio_gpu_resource_create_2d_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    c4_virtio_gpu_rect_t rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) c4_virtio_gpu_set_scanout_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    c4_virtio_gpu_rect_t rect;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_resource_flush_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    c4_virtio_gpu_rect_t rect;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_transfer_to_host_2d_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t capset_id;
    uint32_t capset_version;
} __attribute__((packed)) c4_virtio_gpu_get_capset_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_resource_create_3d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_mem_entry_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) c4_virtio_gpu_resource_attach_backing_hdr_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    c4_virtio_gpu_mem_entry_t entry;
} __attribute__((packed)) c4_virtio_gpu_resource_attach_backing_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    c4_virtgpu_3d_box_t box;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
} __attribute__((packed)) c4_virtio_gpu_transfer_3d_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t size;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_cmd_submit_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint32_t nr_entries;
    uint64_t blob_id;
    uint64_t size;
} __attribute__((packed)) c4_virtio_gpu_resource_create_blob_hdr_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint32_t nr_entries;
    uint64_t blob_id;
    uint64_t size;
    c4_virtio_gpu_mem_entry_t entry;
} __attribute__((packed)) c4_virtio_gpu_resource_create_blob_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
    uint64_t offset;
} __attribute__((packed)) c4_virtio_gpu_resource_map_blob_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_resource_unmap_blob_t;

typedef struct {
    c4_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t map_info;
    uint32_t padding;
} __attribute__((packed)) c4_virtio_gpu_resp_map_info_t;

typedef struct {
    uint32_t handle;
    uint32_t pad;
} c4_drm_gem_close_t;

typedef struct {
    uint64_t plane_id_ptr;
    uint32_t count_planes;
    uint32_t pad;
} c4_drm_mode_get_plane_res_t;

typedef struct {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
    uint32_t count_format_types;
    uint64_t format_type_ptr;
} c4_drm_mode_get_plane_t;

typedef struct {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t prop_id;
    uint32_t flags;
    char     name[32];
    uint32_t count_values;
    uint32_t count_enum_blobs;
} c4_drm_mode_get_property_t;

typedef struct {
    uint64_t value;
    char     name[32];
} c4_drm_mode_property_enum_t;

typedef struct {
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_props;
    uint32_t obj_id;
    uint32_t obj_type;
    uint32_t pad;
} c4_drm_mode_obj_get_properties_t;

typedef struct {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
} c4_drm_mode_fb_cmd2_t;

typedef struct {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
} c4_drm_mode_page_flip_t;

typedef struct {
    uint32_t flags;
    uint32_t crtc_id;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t handle;
} c4_drm_mode_cursor_t;

typedef struct {
    uint32_t flags;
    uint32_t crtc_id;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t handle;
    int32_t  hot_x;
    int32_t  hot_y;
} c4_drm_mode_cursor2_t;

typedef struct {
    uint64_t value;
    uint32_t prop_id;
    uint32_t obj_id;
} c4_drm_mode_set_property_t;

typedef struct {
    uint64_t value;
    uint32_t prop_id;
    uint32_t obj_id;
    uint32_t obj_type;
    uint32_t pad;
} c4_drm_mode_obj_set_property_t;

typedef struct {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    int32_t  crtc_x;
    int32_t  crtc_y;
    uint32_t crtc_w;
    uint32_t crtc_h;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
} c4_drm_mode_set_plane_t;

typedef struct {
    uint64_t data;
    uint32_t length;
    uint32_t blob_id;
} c4_drm_mode_create_blob_t;

typedef struct {
    uint32_t blob_id;
} c4_drm_mode_destroy_blob_t;

typedef struct {
    uint32_t blob_id;
    uint32_t length;
    uint64_t data;
} c4_drm_mode_get_blob_t;

typedef struct {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
} c4_drm_mode_atomic_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
} c4_drm_syncobj_create_t;

typedef struct {
    uint32_t handle;
    uint32_t pad;
} c4_drm_syncobj_destroy_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
    int32_t  fd;
    uint32_t pad;
} c4_drm_syncobj_handle_t;

typedef struct {
    uint64_t handles;
    int64_t  timeout_nsec;
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
} c4_drm_syncobj_wait_t;

typedef struct {
    uint64_t handles;
    uint32_t count_handles;
    uint32_t pad;
} c4_drm_syncobj_array_t;

typedef struct {
    uint64_t handles;
    uint64_t points;
    int64_t  timeout_nsec;
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
} c4_drm_syncobj_timeline_wait_t;

typedef struct {
    uint64_t handles;
    uint64_t points;
    uint32_t count_handles;
    uint32_t flags;
} c4_drm_syncobj_timeline_array_t;

typedef struct {
    uint32_t src_handle;
    uint32_t dst_handle;
    uint64_t src_point;
    uint64_t dst_point;
    uint32_t flags;
    uint32_t pad;
} c4_drm_syncobj_transfer_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
    uint64_t point;
    int32_t  fd;
    uint32_t pad;
} c4_drm_syncobj_eventfd_t;

typedef struct {
    bool     used;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t pixel_format;
    uint64_t size;
    uint64_t alloc_size;
    uint64_t map_offset;
    uint8_t *data;
    bool pmm_backed;
    uint64_t phys;
    uint64_t mob_base_ppn;
    uint64_t mob_pt_phys;
    uint32_t mob_pt_pages;
    uint32_t mob_pt_depth;
    bool real_created;
    bool real_backed;
    bool real_ctx_attached;
    bool real_mmio_backed;
    uint32_t real_blob_mem;
    uint32_t real_ctx_id;
    uint64_t real_host_visible_offset;
    uint32_t real_map_info;
    uint32_t virtgpu_target;
    uint32_t virtgpu_format;
    uint32_t virtgpu_bind;
    uint32_t virtgpu_flags;
    uint32_t kernel_refcount;
    uint32_t handle_refcount;
    uint32_t fb_refcount;
    uint32_t mmap_refcount;
    uint32_t prime_refcount;
    uint32_t owner_file;
} c4_drm_bo_t;

typedef struct {
    bool     used;
    int32_t  fd;
    uint32_t handle;
} c4_drm_prime_t;

typedef struct {
    bool     used;
    uint32_t id;
    uint32_t length;
} c4_drm_prop_blob_t;

typedef struct {
    bool     used;
    uint32_t handle;
    bool     signaled;
    uint64_t point;
    int32_t  export_fd;
} c4_drm_syncobj_t;

typedef struct {
    bool     used;
    uint32_t id;
    int32_t  fd;
    int32_t  pid;
    int32_t  tgid;
    bool     render_node;
    bool     authenticated;
    bool     master;
    bool     lease;
    uint32_t magic;
    uint32_t lessee_id;
} c4_drm_file_t;

typedef struct {
    bool     used;
    uint32_t file_id;
    uint32_t handle;
    uint32_t refs;
} c4_drm_file_handle_t;

static int32_t g_c4_drm_prime_export_fd = 100;
static uint32_t g_c4_drm_next_handle = 2;
static uint32_t g_c4_drm_next_fb_id = 100;
static uint32_t g_c4_drm_next_blob_id = C4_DRM_ID_MODE_BLOB_BASE;
static uint32_t g_c4_drm_next_syncobj = 1;
static uint32_t g_c4_drm_next_magic = 0x52445840u;
static uint32_t g_c4_drm_current_fb_id = C4_DRM_ID_FB;
static uint32_t g_c4_drm_last_nonzero_handle;
static uint32_t g_c4_vmw_rescue_rt_handle;
static uint32_t g_c4_drm_cursor_handle;
static int32_t  g_c4_drm_cursor_x = -1;
static int32_t  g_c4_drm_cursor_y = -1;
static int32_t  g_c4_drm_cursor_hot_x;
static int32_t  g_c4_drm_cursor_hot_y;
static uint32_t g_c4_drm_cursor_w;
static uint32_t g_c4_drm_cursor_h;
static bool     g_c4_drm_cursor_visible;
static bool     g_c4_drm_cursor_swcomp;

static void c4_drm_ensure_software_cursor_visible(void){
    uint32_t w=(g_fb.width?g_fb.width:1024u);
    uint32_t h=(g_fb.height?g_fb.height:768u);
    if(g_c4_drm_cursor_x<0)g_c4_drm_cursor_x=(int32_t)(w/2u);
    if(g_c4_drm_cursor_y<0)g_c4_drm_cursor_y=(int32_t)(h/2u);
    g_c4_drm_cursor_handle=0;
    g_c4_drm_cursor_hot_x=0;
    g_c4_drm_cursor_hot_y=0;
    g_c4_drm_cursor_visible=true;
}

void ridux_drm_cursor_move(int32_t x,int32_t y){
    g_c4_drm_cursor_x=x;
    g_c4_drm_cursor_y=y;
    c4_drm_ensure_software_cursor_visible();
}

static c4_drm_bo_t g_c4_drm_bos[C4_DRM_MAX_BO];
static c4_drm_prime_t g_c4_drm_primes[C4_DRM_MAX_PRIME];
static c4_drm_prop_blob_t g_c4_drm_prop_blobs[C4_DRM_MAX_PROP_BLOB];
static c4_drm_syncobj_t g_c4_drm_syncobjs[C4_DRM_MAX_SYNCOBJ];
static c4_drm_file_t g_c4_drm_files[C4_DRM_MAX_FILES];
static c4_drm_file_handle_t g_c4_drm_file_handles[C4_DRM_MAX_FILE_HANDLES];
static uint32_t g_c4_drm_master_file;
static uint32_t g_c4_drm_trace_count;
static uint32_t g_c4_drm_mmap_trace_count;
static uint32_t g_c4_virtgpu_trace_count;
static uint32_t g_c4_vgpu_last_fb_resource;
static uint32_t g_c4_vgpu_last_render_resource;
static uint32_t g_c4_vgpu_last_visible_scanout_resource;
static uint32_t g_c4_vgpu_present_candidates[8];
static uint32_t g_c4_vgpu_present_candidate_next;
static c4_drm_bo_t *g_c4_native_present_bo;
static const char *g_c4_native_present_backend = "CPU dirty framebuffer blit";
static bool g_c4_native_present_real_active;
static uint32_t g_c4_native_present_failures;
static bool g_c4_native_present_disabled;
static uint32_t g_c4_svga_trace_count;
static struct {
    bool probed;
    bool present;
    bool queue_ready;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t bar[6];
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *device;
    uint32_t notify_multiplier;
    uint16_t queue_size;
    uint16_t notify_off;
    uint16_t last_used_idx;
    uint16_t avail_idx;
    uint32_t features0;
    uint32_t features1;
    uint32_t guest0;
    uint32_t guest1;
    uint32_t ctx_id;
    uint32_t active_context_init;
    bool ctx_virgl_created;
    bool ctx_virgl2_created;
    bool ctx_venus_created;
    bool queue_probe_done;
    bool queue_probe_ok;
    uint64_t fence_next;
    bool host_visible_probed;
    bool host_visible_ready;
    uint64_t host_visible_base;
    uint64_t host_visible_len;
    uint64_t host_visible_next;
} g_c4_vgpu_real;
static struct {
    bool probed;
    bool present;
    bool fifo_ready;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t io_base;
    uint32_t bar[6];
    volatile uint32_t *fifo;
    uint32_t fifo_bytes;
    volatile uint8_t *fb;
    uint32_t fb_start;
    uint32_t fb_size;
    uint32_t fb_pitch;
    uint32_t caps;
    uint32_t fifo_caps;
    uint32_t fence_next;
    bool screen_defined;
    bool screen_object_defined;
    uint32_t screen_width;
    uint32_t screen_height;
    bool cmd_context_started;
    uint64_t cmd_phys;
    uint8_t *cmd_virt;
    uint32_t cmd_bytes;
    uint64_t cmd_seq;
    bool sync_mob_ready;
    uint64_t sync_phys;
    volatile uint64_t *sync_virt;
    uint64_t sync_seq;
    uint32_t display_sid;
    uint32_t display_bo_handle;
    uint32_t display_width;
    uint32_t display_height;
} g_c4_svga_real;

typedef struct __attribute__((packed)) {
    volatile uint32_t status;
    volatile uint32_t error_offset;
    uint64_t id;
    uint32_t flags;
    uint32_t length;
    union {
        uint64_t pa;
        struct {
            uint32_t mobid;
            uint32_t mob_offset;
        } mob;
    } ptr;
    uint32_t offset;
    uint32_t dx_context;
    uint32_t must_be_zero[6];
} c4_svga_cb_header_t;

typedef struct {
    bool ready;
    uint32_t type;
    uint32_t entry_size;
    uint32_t entries;
    uint64_t phys;
    uint64_t bytes;
    uint64_t base_ppn;
    uint64_t pt_phys;
    uint32_t pt_pages;
    uint32_t pt_depth;
} c4_svga_otable_t;
typedef struct {
    bool ready;
    uint32_t type;
    uint32_t mobid;
    uint64_t phys;
    uint64_t bytes;
    uint64_t base_ppn;
    uint64_t pt_phys;
    uint32_t pt_pages;
    uint32_t pt_depth;
} c4_svga_cotable_t;
typedef struct {
    bool used;
    bool defined;
    bool dx_bound;
    bool cotables_ready;
    uint32_t cid;
    uint32_t mobid;
    uint64_t phys;
    uint64_t base_ppn;
    uint64_t pt_phys;
    uint32_t pt_pages;
    uint32_t pt_depth;
    c4_svga_cotable_t cotables[C4_SVGA_COTABLE_COUNT];
} c4_svga_context_mob_t;
static c4_svga_otable_t g_c4_svga_otables[C4_SVGA_OTABLE_COUNT];
static c4_svga_context_mob_t g_c4_svga_context_mobs[C4_SVGA_CONTEXT_MAX];
static bool g_c4_svga_otables_ready;
static c4_virtq_desc_t g_c4_vgpu_desc[C4_VIRTGPU_QUEUE_SIZE] __attribute__((aligned(4096)));
static c4_virtq_avail_t g_c4_vgpu_avail __attribute__((aligned(4096)));
static c4_virtq_used_t g_c4_vgpu_used __attribute__((aligned(4096)));
#define C4_VGPU_CMD_MAX_BYTES (1024u * 1024u)
static uint8_t g_c4_vgpu_cmd[C4_VGPU_CMD_MAX_BYTES] __attribute__((aligned(4096)));
static c4_virtio_gpu_ctrl_hdr_t g_c4_vgpu_resp __attribute__((aligned(16)));
static uint8_t g_c4_vgpu_resp_big[C4_VIRTGPU_CAPSET_RESP_MAX + sizeof(c4_virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));

#define C4_VGPU_SYNC_DESC_HEAD 0u
#define C4_VGPU_ASYNC_DESC_BASE 4u
#define C4_VGPU_ASYNC_DESC_STRIDE 2u
#define C4_VGPU_ASYNC_SLOT_COUNT 24u

typedef struct {
    bool used;
    uint16_t head;
    uint32_t bytes;
    uint8_t cmd[C4_VGPU_CMD_MAX_BYTES];
    c4_virtio_gpu_ctrl_hdr_t resp;
} c4_vgpu_async_cmd_t;

static c4_vgpu_async_cmd_t g_c4_vgpu_async_cmds[C4_VGPU_ASYNC_SLOT_COUNT] __attribute__((aligned(4096)));
static uint32_t g_c4_vgpu_async_next;

static uint64_t c4_align_page(uint64_t v){
    return (v + (uint64_t)PAGE_SIZE - 1ULL) & ~((uint64_t)PAGE_SIZE - 1ULL);
}

static bool c4_svga_alloc_physical(uint64_t bytes,uint64_t *phys_out,uint8_t **virt_out){
    uint32_t pages;
    uint64_t phys;
    uint8_t *virt;
    if(phys_out)*phys_out=0;
    if(virt_out)*virt_out=0;
    if(!bytes)return false;
    pages=(uint32_t)((bytes+(uint64_t)PAGE_SIZE-1ULL)/(uint64_t)PAGE_SIZE);
    if(!pages)return false;
    phys=pmm_alloc_contiguous_frames(pages);
    if(!phys)return false;
    virt=(uint8_t*)PHYS_TO_DMAP(phys);
    ulibc_memset(virt,0,(size_t)((uint64_t)pages*(uint64_t)PAGE_SIZE));
    if(phys_out)*phys_out=phys;
    if(virt_out)*virt_out=virt;
    return true;
}

static bool c4_svga_build_mob_map(uint64_t data_phys,uint64_t data_bytes,
                                  uint64_t *base_ppn,uint32_t *pt_depth,
                                  uint64_t *pt_phys,uint32_t *pt_pages){
    uint64_t data_pages,l1_pages,root_pages,total_pt_pages;
    uint64_t ptp=pt_phys?*pt_phys:0;
    uint64_t i;
    if(base_ppn)*base_ppn=0;
    if(pt_depth)*pt_depth=0;
    if(pt_pages)*pt_pages=0;
    if(!data_phys||!data_bytes||!base_ppn||!pt_depth)return false;
    data_pages=(data_bytes+(uint64_t)PAGE_SIZE-1ULL)/(uint64_t)PAGE_SIZE;
    if(!data_pages)return false;
    if(data_pages==1){
        *base_ppn=data_phys/(uint64_t)PAGE_SIZE;
        *pt_depth=C4_SVGA3D_MOBFMT_PT64_0;
        if(pt_pages)*pt_pages=0;
        __sync_synchronize();
        return true;
    }
    l1_pages=((data_pages*sizeof(uint64_t))+(uint64_t)PAGE_SIZE-1ULL)/(uint64_t)PAGE_SIZE;
    root_pages=(l1_pages>1)?(((l1_pages*sizeof(uint64_t))+(uint64_t)PAGE_SIZE-1ULL)/(uint64_t)PAGE_SIZE):0;
    if(root_pages>1)return false;
    total_pt_pages=l1_pages+(root_pages?root_pages:0);
    if(!ptp){
        if(!c4_svga_alloc_physical(total_pt_pages*(uint64_t)PAGE_SIZE,&ptp,0))return false;
        if(pt_phys)*pt_phys=ptp;
    }
    if(pt_pages)*pt_pages=(uint32_t)total_pt_pages;
    {
        uint64_t *l1=(uint64_t*)PHYS_TO_DMAP(ptp);
        ulibc_memset(l1,0,(size_t)(total_pt_pages*(uint64_t)PAGE_SIZE));
        for(i=0;i<data_pages;++i)l1[i]=(data_phys/(uint64_t)PAGE_SIZE)+i;
        if(root_pages){
            uint64_t root_phys=ptp+l1_pages*(uint64_t)PAGE_SIZE;
            uint64_t *root=(uint64_t*)PHYS_TO_DMAP(root_phys);
            for(i=0;i<l1_pages;++i)root[i]=(ptp/(uint64_t)PAGE_SIZE)+i;
            *base_ppn=root_phys/(uint64_t)PAGE_SIZE;
            *pt_depth=C4_SVGA3D_MOBFMT_PT64_2;
        }else{
            *base_ppn=ptp/(uint64_t)PAGE_SIZE;
            *pt_depth=C4_SVGA3D_MOBFMT_PT64_1;
        }
    }
    __sync_synchronize();
    return true;
}

static inline uint32_t c4_pci_rd32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off){
    __asm__ volatile("outl %0,%1"::"a"(0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)func<<8)|(off&0xFCu)),"Nd"((uint16_t)0xCF8));
    {uint32_t r;__asm__ volatile("inl %1,%0":"=a"(r):"Nd"((uint16_t)0xCFC));return r;}
}

static inline uint16_t c4_pci_rd16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off){
    uint32_t v=c4_pci_rd32(bus,slot,func,(uint8_t)(off&0xFCu));
    return (uint16_t)((v>>((off&2u)*8u))&0xFFFFu);
}

static inline uint8_t c4_pci_rd8(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off){
    uint32_t v=c4_pci_rd32(bus,slot,func,(uint8_t)(off&0xFCu));
    return (uint8_t)((v>>((off&3u)*8u))&0xFFu);
}

static inline void c4_pci_wr32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off,uint32_t val){
    __asm__ volatile("outl %0,%1"::"a"(0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)func<<8)|(off&0xFCu)),"Nd"((uint16_t)0xCF8));
    __asm__ volatile("outl %0,%1"::"a"(val),"Nd"((uint16_t)0xCFC));
}

static bool c4_map_mmio_page(address_space_t *as,uint64_t va,uint64_t phys,uint64_t flags){
    uint64_t entry;
    uint64_t mapped;
    if(!as||!as->pml4)return false;
    va&=~(uint64_t)(PAGE_SIZE-1u);
    phys&=~(uint64_t)(PAGE_SIZE-1u);
    entry=paging_get_entry(as,va);
    if(entry&PAGE_PRESENT){
        mapped=paging_translate(as,va)&~(uint64_t)(PAGE_SIZE-1u);
        if(mapped==phys&&((entry&flags)==flags))return true;
    }
    return paging_map(as,va,phys,flags);
}

static bool c4_map_mmio_range(uint64_t phys,uint32_t size){
    uint64_t start=phys&~(uint64_t)(PAGE_SIZE-1u);
    uint64_t end=(phys+(uint64_t)size+(uint64_t)PAGE_SIZE-1u)&~(uint64_t)(PAGE_SIZE-1u);
    uint64_t flags=PAGE_PRESENT|PAGE_WRITABLE|C4_PAGE_WRITETHROUGH|C4_PAGE_CACHE_DISABLE;
    address_space_t *kas=paging_get_kernel_space();
    task_t *cur=task_current();
    uint64_t va;
    if(!kas||!kas->pml4)return false;
    for(va=start;va<end;va+=PAGE_SIZE){
        if(!c4_map_mmio_page(kas,va,va,flags))return false;
        if(cur&&cur->addr_space&&cur->addr_space!=kas){
            if(!c4_map_mmio_page(cur->addr_space,va,va,flags))return false;
        }
    }
    __sync_synchronize();
    return true;
}

static bool c4_vmwgfx_trace_enabled(void){
    static int cached=-1;
    if(cached<0)cached=kvfs_exists("/etc/ridux-vmwgfx-trace.enable")?1:0;
    return cached!=0;
}

static void c4_svga_real_trace(const char *tag,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
    static uint32_t present_loop_trace;
    bool force_present_trace=tag&&
        (ulibc_strcmp(tag,"screen-present")==0||
         ulibc_strcmp(tag,"screen-present-fail")==0);
    if(!c4_vmwgfx_trace_enabled())return;
    if(tag&&(ulibc_strcmp(tag,"surface-update")==0||
              ulibc_strcmp(tag,"screen-present")==0||
              ulibc_strcmp(tag,"screen-present-fail")==0||
              ulibc_strcmp(tag,"gmrfb-blit")==0||
              ulibc_strcmp(tag,"vram-present")==0||
              ulibc_strcmp(tag,"vram-skip")==0)){
        if(present_loop_trace++>=24u)return;
    }
    if(!force_present_trace&&g_c4_svga_trace_count>=96u)return;
    ++g_c4_svga_trace_count;
    __boot_serial_force_puts("[vmsvga-real] ");
    __boot_serial_force_puts(tag?tag:"?");
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(g_c4_svga_trace_count);
    __boot_serial_force_puts(" a=");
    __boot_serial_force_puthex64(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_puthex64(b);
    __boot_serial_force_puts(" c=");
    __boot_serial_force_puthex64(c);
    __boot_serial_force_puts(" d=");
    __boot_serial_force_puthex64(d);
    __boot_serial_force_puts("\n");
}

static inline void c4_svga_io_wr32(uint16_t port,uint32_t val){
    __asm__ volatile("outl %0,%1"::"a"(val),"Nd"(port));
}

static inline uint32_t c4_svga_io_rd32(uint16_t port){
    uint32_t val;
    __asm__ volatile("inl %1,%0":"=a"(val):"Nd"(port));
    return val;
}

static void c4_svga_wr_reg(uint32_t reg,uint32_t val){
    c4_svga_io_wr32((uint16_t)(g_c4_svga_real.io_base+0u),reg);
    c4_svga_io_wr32((uint16_t)(g_c4_svga_real.io_base+1u),val);
}

static uint32_t c4_svga_rd_reg(uint32_t reg){
    c4_svga_io_wr32((uint16_t)(g_c4_svga_real.io_base+0u),reg);
    return c4_svga_io_rd32((uint16_t)(g_c4_svga_real.io_base+1u));
}

static bool c4_svga_real_find_device(void){
    int bus,slot,func,i;
    if(g_c4_svga_real.present)return true;
    for(bus=0;bus<1;++bus)for(slot=0;slot<32;++slot)for(func=0;func<8;++func){
        uint16_t vid=c4_pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0);
        uint16_t did;
        uint16_t io=0;
        if(vid==0xFFFFu)continue;
        did=c4_pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,2);
        if(!((vid==C4_SVGA_VENDOR_VBOX&&did==C4_SVGA_DEVICE_VBOX)||
             (vid==C4_SVGA_VENDOR_VMWARE&&did==C4_SVGA_DEVICE_VMWARE_SVGA2)))continue;
        for(i=0;i<6;++i){
            uint32_t bar=c4_pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,(uint8_t)(0x10+i*4));
            g_c4_svga_real.bar[i]=bar;
            if((bar&1u)&&!io)io=(uint16_t)(bar&~3u);
        }
        if(!io)continue;
        g_c4_svga_real.bus=(uint8_t)bus;
        g_c4_svga_real.slot=(uint8_t)slot;
        g_c4_svga_real.func=(uint8_t)func;
        g_c4_svga_real.io_base=io;
        c4_pci_wr32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4,
                    c4_pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4)|7u);
        g_c4_svga_real.present=true;
        c4_svga_real_trace("found",((uint64_t)bus<<16)|((uint64_t)slot<<8)|(uint64_t)func,
                            ((uint64_t)vid<<32)|did,io,g_c4_svga_real.bar[2]);
        return true;
    }
    return false;
}

static bool c4_svga_real_init(void){
    uint32_t id;
    uint32_t mem_start;
    uint32_t mem_size;
    uint32_t min_bytes;
    if(g_c4_svga_real.fifo_ready)return true;
    if(g_c4_svga_real.probed&&!g_c4_svga_real.present)return false;
    g_c4_svga_real.probed=true;
    if(!c4_svga_real_find_device())return false;
    c4_svga_wr_reg(C4_SVGA_REG_ID,C4_SVGA_ID_2);
    id=c4_svga_rd_reg(C4_SVGA_REG_ID);
    if(id!=C4_SVGA_ID_2){
        c4_svga_wr_reg(C4_SVGA_REG_ID,C4_SVGA_ID_1);
        id=c4_svga_rd_reg(C4_SVGA_REG_ID);
    }
    if(id!=C4_SVGA_ID_2&&id!=C4_SVGA_ID_1){
        c4_svga_real_trace("bad-id",id,C4_SVGA_ID_2,C4_SVGA_ID_1,0);
        return false;
    }
    g_c4_svga_real.caps=c4_svga_rd_reg(C4_SVGA_REG_CAPABILITIES);
    if((g_c4_svga_real.caps&(C4_SVGA_CAP_3D|C4_SVGA_CAP_EXTENDED_FIFO))!=
       (C4_SVGA_CAP_3D|C4_SVGA_CAP_EXTENDED_FIFO)){
        c4_svga_real_trace("caps-missing",g_c4_svga_real.caps,C4_SVGA_CAP_3D,C4_SVGA_CAP_EXTENDED_FIFO,0);
        return false;
    }
    mem_start=c4_svga_rd_reg(C4_SVGA_REG_MEM_START);
    mem_size=c4_svga_rd_reg(C4_SVGA_REG_MEM_SIZE);
    if(!mem_start&&(g_c4_svga_real.bar[2]&~0xFu))mem_start=g_c4_svga_real.bar[2]&~0xFu;
    if(mem_size<C4_SVGA_FIFO_MIN_BYTES)mem_size=2u*1024u*1024u;
    if(!c4_map_mmio_range(mem_start,mem_size)){
        c4_svga_real_trace("fifo-map-fail",mem_start,mem_size,g_c4_svga_real.caps,0);
        return false;
    }
    g_c4_svga_real.fifo=(volatile uint32_t*)(uintptr_t)mem_start;
    g_c4_svga_real.fifo_bytes=mem_size;
    min_bytes=C4_SVGA_FIFO_NUM_REGS*4u;
    if(min_bytes<C4_SVGA_FIFO_MIN_BYTES)min_bytes=C4_SVGA_FIFO_MIN_BYTES;
    if(g_c4_svga_real.fifo[C4_SVGA_FIFO_MIN]<min_bytes||
       g_c4_svga_real.fifo[C4_SVGA_FIFO_MAX]>mem_size||
       g_c4_svga_real.fifo[C4_SVGA_FIFO_MAX]<=g_c4_svga_real.fifo[C4_SVGA_FIFO_MIN]){
        g_c4_svga_real.fifo[C4_SVGA_FIFO_MIN]=min_bytes;
        g_c4_svga_real.fifo[C4_SVGA_FIFO_MAX]=mem_size;
        g_c4_svga_real.fifo[C4_SVGA_FIFO_NEXT_CMD]=min_bytes;
        g_c4_svga_real.fifo[C4_SVGA_FIFO_STOP]=min_bytes;
        g_c4_svga_real.fifo[C4_SVGA_FIFO_CAPABILITIES]=0;
    }
    c4_svga_wr_reg(C4_SVGA_REG_GUEST_ID,2u);
    c4_svga_wr_reg(C4_SVGA_REG_WIDTH,g_fb.width?g_fb.width:1024u);
    c4_svga_wr_reg(C4_SVGA_REG_HEIGHT,g_fb.height?g_fb.height:768u);
    c4_svga_wr_reg(C4_SVGA_REG_NUM_GUEST_DISPLAYS,1u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_ID,0u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_IS_PRIMARY,1u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_POSITION_X,0u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_POSITION_Y,0u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_WIDTH,g_fb.width?g_fb.width:1024u);
    c4_svga_wr_reg(C4_SVGA_REG_DISPLAY_HEIGHT,g_fb.height?g_fb.height:768u);
    c4_svga_wr_reg(C4_SVGA_REG_BLANK_SCREEN_TARGETS,0u);
    c4_svga_wr_reg(C4_SVGA_REG_CONFIG_DONE,1u);
    c4_svga_wr_reg(C4_SVGA_REG_ENABLE,1u);
    g_c4_svga_real.fb_start=c4_svga_rd_reg(C4_SVGA_REG_FB_START)+c4_svga_rd_reg(C4_SVGA_REG_FB_OFFSET);
    g_c4_svga_real.fb_size=c4_svga_rd_reg(C4_SVGA_REG_FB_SIZE);
    if(!g_c4_svga_real.fb_size)g_c4_svga_real.fb_size=c4_svga_rd_reg(C4_SVGA_REG_VRAM_SIZE);
    g_c4_svga_real.fb_pitch=c4_svga_rd_reg(C4_SVGA_REG_BYTES_PER_LINE);
    if(!g_c4_svga_real.fb_pitch)g_c4_svga_real.fb_pitch=(g_fb.width?g_fb.width:1024u)*4u;
    if(g_c4_svga_real.fb_start&&g_c4_svga_real.fb_size&&
       c4_map_mmio_range(g_c4_svga_real.fb_start,g_c4_svga_real.fb_size)){
        g_c4_svga_real.fb=(volatile uint8_t*)(uintptr_t)g_c4_svga_real.fb_start;
        c4_svga_real_trace("fb-map",g_c4_svga_real.fb_start,g_c4_svga_real.fb_size,
                            g_c4_svga_real.fb_pitch,c4_svga_rd_reg(C4_SVGA_REG_BITS_PER_PIXEL));
    }
    g_c4_svga_real.fifo_caps=c4_svga_rd_reg(C4_SVGA_REG_FIFO_CAPS);
    g_c4_svga_real.fence_next=1;
    g_c4_svga_real.fifo_ready=true;
    c4_svga_real_trace("fifo-ready",mem_start,mem_size,g_c4_svga_real.caps,g_c4_svga_real.fifo_caps);
    return true;
}

static void c4_svga_real_sync(void){
    uint32_t spin;
    if(!g_c4_svga_real.fifo_ready)return;
    c4_svga_wr_reg(C4_SVGA_REG_SYNC,1u);
    for(spin=0;spin<5000000u;++spin){
        if(!c4_svga_rd_reg(C4_SVGA_REG_BUSY))break;
        __asm__ volatile("pause");
    }
}

static bool c4_svga_real_fifo_write(const void *data,uint32_t len){
    volatile uint32_t *fifo;
    uint32_t min,max,next,stop;
    uint32_t spin;
    if(!data||!len)return false;
    if(!c4_svga_real_init())return false;
    if(len&3u)return false;
    fifo=g_c4_svga_real.fifo;
    min=fifo[C4_SVGA_FIFO_MIN];
    max=fifo[C4_SVGA_FIFO_MAX];
    if(min<C4_SVGA_FIFO_MIN_BYTES||max<=min||max>g_c4_svga_real.fifo_bytes)return false;
    for(spin=0;spin<2u;++spin){
        next=fifo[C4_SVGA_FIFO_NEXT_CMD];
        stop=fifo[C4_SVGA_FIFO_STOP];
        if(next<min||next>=max||stop<min||stop>=max)return false;
        if(next+len<=max){
            if(next>=stop||next+len<stop){
                ulibc_memcpy((void*)((uint8_t*)(uintptr_t)fifo+next),data,len);
                __sync_synchronize();
                fifo[C4_SVGA_FIFO_NEXT_CMD]=next+len;
                return true;
            }
        }
        c4_svga_real_sync();
        next=fifo[C4_SVGA_FIFO_NEXT_CMD];
        stop=fifo[C4_SVGA_FIFO_STOP];
        if(next+len>max&&next==stop){
            fifo[C4_SVGA_FIFO_NEXT_CMD]=min;
            fifo[C4_SVGA_FIFO_STOP]=min;
        }
    }
    c4_svga_real_trace("fifo-full",len,fifo[C4_SVGA_FIFO_NEXT_CMD],fifo[C4_SVGA_FIFO_STOP],fifo[C4_SVGA_FIFO_MAX]);
    return false;
}

static bool c4_svga_real_emit_command(uint32_t id,const void *payload,uint32_t payload_size){
    uint8_t buf[256];
    uint32_t *h=(uint32_t*)buf;
    if(payload_size+8u>sizeof(buf))return false;
    h[0]=id;
    h[1]=payload_size;
    if(payload_size&&payload)ulibc_memcpy(buf+8u,payload,payload_size);
    return c4_svga_real_fifo_write(buf,8u+payload_size);
}

static bool c4_svga_real_emit_legacy(uint32_t id,const void *payload,uint32_t payload_size){
    uint8_t buf[256];
    uint32_t *h=(uint32_t*)buf;
    if(payload_size+4u>sizeof(buf))return false;
    h[0]=id;
    if(payload_size&&payload)ulibc_memcpy(buf+4u,payload,payload_size);
    return c4_svga_real_fifo_write(buf,4u+payload_size);
}

static bool c4_svga_real_submit_raw(const void *data,uint32_t len){
    if(!data||!len)return false;
    if(len&3u)return false;
    return c4_svga_real_fifo_write(data,len);
}

static bool c4_svga_real_fence(void);
static bool c4_svga_real_define_mob(uint32_t mobid,uint64_t phys,uint64_t bytes,
                                    uint64_t *base_ppn,uint32_t *pt_depth,
                                    uint64_t *pt_phys,uint32_t *pt_pages);

static bool c4_svga_real_submit_cb_packet(const void *data,uint32_t len,
                                          uint32_t dx_context,uint32_t cb_context){
    c4_svga_cb_header_t *hdr;
    uint8_t *cmd;
    uint64_t hdr_phys;
    uint32_t spin,status;
    if(!data||!len||len>C4_SVGA_CB_MAX_COMMAND_BYTES)return false;
    if(len&3u)return false;
    if(!c4_svga_real_init())return false;
    if(!(g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS))return false;
    if(!g_c4_svga_real.cmd_virt){
        uint32_t bytes=C4_SVGA_CB_MAX_COMMAND_BYTES+(uint32_t)sizeof(c4_svga_cb_header_t);
        if(!c4_svga_alloc_physical(bytes,&g_c4_svga_real.cmd_phys,(uint8_t**)&g_c4_svga_real.cmd_virt))
            return false;
        g_c4_svga_real.cmd_bytes=bytes;
    }
    if((uint64_t)len+(uint64_t)sizeof(c4_svga_cb_header_t)>g_c4_svga_real.cmd_bytes)return false;
    hdr=(c4_svga_cb_header_t*)g_c4_svga_real.cmd_virt;
    cmd=g_c4_svga_real.cmd_virt+sizeof(c4_svga_cb_header_t);
    ulibc_memset(hdr,0,sizeof(*hdr));
    ulibc_memcpy(cmd,data,len);
    hdr->status=C4_SVGA_CB_STATUS_NONE;
    hdr->error_offset=0;
    hdr->id=++g_c4_svga_real.cmd_seq;
    hdr->flags=dx_context?C4_SVGA_CB_FLAG_DX_CONTEXT:0u;
    hdr->length=len;
    hdr->ptr.pa=g_c4_svga_real.cmd_phys+(uint64_t)sizeof(c4_svga_cb_header_t);
    hdr->offset=0;
    hdr->dx_context=dx_context;
    hdr_phys=g_c4_svga_real.cmd_phys;
    __sync_synchronize();
    c4_svga_wr_reg(C4_SVGA_REG_COMMAND_HIGH,(uint32_t)(hdr_phys>>32));
    c4_svga_wr_reg(C4_SVGA_REG_COMMAND_LOW,((uint32_t)hdr_phys)|(cb_context&C4_SVGA_CB_CONTEXT_MASK));
    for(spin=0;spin<5000000u;++spin){
        status=hdr->status;
        if(status!=C4_SVGA_CB_STATUS_NONE)break;
        __asm__ volatile("pause");
    }
    __sync_synchronize();
    status=hdr->status;
    if(status==C4_SVGA_CB_STATUS_COMPLETED)return true;
    c4_svga_real_trace("cmdbuf-fail",status,hdr->error_offset,len,dx_context);
    return false;
}

static bool c4_svga_real_ensure_sync_mob(void){
    uint64_t base_ppn=0,pt_phys=0;
    uint32_t pt_depth=0,pt_pages=0;
    if(g_c4_svga_real.sync_mob_ready)return true;
    if(!g_c4_svga_real.sync_virt){
        if(!c4_svga_alloc_physical(PAGE_SIZE,&g_c4_svga_real.sync_phys,(uint8_t**)&g_c4_svga_real.sync_virt))
            return false;
    }
    if(!c4_svga_real_define_mob(C4_SVGA_SYNC_MOB_ID,g_c4_svga_real.sync_phys,PAGE_SIZE,
                                &base_ppn,&pt_depth,&pt_phys,&pt_pages)){
        c4_svga_real_trace("dx-fence-mob-fail",C4_SVGA_SYNC_MOB_ID,g_c4_svga_real.sync_phys,base_ppn,pt_depth);
        return false;
    }
    g_c4_svga_real.sync_mob_ready=true;
    c4_svga_real_fence();
    c4_svga_real_trace("dx-fence-mob",C4_SVGA_SYNC_MOB_ID,g_c4_svga_real.sync_phys,base_ppn,pt_depth);
    return true;
}

static bool c4_svga_real_wait_dx_fence(uint32_t dx_context){
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint64_t value;
        uint32_t mobid;
        uint32_t mob_offset;
    } cmd;
    uint64_t seq;
    uint32_t spin;
    if(!dx_context)return true;
    if(!c4_svga_real_ensure_sync_mob())return false;
    seq=++g_c4_svga_real.sync_seq;
    if(!seq)seq=++g_c4_svga_real.sync_seq;
    *g_c4_svga_real.sync_virt=0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.id=C4_SVGA_3D_CMD_DX_MOB_FENCE_64;
    cmd.size=(uint32_t)(sizeof(cmd)-8u);
    cmd.value=seq;
    cmd.mobid=C4_SVGA_SYNC_MOB_ID;
    __sync_synchronize();
    if(!c4_svga_real_submit_cb_packet(&cmd,sizeof(cmd),dx_context,C4_SVGA_CB_CONTEXT_0))
        return false;
    for(spin=0;spin<5000000u;++spin){
        __sync_synchronize();
        if(*g_c4_svga_real.sync_virt==seq){
            c4_svga_real_trace("dx-fence",seq,dx_context,spin,C4_SVGA_SYNC_MOB_ID);
            return true;
        }
        __asm__ volatile("pause");
    }
    c4_svga_real_trace("dx-fence-timeout",seq,dx_context,*g_c4_svga_real.sync_virt,C4_SVGA_SYNC_MOB_ID);
    return false;
}

static bool c4_svga_real_wait_gb_fence(void){
    struct __attribute__((packed)) {
        uint32_t value;
        uint32_t mobid;
        uint32_t mob_offset;
    } cmd;
    uint64_t seq64;
    uint32_t seq;
    uint32_t spin;
    if(!c4_svga_real_ensure_sync_mob())return false;
    seq64=++g_c4_svga_real.sync_seq;
    if(!seq64)seq64=++g_c4_svga_real.sync_seq;
    seq=(uint32_t)seq64;
    if(!seq)seq=(uint32_t)++g_c4_svga_real.sync_seq;
    *g_c4_svga_real.sync_virt=0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.value=seq;
    cmd.mobid=C4_SVGA_SYNC_MOB_ID;
    cmd.mob_offset=0;
    __sync_synchronize();
    if(!c4_svga_real_emit_command(C4_SVGA_3D_CMD_GB_MOB_FENCE,&cmd,sizeof(cmd)))
        return false;
    for(spin=0;spin<5000000u;++spin){
        __sync_synchronize();
        if(((volatile uint32_t*)g_c4_svga_real.sync_virt)[0]==seq){
            c4_svga_real_trace("gb-fence",seq,spin,C4_SVGA_SYNC_MOB_ID,0);
            return true;
        }
        __asm__ volatile("pause");
    }
    c4_svga_real_trace("gb-fence-timeout",seq,((volatile uint32_t*)g_c4_svga_real.sync_virt)[0],
                        C4_SVGA_SYNC_MOB_ID,0);
    return false;
}

static bool c4_svga_real_start_command_buffers(void){
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t enable;
        uint32_t context;
    } cmd;
    bool ok;
    if(g_c4_svga_real.cmd_context_started)return true;
    if(!(g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS))return false;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.id=C4_SVGA_DC_CMD_START_STOP_CONTEXT;
    cmd.enable=1;
    cmd.context=C4_SVGA_CB_CONTEXT_0;
    ok=c4_svga_real_submit_cb_packet(&cmd,sizeof(cmd),0,C4_SVGA_CB_CONTEXT_DEVICE);
    g_c4_svga_real.cmd_context_started=ok;
    c4_svga_real_trace(ok?"cmdbuf-start":"cmdbuf-start-fail",g_c4_svga_real.caps,
                        g_c4_svga_real.cmd_phys,g_c4_svga_real.cmd_bytes,0);
    return ok;
}

static bool c4_svga_real_update_surface(uint32_t sid){
    bool ok;
    if(!sid)return false;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_UPDATE_GB_SURFACE,&sid,sizeof(sid));
    c4_svga_real_trace(ok?"surface-update":"surface-update-fail",sid,0,0,0);
    return ok;
}

static bool c4_svga_real_clear_rtv(uint32_t dx_context,uint32_t view_id,uint32_t probe){
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t view_id;
        uint32_t rgba[4];
    } cmd;
    bool ok;
    if(!dx_context||!view_id)return false;
    if(!(g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS))return false;
    if(!c4_svga_real_start_command_buffers())return false;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.id=C4_SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW;
    cmd.size=(uint32_t)(sizeof(cmd)-8u);
    cmd.view_id=view_id;
    cmd.rgba[0]=0x3dcccccd; /* 0.1 */
    cmd.rgba[1]=(probe&1u)?0x3f4ccccd:0x3e99999a; /* 0.8 / 0.3 */
    cmd.rgba[2]=(probe&1u)?0x3e99999a:0x3f4ccccd; /* 0.3 / 0.8 */
    cmd.rgba[3]=0x3f800000; /* 1.0 */
    ok=c4_svga_real_submit_cb_packet(&cmd,sizeof(cmd),dx_context,C4_SVGA_CB_CONTEXT_0);
    if(ok)ok=c4_svga_real_wait_dx_fence(dx_context);
    c4_svga_real_trace(ok?"dx-clear-rtv":"dx-clear-rtv-fail",dx_context,view_id,probe,0);
    return ok;
}

static bool c4_svga_real_dx_readback_surface(uint32_t dx_context,uint32_t sid){
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t sid;
        uint32_t subresource;
    } cmd;
    bool ok;
    if(!dx_context||!sid)return false;
    if(!(g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS))return false;
    if(!c4_svga_real_start_command_buffers())return false;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.id=C4_SVGA_3D_CMD_DX_READBACK_SUBRESOURCE;
    cmd.size=(uint32_t)(sizeof(cmd)-8u);
    cmd.sid=sid;
    cmd.subresource=0;
    ok=c4_svga_real_submit_cb_packet(&cmd,sizeof(cmd),dx_context,C4_SVGA_CB_CONTEXT_0);
    if(ok)ok=c4_svga_real_wait_dx_fence(dx_context);
    c4_svga_real_trace(ok?"dx-readback":"dx-readback-fail",dx_context,sid,0,0);
    return ok;
}

static bool c4_svga_real_clear_readback_rtv(uint32_t dx_context,uint32_t view_id,
                                            uint32_t sid,uint32_t format,
                                            uint32_t dimension,uint32_t probe){
    uint8_t buf[128];
    uint32_t off=0;
    uint32_t test_view=64u+(probe&15u);
    bool ok;
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t view_id;
        uint32_t sid;
        uint32_t format;
        uint32_t resource_dimension;
        uint32_t first_array_slice;
        uint32_t array_size;
        uint32_t mip_slice;
    } def;
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t dsv;
        uint32_t rtv;
    } setrt;
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t view_id;
        uint32_t rgba[4];
    } clear;
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
        uint32_t sid;
        uint32_t subresource;
    } rb;
    if(!dx_context||!view_id||!sid)return false;
    if(!(g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS))return false;
    if(!c4_svga_real_start_command_buffers())return false;
    ulibc_memset(buf,0,sizeof(buf));
    ulibc_memset(&def,0,sizeof(def));
    def.id=C4_SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW;
    def.size=(uint32_t)(sizeof(def)-8u);
    def.view_id=test_view;
    def.sid=sid;
    def.format=format?format:0x8eu;
    def.resource_dimension=dimension?dimension:3u;
    def.array_size=1u;
    ulibc_memcpy(buf+off,&def,sizeof(def));
    off+=(uint32_t)sizeof(def);
    ulibc_memset(&setrt,0,sizeof(setrt));
    setrt.id=C4_SVGA_3D_CMD_DX_SET_RENDERTARGETS;
    setrt.size=(uint32_t)(sizeof(setrt)-8u);
    setrt.dsv=0xffffffffu;
    setrt.rtv=test_view;
    ulibc_memcpy(buf+off,&setrt,sizeof(setrt));
    off+=(uint32_t)sizeof(setrt);
    ulibc_memset(&clear,0,sizeof(clear));
    clear.id=C4_SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW;
    clear.size=(uint32_t)(sizeof(clear)-8u);
    clear.view_id=test_view;
    clear.rgba[0]=0x3dcccccd; /* 0.1 */
    clear.rgba[1]=(probe&1u)?0x3f4ccccd:0x3e99999a; /* 0.8 / 0.3 */
    clear.rgba[2]=(probe&1u)?0x3e99999a:0x3f4ccccd; /* 0.3 / 0.8 */
    clear.rgba[3]=0x3f800000; /* 1.0 */
    ulibc_memcpy(buf+off,&clear,sizeof(clear));
    off+=(uint32_t)sizeof(clear);
    ulibc_memset(&rb,0,sizeof(rb));
    rb.id=C4_SVGA_3D_CMD_DX_READBACK_SUBRESOURCE;
    rb.size=(uint32_t)(sizeof(rb)-8u);
    rb.sid=sid;
    rb.subresource=0;
    ulibc_memcpy(buf+off,&rb,sizeof(rb));
    off+=(uint32_t)sizeof(rb);
    ok=c4_svga_real_submit_cb_packet(buf,off,dx_context,C4_SVGA_CB_CONTEXT_0);
    if(ok)ok=c4_svga_real_wait_dx_fence(dx_context);
    c4_svga_real_trace(ok?"dx-clear-readback":"dx-clear-readback-fail",
                       dx_context,((uint64_t)test_view<<32)|view_id,sid,
                       ((uint64_t)def.format<<32)|def.resource_dimension);
    return ok;
}

static bool c4_svga_real_prepare_updated_surfaces(const void *data,uint32_t len){
    typedef struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
    } c4_svga_exec_hdr_t;
    const uint8_t *base=(const uint8_t*)data;
    uint32_t off=0;
    bool touched=false;
    bool ok=true;
    if(!data||!len)return true;
    while(off+sizeof(c4_svga_exec_hdr_t)<=len){
        const c4_svga_exec_hdr_t *h=(const c4_svga_exec_hdr_t*)(const void*)(base+off);
        const uint32_t *p=(const uint32_t*)(const void*)(base+off+sizeof(*h));
        uint32_t next=off+(uint32_t)sizeof(*h)+h->size;
        if(h->size>(len-off-(uint32_t)sizeof(*h))||next<=off)break;
        if(h->id==C4_SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE&&h->size>=4u){
            ok=c4_svga_real_update_surface(p[0])&&ok;
            touched=true;
        }
        off=next;
        if(off&3u)off=(off+3u)&~3u;
    }
    if(touched&&ok)c4_svga_real_fence();
    return ok;
}

static bool c4_svga_real_submit_dx_commands(const void *data,uint32_t len,uint32_t dx_context){
    typedef struct __attribute__((packed)) {
        uint32_t id;
        uint32_t size;
    } c4_svga_exec_hdr_t;
    const uint8_t *base=(const uint8_t*)data;
    uint32_t off=0;
    bool parsed=true;
    bool has_dx=false;
    if(!data||!len)return false;
    while(off+sizeof(c4_svga_exec_hdr_t)<=len){
        const c4_svga_exec_hdr_t *h=(const c4_svga_exec_hdr_t*)(const void*)(base+off);
        uint32_t next=off+(uint32_t)sizeof(*h)+h->size;
        if(h->size>(len-off-(uint32_t)sizeof(*h))||next<=off){parsed=false;break;}
        if(h->id>=C4_SVGA_3D_CMD_DX_DEFINE_CONTEXT&&h->id<=C4_SVGA_3D_CMD_DX_MOB_FENCE_64)
            has_dx=true;
        off=next;
        if(off&3u)off=(off+3u)&~3u;
    }
    if(!dx_context&&parsed&&!has_dx){
        bool ok=true;
        off=0;
        while(off+sizeof(c4_svga_exec_hdr_t)<=len){
            const c4_svga_exec_hdr_t *h=(const c4_svga_exec_hdr_t*)(const void*)(base+off);
            const void *payload=(const void*)(base+off+sizeof(*h));
            uint32_t next=off+(uint32_t)sizeof(*h)+h->size;
            if(h->size>(len-off-(uint32_t)sizeof(*h))||next<=off){ok=false;break;}
            ok=c4_svga_real_emit_command(h->id,payload,h->size)&&ok;
            off=next;
            if(off&3u)off=(off+3u)&~3u;
        }
        if(ok)c4_svga_real_fence();
        c4_svga_real_trace(ok?"exec-fifo":"exec-fifo-fail",len,dx_context,off,0);
        return ok;
    }
    (void)c4_svga_real_prepare_updated_surfaces(data,len);
    if((g_c4_svga_real.caps&C4_SVGA_CAP_COMMAND_BUFFERS)&&c4_svga_real_start_command_buffers()){
        if(c4_svga_real_submit_cb_packet(data,len,dx_context,C4_SVGA_CB_CONTEXT_0)){
            if(!c4_svga_real_wait_dx_fence(dx_context))
                (void)c4_svga_real_wait_gb_fence();
            return true;
        }
    }
    return c4_svga_real_submit_raw(data,len);
}

static bool c4_svga_real_fence(void){
    uint32_t payload[2];
    uint32_t fence;
    if(!c4_svga_real_init())return false;
    fence=(uint32_t)g_c4_svga_real.fence_next++;
    if(!fence)fence=(uint32_t)g_c4_svga_real.fence_next++;
    payload[0]=C4_SVGA_CMD_FENCE;
    payload[1]=fence;
    if(!c4_svga_real_fifo_write(payload,sizeof(payload)))return false;
    c4_svga_wr_reg(C4_SVGA_REG_FENCE,fence);
    c4_svga_real_sync();
    return true;
}

static void c4_vgpu_real_trace(const char *tag,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
    static int cached=-1;
    if(cached<0)cached=(kvfs_exists("/etc/ridux-virtgpu-trace.enable")||
                         kvfs_exists("/etc/ridux-hyprland-debug.enable"))?1:0;
    if(!cached)return;
    __boot_serial_force_puts("[virtio-real] ");
    __boot_serial_force_puts(tag?tag:"?");
    __boot_serial_force_puts(" a=");
    __boot_serial_force_puthex64(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_puthex64(b);
    __boot_serial_force_puts(" c=");
    __boot_serial_force_puthex64(c);
    __boot_serial_force_puts(" d=");
    __boot_serial_force_puthex64(d);
    __boot_serial_force_puts("\n");
}

static bool c4_virtgpu_trace_enabled(void){
    static int cached=-1;
    if(cached<0)cached=(kvfs_exists("/etc/ridux-virtgpu-trace.enable")||
                         kvfs_exists("/etc/ridux-hyprland-debug.enable"))?1:0;
    return cached!=0;
}

static bool c4_drm_present_trace_enabled(void){
    static int cached=-1;
    if(cached<0)cached=(kvfs_exists("/etc/ridux-drm-present-trace.enable")||
                         kvfs_exists("/etc/ridux-hyprland-log.enable")||
                         kvfs_exists("/etc/ridux-hyprland-debug.enable"))?1:0;
    return cached!=0;
}

static bool c4_virtgpu_strict_wait_enabled(void){
    static int cached=-1;
    if(cached<0)cached=kvfs_exists("/etc/ridux-virtgpu-strict-wait.enable")?1:0;
    return cached!=0;
}

static bool c4_virtgpu_exec_sync_enabled(void){
    static int cached=-1;
    if(cached<0)cached=kvfs_exists("/etc/ridux-virtgpu-exec-sync.enable")?1:0;
    return cached!=0;
}

static uint64_t c4_vgpu_real_bar_base(uint8_t bar){
    uint32_t raw;
    uint64_t base;
    if(bar>=6)return 0;
    raw=g_c4_vgpu_real.bar[bar];
    if(raw&1u)return 0;
    base=(uint64_t)(raw&~0xFu);
    if((raw&0x6u)==0x4u&&bar+1u<6u){
        base|=((uint64_t)g_c4_vgpu_real.bar[bar+1u]<<32);
    }
    return base;
}

static bool c4_vgpu_real_find_device(void){
    int bus,slot,func,i;
    if(g_c4_vgpu_real.present)return true;
    for(bus=0;bus<1;++bus)for(slot=0;slot<32;++slot)for(func=0;func<8;++func){
        uint16_t vid=c4_pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0);
        uint16_t did;
        if(vid==0xFFFFu)continue;
        did=c4_pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,2);
        if(vid!=C4_VIRTIO_GPU_VENDOR||did!=C4_VIRTIO_GPU_DEVICE_MODERN)continue;
        g_c4_vgpu_real.bus=(uint8_t)bus;
        g_c4_vgpu_real.slot=(uint8_t)slot;
        g_c4_vgpu_real.func=(uint8_t)func;
        for(i=0;i<6;++i)g_c4_vgpu_real.bar[i]=c4_pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,(uint8_t)(0x10+i*4));
        c4_pci_wr32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4,c4_pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4)|6u);
        g_c4_vgpu_real.present=true;
        c4_vgpu_real_trace("found",((uint64_t)bus<<16)|((uint64_t)slot<<8)|(uint64_t)func,((uint64_t)vid<<32)|did,g_c4_vgpu_real.bar[1],g_c4_vgpu_real.bar[4]);
        c4_vgpu_real_trace("bars0",g_c4_vgpu_real.bar[0],g_c4_vgpu_real.bar[1],g_c4_vgpu_real.bar[2],g_c4_vgpu_real.bar[3]);
        c4_vgpu_real_trace("bars4",g_c4_vgpu_real.bar[4],g_c4_vgpu_real.bar[5],0,0);
        return true;
    }
    return false;
}

static volatile uint8_t *c4_vgpu_real_cap_ptr(uint8_t cap,uint32_t *out_len,uint32_t *out_notify_mult){
    uint8_t p;
    if(out_len)*out_len=0;
    if(out_notify_mult)*out_notify_mult=0;
    if(!c4_vgpu_real_find_device())return 0;
    for(p=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,0x34);p;p=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+1))){
        uint8_t cfg_type;
        uint8_t bar;
        uint32_t off,len;
        uint64_t base;
        if(c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,p)!=0x09u)continue;
        cfg_type=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+3));
        if(cfg_type!=cap)continue;
        bar=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+4));
        off=c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+8));
        len=c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+12));
        if(bar>=6)return 0;
        base=c4_vgpu_real_bar_base(bar);
        if(!base)return 0;
        if(!c4_map_mmio_range(base+(uint64_t)off,len?len:PAGE_SIZE))return 0;
        if(out_len)*out_len=len;
        if(out_notify_mult&&cap==C4_VIRTIO_PCI_CAP_NOTIFY_CFG)*out_notify_mult=c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+16));
        c4_vgpu_real_trace("cap",cap,bar,base+(uint64_t)off,len);
        return (volatile uint8_t*)(uintptr_t)(base+(uint64_t)off);
    }
    return 0;
}

static bool c4_vgpu_real_probe_host_visible(void){
    uint8_t p;
    if(g_c4_vgpu_real.host_visible_probed)return g_c4_vgpu_real.host_visible_ready;
    g_c4_vgpu_real.host_visible_probed=true;
    g_c4_vgpu_real.host_visible_ready=false;
    g_c4_vgpu_real.host_visible_base=0;
    g_c4_vgpu_real.host_visible_len=0;
    g_c4_vgpu_real.host_visible_next=0;
    if(!c4_vgpu_real_find_device())return false;
    for(p=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,0x34);p;p=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+1))){
        uint8_t cfg_type,bar,id,cap_len;
        uint64_t off,len,base;
        if(c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,p)!=0x09u)continue;
        cap_len=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+2));
        cfg_type=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+3));
        if(cfg_type!=C4_VIRTIO_PCI_CAP_SHARED_MEMORY_CFG)continue;
        bar=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+4));
        id=c4_pci_rd8(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+5));
        if(id!=C4_VIRTIO_GPU_SHM_ID_HOST_VISIBLE)continue;
        if(bar>=6)continue;
        off=(uint64_t)c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+8));
        len=(uint64_t)c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+12));
        if(cap_len>=24u){
            off|=(uint64_t)c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+16))<<32;
            len|=(uint64_t)c4_pci_rd32(g_c4_vgpu_real.bus,g_c4_vgpu_real.slot,g_c4_vgpu_real.func,(uint8_t)(p+20))<<32;
        }
        base=c4_vgpu_real_bar_base(bar);
        if(!base||!len||off>~0ULL-base)continue;
        g_c4_vgpu_real.host_visible_base=base+off;
        g_c4_vgpu_real.host_visible_len=len;
        g_c4_vgpu_real.host_visible_next=0;
        g_c4_vgpu_real.host_visible_ready=true;
        c4_vgpu_real_trace("host-visible",bar,id,g_c4_vgpu_real.host_visible_base,len);
        return true;
    }
    c4_vgpu_real_trace("host-visible-miss",0,0,0,0);
    return false;
}

static uint64_t c4_vgpu_real_alloc_host_visible(uint64_t bytes){
    uint64_t size,off;
    if(!bytes||!c4_vgpu_real_probe_host_visible())return ~0ULL;
    size=c4_align_page(bytes);
    off=c4_align_page(g_c4_vgpu_real.host_visible_next);
    if(size>g_c4_vgpu_real.host_visible_len||off>g_c4_vgpu_real.host_visible_len-size)
        return ~0ULL;
    g_c4_vgpu_real.host_visible_next=off+size;
    return off;
}

static inline uint8_t c4_mmio_r8(volatile uint8_t *p,uint32_t off){return *(volatile uint8_t*)(p+off);}
static inline uint16_t c4_mmio_r16(volatile uint8_t *p,uint32_t off){return *(volatile uint16_t*)(p+off);}
static inline uint32_t c4_mmio_r32(volatile uint8_t *p,uint32_t off){return *(volatile uint32_t*)(p+off);}
static inline void c4_mmio_w8(volatile uint8_t *p,uint32_t off,uint8_t v){*(volatile uint8_t*)(p+off)=v;}
static inline void c4_mmio_w16(volatile uint8_t *p,uint32_t off,uint16_t v){*(volatile uint16_t*)(p+off)=v;}
static inline void c4_mmio_w32(volatile uint8_t *p,uint32_t off,uint32_t v){*(volatile uint32_t*)(p+off)=v;}

static inline void c4_mmio_w64(volatile uint8_t *p,uint32_t off,uint64_t v){
    c4_mmio_w32(p,off,(uint32_t)v);
    c4_mmio_w32(p,off+4,(uint32_t)(v>>32));
}

static uint64_t c4_dma_addr(const void *p){
    uint64_t v=(uint64_t)(uintptr_t)p;
    uint64_t phys=0;
    task_t *cur;
    if(v>=DMAP_BASE)return DMAP_TO_PHYS(v);
    cur=task_current();
    if(cur&&cur->addr_space&&cur->addr_space->pml4){
        phys=paging_translate(cur->addr_space,v);
        if(phys)return phys;
    }
    {
        address_space_t *kas=paging_get_kernel_space();
        if(kas&&kas->pml4){
            phys=paging_translate(kas,v);
            if(phys)return phys;
        }
    }
    return v;
}

static c4_drm_bo_t *c4_drm_find_bo_by_handle(uint32_t handle);
static c4_drm_bo_t *c4_drm_find_bo_by_fb(uint32_t fb_id);
static void c4_drm_sample_bo(c4_drm_bo_t *bo,uint32_t *nz,uint64_t *sum,
                             uint64_t *p0,uint64_t *p1);
static void c4_drm_trace_bo(const char *tag,c4_drm_bo_t *bo,uint32_t a,uint32_t b);
static void c4_drm_refresh_bo_from_gpu(c4_drm_bo_t *bo,const char *tag);
static void c4_drm_static_blobs_ensure(void);
static uint32_t c4_drm_modes_count(void);
static bool c4_drm_fill_mode_at(uint32_t idx,c4_drm_mode_modeinfo_t *m);
static c4_drm_bo_t *c4_drm_alloc_bo(uint32_t width,uint32_t height,uint32_t bpp);
static void c4_drm_free_storage(c4_drm_bo_t *bo);
static c4_drm_bo_t *c4_drm_latest_bo(void);
static int32_t c4_drm_prime_fd_for_bo(c4_drm_bo_t *bo);
static uint32_t c4_drm_handle_from_prime_fd(int32_t fd);
static c4_drm_file_t *c4_drm_file_get(int fd,bool create);
static c4_drm_bo_t *c4_drm_bo_for_fd_handle(int fd,uint32_t handle);
static bool c4_drm_file_handle_add(c4_drm_file_t *file,c4_drm_bo_t *bo);
static bool c4_drm_file_handle_remove(c4_drm_file_t *file,uint32_t handle);
static bool c4_drm_file_can_modeset(int fd);
static void c4_drm_bo_try_free(c4_drm_bo_t *bo);
static bool c4_drm_vbox_mode(void);
static bool c4_svga_real_present_bo(c4_drm_bo_t *bo);
static bool c4_vmw_draw_cursor_overlay(c4_drm_bo_t *rt);
static void c4_virtgpu_trace_u64(const char *tag,uint64_t a,uint64_t b,uint64_t c,uint64_t d);
extern uint64_t c3_copy_visible_shell_frame(void *dst,uint32_t pitch,uint32_t width,uint32_t height);

static bool c4_vgpu_real_init(void){
    uint32_t len=0,notify_mult=0;
    uint16_t qmax,qsz;
    uint8_t status;
    if(g_c4_vgpu_real.queue_ready)return true;
    if(g_c4_vgpu_real.probed&&!g_c4_vgpu_real.present)return false;
    g_c4_vgpu_real.probed=true;
    if(!c4_vgpu_real_find_device())return false;
    g_c4_vgpu_real.common=c4_vgpu_real_cap_ptr(C4_VIRTIO_PCI_CAP_COMMON_CFG,&len,0);
    g_c4_vgpu_real.notify=c4_vgpu_real_cap_ptr(C4_VIRTIO_PCI_CAP_NOTIFY_CFG,&len,&notify_mult);
    g_c4_vgpu_real.device=c4_vgpu_real_cap_ptr(C4_VIRTIO_PCI_CAP_DEVICE_CFG,&len,0);
    g_c4_vgpu_real.notify_multiplier=notify_mult?notify_mult:2u;
    if(!g_c4_vgpu_real.common||!g_c4_vgpu_real.notify)return false;
    c4_vgpu_real_trace("init-common",(uint64_t)(uintptr_t)g_c4_vgpu_real.common,(uint64_t)(uintptr_t)g_c4_vgpu_real.notify,(uint64_t)(uintptr_t)g_c4_vgpu_real.device,g_c4_vgpu_real.notify_multiplier);
    c4_vgpu_real_trace("init-reset-skip",c4_mmio_r8(g_c4_vgpu_real.common,20),0,0,0);
    c4_mmio_w8(g_c4_vgpu_real.common,20,C4_VIRTIO_STATUS_ACKNOWLEDGE|C4_VIRTIO_STATUS_DRIVER);
    c4_vgpu_real_trace("init-ack",c4_mmio_r8(g_c4_vgpu_real.common,20),0,0,0);
    c4_mmio_w32(g_c4_vgpu_real.common,0,0);
    g_c4_vgpu_real.features0=c4_mmio_r32(g_c4_vgpu_real.common,4);
    c4_vgpu_real_trace("init-feat0",g_c4_vgpu_real.features0,0,0,0);
    c4_mmio_w32(g_c4_vgpu_real.common,0,1);
    g_c4_vgpu_real.features1=c4_mmio_r32(g_c4_vgpu_real.common,4);
    c4_vgpu_real_trace("init-feat1",g_c4_vgpu_real.features1,0,0,0);
    g_c4_vgpu_real.guest0=g_c4_vgpu_real.features0&((1u<<C4_VIRTIO_GPU_F_VIRGL)|(1u<<C4_VIRTIO_GPU_F_RESOURCE_BLOB)|(1u<<C4_VIRTIO_GPU_F_CONTEXT_INIT));
    g_c4_vgpu_real.guest1=g_c4_vgpu_real.features1&(1u<<(C4_VIRTIO_F_VERSION_1-32u));
    c4_mmio_w32(g_c4_vgpu_real.common,8,0);
    c4_mmio_w32(g_c4_vgpu_real.common,12,g_c4_vgpu_real.guest0);
    c4_mmio_w32(g_c4_vgpu_real.common,8,1);
    c4_mmio_w32(g_c4_vgpu_real.common,12,g_c4_vgpu_real.guest1);
    c4_vgpu_real_trace("init-guest",g_c4_vgpu_real.guest0,g_c4_vgpu_real.guest1,0,0);
    status=C4_VIRTIO_STATUS_ACKNOWLEDGE|C4_VIRTIO_STATUS_DRIVER|C4_VIRTIO_STATUS_FEATURES_OK;
    c4_mmio_w8(g_c4_vgpu_real.common,20,status);
    c4_vgpu_real_trace("init-feature-ok",c4_mmio_r8(g_c4_vgpu_real.common,20),status,0,0);
    if(!(c4_mmio_r8(g_c4_vgpu_real.common,20)&C4_VIRTIO_STATUS_FEATURES_OK)){
        c4_vgpu_real_trace("features-fail",g_c4_vgpu_real.features0,g_c4_vgpu_real.features1,g_c4_vgpu_real.guest0,g_c4_vgpu_real.guest1);
        return false;
    }
    c4_mmio_w16(g_c4_vgpu_real.common,22,0);
    qmax=c4_mmio_r16(g_c4_vgpu_real.common,24);
    c4_vgpu_real_trace("init-qmax",qmax,c4_mmio_r16(g_c4_vgpu_real.common,18),0,0);
    if(!qmax)return false;
    qsz=(qmax<C4_VIRTGPU_QUEUE_SIZE)?qmax:C4_VIRTGPU_QUEUE_SIZE;
    g_c4_vgpu_real.queue_size=qsz;
    g_c4_vgpu_real.notify_off=c4_mmio_r16(g_c4_vgpu_real.common,30);
    c4_vgpu_real_trace("init-qsetup",qsz,g_c4_vgpu_real.notify_off,0,0);
    ulibc_memset(g_c4_vgpu_desc,0,sizeof(g_c4_vgpu_desc));
    ulibc_memset(&g_c4_vgpu_avail,0,sizeof(g_c4_vgpu_avail));
    ulibc_memset(&g_c4_vgpu_used,0,sizeof(g_c4_vgpu_used));
    ulibc_memset(&g_c4_vgpu_resp,0,sizeof(g_c4_vgpu_resp));
    ulibc_memset(g_c4_vgpu_async_cmds,0,sizeof(g_c4_vgpu_async_cmds));
    g_c4_vgpu_async_next=0;
    c4_mmio_w16(g_c4_vgpu_real.common,24,qsz);
    c4_mmio_w64(g_c4_vgpu_real.common,32,c4_dma_addr(g_c4_vgpu_desc));
    c4_mmio_w64(g_c4_vgpu_real.common,40,c4_dma_addr(&g_c4_vgpu_avail));
    c4_mmio_w64(g_c4_vgpu_real.common,48,c4_dma_addr(&g_c4_vgpu_used));
    c4_vgpu_real_trace("init-qaddr",c4_dma_addr(g_c4_vgpu_desc),c4_dma_addr(&g_c4_vgpu_avail),c4_dma_addr(&g_c4_vgpu_used),0);
    c4_mmio_w16(g_c4_vgpu_real.common,28,1);
    c4_vgpu_real_trace("init-qenable",c4_mmio_r16(g_c4_vgpu_real.common,28),0,0,0);
    g_c4_vgpu_real.last_used_idx=g_c4_vgpu_used.idx;
    g_c4_vgpu_real.avail_idx=0;
    g_c4_vgpu_real.ctx_id=1;
    g_c4_vgpu_real.active_context_init=0;
    g_c4_vgpu_real.ctx_virgl_created=false;
    g_c4_vgpu_real.ctx_virgl2_created=false;
    g_c4_vgpu_real.ctx_venus_created=false;
    g_c4_vgpu_real.queue_probe_done=false;
    g_c4_vgpu_real.queue_probe_ok=false;
    g_c4_vgpu_real.fence_next=1;
    c4_mmio_w8(g_c4_vgpu_real.common,20,status|C4_VIRTIO_STATUS_DRIVER_OK);
    g_c4_vgpu_real.queue_ready=true;
    (void)c4_vgpu_real_probe_host_visible();
    c4_vgpu_real_trace("queue",qmax,qsz,g_c4_vgpu_real.notify_off,g_c4_vgpu_real.notify_multiplier);
    c4_vgpu_real_trace("features",g_c4_vgpu_real.features0,g_c4_vgpu_real.features1,g_c4_vgpu_real.guest0,g_c4_vgpu_real.guest1);
    return true;
}

static volatile uint16_t *c4_vgpu_real_notify_ptr(const char *tag){
    volatile uint16_t *notify;
    notify=(volatile uint16_t*)(uintptr_t)
        ((uint64_t)(uintptr_t)g_c4_vgpu_real.notify+
         (uint64_t)g_c4_vgpu_real.notify_off*
         (uint64_t)g_c4_vgpu_real.notify_multiplier);
    if(!c4_map_mmio_range((uint64_t)(uintptr_t)notify,sizeof(*notify))){
        c4_vgpu_real_trace("notify-map-fail",tag?(uint64_t)(uintptr_t)tag:0,
                            (uint64_t)(uintptr_t)notify,
                            g_c4_vgpu_real.notify_off,
                            g_c4_vgpu_real.notify_multiplier);
        return 0;
    }
    return notify;
}

static int c4_vgpu_async_slot_from_head(uint32_t head){
    uint32_t i;
    for(i=0;i<C4_VGPU_ASYNC_SLOT_COUNT;++i){
        uint32_t h=C4_VGPU_ASYNC_DESC_BASE+i*C4_VGPU_ASYNC_DESC_STRIDE;
        if(head==h)return (int)i;
    }
    return -1;
}

static void c4_vgpu_real_mark_used_id(uint32_t id){
    int slot=c4_vgpu_async_slot_from_head(id);
    if(slot>=0){
        c4_vgpu_async_cmd_t *a=&g_c4_vgpu_async_cmds[slot];
        uint32_t type=a->resp.type;
        if(a->used&&type!=C4_VIRTIO_GPU_RESP_OK_NODATA){
            c4_vgpu_real_trace("async-resp",id,type,a->bytes,a->resp.fence_id);
        }
        a->used=false;
        a->bytes=0;
    }
}

static bool c4_vgpu_real_consume_used(uint32_t *id_out){
    uint16_t used_idx;
    uint16_t used_slot;
    uint32_t id;
    __sync_synchronize();
    used_idx=g_c4_vgpu_used.idx;
    if(g_c4_vgpu_real.last_used_idx==used_idx)return false;
    used_slot=(uint16_t)(g_c4_vgpu_real.last_used_idx%g_c4_vgpu_real.queue_size);
    id=g_c4_vgpu_used.ring[used_slot].id;
    __sync_synchronize();
    g_c4_vgpu_real.last_used_idx=(uint16_t)(g_c4_vgpu_real.last_used_idx+1u);
    if(id!=C4_VGPU_SYNC_DESC_HEAD)c4_vgpu_real_mark_used_id(id);
    if(id_out)*id_out=id;
    return true;
}

static void c4_vgpu_real_drain_used(void){
    uint32_t id;
    while(c4_vgpu_real_consume_used(&id)){}
}

void drm_virtgpu_pump(void){
    if(g_c4_vgpu_real.queue_ready)
        c4_vgpu_real_drain_used();
}

static bool c4_vgpu_real_wait_for_async_slot(uint32_t spins){
    uint32_t spin;
    for(spin=0;spin<spins;++spin){
        uint32_t id;
        while(c4_vgpu_real_consume_used(&id)){
            if(c4_vgpu_async_slot_from_head(id)>=0)return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool c4_vgpu_real_wait_for_head(uint32_t head,uint32_t spins,const char *tag){
    uint32_t spin;
    for(spin=0;spin<spins;++spin){
        uint32_t id;
        while(c4_vgpu_real_consume_used(&id)){
            if(id==head)return true;
        }
        __asm__ volatile("pause");
    }
    c4_vgpu_real_trace("wait-head-timeout",tag?(uint64_t)(uintptr_t)tag:0,
                        head,g_c4_vgpu_real.avail_idx,g_c4_vgpu_real.last_used_idx);
    return false;
}

static bool c4_vgpu_real_queue_head(uint32_t head,const char *tag){
    uint16_t slot;
    volatile uint16_t *notify;
    if(!g_c4_vgpu_real.queue_size)return false;
    c4_vgpu_real_drain_used();
    if((uint16_t)(g_c4_vgpu_real.avail_idx-g_c4_vgpu_real.last_used_idx)>=g_c4_vgpu_real.queue_size)
        c4_vgpu_real_drain_used();
    if((uint16_t)(g_c4_vgpu_real.avail_idx-g_c4_vgpu_real.last_used_idx)>=g_c4_vgpu_real.queue_size)
        return false;
    slot=(uint16_t)(g_c4_vgpu_real.avail_idx%g_c4_vgpu_real.queue_size);
    g_c4_vgpu_avail.ring[slot]=(uint16_t)head;
    __sync_synchronize();
    g_c4_vgpu_avail.idx=(uint16_t)(g_c4_vgpu_real.avail_idx+1u);
    g_c4_vgpu_real.avail_idx=g_c4_vgpu_avail.idx;
    __sync_synchronize();
    notify=c4_vgpu_real_notify_ptr(tag);
    if(!notify)return false;
    *notify=0;
    return true;
}

static int c4_vgpu_real_send_async_nodata(const void *cmd,uint32_t cmd_len,
                                          const void *data,uint32_t data_len,
                                          const char *tag){
    uint32_t i,pass,total;
    static uint32_t async_force_trace;
    if(kvfs_exists("/etc/ridux-virtgpu-sync-submit.enable"))return -EAGAIN;
    if(!cmd||!cmd_len)return -EINVAL;
    total=cmd_len+data_len;
    if(data_len&&!data)return -EINVAL;
    if(total>sizeof(g_c4_vgpu_async_cmds[0].cmd))return -E2BIG;
    if(g_c4_vgpu_real.queue_size<
       C4_VGPU_ASYNC_DESC_BASE+C4_VGPU_ASYNC_SLOT_COUNT*C4_VGPU_ASYNC_DESC_STRIDE)
        return -EAGAIN;
    for(pass=0;pass<2u;++pass){
        c4_vgpu_real_drain_used();
        for(i=0;i<C4_VGPU_ASYNC_SLOT_COUNT;++i){
            uint32_t idx=(g_c4_vgpu_async_next+i)%C4_VGPU_ASYNC_SLOT_COUNT;
            c4_vgpu_async_cmd_t *a=&g_c4_vgpu_async_cmds[idx];
            uint32_t head=C4_VGPU_ASYNC_DESC_BASE+idx*C4_VGPU_ASYNC_DESC_STRIDE;
            uint32_t async_trace_id=0;
            if(a->used)continue;
            ulibc_memset(&a->resp,0,sizeof(a->resp));
            a->used=true;
            a->head=(uint16_t)head;
            a->bytes=total;
            ulibc_memcpy(a->cmd,cmd,cmd_len);
            if(data_len)ulibc_memcpy(a->cmd+cmd_len,data,data_len);
            g_c4_vgpu_desc[head].addr=c4_dma_addr(a->cmd);
            g_c4_vgpu_desc[head].len=total;
            g_c4_vgpu_desc[head].flags=C4_VIRTQ_DESC_F_NEXT;
            g_c4_vgpu_desc[head].next=(uint16_t)(head+1u);
            g_c4_vgpu_desc[head+1u].addr=c4_dma_addr(&a->resp);
            g_c4_vgpu_desc[head+1u].len=sizeof(a->resp);
            g_c4_vgpu_desc[head+1u].flags=C4_VIRTQ_DESC_F_WRITE;
            g_c4_vgpu_desc[head+1u].next=0;
            __sync_synchronize();
            if(!c4_vgpu_real_queue_head(head,tag)){
                a->used=false;
                a->bytes=0;
                return -EAGAIN;
            }
            g_c4_vgpu_async_next=(idx+1u)%C4_VGPU_ASYNC_SLOT_COUNT;
            if(c4_virtgpu_trace_enabled()&&async_force_trace<64u){
                ++async_force_trace;
                async_trace_id=async_force_trace;
                __boot_serial_force_puts("[virtgpu-async-submit] #");
                __boot_serial_force_putu32(async_trace_id);
                __boot_serial_force_puts(" tag=");
                __boot_serial_force_puts(tag?tag:"?");
                __boot_serial_force_puts(" head=");
                __boot_serial_force_putu32(head);
                __boot_serial_force_puts(" total=");
                __boot_serial_force_putu32(total);
                __boot_serial_force_puts(" avail=");
                __boot_serial_force_putu32((uint32_t)g_c4_vgpu_real.avail_idx);
                __boot_serial_force_puts(" used=");
                __boot_serial_force_putu32((uint32_t)g_c4_vgpu_real.last_used_idx);
                __boot_serial_force_puts("/");
                __boot_serial_force_putu32((uint32_t)g_c4_vgpu_used.idx);
                __boot_serial_force_puts("\n");
            }
            c4_vgpu_real_trace("async-submit",tag?(uint64_t)(uintptr_t)tag:0,
                                head,total,g_c4_vgpu_real.avail_idx);
            c4_vgpu_real_drain_used();
            if(async_trace_id){
                __boot_serial_force_puts("[virtgpu-async-drained] #");
                __boot_serial_force_putu32(async_trace_id);
                __boot_serial_force_puts(" head=");
                __boot_serial_force_putu32(head);
                __boot_serial_force_puts(" used=");
                __boot_serial_force_putu32((uint32_t)g_c4_vgpu_real.last_used_idx);
                __boot_serial_force_puts("/");
                __boot_serial_force_putu32((uint32_t)g_c4_vgpu_used.idx);
                __boot_serial_force_puts("\n");
            }
            return 0;
        }
        if(!c4_vgpu_real_wait_for_async_slot(250000u))break;
    }
    return -EAGAIN;
}

static void c4_vgpu_real_trace_submit(const char *tag,const void *cmd,
                                      uint32_t len,uint32_t desc_count,
                                      volatile uint16_t *notify){
    static uint32_t traced;
    uint32_t type=cmd?((const c4_virtio_gpu_ctrl_hdr_t*)cmd)->type:0;
    if(traced++>=64u)return;
    c4_vgpu_real_trace("send",type,len,g_c4_vgpu_real.avail_idx,g_c4_vgpu_used.idx);
    c4_vgpu_real_trace("send-tag",tag?(uint64_t)(uintptr_t)tag:0,
                        desc_count,(uint64_t)(uintptr_t)notify,
                        g_c4_vgpu_real.notify_off);
    if(desc_count>0)
        c4_vgpu_real_trace("send-desc0",g_c4_vgpu_desc[0].addr,
                            g_c4_vgpu_desc[0].len,g_c4_vgpu_desc[0].flags,
                            g_c4_vgpu_desc[0].next);
    if(desc_count>1)
        c4_vgpu_real_trace("send-desc1",g_c4_vgpu_desc[1].addr,
                            g_c4_vgpu_desc[1].len,g_c4_vgpu_desc[1].flags,
                            g_c4_vgpu_desc[1].next);
    if(desc_count>2)
        c4_vgpu_real_trace("send-desc2",g_c4_vgpu_desc[2].addr,
                            g_c4_vgpu_desc[2].len,g_c4_vgpu_desc[2].flags,
                            g_c4_vgpu_desc[2].next);
}

static int c4_vgpu_real_send(const void *cmd,uint32_t len,const char *tag){
    if(!cmd||!len)return -EINVAL;
    if(!c4_vgpu_real_init())return -ENODEV;
    if(len>sizeof(g_c4_vgpu_cmd))return -EINVAL;
    ulibc_memset(g_c4_vgpu_cmd,0,sizeof(g_c4_vgpu_cmd));
    ulibc_memset(&g_c4_vgpu_resp,0,sizeof(g_c4_vgpu_resp));
    ulibc_memcpy(g_c4_vgpu_cmd,cmd,len);
    g_c4_vgpu_desc[0].addr=c4_dma_addr(g_c4_vgpu_cmd);
    g_c4_vgpu_desc[0].len=len;
    g_c4_vgpu_desc[0].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[0].next=1;
    g_c4_vgpu_desc[1].addr=c4_dma_addr(&g_c4_vgpu_resp);
    g_c4_vgpu_desc[1].len=sizeof(g_c4_vgpu_resp);
    g_c4_vgpu_desc[1].flags=C4_VIRTQ_DESC_F_WRITE;
    g_c4_vgpu_desc[1].next=0;
    c4_vgpu_real_trace_submit(tag,cmd,len,2u,0);
    if(!c4_vgpu_real_queue_head(C4_VGPU_SYNC_DESC_HEAD,tag))return -EIO;
    if(!c4_vgpu_real_wait_for_head(C4_VGPU_SYNC_DESC_HEAD,5000000u,tag)){
        c4_vgpu_real_trace("timeout",tag?(uint64_t)(uintptr_t)tag:0,len,g_c4_vgpu_real.avail_idx,g_c4_vgpu_used.idx);
        return -ETIMEDOUT;
    }
    __sync_synchronize();
    c4_vgpu_real_trace(tag?tag:"cmd",((const c4_virtio_gpu_ctrl_hdr_t*)cmd)->type,len,g_c4_vgpu_resp.type,g_c4_vgpu_resp.fence_id);
    return (g_c4_vgpu_resp.type==C4_VIRTIO_GPU_RESP_OK_NODATA)?0:-EIO;
}

static int c4_vgpu_real_send_data(const void *cmd,uint32_t cmd_len,
                                  const void *data,uint32_t data_len,
                                  const char *tag){
    if(!cmd||!cmd_len)return -EINVAL;
    if(!data||!data_len)return c4_vgpu_real_send(cmd,cmd_len,tag);
    if(!c4_vgpu_real_init())return -ENODEV;
    if(cmd_len>sizeof(g_c4_vgpu_cmd))return -EINVAL;
    if(data_len>sizeof(g_c4_vgpu_cmd)-cmd_len)return -EINVAL;
    ulibc_memset(g_c4_vgpu_cmd,0,sizeof(g_c4_vgpu_cmd));
    ulibc_memset(&g_c4_vgpu_resp,0,sizeof(g_c4_vgpu_resp));
    ulibc_memcpy(g_c4_vgpu_cmd,cmd,cmd_len);
    ulibc_memcpy(g_c4_vgpu_cmd+cmd_len,data,data_len);
    g_c4_vgpu_desc[0].addr=c4_dma_addr(g_c4_vgpu_cmd);
    g_c4_vgpu_desc[0].len=cmd_len;
    g_c4_vgpu_desc[0].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[0].next=1;
    g_c4_vgpu_desc[1].addr=c4_dma_addr(g_c4_vgpu_cmd+cmd_len);
    g_c4_vgpu_desc[1].len=data_len;
    g_c4_vgpu_desc[1].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[1].next=2;
    g_c4_vgpu_desc[2].addr=c4_dma_addr(&g_c4_vgpu_resp);
    g_c4_vgpu_desc[2].len=sizeof(g_c4_vgpu_resp);
    g_c4_vgpu_desc[2].flags=C4_VIRTQ_DESC_F_WRITE;
    g_c4_vgpu_desc[2].next=0;
    c4_vgpu_real_trace_submit(tag,cmd,cmd_len,3u,0);
    if(!c4_vgpu_real_queue_head(C4_VGPU_SYNC_DESC_HEAD,tag))return -EIO;
    if(!c4_vgpu_real_wait_for_head(C4_VGPU_SYNC_DESC_HEAD,5000000u,tag)){
        c4_vgpu_real_trace("timeout",tag?(uint64_t)(uintptr_t)tag:0,
                            ((uint64_t)cmd_len<<32)|data_len,
                            g_c4_vgpu_real.avail_idx,g_c4_vgpu_used.idx);
        return -ETIMEDOUT;
    }
    __sync_synchronize();
    c4_vgpu_real_trace(tag?tag:"cmd-data",
                       ((const c4_virtio_gpu_ctrl_hdr_t*)cmd)->type,
                       ((uint64_t)cmd_len<<32)|data_len,
                       g_c4_vgpu_resp.type,g_c4_vgpu_resp.fence_id);
    return (g_c4_vgpu_resp.type==C4_VIRTIO_GPU_RESP_OK_NODATA)?0:-EIO;
}

static int c4_vgpu_real_send_submit3d(uint32_t hdr_len,uint32_t data_len,const char *tag){
    if(!hdr_len||!data_len||hdr_len+data_len>sizeof(g_c4_vgpu_cmd))return -EINVAL;
    if(!c4_vgpu_real_init())return -ENODEV;
    ulibc_memset(&g_c4_vgpu_resp,0,sizeof(g_c4_vgpu_resp));
    g_c4_vgpu_desc[0].addr=c4_dma_addr(g_c4_vgpu_cmd);
    g_c4_vgpu_desc[0].len=hdr_len;
    g_c4_vgpu_desc[0].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[0].next=1;
    g_c4_vgpu_desc[1].addr=c4_dma_addr(g_c4_vgpu_cmd+hdr_len);
    g_c4_vgpu_desc[1].len=data_len;
    g_c4_vgpu_desc[1].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[1].next=2;
    g_c4_vgpu_desc[2].addr=c4_dma_addr(&g_c4_vgpu_resp);
    g_c4_vgpu_desc[2].len=sizeof(g_c4_vgpu_resp);
    g_c4_vgpu_desc[2].flags=C4_VIRTQ_DESC_F_WRITE;
    g_c4_vgpu_desc[2].next=0;
    c4_vgpu_real_trace_submit(tag,g_c4_vgpu_cmd,hdr_len,3u,0);
    if(!c4_vgpu_real_queue_head(C4_VGPU_SYNC_DESC_HEAD,tag))return -EIO;
    if(!c4_vgpu_real_wait_for_head(C4_VGPU_SYNC_DESC_HEAD,5000000u,tag)){
        c4_vgpu_real_trace("timeout",tag?(uint64_t)(uintptr_t)tag:0,
                            ((uint64_t)hdr_len<<32)|data_len,
                            g_c4_vgpu_real.avail_idx,g_c4_vgpu_used.idx);
        return -ETIMEDOUT;
    }
    __sync_synchronize();
    c4_vgpu_real_trace(tag?tag:"submit3d",
                       ((const c4_virtio_gpu_ctrl_hdr_t*)g_c4_vgpu_cmd)->type,
                       ((uint64_t)hdr_len<<32)|data_len,
                       g_c4_vgpu_resp.type,g_c4_vgpu_resp.fence_id);
    return (g_c4_vgpu_resp.type==C4_VIRTIO_GPU_RESP_OK_NODATA)?0:-EIO;
}

static int c4_vgpu_real_send_response(const void *cmd,uint32_t len,void *resp,uint32_t resp_len,const char *tag){
    c4_virtio_gpu_ctrl_hdr_t *rh;
    if(!cmd||!len||!resp||resp_len<sizeof(c4_virtio_gpu_ctrl_hdr_t))return -EINVAL;
    if(!c4_vgpu_real_init())return -ENODEV;
    if(len>sizeof(g_c4_vgpu_cmd))return -EINVAL;
    ulibc_memset(g_c4_vgpu_cmd,0,sizeof(g_c4_vgpu_cmd));
    ulibc_memset(resp,0,resp_len);
    ulibc_memcpy(g_c4_vgpu_cmd,cmd,len);
    g_c4_vgpu_desc[0].addr=c4_dma_addr(g_c4_vgpu_cmd);
    g_c4_vgpu_desc[0].len=len;
    g_c4_vgpu_desc[0].flags=C4_VIRTQ_DESC_F_NEXT;
    g_c4_vgpu_desc[0].next=1;
    g_c4_vgpu_desc[1].addr=c4_dma_addr(resp);
    g_c4_vgpu_desc[1].len=resp_len;
    g_c4_vgpu_desc[1].flags=C4_VIRTQ_DESC_F_WRITE;
    g_c4_vgpu_desc[1].next=0;
    c4_vgpu_real_trace_submit(tag,cmd,len,2u,0);
    if(!c4_vgpu_real_queue_head(C4_VGPU_SYNC_DESC_HEAD,tag))return -EIO;
    if(!c4_vgpu_real_wait_for_head(C4_VGPU_SYNC_DESC_HEAD,5000000u,tag)){
        c4_vgpu_real_trace("timeout",tag?(uint64_t)(uintptr_t)tag:0,len,g_c4_vgpu_real.avail_idx,g_c4_vgpu_used.idx);
        return -ETIMEDOUT;
    }
    __sync_synchronize();
    rh=(c4_virtio_gpu_ctrl_hdr_t*)resp;
    c4_vgpu_real_trace(tag?tag:"cmd",((const c4_virtio_gpu_ctrl_hdr_t*)cmd)->type,len,rh->type,rh->fence_id);
    return (int)rh->type;
}

static bool c4_vgpu_real_probe_queue_once(void){
    c4_virtio_gpu_ctrl_hdr_t cmd;
    int type;
    if(g_c4_vgpu_real.queue_probe_done)return g_c4_vgpu_real.queue_probe_ok;
    g_c4_vgpu_real.queue_probe_done=true;
    g_c4_vgpu_real.queue_probe_ok=false;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.type=C4_VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    type=c4_vgpu_real_send_response(&cmd,sizeof(cmd),g_c4_vgpu_resp_big,
                                    512u,"display-info");
    c4_vgpu_real_trace("display-info-probe",(uint64_t)(int64_t)type,
                        g_c4_vgpu_resp_big[0],g_c4_vgpu_used.idx,
                        g_c4_vgpu_real.last_used_idx);
    if((uint32_t)type==C4_VIRTIO_GPU_RESP_OK_DISPLAY_INFO){
        g_c4_vgpu_real.queue_probe_ok=true;
        return true;
    }
    return false;
}

static int c4_vgpu_real_get_capset(uint32_t id,uint32_t version,void *addr,uint32_t size){
    c4_virtio_gpu_get_capset_t cmd;
    static uint32_t cap_trace;
    uint32_t resp_len;
    uint32_t first=0,second=0;
    int type;
    if(!addr||!size)return -EINVAL;
    if(size>C4_VIRTGPU_CAPSET_RESP_MAX)return -ENOSPC;
    ulibc_memset(&cmd,0,sizeof(cmd));
    ulibc_memset(addr,0,size);
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_GET_CAPSET;
    cmd.capset_id=id;
    cmd.capset_version=version;
    resp_len=(uint32_t)sizeof(c4_virtio_gpu_ctrl_hdr_t)+size;
    type=c4_vgpu_real_send_response(&cmd,sizeof(cmd),g_c4_vgpu_resp_big,resp_len,"get-capset-real");
    if(type<0)return type;
    if((uint32_t)type!=C4_VIRTIO_GPU_RESP_OK_CAPSET){
        if(cap_trace<32u){
            ++cap_trace;
            __boot_serial_force_puts("[virtgpu-capset] #");
            __boot_serial_force_putu32(cap_trace);
            __boot_serial_force_puts(" id=");
            __boot_serial_force_putu32(id);
            __boot_serial_force_puts(" ver=");
            __boot_serial_force_putu32(version);
            __boot_serial_force_puts(" size=");
            __boot_serial_force_putu32(size);
            __boot_serial_force_puts(" type=");
            __boot_serial_force_puthex64((uint64_t)(uint32_t)type);
            __boot_serial_force_puts(" rc=-EIO\n");
        }
        return -EIO;
    }
    ulibc_memcpy(addr,g_c4_vgpu_resp_big+sizeof(c4_virtio_gpu_ctrl_hdr_t),size);
    if(size>=4u)first=*(uint32_t*)addr;
    if(size>=8u)second=((uint32_t*)addr)[1];
    if(cap_trace<32u){
        ++cap_trace;
        __boot_serial_force_puts("[virtgpu-capset] #");
        __boot_serial_force_putu32(cap_trace);
        __boot_serial_force_puts(" id=");
        __boot_serial_force_putu32(id);
        __boot_serial_force_puts(" ver=");
        __boot_serial_force_putu32(version);
        __boot_serial_force_puts(" size=");
        __boot_serial_force_putu32(size);
        __boot_serial_force_puts(" first=");
        __boot_serial_force_puthex64(first);
        __boot_serial_force_puts(" second=");
        __boot_serial_force_puthex64(second);
        __boot_serial_force_puts(" rc=0\n");
    }
    return 0;
}

static bool c4_virtgpu_virgl2_enabled(void){
    return kvfs_exists("/etc/ridux-virtgpu-virgl2.enable");
}

static bool c4_virtgpu_venus_enabled(void){
    return kvfs_exists("/etc/ridux-virtgpu-venus.enable");
}

static bool c4_virtgpu_venus_available(void){
    return c4_virtgpu_venus_enabled()&&
           c4_vgpu_real_init()&&
           ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_RESOURCE_BLOB))!=0u)&&
           ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_CONTEXT_INIT))!=0u)&&
           c4_vgpu_real_probe_host_visible();
}

static bool c4_virtgpu_host_capset_available(uint32_t id,uint32_t version,
                                             uint32_t probe_size){
    static int virgl2_cached=-1;
    uint8_t scratch[0x580];
    int rc;
    int *cached=0;
    if(!probe_size||probe_size>sizeof(scratch))return false;
    if(id==C4_VIRTGPU_CAPSET_VIRGL2&&version==0&&probe_size==0x580u)
        cached=&virgl2_cached;
    if(cached&&*cached>=0)return *cached!=0;
    if(!c4_vgpu_real_init()||
       ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_VIRGL))==0u)){
        if(cached)*cached=0;
        return false;
    }
    rc=c4_vgpu_real_get_capset(id,version,scratch,probe_size);
    if(cached)*cached=(rc==0)?1:0;
    c4_virtgpu_trace_u64("host-capset-probe",((uint64_t)version<<32)|id,
                          probe_size,(uint64_t)(int64_t)rc,
                          cached?(uint64_t)(uint32_t)*cached:0);
    return rc==0;
}

static uint64_t c4_virtgpu_supported_capset_mask(void){
    uint64_t mask=1ULL<<C4_VIRTGPU_CAPSET_VIRGL;
    if(c4_virtgpu_virgl2_enabled()||
       c4_virtgpu_host_capset_available(C4_VIRTGPU_CAPSET_VIRGL2,0,0x580u))
        mask|=1ULL<<C4_VIRTGPU_CAPSET_VIRGL2;
    if(c4_virtgpu_venus_available())
        mask|=1ULL<<C4_VIRTGPU_CAPSET_VENUS;
    return mask;
}

static uint32_t c4_vgpu_real_preferred_virgl_context_init(void){
    if(!c4_vgpu_real_init())return 0;
    if(g_c4_vgpu_real.active_context_init==C4_VIRTGPU_CAPSET_VIRGL2)
        return C4_VIRTGPU_CAPSET_VIRGL2;
    if((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_CONTEXT_INIT))&&
       (c4_virtgpu_virgl2_enabled()||
        c4_virtgpu_host_capset_available(C4_VIRTGPU_CAPSET_VIRGL2,0,0x580u)))
        return C4_VIRTGPU_CAPSET_VIRGL2;
    return 0;
}

static uint32_t c4_vgpu_context_init_for_ctx_id(uint32_t ctx_id){
    if(ctx_id==2u)return C4_VIRTGPU_CAPSET_VENUS;
    if(ctx_id==3u)return C4_VIRTGPU_CAPSET_VIRGL2;
    return 0;
}

static int c4_vgpu_real_create_context(uint32_t context_init){
    c4_virtio_gpu_ctx_create_t cmd;
    uint32_t ctx_id;
    bool *created;
    int rc;
    if(context_init==C4_VIRTGPU_CAPSET_VIRGL)context_init=0;
    if(context_init&&
       ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_CONTEXT_INIT))==0u))
        context_init=0;
    if(context_init==C4_VIRTGPU_CAPSET_VIRGL2){
        bool virgl2_supported=c4_virtgpu_virgl2_enabled()||
            c4_virtgpu_host_capset_available(C4_VIRTGPU_CAPSET_VIRGL2,0,0x580u);
        if(!virgl2_supported){
            c4_virtgpu_trace_u64("ctx-capset-clamp",context_init,
                                  C4_VIRTGPU_CAPSET_VIRGL,0,0);
            context_init=0;
        }
    }
    if(context_init==C4_VIRTGPU_CAPSET_VENUS&&!c4_virtgpu_venus_available()){
        c4_virtgpu_trace_u64("ctx-venus-unavailable",context_init,
                              g_c4_vgpu_real.features0,
                              g_c4_vgpu_real.host_visible_ready,
                              g_c4_vgpu_real.host_visible_len);
        return -ENODEV;
    }
    if(context_init!=0&&context_init!=C4_VIRTGPU_CAPSET_VIRGL2&&
       context_init!=C4_VIRTGPU_CAPSET_VENUS){
        c4_virtgpu_trace_u64("ctx-capset-unsupported",context_init,0,0,0);
        return -EINVAL;
    }
    if(context_init==C4_VIRTGPU_CAPSET_VENUS){
        ctx_id=2u;
        created=&g_c4_vgpu_real.ctx_venus_created;
    }else if(context_init==C4_VIRTGPU_CAPSET_VIRGL2){
        ctx_id=3u;
        created=&g_c4_vgpu_real.ctx_virgl2_created;
    }else{
        ctx_id=1u;
        created=&g_c4_vgpu_real.ctx_virgl_created;
    }
    if(*created){
        g_c4_vgpu_real.ctx_id=ctx_id;
        g_c4_vgpu_real.active_context_init=context_init;
        return 0;
    }
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_CTX_CREATE;
    cmd.hdr.ctx_id=ctx_id;
    cmd.nlen=5;
    cmd.context_init=context_init;
    ulibc_memcpy(cmd.debug_name,"ridux",5);
    rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"ctx-create");
    if(rc==0){
        *created=true;
        g_c4_vgpu_real.ctx_id=ctx_id;
        g_c4_vgpu_real.active_context_init=context_init;
    }
    return rc;
}

static int c4_vgpu_real_ctx_attach_bo(c4_drm_bo_t *bo){
    c4_virtio_gpu_ctx_resource_t cmd;
    uint32_t context_init;
    uint32_t ctx_id;
    int rc;
    if(!bo||!bo->real_created)return -EINVAL;
    context_init=bo->real_ctx_id?
        c4_vgpu_context_init_for_ctx_id(bo->real_ctx_id):
        g_c4_vgpu_real.active_context_init;
    if(!context_init)
        context_init=c4_vgpu_real_preferred_virgl_context_init();
    rc=c4_vgpu_real_create_context(context_init);
    if(rc<0)return rc;
    ctx_id=g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u;
    if(bo->real_ctx_attached&&bo->real_ctx_id==ctx_id)return 0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    cmd.hdr.ctx_id=ctx_id;
    cmd.resource_id=bo->handle;
    rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"ctx-attach");
    if(rc==0){
        bo->real_ctx_attached=true;
        bo->real_ctx_id=ctx_id;
    }
    return rc;
}

static int c4_vgpu_real_set_scanout_bo(c4_drm_bo_t *bo){
    static uint32_t current_resource_id;
    c4_virtio_gpu_set_scanout_t cmd;
    uint32_t w,h;
    if(!bo||!bo->real_created)return -EINVAL;
    w=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    h=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    if(!w||!h)return -EINVAL;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.rect.width=w;
    cmd.rect.height=h;
    cmd.scanout_id=0;
    cmd.resource_id=bo->handle;
    if(current_resource_id==bo->handle)return 0;
    {
        int rc=c4_vgpu_real_send_async_nodata(&cmd,sizeof(cmd),0,0,"set-scanout");
        if(rc==-EAGAIN)rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"set-scanout");
        if(rc==0)current_resource_id=bo->handle;
        return rc;
    }
}

static int c4_vgpu_real_flush_bo(c4_drm_bo_t *bo){
    c4_virtio_gpu_resource_flush_t cmd;
    uint32_t w,h;
    if(!bo||!bo->real_created)return -EINVAL;
    w=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    h=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    if(!w||!h)return -EINVAL;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd.rect.width=w;
    cmd.rect.height=h;
    cmd.resource_id=bo->handle;
    {
        int rc=c4_vgpu_real_send_async_nodata(&cmd,sizeof(cmd),0,0,"res-flush");
        if(rc==-EAGAIN)rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"res-flush");
        return rc;
    }
}

static bool c4_vgpu_bo_matches_scanout(c4_drm_bo_t *bo){
    uint32_t out_w=(g_fb.width?g_fb.width:1024u);
    uint32_t out_h=(g_fb.height?g_fb.height:768u);
    uint32_t w,h;
    if(!bo||!bo->real_created)return false;
    w=bo->width?bo->width:out_w;
    h=bo->height?bo->height:out_h;
    return w==out_w&&h==out_h;
}

static bool c4_vgpu_real_present_scanout_candidate(uint32_t handle,const char *tag){
    static uint32_t readback_trace;
    c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
    int scan_rc,flush_rc;
    bool probe=c4_drm_scanout_probe_enabled();
    bool probed=false,visible=false;
    if(!c4_vgpu_bo_matches_scanout(bo))return false;
    scan_rc=c4_vgpu_real_set_scanout_bo(bo);
    flush_rc=(scan_rc==0)?c4_vgpu_real_flush_bo(bo):scan_rc;
    c4_virtgpu_trace_u64(tag?tag:"exec-scanout",handle,
                          ((uint64_t)(uint32_t)scan_rc<<32)|(uint32_t)flush_rc,
                          ((uint64_t)bo->width<<32)|bo->height,
                          bo->size);
    if(scan_rc==0&&flush_rc==0&&!probe){
        static uint32_t direct_trace;
        if(direct_trace<64u){
            ++direct_trace;
            c4_virtgpu_trace_u64("exec-scanout-real",handle,
                                  ((uint64_t)direct_trace<<32)|bo->fb_id,
                                  ((uint64_t)bo->width<<32)|bo->height,
                                  bo->size);
        }
        return true;
    }
    if(scan_rc==0&&flush_rc==0&&probe&&readback_trace<8u){
        uint32_t nz=0;
        uint64_t sum=0,p0=0,p1=0;
        probed=true;
        ++readback_trace;
        c4_drm_refresh_bo_from_gpu(bo,"exec-scanout-readback");
        c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
        visible=(nz!=0u);
        c4_virtgpu_trace_u64("exec-scanout-visible",handle,
                              ((uint64_t)readback_trace<<32)|nz,
                              sum,p0^p1);
    }
    if(probed&&visible){
        g_c4_vgpu_last_visible_scanout_resource=handle;
    }else if(probed&&!visible&&g_c4_vgpu_last_visible_scanout_resource&&
             g_c4_vgpu_last_visible_scanout_resource!=handle){
        c4_drm_bo_t *prev=c4_drm_find_bo_by_handle(g_c4_vgpu_last_visible_scanout_resource);
        if(prev&&c4_vgpu_bo_matches_scanout(prev)){
            int restore_scan=c4_vgpu_real_set_scanout_bo(prev);
            int restore_flush=(restore_scan==0)?c4_vgpu_real_flush_bo(prev):restore_scan;
            c4_virtgpu_trace_u64("exec-scanout-restore",
                                  g_c4_vgpu_last_visible_scanout_resource,
                                  handle,
                                  ((uint64_t)(uint32_t)restore_scan<<32)|
                                      (uint32_t)restore_flush,
                                  ((uint64_t)prev->width<<32)|prev->height);
        }
    }
    return scan_rc==0&&flush_rc==0&&(!probed||visible);
}

static bool c4_vgpu_real_present_visible_candidate(uint32_t handle,const char *tag){
    static uint32_t readback_trace;
    c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
    int scan_rc,flush_rc;
    uint32_t nz=0;
    uint64_t sum=0,p0=0,p1=0;
    if(!bo||!bo->real_created)return false;
    if(!c4_drm_scanout_probe_enabled())return false;
    if(readback_trace>=16u)return false;
    ++readback_trace;
    c4_drm_refresh_bo_from_gpu(bo,"exec-candidate-readback");
    c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
    c4_virtgpu_trace_u64(tag?tag:"exec-candidate-visible",handle,
                          ((uint64_t)readback_trace<<32)|nz,
                          ((uint64_t)bo->width<<32)|bo->height,
                          sum^p0^p1);
    if(!nz)return false;
    scan_rc=c4_vgpu_real_set_scanout_bo(bo);
    flush_rc=(scan_rc==0)?c4_vgpu_real_flush_bo(bo):scan_rc;
    c4_virtgpu_trace_u64("exec-candidate-scanout",handle,
                          ((uint64_t)(uint32_t)scan_rc<<32)|(uint32_t)flush_rc,
                          ((uint64_t)bo->width<<32)|bo->height,
                          bo->size);
    if(scan_rc==0&&flush_rc==0)g_c4_vgpu_last_visible_scanout_resource=handle;
    return scan_rc==0&&flush_rc==0;
}

static void c4_vgpu_real_present_exec_scanout(void){
    uint32_t i,best=0,best_handle=0;
    c4_drm_bo_t *current=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
    if(current&&c4_vgpu_real_present_scanout_candidate(current->handle,
                                                       "exec-scanout-current"))
        return;
    if(c4_vgpu_real_present_scanout_candidate(g_c4_vgpu_last_fb_resource,"exec-scanout-fb"))
        return;
    if(c4_vgpu_real_present_scanout_candidate(g_c4_vgpu_last_render_resource,"exec-scanout-render"))
        return;
    for(i=0;i<(uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));++i){
        uint32_t idx=(g_c4_vgpu_present_candidate_next+
                      (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]))-1u-i)%
                     (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));
        if(c4_vgpu_real_present_scanout_candidate(g_c4_vgpu_present_candidates[idx],
                                                  "exec-scanout-candidate"))
            return;
    }
    if(c4_vgpu_real_present_visible_candidate(g_c4_vgpu_last_fb_resource,
                                              "exec-fb-visible"))
        return;
    if(c4_vgpu_real_present_visible_candidate(g_c4_vgpu_last_render_resource,
                                              "exec-render-visible"))
        return;
    for(i=0;i<(uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));++i){
        uint32_t idx=(g_c4_vgpu_present_candidate_next+
                      (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]))-1u-i)%
                     (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));
        if(c4_vgpu_real_present_visible_candidate(g_c4_vgpu_present_candidates[idx],
                                                  "exec-candidate-visible"))
            return;
    }
    for(i=0;i<C4_DRM_MAX_BO;++i){
        c4_drm_bo_t *bo=&g_c4_drm_bos[i];
        if(!c4_vgpu_bo_matches_scanout(bo))continue;
        if(!best||bo->handle>best_handle){
            best=i+1u;
            best_handle=bo->handle;
        }
    }
    if(best)
        (void)c4_vgpu_real_present_scanout_candidate(g_c4_drm_bos[best-1u].handle,
                                                     "exec-scanout-latest");
}

static int c4_vgpu_real_attach_bo(c4_drm_bo_t *bo){
    c4_virtio_gpu_resource_attach_backing_hdr_t cmd;
    c4_virtio_gpu_mem_entry_t entry;
    int rc;
    if(!bo||!bo->data||!bo->alloc_size)return -EINVAL;
    if(bo->real_backed)return 0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    ulibc_memset(&entry,0,sizeof(entry));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd.resource_id=bo->handle;
    cmd.nr_entries=1;
    entry.addr=c4_dma_addr(bo->data);
    entry.length=(uint32_t)bo->alloc_size;
    c4_virtgpu_trace_u64("attach-req",bo->handle,entry.addr,entry.length,bo->phys);
    rc=c4_vgpu_real_send_data(&cmd,sizeof(cmd),&entry,sizeof(entry),"attach");
    if(rc==0)bo->real_backed=true;
    return rc;
}

static int c4_vgpu_real_resource_create_2d(c4_drm_bo_t *bo){
    c4_virtio_gpu_resource_create_2d_t cmd;
    int rc;
    if(!bo)return -EINVAL;
    if(bo->real_created)return 0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.resource_id=bo->handle;
    cmd.format=2u;
    cmd.width=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    cmd.height=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"res2d");
    if(rc==0){
        bo->real_created=true;
        rc=c4_vgpu_real_attach_bo(bo);
    }
    return rc;
}

static int c4_vgpu_real_transfer_2d_to_host(c4_drm_bo_t *bo){
    c4_virtio_gpu_transfer_to_host_2d_t cmd;
    uint32_t w,h;
    if(!bo||!bo->real_created)return -EINVAL;
    w=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    h=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    if(!w||!h)return -EINVAL;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.hdr.flags=C4_VIRTIO_GPU_FLAG_FENCE;
    cmd.hdr.fence_id=g_c4_vgpu_real.fence_next++;
    cmd.rect.width=w;
    cmd.rect.height=h;
    cmd.resource_id=bo->handle;
    return c4_vgpu_real_send(&cmd,sizeof(cmd),"xfer2d-to");
}

static int c4_vgpu_real_resource_create_3d(c4_drm_bo_t *bo,const c4_virtgpu_resource_create_t *r){
    c4_virtio_gpu_resource_create_3d_t cmd;
    int rc;
    if(!bo||!r)return -EINVAL;
    if(bo->real_created)return 0;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    cmd.resource_id=bo->handle;
    cmd.target=r->target;
    cmd.format=r->format;
    cmd.bind=r->bind;
    cmd.width=r->width?r->width:bo->width;
    cmd.height=r->height?r->height:bo->height;
    cmd.depth=r->depth?r->depth:1u;
    cmd.array_size=r->array_size?r->array_size:1u;
    cmd.last_level=r->last_level;
    cmd.nr_samples=r->nr_samples;
    cmd.flags=r->flags;
    bo->virtgpu_target=r->target;
    bo->virtgpu_format=r->format;
    bo->virtgpu_bind=r->bind;
    bo->virtgpu_flags=r->flags;
    rc=c4_vgpu_real_create_context(c4_vgpu_real_preferred_virgl_context_init());
    if(rc<0)return rc;
    bo->real_ctx_id=g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u;
    rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"res3d");
    if(rc==0){
        bo->real_created=true;
        rc=c4_vgpu_real_attach_bo(bo);
        if(rc==0)rc=c4_vgpu_real_ctx_attach_bo(bo);
    }
    return rc;
}

static int c4_vgpu_real_map_blob_bo(c4_drm_bo_t *bo){
    c4_virtio_gpu_resource_map_blob_t cmd;
    c4_virtio_gpu_resp_map_info_t resp;
    uint64_t off,phys;
    int type;
    if(!bo||!bo->real_created)return -EINVAL;
    if(bo->real_mmio_backed)return 0;
    if(!c4_vgpu_real_probe_host_visible())return -ENODEV;
    off=c4_vgpu_real_alloc_host_visible(bo->alloc_size);
    if(off==~0ULL)return -ENOMEM;
    if(off>g_c4_vgpu_real.host_visible_len||
       bo->alloc_size>g_c4_vgpu_real.host_visible_len-off)
        return -ENOMEM;
    ulibc_memset(&cmd,0,sizeof(cmd));
    ulibc_memset(&resp,0,sizeof(resp));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB;
    cmd.hdr.ctx_id=bo->real_ctx_id?bo->real_ctx_id:
        (g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u);
    cmd.resource_id=bo->handle;
    cmd.offset=off;
    type=c4_vgpu_real_send_response(&cmd,sizeof(cmd),&resp,sizeof(resp),
                                    "blob-map-real");
    if((uint32_t)type!=C4_VIRTIO_GPU_RESP_OK_MAP_INFO){
        c4_virtgpu_trace_u64("blob-map-fail",bo->handle,off,
                              (uint64_t)(int64_t)type,
                              g_c4_vgpu_real.host_visible_len);
        return (type<0)?type:-EIO;
    }
    phys=g_c4_vgpu_real.host_visible_base+off;
    if(!c4_map_mmio_range(phys,(uint32_t)bo->alloc_size)){
        c4_virtgpu_trace_u64("blob-map-mmio-fail",bo->handle,phys,
                              bo->alloc_size,resp.map_info);
        return -EIO;
    }
    c4_drm_free_storage(bo);
    bo->data=(uint8_t*)(uintptr_t)phys;
    bo->phys=phys;
    bo->pmm_backed=false;
    bo->real_mmio_backed=true;
    bo->real_host_visible_offset=off;
    bo->real_map_info=resp.map_info;
    bo->real_backed=true;
    c4_virtgpu_trace_u64("blob-map",bo->handle,phys,off,resp.map_info);
    return 0;
}

static int c4_vgpu_real_execbuffer(const c4_virtgpu_execbuffer_t *e);

static int c4_vgpu_real_submit_blob_cmd(const c4_virtgpu_resource_create_blob_t *b){
    c4_virtgpu_execbuffer_t e;
    static uint32_t blob_cmd_trace;
    int rc;
    if(!b||!b->cmd_size)return 0;
    if(!b->cmd||(b->cmd_size&3u))return -EINVAL;
    ulibc_memset(&e,0,sizeof(e));
    e.command=b->cmd;
    e.size=b->cmd_size;
    e.fence_fd=-1;
    rc=c4_vgpu_real_execbuffer(&e);
    if(blob_cmd_trace<32u){
        ++blob_cmd_trace;
        __boot_serial_force_puts("[virtgpu-blob-cmd] #");
        __boot_serial_force_putu32(blob_cmd_trace);
        __boot_serial_force_puts(" size=");
        __boot_serial_force_putu32(b->cmd_size);
        __boot_serial_force_puts(" blob=");
        __boot_serial_force_puthex64(b->blob_id);
        __boot_serial_force_puts(" rc=");
        __boot_serial_force_puthex64((uint64_t)(int64_t)rc);
        __boot_serial_force_puts("\n");
    }
    return rc;
}

static int c4_vgpu_real_resource_create_blob(c4_drm_bo_t *bo,const c4_virtgpu_resource_create_blob_t *b){
    c4_virtio_gpu_resource_create_blob_hdr_t cmd;
    c4_virtio_gpu_mem_entry_t entry;
    c4_virtgpu_resource_create_blob_t *rw=(c4_virtgpu_resource_create_blob_t*)b;
    uint32_t blob_mem;
    int rc;
    if(!bo||!b)return -EINVAL;
    if(bo->real_created)return 0;
    blob_mem=b->blob_mem;
    if(blob_mem==2u&&(b->blob_flags&1u)&&!c4_vgpu_real_probe_host_visible())
        blob_mem=3u;
    if(blob_mem!=1u){
        rc=c4_vgpu_real_create_context(g_c4_vgpu_real.active_context_init);
        if(rc<0)return rc;
    }
    rc=c4_vgpu_real_submit_blob_cmd(b);
    if(rc<0)return rc;
    ulibc_memset(&cmd,0,sizeof(cmd));
    ulibc_memset(&entry,0,sizeof(entry));
    cmd.hdr.type=C4_VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB;
    cmd.hdr.ctx_id=g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u;
    cmd.resource_id=bo->handle;
    cmd.blob_mem=blob_mem;
    cmd.blob_flags=b->blob_flags;
    cmd.nr_entries=(blob_mem==2u)?0u:1u;
    cmd.blob_id=b->blob_id;
    cmd.size=b->size;
    entry.addr=c4_dma_addr(bo->data);
    entry.length=(uint32_t)bo->alloc_size;
    bo->real_ctx_id=cmd.hdr.ctx_id;
    c4_virtgpu_trace_u64("blob-req",bo->handle,entry.addr,
                          ((uint64_t)cmd.nr_entries<<32)|entry.length,
                          ((uint64_t)blob_mem<<32)|cmd.blob_flags);
    rc=cmd.nr_entries?
        c4_vgpu_real_send_data(&cmd,sizeof(cmd),&entry,sizeof(entry),"blob-real"):
        c4_vgpu_real_send(&cmd,sizeof(cmd),"blob-real");
    if(rc==0){
        bo->real_created=true;
        bo->real_backed=true;
        bo->real_blob_mem=blob_mem;
        rw->blob_mem=blob_mem;
        if((b->blob_flags&1u)&&(blob_mem==2u||blob_mem==3u))
            rc=c4_vgpu_real_map_blob_bo(bo);
        if(rc==0)rc=c4_vgpu_real_ctx_attach_bo(bo);
    }
    return rc;
}

static int c4_vgpu_real_transfer_3d(const c4_virtgpu_3d_transfer_t *t,bool to_host){
    c4_virtio_gpu_transfer_3d_t cmd;
    c4_drm_bo_t *bo;
    bool host3d_blob;
    if(!t)return -EINVAL;
    bo=c4_drm_find_bo_by_handle(t->bo_handle);
    if(!bo)return -EINVAL;
    if(!bo->real_created)return 0;
    host3d_blob=(bo->real_blob_mem==2u||bo->real_blob_mem==3u);
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.hdr.type=to_host?C4_VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D:C4_VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D;
    cmd.hdr.flags=C4_VIRTIO_GPU_FLAG_FENCE;
    cmd.hdr.fence_id=g_c4_vgpu_real.fence_next++;
    cmd.hdr.ctx_id=bo->real_ctx_id?bo->real_ctx_id:
        (g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u);
    cmd.resource_id=bo->handle;
    cmd.level=t->level;
    cmd.box=t->box;
    if(!cmd.box.w)cmd.box.w=bo->width;
    if(!cmd.box.h)cmd.box.h=bo->height;
    if(!cmd.box.d)cmd.box.d=1;
    cmd.offset=t->offset;
    if(host3d_blob){
        cmd.stride=t->stride?t->stride:bo->pitch;
        cmd.layer_stride=t->layer_stride?t->layer_stride:(bo->pitch*bo->height);
    }else{
        /* Match Linux virtio-gpu: classic virgl resources leave these zero.
         * Supplying invented pitches here can make virgl accept transfers
         * while interpreting buffer uploads/readbacks with the wrong layout. */
        cmd.stride=0;
        cmd.layer_stride=0;
    }
    {
        static uint32_t xfer_detail_trace;
        uint32_t before_nz=0,after_nz=0;
        uint64_t before_p0=0,after_p0=0,after_p1=0;
        int rc;
        bool trace=c4_virtgpu_trace_enabled()&&
                   kvfs_exists("/etc/ridux-virtgpu-xfer-trace.enable")&&
                   xfer_detail_trace<80u;
        if(trace&&bo->data)c4_drm_sample_bo(bo,&before_nz,0,&before_p0,0);
        rc=to_host?c4_vgpu_real_send_async_nodata(&cmd,sizeof(cmd),0,0,"xfer-to"):
                   c4_vgpu_real_send(&cmd,sizeof(cmd),"xfer-from");
        if(to_host&&rc==-EAGAIN)rc=c4_vgpu_real_send(&cmd,sizeof(cmd),"xfer-to");
        if(trace){
            if(!to_host&&bo->data)c4_drm_sample_bo(bo,&after_nz,0,&after_p0,&after_p1);
            ++xfer_detail_trace;
            c4_virtgpu_trace_u64(to_host?"xfer3d-to-box":"xfer3d-from-box",
                                  bo->handle,
                                  ((uint64_t)cmd.box.x<<32)|cmd.box.y,
                                  ((uint64_t)cmd.box.w<<32)|cmd.box.h,
                                  ((uint64_t)cmd.level<<32)|(uint32_t)cmd.offset);
            c4_virtgpu_trace_u64(to_host?"xfer3d-to-state":"xfer3d-from-state",
                                  ((uint64_t)cmd.stride<<32)|cmd.layer_stride,
                                  (uint64_t)(int64_t)rc,
                                  ((uint64_t)before_nz<<32)|after_nz,
                                  before_p0^after_p0^after_p1);
        }
        return rc;
    }
}

static int c4_vgpu_real_wait_ioctl(const c4_virtgpu_3d_wait_t *w){
    c4_drm_bo_t *bo;
    uint32_t spin;
    uint32_t flags;
    if(!w)return -EINVAL;
    bo=c4_drm_find_bo_by_handle(w->handle);
    if(!bo)return -ENOENT;
    flags=w->flags;
    __sync_synchronize();
    if(!c4_virtgpu_strict_wait_enabled()){
        c4_vgpu_real_drain_used();
        c4_virtgpu_trace_u64((flags&C4_VIRTGPU_WAIT_NOWAIT)?"wait-fast-nowait":"wait-fast",
                              w->handle,flags,
                              g_c4_vgpu_real.last_used_idx,
                              g_c4_vgpu_real.avail_idx);
        return 0;
    }
    if(g_c4_vgpu_real.last_used_idx!=g_c4_vgpu_real.avail_idx){
        if(flags&C4_VIRTGPU_WAIT_NOWAIT){
            c4_virtgpu_trace_u64("wait-busy",w->handle,flags,
                                  g_c4_vgpu_real.last_used_idx,
                                  g_c4_vgpu_real.avail_idx);
            return -EBUSY;
        }
        for(spin=0;spin<5000000u;++spin){
            c4_vgpu_real_drain_used();
            if(g_c4_vgpu_real.last_used_idx==g_c4_vgpu_real.avail_idx)break;
            __asm__ volatile("pause");
        }
        if(g_c4_vgpu_real.last_used_idx!=g_c4_vgpu_real.avail_idx){
            c4_virtgpu_trace_u64("wait-timeout",w->handle,flags,
                                  g_c4_vgpu_real.last_used_idx,
                                  g_c4_vgpu_real.avail_idx);
            return -EBUSY;
        }
    }
    __sync_synchronize();
    c4_virtgpu_trace_u64("wait-real",w->handle,flags,
                          g_c4_vgpu_real.last_used_idx,
                          g_c4_vgpu_real.avail_idx);
    return 0;
}

static void c4_vgpu_remember_present_candidate(uint32_t handle){
    uint32_t i;
    if(!handle)return;
    g_c4_vgpu_last_render_resource=handle;
    for(i=0;i<(uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));++i){
        if(g_c4_vgpu_present_candidates[i]==handle)return;
    }
    g_c4_vgpu_present_candidates[g_c4_vgpu_present_candidate_next++%
        (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]))]=handle;
}

static void c4_vgpu_real_upload_exec_bos(const c4_virtgpu_execbuffer_t *e){
    static uint32_t upload_trace;
    const uint32_t *handles;
    uint32_t i,limit;
    if(!e||!e->bo_handles||!e->num_bo_handles)return;
    if(kvfs_exists("/etc/ridux-virtgpu-exec-upload.disable")){
        if(upload_trace<8u){
            ++upload_trace;
            c4_virtgpu_trace_u64("exec-upload-disabled",e->num_bo_handles,
                                  e->size,e->flags,e->ring_idx);
        }
        return;
    }
    handles=(const uint32_t*)(uintptr_t)e->bo_handles;
    limit=e->num_bo_handles<8u?e->num_bo_handles:8u;
    for(i=0;i<limit;++i){
        c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handles[i]);
        c4_virtgpu_3d_transfer_t t;
        uint32_t nz=0;
        uint64_t sum=0,p0=0,p1=0;
        int rc;
        if(!bo||!bo->real_created||!bo->data||!bo->size)continue;
        c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
        if(!nz)continue;
        if(bo->virtgpu_bind&2u){
            if(upload_trace<64u){
                ++upload_trace;
                c4_virtgpu_trace_u64("exec-upload-skip-rt",bo->handle,
                                      bo->virtgpu_bind,
                                      ((uint64_t)bo->width<<32)|bo->height,
                                      p0);
            }
            continue;
        }
        ulibc_memset(&t,0,sizeof(t));
        t.bo_handle=bo->handle;
        t.box.w=bo->width?bo->width:(uint32_t)bo->size;
        t.box.h=bo->height?bo->height:1u;
        t.box.d=1u;
        if(bo->real_blob_mem==2u||bo->real_blob_mem==3u){
            t.stride=bo->pitch?bo->pitch:t.box.w;
            t.layer_stride=t.stride*t.box.h;
        }
        rc=c4_vgpu_real_transfer_3d(&t,true);
        if(upload_trace<64u){
            ++upload_trace;
            c4_virtgpu_trace_u64("exec-upload",bo->handle,
                                  ((uint64_t)(uint32_t)rc<<32)|nz,
                                  ((uint64_t)t.box.w<<32)|t.box.h,
                                  p0);
        }
    }
}

static int c4_vgpu_real_attach_exec_bos(const c4_virtgpu_execbuffer_t *e){
    const uint32_t *handles;
    uint32_t i,limit;
    if(!e||!e->bo_handles||!e->num_bo_handles)return 0;
    handles=(const uint32_t*)(uintptr_t)e->bo_handles;
    limit=e->num_bo_handles<32u?e->num_bo_handles:32u;
    for(i=0;i<limit;++i){
        c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handles[i]);
        int rc;
        if(!bo||!bo->real_created)continue;
        rc=c4_vgpu_real_ctx_attach_bo(bo);
        if(rc<0)return rc;
    }
    return 0;
}

static void c4_virgl_trace_copied_submit(const uint8_t *data,uint32_t size,
                                         const c4_virtgpu_execbuffer_t *e){
    static uint32_t trace_count;
    static uint32_t surf_handles[128];
    static uint32_t surf_resources[128];
    static uint32_t surf_formats[128];
    static uint32_t res_formats[256];
    static uint32_t surf_next;
    const uint32_t *w=(const uint32_t*)(const void*)data;
    uint32_t words=size/4u,off=0,cmds=0,bad=0;
    uint32_t create=0,bind=0,fb=0,clear=0,draw=0,iw=0,blit=0,copy=0,transfer=0;
    uint32_t end_transfer=0,fb_no_attach=0;
    uint32_t last_iw=0,last_dst=0,last_src=0,h0=0,h1=0,h2=0,h3=0;
    uint32_t last_fb_surf=0,last_fb_res=0,last_xfer_res=0,last_xfer_dir=0;
    uint32_t last_z_surf=0,last_z_res=0,last_z_fmt=0,last_fb_fmt=0;
    uint32_t last_create_surf=0,last_create_res=0,last_create_fmt=0;
    uint32_t pipe_create=0,last_pipe_res=0,last_pipe_target=0,last_pipe_fmt=0,last_pipe_bind=0;
    uint32_t last_pipe_w=0,last_pipe_h=0,last_pipe_flags=0,last_pipe_blob=0;
    uint32_t last_clear_mask=0,last_clear0=0,last_clear1=0,last_clear2=0,last_clear3=0;
    if(!data||!size)return;
    if(words>0)h0=w[0];
    if(words>1)h1=w[1];
    if(words>2)h2=w[2];
    if(words>3)h3=w[3];
    while(off<words&&cmds<512u){
        uint32_t header=w[off];
        uint32_t op=header&0xFFu;
        uint32_t obj=(header>>8)&0xFFu;
        uint32_t len=(header>>16)&0xFFFFu;
        uint32_t next=off+1u+len;
        if(next<=off||next>words){bad=header;break;}
        ++cmds;
        switch(op){
            case 1u:
                ++create;
                if(obj==8u&&len>=5u){
                    uint32_t surf=w[off+1u],res=w[off+2u];
                    uint32_t slot=128u,si;
                    for(si=0;si<128u;++si){
                        if(surf_handles[si]==surf||!surf_handles[si]){slot=si;break;}
                    }
                    if(slot==128u){
                        slot=surf_next++%128u;
                    }
                    surf_handles[slot]=surf;
                    surf_resources[slot]=res;
                    surf_formats[slot]=w[off+3u];
                    if(!surf_formats[slot]&&res<256u)surf_formats[slot]=res_formats[res];
                    last_create_surf=surf;
                    last_create_res=res;
                    last_create_fmt=w[off+3u];
                }
                break;
            case 2u: ++bind; break;
            case 5u:
                ++fb;
                if(len>=3u){
                    uint32_t si;
                    last_z_surf=w[off+2u];
                    last_fb_surf=(len>=3u)?w[off+3u]:0u;
                    last_fb_res=0;
                    last_z_res=0;
                    for(si=0;si<128u;++si){
                        if(surf_handles[si]==last_fb_surf){
                            last_fb_res=surf_resources[si];
                            last_fb_fmt=surf_formats[si];
                        }
                        if(surf_handles[si]==last_z_surf){
                            last_z_res=surf_resources[si];
                            last_z_fmt=surf_formats[si];
                        }
                    }
                    if(last_fb_res){
                        g_c4_vgpu_last_fb_resource=last_fb_res;
                        c4_vgpu_remember_present_candidate(last_fb_res);
                    }
                }
                break;
            case 7u:
                ++clear;
                if(len>=5u){
                    last_clear_mask=w[off+1u];
                    last_clear0=w[off+2u];
                    last_clear1=w[off+3u];
                    last_clear2=w[off+4u];
                    if(len>=6u)last_clear3=w[off+5u];
                }
                break;
            case 8u: ++draw; break;
            case 9u:
                ++iw;
                if(len>=1u)last_iw=w[off+1u];
                break;
            case 16u:
                ++blit;
                if(len>=13u){
                    last_dst=w[off+4u];last_src=w[off+13u];
                    c4_vgpu_remember_present_candidate(last_dst);
                }
                break;
            case 17u:
                ++copy;
                if(len>=6u){
                    last_dst=w[off+1u];last_src=w[off+6u];
                    c4_vgpu_remember_present_candidate(last_dst);
                }
                break;
            case 38u:
                ++fb;
                ++fb_no_attach;
                break;
            case 43u:
                ++transfer;
                if(len>=13u){
                    last_xfer_res=w[off+1u];
                    last_xfer_dir=w[off+13u];
                }
                break;
            case 44u: ++end_transfer; break;
            case 45u:
                ++copy;
                if(len>=14u){
                    last_dst=w[off+1u];last_src=w[off+12u];
                    c4_vgpu_remember_present_candidate(last_dst);
                }
                break;
            case 49u:
                ++pipe_create;
                if(len>=10u){
                    uint32_t base=1u;
                    uint32_t maybe_res=w[off+1u];
                    if(len>=11u||(maybe_res>10u&&w[off+2u]<=10u))base=2u;
                    last_pipe_res=(base==2u)?maybe_res:0u;
                    last_pipe_target=w[off+base];
                    last_pipe_fmt=w[off+base+1u];
                    last_pipe_bind=w[off+base+2u];
                    last_pipe_w=w[off+base+3u];
                    last_pipe_h=w[off+base+4u];
                    if(base+9u<=len)last_pipe_flags=w[off+base+9u];
                    if(base+10u<=len)last_pipe_blob=w[off+base+10u];
                    if(last_pipe_res&&last_pipe_res<256u)res_formats[last_pipe_res]=last_pipe_fmt;
                }
                break;
            default: break;
        }
        off=next;
    }
    if(!c4_virtgpu_trace_enabled()&&!c4_drm_scanout_probe_enabled())return;
    if(trace_count>=64u)return;
    ++trace_count;
    __boot_serial_force_puts("[virgl-submit] #");
    __boot_serial_force_putu32(trace_count);
    __boot_serial_force_puts(" bytes=");
    __boot_serial_force_putu32(size);
    __boot_serial_force_puts(" cmds=");
    __boot_serial_force_putu32(cmds);
    __boot_serial_force_puts(" nbo=");
    __boot_serial_force_putu32(e?e->num_bo_handles:0);
    __boot_serial_force_puts(" ops create/bind/fb/clear/draw/iw/blit/copy/xfer=");
    __boot_serial_force_putu32(create);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(bind);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(fb);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(clear);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(draw);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(iw);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(blit);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(copy);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(transfer);
    __boot_serial_force_puts(" iw=");
    __boot_serial_force_putu32(last_iw);
    __boot_serial_force_puts(" fb=");
    __boot_serial_force_putu32(last_fb_surf);__boot_serial_force_puts(">");
    __boot_serial_force_putu32(last_fb_res);
    __boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_fb_fmt);
    __boot_serial_force_puts(" z=");
    __boot_serial_force_putu32(last_z_surf);__boot_serial_force_puts(">");
    __boot_serial_force_putu32(last_z_res);
    __boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_z_fmt);
    __boot_serial_force_puts(" new=");
    __boot_serial_force_putu32(last_create_surf);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(last_create_res);__boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_create_fmt);
    __boot_serial_force_puts(" pipe=");
    __boot_serial_force_putu32(pipe_create);__boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_pipe_res);__boot_serial_force_puts("/");
    __boot_serial_force_putu32(last_pipe_target);__boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_pipe_fmt);__boot_serial_force_puts(":");
    __boot_serial_force_puthex64(((uint64_t)last_pipe_bind<<32)|last_pipe_flags);
    __boot_serial_force_puts(":");
    __boot_serial_force_puthex64(((uint64_t)last_pipe_w<<32)|last_pipe_h);
    if(last_pipe_blob){
        __boot_serial_force_puts(":blob=");
        __boot_serial_force_putu32(last_pipe_blob);
    }
    __boot_serial_force_puts(" xfer=");
    __boot_serial_force_putu32(last_xfer_dir);__boot_serial_force_puts(":");
    __boot_serial_force_putu32(last_xfer_res);
    __boot_serial_force_puts(" endx=");
    __boot_serial_force_putu32(end_transfer);
    __boot_serial_force_puts(" fbna=");
    __boot_serial_force_putu32(fb_no_attach);
    __boot_serial_force_puts(" blit=");
    __boot_serial_force_putu32(last_src);__boot_serial_force_puts(">");
    __boot_serial_force_putu32(last_dst);
    if(clear){
        __boot_serial_force_puts(" clear=");
        __boot_serial_force_puthex64(((uint64_t)last_clear_mask<<32)|last_clear0);
        __boot_serial_force_puts(",");
        __boot_serial_force_puthex64(((uint64_t)last_clear1<<32)|last_clear2);
        __boot_serial_force_puts(",");
        __boot_serial_force_puthex64(last_clear3);
    }
    if(bad){
        __boot_serial_force_puts(" bad=");
        __boot_serial_force_puthex64(bad);
    }
    if(e&&e->bo_handles&&e->num_bo_handles){
        const uint32_t *handles=(const uint32_t*)(uintptr_t)e->bo_handles;
        uint32_t i,limit=e->num_bo_handles<4u?e->num_bo_handles:4u;
        __boot_serial_force_puts(" bos=");
        for(i=0;i<limit;++i){
            c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handles[i]);
            uint32_t nz=0;
            uint64_t sum=0,p0=0,p1=0;
            if(bo)c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
            if(i)__boot_serial_force_puts(";");
            __boot_serial_force_putu32(handles[i]);
            __boot_serial_force_puts(":");
            __boot_serial_force_putu32(nz);
            __boot_serial_force_puts(":");
            __boot_serial_force_puthex64(p0);
        }
    }
    __boot_serial_force_puts(" head=");
    __boot_serial_force_puthex64(((uint64_t)h1<<32)|h0);__boot_serial_force_puts(",");
    __boot_serial_force_puthex64(((uint64_t)h3<<32)|h2);
    __boot_serial_force_puts("\n");
}

static int c4_vgpu_real_execbuffer(const c4_virtgpu_execbuffer_t *e){
    c4_virtio_gpu_cmd_submit_t *cmd;
    uint32_t total;
    int rc;
    static uint32_t exec_force_trace;
    uint32_t exec_trace_id=0;
    if(!e||!e->command||!e->size)return -EINVAL;
    if(e->size>sizeof(g_c4_vgpu_cmd)-sizeof(c4_virtio_gpu_cmd_submit_t))return -EINVAL;
    if(c4_virtgpu_trace_enabled()&&exec_force_trace<48u){
        ++exec_force_trace;
        exec_trace_id=exec_force_trace;
        __boot_serial_force_puts("[virtgpu-exec>] #");
        __boot_serial_force_putu32(exec_trace_id);
        __boot_serial_force_puts(" size=");
        __boot_serial_force_putu32(e->size);
        __boot_serial_force_puts(" flags=");
        __boot_serial_force_puthex64(e->flags);
        __boot_serial_force_puts(" nbo=");
        __boot_serial_force_putu32(e->num_bo_handles);
        __boot_serial_force_puts(" ring=");
        __boot_serial_force_putu32(e->ring_idx);
        __boot_serial_force_puts("\n");
    }
    rc=c4_vgpu_real_create_context(g_c4_vgpu_real.active_context_init?
                                   g_c4_vgpu_real.active_context_init:
                                   c4_vgpu_real_preferred_virgl_context_init());
    if(rc<0)return rc;
    rc=c4_vgpu_real_attach_exec_bos(e);
    if(rc<0)return rc;
    c4_vgpu_real_upload_exec_bos(e);
    total=(uint32_t)sizeof(c4_virtio_gpu_cmd_submit_t)+e->size;
    ulibc_memset(g_c4_vgpu_cmd,0,total);
    cmd=(c4_virtio_gpu_cmd_submit_t*)g_c4_vgpu_cmd;
    cmd->hdr.type=C4_VIRTIO_GPU_CMD_SUBMIT_3D;
    cmd->hdr.flags=C4_VIRTIO_GPU_FLAG_FENCE;
    cmd->hdr.fence_id=g_c4_vgpu_real.fence_next++;
    cmd->hdr.ctx_id=g_c4_vgpu_real.ctx_id?g_c4_vgpu_real.ctx_id:1u;
    cmd->hdr.padding=e->ring_idx;
    cmd->size=e->size;
    ulibc_memcpy(g_c4_vgpu_cmd+sizeof(c4_virtio_gpu_cmd_submit_t),(const void*)(uintptr_t)e->command,e->size);
    c4_virgl_trace_copied_submit(g_c4_vgpu_cmd+sizeof(c4_virtio_gpu_cmd_submit_t),e->size,e);
    if(c4_virtgpu_exec_sync_enabled()){
        rc=c4_vgpu_real_send_submit3d((uint32_t)sizeof(c4_virtio_gpu_cmd_submit_t),
                                      e->size,
                                      "submit3d");
    }else{
        rc=c4_vgpu_real_send_async_nodata(g_c4_vgpu_cmd,
                                          (uint32_t)sizeof(c4_virtio_gpu_cmd_submit_t),
                                          g_c4_vgpu_cmd+sizeof(c4_virtio_gpu_cmd_submit_t),
                                          e->size,
                                          "submit3d");
        if(rc==-EAGAIN)
            rc=c4_vgpu_real_send_submit3d((uint32_t)sizeof(c4_virtio_gpu_cmd_submit_t),
                                          e->size,
                                          "submit3d");
    }
    if(rc==0){
        if(e->flags&C4_VIRTGPU_EXECBUF_FENCE_FD_OUT){
            c4_virtgpu_execbuffer_t *out=(c4_virtgpu_execbuffer_t*)e;
            int64_t fence_fd=real_sys_eventfd2(1u,0);
            if(fence_fd<0)return (int)fence_fd;
            out->fence_fd=(int32_t)fence_fd;
            c4_virtgpu_trace_u64("exec-fence-out",
                                  e->flags,(uint32_t)out->fence_fd,
                                  e->ring_idx,e->num_bo_handles);
        }
        if(c4_drm_scanout_probe_enabled())
            c4_vgpu_real_present_exec_scanout();
    }
    if(exec_trace_id){
        __boot_serial_force_puts("[virtgpu-exec<] #");
        __boot_serial_force_putu32(exec_trace_id);
        __boot_serial_force_puts(" rc=");
        if(rc<0){__boot_serial_force_puts("-");__boot_serial_force_putu32((uint32_t)(-rc));}
        else __boot_serial_force_puthex64((uint64_t)rc);
        __boot_serial_force_puts(" fence=");
        __boot_serial_force_putu32((uint32_t)g_c4_vgpu_real.fence_next);
        __boot_serial_force_puts(" used=");
        __boot_serial_force_putu32((uint32_t)g_c4_vgpu_real.last_used_idx);
        __boot_serial_force_puts("/");
        __boot_serial_force_putu32((uint32_t)g_c4_vgpu_used.idx);
        __boot_serial_force_puts("\n");
    }
    return rc;
}

static c4_drm_bo_t *c4_drm_find_bo_by_handle(uint32_t handle){
    int i;
    if(!handle)return 0;
    for(i=0;i<C4_DRM_MAX_BO;++i)
        if(g_c4_drm_bos[i].used&&g_c4_drm_bos[i].handle==handle)
            return &g_c4_drm_bos[i];
    return 0;
}

static c4_drm_bo_t *c4_drm_find_bo_by_fb(uint32_t fb_id){
    int i;
    if(!fb_id)return 0;
    for(i=0;i<C4_DRM_MAX_BO;++i)
        if(g_c4_drm_bos[i].used&&g_c4_drm_bos[i].fb_id==fb_id)
            return &g_c4_drm_bos[i];
    return 0;
}

static uint32_t c4_drm_current_tgid(void){
    task_t *t=task_current();
    if(!t)return 0;
    return (uint32_t)(t->tgid?t->tgid:t->pid);
}

static uint32_t c4_drm_file_key(int fd){
    uint32_t tgid=c4_drm_current_tgid();
    return 0x44000000u ^ ((tgid&0x7FFFu)<<16) ^ (uint32_t)(fd&0xFFFFu);
}

static c4_drm_file_t *c4_drm_file_find(uint32_t id){
    int i;
    if(!id)return 0;
    for(i=0;i<C4_DRM_MAX_FILES;++i)
        if(g_c4_drm_files[i].used&&g_c4_drm_files[i].id==id)
            return &g_c4_drm_files[i];
    return 0;
}

static c4_drm_file_t *c4_drm_file_get(int fd,bool create){
    task_t *t=task_current();
    uint32_t id=c4_drm_file_key(fd);
    c4_drm_file_t *f=c4_drm_file_find(id);
    int i;
    if(f||!create)return f;
    if(fd<0||!t)return 0;
    if(!fd_valid(&t->fdt,fd)||t->fdt.fds[fd].kind!=FDKIND_DEVFB)return 0;
    for(i=0;i<C4_DRM_MAX_FILES;++i){
        if(g_c4_drm_files[i].used)continue;
        f=&g_c4_drm_files[i];
        ulibc_memset(f,0,sizeof(*f));
        f->used=true;
        f->id=id;
        f->fd=fd;
        f->pid=t->pid;
        f->tgid=t->tgid?t->tgid:t->pid;
        f->render_node=drm_fd_is_render_node(fd);
        f->authenticated=f->render_node;
        if(!f->render_node&&!g_c4_drm_master_file){
            f->master=true;
            f->authenticated=true;
            g_c4_drm_master_file=f->id;
        }
        return f;
    }
    return 0;
}

static bool c4_drm_file_same_tgid(const c4_drm_file_t *a,const c4_drm_file_t *b){
    return a&&b&&a->tgid==b->tgid;
}

static c4_drm_file_handle_t *c4_drm_file_handle_find(c4_drm_file_t *file,uint32_t handle,bool same_tgid){
    int i;
    if(!file||!handle)return 0;
    for(i=0;i<C4_DRM_MAX_FILE_HANDLES;++i){
        c4_drm_file_handle_t *h=&g_c4_drm_file_handles[i];
        c4_drm_file_t *owner;
        if(!h->used||h->handle!=handle)continue;
        if(h->file_id==file->id)return h;
        owner=c4_drm_file_find(h->file_id);
        if(same_tgid&&c4_drm_file_same_tgid(file,owner))return h;
    }
    return 0;
}

static bool c4_drm_file_handle_add(c4_drm_file_t *file,c4_drm_bo_t *bo){
    c4_drm_file_handle_t *h;
    int i;
    if(!file||!bo||!bo->used||!bo->handle)return false;
    h=c4_drm_file_handle_find(file,bo->handle,false);
    if(h){
        ++h->refs;
        ++bo->handle_refcount;
        return true;
    }
    for(i=0;i<C4_DRM_MAX_FILE_HANDLES;++i){
        if(g_c4_drm_file_handles[i].used)continue;
        h=&g_c4_drm_file_handles[i];
        ulibc_memset(h,0,sizeof(*h));
        h->used=true;
        h->file_id=file->id;
        h->handle=bo->handle;
        h->refs=1;
        ++bo->handle_refcount;
        if(!bo->owner_file)bo->owner_file=file->id;
        return true;
    }
    return false;
}

static bool c4_drm_file_handle_remove(c4_drm_file_t *file,uint32_t handle){
    c4_drm_file_handle_t *h=c4_drm_file_handle_find(file,handle,false);
    c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
    if(!h)h=c4_drm_file_handle_find(file,handle,true);
    if(!h){
        /* Mesa/wlroots can pass the same GEM handle through the primary and
         * render-node fds inside one process. Linux drivers tolerate the
         * compositor closing a buffer handle after the fd ownership crossed
         * that boundary; returning EINVAL here makes wlroots drop the KMS fb
         * before it ever becomes visible. Keep the real GPU object alive and
         * treat this as an idempotent close for scanout/exported buffers. */
        if(bo&&(bo->real_created||bo->fb_id||bo->kernel_refcount||
                bo->prime_refcount))
            return true;
        return false;
    }
    if(h->refs>1)h->refs--;
    else h->used=false;
    if(bo&&bo->handle_refcount)bo->handle_refcount--;
    if(bo)c4_drm_bo_try_free(bo);
    return true;
}

static c4_drm_bo_t *c4_drm_bo_for_fd_handle(int fd,uint32_t handle){
    c4_drm_file_t *file=c4_drm_file_get(fd,true);
    c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
    if(!bo)return 0;
    if(bo->kernel_refcount)return bo;
    if(file&&c4_drm_file_handle_find(file,handle,true))return bo;
    return 0;
}

static bool c4_drm_file_can_modeset(int fd){
    c4_drm_file_t *f=c4_drm_file_get(fd,true);
    if(!f||f->render_node)return false;
    /* Ridux currently exposes one local seat and one KMS primary node. wlroots
     * can open/pass that node through helper threads before the compositor
     * performs legacy modeset, so tying modeset permission to the first file
     * object rejects the real compositor with EACCES. Render nodes still stay
     * render-only; any primary-node fd can drive the single seat. */
    return true;
}

static c4_drm_bo_t *c4_drm_latest_bo(void){
    int i;
    c4_drm_bo_t *best=0;
    for(i=0;i<C4_DRM_MAX_BO;++i){
        if(!g_c4_drm_bos[i].used)continue;
        if(!best||g_c4_drm_bos[i].handle>best->handle)best=&g_c4_drm_bos[i];
    }
    return best;
}

static void c4_drm_sample_bo(c4_drm_bo_t *bo,uint32_t *nonzero,uint64_t *sum,
                             uint64_t *p0,uint64_t *p1){
    uint64_t n,i,s=0,q0=0,q1=0;
    uint32_t nz=0;
    const uint8_t *d;
    if(nonzero)*nonzero=0;
    if(sum)*sum=0;
    if(p0)*p0=0;
    if(p1)*p1=0;
    if(!bo||!bo->data||!bo->size)return;
    d=bo->data;
    n=bo->size;
    if(n>65536ULL)n=65536ULL;
    for(i=0;i<n;++i){
        uint8_t v=d[i];
        if(v)++nz;
        s=(s*1315423911ULL)^(uint64_t)v^(i<<7);
    }
    for(i=0;i<8&&i<bo->size;++i)q0|=((uint64_t)d[i])<<(i*8);
    for(i=0;i<8&&(i+8)<bo->size;++i)q1|=((uint64_t)d[i+8])<<(i*8);
    if(nonzero)*nonzero=nz;
    if(sum)*sum=s;
    if(p0)*p0=q0;
    if(p1)*p1=q1;
}

static c4_drm_bo_t *c4_drm_visible_bo_for_present(c4_drm_bo_t *bo){
    c4_drm_bo_t *last;
    uint32_t nz=0,last_nz=0;
    if(g_c4_vmw_rescue_rt_handle&&(!bo||bo->handle!=g_c4_vmw_rescue_rt_handle)){
        c4_drm_bo_t *rescue=c4_drm_find_bo_by_handle(g_c4_vmw_rescue_rt_handle);
        uint32_t rescue_nz=0;
        c4_drm_sample_bo(rescue,&rescue_nz,0,0,0);
        if(rescue&&rescue_nz){
            g_c4_drm_last_nonzero_handle=rescue->handle;
            return rescue;
        }
    }
    c4_drm_sample_bo(bo,&nz,0,0,0);
    if(nz){
        g_c4_drm_last_nonzero_handle=bo->handle;
        return bo;
    }
    if(!g_c4_drm_last_nonzero_handle||!bo)return bo;
    if(bo->handle==g_c4_drm_last_nonzero_handle)return bo;
    last=c4_drm_find_bo_by_handle(g_c4_drm_last_nonzero_handle);
    c4_drm_sample_bo(last,&last_nz,0,0,0);
    return last_nz?last:bo;
}

static void c4_drm_refresh_bo_from_gpu(c4_drm_bo_t *bo,const char *tag){
    static uint32_t refresh_trace;
    c4_virtgpu_3d_transfer_t t;
    int rc;
    if(!bo||!bo->real_created||!bo->real_backed)return;
    ulibc_memset(&t,0,sizeof(t));
    t.bo_handle=bo->handle;
    t.box.w=bo->width?bo->width:1u;
    t.box.h=bo->height?bo->height:1u;
    t.box.d=1u;
    t.offset=0;
    rc=c4_vgpu_real_transfer_3d(&t,false);
    if(refresh_trace<48u){
        ++refresh_trace;
        c4_virtgpu_trace_u64(tag?tag:"present-refresh",bo->handle,
                             (uint64_t)(int64_t)rc,
                             ((uint64_t)t.box.w<<32)|t.box.h,
                             bo->size);
    }
}

static void c4_drm_trace_bo(const char *tag,c4_drm_bo_t *bo,uint32_t a,uint32_t b){
    uint32_t nz=0;
    uint64_t sum=0,p0=0,p1=0;
    if(!c4_virtgpu_trace_enabled()&&!c4_drm_scanout_probe_enabled())return;
    if(g_c4_drm_trace_count>=220u)return;
    ++g_c4_drm_trace_count;
    c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
    __boot_serial_force_puts("[drm-bo] ");
    __boot_serial_force_puts(tag?tag:"?");
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(g_c4_drm_trace_count);
    if(bo){
        __boot_serial_force_puts(" fb=");
        __boot_serial_force_putu32(bo->fb_id);
        __boot_serial_force_puts(" handle=");
        __boot_serial_force_putu32(bo->handle);
        __boot_serial_force_puts(" fmt=");
        __boot_serial_force_puthex64((uint64_t)bo->pixel_format);
        __boot_serial_force_puts(" pitch=");
        __boot_serial_force_putu32(bo->pitch);
        __boot_serial_force_puts(" size=");
        __boot_serial_force_puthex64(bo->size);
        __boot_serial_force_puts(" map=");
        __boot_serial_force_puthex64(bo->map_offset);
        __boot_serial_force_puts(" nz=");
        __boot_serial_force_putu32(nz);
        __boot_serial_force_puts(" sum=");
        __boot_serial_force_puthex64(sum);
        __boot_serial_force_puts(" p0=");
        __boot_serial_force_puthex64(p0);
        __boot_serial_force_puts(" p1=");
        __boot_serial_force_puthex64(p1);
    }
    __boot_serial_force_puts(" a=");
    __boot_serial_force_putu32(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_putu32(b);
    __boot_serial_force_puts("\n");
}

static uint8_t *c4_drm_alloc_storage(uint64_t alloc_size,uint64_t *phys_out,bool *pmm_backed_out){
    uint32_t pages;
    uint64_t phys;
    uint8_t *data;
    if(phys_out)*phys_out=0;
    if(pmm_backed_out)*pmm_backed_out=false;
    if(!alloc_size||alloc_size>C4_DRM_MAX_BO_BYTES)return 0;
    pages=(uint32_t)((alloc_size+(uint64_t)PAGE_SIZE-1ULL)/(uint64_t)PAGE_SIZE);
    phys=pmm_alloc_contiguous_frames(pages);
    if(phys){
        data=(uint8_t*)PHYS_TO_DMAP(phys);
        ulibc_memset(data,0,(size_t)((uint64_t)pages*(uint64_t)PAGE_SIZE));
        if(phys_out)*phys_out=phys;
        if(pmm_backed_out)*pmm_backed_out=true;
        return data;
    }
    data=(uint8_t*)ulibc_memalign(PAGE_SIZE,(size_t)alloc_size);
    if(data)ulibc_memset(data,0,(size_t)alloc_size);
    return data;
}

static void c4_drm_free_storage(c4_drm_bo_t *bo){
    uint64_t off;
    if(!bo||!bo->data)return;
    if(bo->real_mmio_backed){
        bo->data=0;
        bo->phys=0;
        bo->real_mmio_backed=false;
        return;
    }
    if(bo->pmm_backed&&bo->phys){
        for(off=0;off<bo->alloc_size;off+=PAGE_SIZE)
            pmm_free_frame(bo->phys+off);
    }else{
        ulibc_free(bo->data);
    }
    bo->data=0;
    bo->phys=0;
}

static void c4_drm_bo_try_free(c4_drm_bo_t *bo){
    if(!bo||!bo->used)return;
    if(bo->kernel_refcount||bo->handle_refcount||bo->fb_refcount||
       bo->mmap_refcount||bo->prime_refcount)return;
    if(bo->fb_id&&bo->fb_id==g_c4_drm_current_fb_id)return;
    if(bo->handle==g_c4_drm_cursor_handle||
       bo->handle==g_c4_vmw_rescue_rt_handle||
       bo==g_c4_native_present_bo)return;
    c4_drm_free_storage(bo);
    ulibc_memset(bo,0,sizeof(*bo));
}

static c4_drm_bo_t *c4_drm_alloc_bo(uint32_t width,uint32_t height,uint32_t bpp){
    int i;
    c4_drm_bo_t *bo=0;
    uint64_t pitch,size,alloc_size;
    if(!width)width=g_fb.width?g_fb.width:1024u;
    if(!height)height=g_fb.height?g_fb.height:768u;
    if(!bpp)bpp=32u;
    if(bpp<8u)bpp=32u;
    pitch=(((uint64_t)width*(uint64_t)bpp)+7ULL)/8ULL;
    pitch=(pitch+63ULL)&~63ULL;
    size=pitch*(uint64_t)height;
    alloc_size=c4_align_page(size);
    if(!size||alloc_size>C4_DRM_MAX_BO_BYTES)return 0;
    for(i=0;i<C4_DRM_MAX_BO;++i){
        if(g_c4_drm_bos[i].used)continue;
        bo=&g_c4_drm_bos[i];
        break;
    }
    if(!bo)return 0;
    ulibc_memset(bo,0,sizeof(*bo));
    bo->data=c4_drm_alloc_storage(alloc_size,&bo->phys,&bo->pmm_backed);
    if(!bo->data)return 0;
    bo->used=true;
    bo->handle=g_c4_drm_next_handle++;
    if(g_c4_drm_next_handle<2)g_c4_drm_next_handle=2;
    bo->width=width;
    bo->height=height;
    bo->bpp=bpp;
    bo->pitch=(uint32_t)pitch;
    bo->pixel_format=C4_DRM_FORMAT_XRGB8888;
    bo->size=size;
    bo->alloc_size=alloc_size;
    bo->map_offset=C4_DRM_MMAP_BASE + (uint64_t)bo->handle*C4_DRM_MMAP_STRIDE;
    bo->kernel_refcount=1;
    return bo;
}

static c4_drm_bo_t *c4_drm_alloc_bo_bytes(uint64_t size,uint32_t width,uint32_t height,
                                          uint32_t bpp,uint32_t pitch){
    int i;
    c4_drm_bo_t *bo=0;
    uint64_t alloc_size;
    if(!width)width=g_fb.width?g_fb.width:1024u;
    if(!height)height=g_fb.height?g_fb.height:768u;
    if(!bpp)bpp=32u;
    if(!pitch)pitch=(uint32_t)((((uint64_t)width*(uint64_t)bpp)+7ULL)/8ULL);
    if(!size)size=(uint64_t)pitch*(uint64_t)height;
    alloc_size=c4_align_page(size);
    if(!size||alloc_size>C4_DRM_MAX_BO_BYTES)return 0;
    for(i=0;i<C4_DRM_MAX_BO;++i){
        if(g_c4_drm_bos[i].used)continue;
        bo=&g_c4_drm_bos[i];
        break;
    }
    if(!bo)return 0;
    ulibc_memset(bo,0,sizeof(*bo));
    bo->data=c4_drm_alloc_storage(alloc_size,&bo->phys,&bo->pmm_backed);
    if(!bo->data)return 0;
    bo->used=true;
    bo->handle=g_c4_drm_next_handle++;
    if(g_c4_drm_next_handle<2)g_c4_drm_next_handle=2;
    bo->width=width;
    bo->height=height;
    bo->bpp=bpp;
    bo->pitch=pitch;
    bo->pixel_format=C4_DRM_FORMAT_XRGB8888;
    bo->size=size;
    bo->alloc_size=alloc_size;
    bo->map_offset=C4_DRM_MMAP_BASE + (uint64_t)bo->handle*C4_DRM_MMAP_STRIDE;
    bo->kernel_refcount=1;
    return bo;
}

static bool c4_native_present_copy(c4_drm_bo_t *bo,const uint32_t *pixels,
                                   uint32_t width,uint32_t height,
                                   uint32_t stride_pixels){
    uint32_t y;
    if(!bo||!bo->data||!pixels||!width||!height||!stride_pixels)return false;
    if(bo->pitch<width*4u||bo->size<(uint64_t)bo->pitch*(uint64_t)height)
        return false;
    for(y=0;y<height;++y){
        const uint8_t *src=(const uint8_t*)(const void*)(pixels+(size_t)y*(size_t)stride_pixels);
        uint8_t *dst=bo->data+(size_t)y*(size_t)bo->pitch;
        ulibc_memcpy(dst,src,(size_t)width*4u);
    }
    return true;
}

bool ridux_gpu_present_backbuffer(const uint32_t *pixels,uint32_t width,
                                  uint32_t height,uint32_t stride_pixels){
    int rc_create,rc_xfer,rc_scan,rc_flush;
    if(g_c4_native_present_disabled)return false;
    if(!pixels||!width||!height||!stride_pixels)return false;
    if(!g_fb.ready||g_fb.bpp!=32u){
        g_c4_native_present_backend="CPU framebuffer fallback";
        g_c4_native_present_real_active=false;
        return false;
    }
    if(!g_c4_native_present_bo||
       g_c4_native_present_bo->width!=width||
       g_c4_native_present_bo->height!=height){
        g_c4_native_present_bo=c4_drm_alloc_bo(width,height,32u);
        if(!g_c4_native_present_bo){
            g_c4_native_present_backend="CPU framebuffer fallback (no GPU BO)";
            g_c4_native_present_real_active=false;
            return false;
        }
        g_c4_native_present_bo->fb_id=C4_DRM_ID_FB;
        g_c4_native_present_bo->pixel_format=C4_DRM_FORMAT_XRGB8888;
    }
    if(!c4_native_present_copy(g_c4_native_present_bo,pixels,width,height,stride_pixels)){
        g_c4_native_present_backend="CPU framebuffer fallback (copy failed)";
        g_c4_native_present_real_active=false;
        return false;
    }

    if(c4_svga_real_present_bo(g_c4_native_present_bo)){
        g_c4_native_present_backend="VMSVGA/SVGA3D real present";
        g_c4_native_present_real_active=true;
        g_c4_native_present_failures=0;
        return true;
    }

    rc_create=c4_vgpu_real_resource_create_2d(g_c4_native_present_bo);
    rc_xfer=(rc_create==0)?c4_vgpu_real_transfer_2d_to_host(g_c4_native_present_bo):rc_create;
    rc_scan=(rc_xfer==0)?c4_vgpu_real_set_scanout_bo(g_c4_native_present_bo):rc_xfer;
    rc_flush=(rc_scan==0)?c4_vgpu_real_flush_bo(g_c4_native_present_bo):rc_scan;
    if(rc_create==0&&rc_xfer==0&&rc_scan==0&&rc_flush==0){
        g_c4_native_present_backend="virtio-gpu 2D real scanout";
        g_c4_native_present_real_active=true;
        g_c4_native_present_failures=0;
        return true;
    }

    g_c4_native_present_backend="CPU dirty framebuffer blit (GPU present unavailable)";
    g_c4_native_present_real_active=false;
    if(++g_c4_native_present_failures>=8u)g_c4_native_present_disabled=true;
    return false;
}

bool ridux_gpu_real_present_available(void){
    return g_c4_native_present_real_active;
}

const char *ridux_gpu_present_backend(void){
    return g_c4_native_present_backend?g_c4_native_present_backend:
           "CPU dirty framebuffer blit";
}

static uint64_t c4_virtgpu_param_value(uint64_t param){
    bool real=c4_vgpu_real_init();
    if(real){
        switch(param){
            case C4_VIRTGPU_PARAM_3D_FEATURES:
                return (g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_VIRGL))?1ULL:0ULL;
            case C4_VIRTGPU_PARAM_RESOURCE_BLOB:
                return (g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_RESOURCE_BLOB))?1ULL:0ULL;
            case C4_VIRTGPU_PARAM_HOST_VISIBLE:
                return ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_RESOURCE_BLOB))&&
                        c4_vgpu_real_probe_host_visible())?1ULL:0ULL;
            case C4_VIRTGPU_PARAM_CONTEXT_INIT:
                return (g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_CONTEXT_INIT))?1ULL:0ULL;
            case C4_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS:
                return (g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_VIRGL))?
                       c4_virtgpu_supported_capset_mask():0ULL;
            default:
                break;
        }
    }
    switch(param){
        case C4_VIRTGPU_PARAM_3D_FEATURES:
        case C4_VIRTGPU_PARAM_CAPSET_QUERY_FIX:
        case C4_VIRTGPU_PARAM_CONTEXT_INIT:
        case C4_VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME:
            return 1;
        case C4_VIRTGPU_PARAM_RESOURCE_BLOB:
        case C4_VIRTGPU_PARAM_HOST_VISIBLE:
        case C4_VIRTGPU_PARAM_CREATE_GUEST_HANDLE:
            return 0;
        case C4_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS:
            return c4_virtgpu_supported_capset_mask();
        case C4_VIRTGPU_PARAM_CROSS_DEVICE:
        default:
            return 0;
    }
}

static bool c4_virtgpu_param_known(uint64_t param){
    switch(param){
        case C4_VIRTGPU_PARAM_3D_FEATURES:
        case C4_VIRTGPU_PARAM_CAPSET_QUERY_FIX:
        case C4_VIRTGPU_PARAM_RESOURCE_BLOB:
        case C4_VIRTGPU_PARAM_HOST_VISIBLE:
        case C4_VIRTGPU_PARAM_CROSS_DEVICE:
        case C4_VIRTGPU_PARAM_CONTEXT_INIT:
        case C4_VIRTGPU_PARAM_SUPPORTED_CAPSET_IDS:
        case C4_VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME:
        case C4_VIRTGPU_PARAM_CREATE_GUEST_HANDLE:
            return true;
        default:
            return false;
    }
}

static void c4_virgl_set_format(uint32_t *u,uint32_t base,uint32_t words,uint32_t fmt){
    uint32_t idx=fmt>>5;
    uint32_t bit=fmt&31u;
    if(base+idx<words)u[base+idx]|=1u<<bit;
}

static void c4_virgl_fill_format_masks(uint32_t *u,uint32_t words){
    static const uint32_t color_formats[]={
        1u,2u,3u,4u,7u,67u,100u,101u,104u,121u,134u
    };
    static const uint32_t depth_formats[]={16u,17u,18u,19u,20u,21u,23u,126u,136u,137u};
    static const uint32_t vertex_formats[]={28u,29u,30u,31u,64u,65u,67u,91u,92u,94u,193u,194u,196u};
    uint32_t i;
    for(i=0;i<(uint32_t)(sizeof(color_formats)/sizeof(color_formats[0]));++i){
        c4_virgl_set_format(u,1u,words,color_formats[i]);
        c4_virgl_set_format(u,17u,words,color_formats[i]);
        c4_virgl_set_format(u,140u,words,color_formats[i]);
        c4_virgl_set_format(u,156u,words,color_formats[i]);
    }
    for(i=0;i<(uint32_t)(sizeof(depth_formats)/sizeof(depth_formats[0]));++i){
        c4_virgl_set_format(u,1u,words,depth_formats[i]);
        c4_virgl_set_format(u,33u,words,depth_formats[i]);
    }
    for(i=0;i<(uint32_t)(sizeof(vertex_formats)/sizeof(vertex_formats[0]));++i){
        c4_virgl_set_format(u,1u,words,vertex_formats[i]);
        c4_virgl_set_format(u,49u,words,vertex_formats[i]);
    }
}

static void c4_virtgpu_fill_caps(c4_virtgpu_get_caps_t *caps){
    uint32_t *u;
    uint32_t words;
    if(!caps||!caps->addr||!caps->size)return;
    ulibc_memset((void*)(uintptr_t)caps->addr,0,caps->size);
    u=(uint32_t*)(uintptr_t)caps->addr;
    words=caps->size/4u;
    if(words==0)return;
    if(caps->cap_set_id==C4_VIRTGPU_CAPSET_VIRGL||
       caps->cap_set_id==C4_VIRTGPU_CAPSET_VIRGL2){
        u[0]=2u;
        c4_virgl_fill_format_masks(u,words);
        if(words>65)u[65]=0x005fffffu;
        if(words>66)u[66]=330u;
        if(words>67)u[67]=256u;
        if(words>68)u[68]=4u;
        if(words>69)u[69]=1u;
        if(words>70)u[70]=4u;
        if(words>71)u[71]=4u;
        if(words>72)u[72]=0xffffu;
        if(words>73)u[73]=65536u;
        if(words>74)u[74]=12u;
        if(words>75)u[75]=1u;
        if(words>76)u[76]=4u;
        if(words>77)u[77]=0x3f800000u;
        if(words>78)u[78]=0x40000000u;
        if(words>79)u[79]=0x3f800000u;
        if(words>80)u[80]=0x40000000u;
        if(words>81)u[81]=0x3f800000u;
        if(words>82)u[82]=0x40000000u;
        if(words>83)u[83]=0x3f800000u;
        if(words>84)u[84]=0x40000000u;
        if(words>85)u[85]=0x40000000u;
        if(words>86)u[86]=256u;
        if(words>87)u[87]=1024u;
        if(words>88)u[88]=32u;
        if(words>89)u[89]=16u;
        if(words>90)u[90]=32u;
        if(words>91)u[91]=(uint32_t)-8;
        if(words>92)u[92]=7u;
        if(words>93)u[93]=(uint32_t)-8;
        if(words>94)u[94]=7u;
        if(words>95)u[95]=16u;
        if(words>96)u[96]=256u;
        if(words>97)u[97]=256u;
        if(words>98)u[98]=0u;
        if(words>107)u[107]=2048u;
        if(words>108)u[108]=8u;
        if(words>109)u[109]=8u;
        if(words>110)u[110]=8u;
        if(words>111)u[111]=8u;
        if(words>112)u[112]=4u;
        if(words>121)u[121]=4096u;
        if(words>122)u[122]=256u;
        if(words>123)u[123]=4096u;
        if(words>124)u[124]=16u;
        if(words>139)u[139]=0u;
    }
}

static bool c4_vgpu_real_virgl_available(void){
    return g_c4_vgpu_real.queue_ready&&
           ((g_c4_vgpu_real.features0&(1u<<C4_VIRTIO_GPU_F_VIRGL))!=0u);
}

static bool c4_virtgpu_capset_supported(uint32_t id){
    if((g_c4_vgpu_real.queue_ready||g_c4_vgpu_real.present)&&!c4_vgpu_real_virgl_available())return false;
    if(id==C4_VIRTGPU_CAPSET_VIRGL)return true;
    if(id==C4_VIRTGPU_CAPSET_VIRGL2)
        return c4_virtgpu_virgl2_enabled()||
               c4_virtgpu_host_capset_available(C4_VIRTGPU_CAPSET_VIRGL2,0,0x580u);
    return id==C4_VIRTGPU_CAPSET_VENUS&&c4_virtgpu_venus_available();
}

static void c4_virtgpu_trace_u64(const char *tag,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
    bool scanout_probe_trace=false;
    if(tag&&c4_drm_scanout_probe_enabled()){
        scanout_probe_trace=ulibc_strstr(tag,"scanout")||
                            ulibc_strstr(tag,"visible")||
                            ulibc_strstr(tag,"atomic")||
                            ulibc_strstr(tag,"present");
    }
    if(!c4_virtgpu_trace_enabled()&&!scanout_probe_trace)return;
    if(g_c4_virtgpu_trace_count>=1024u)return;
    ++g_c4_virtgpu_trace_count;
    __boot_serial_force_puts("[virtgpu] ");
    __boot_serial_force_puts(tag?tag:"?");
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(g_c4_virtgpu_trace_count);
    __boot_serial_force_puts(" a=");
    __boot_serial_force_puthex64(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_puthex64(b);
    __boot_serial_force_puts(" c=");
    __boot_serial_force_puthex64(c);
    __boot_serial_force_puts(" d=");
    __boot_serial_force_puthex64(d);
    __boot_serial_force_puts("\n");
}

static int c4_virtgpu_ioctl(uint64_t request,void *arg){
    uint32_t nr=(uint32_t)(request&0xFFu);
    if(!g_c4_vgpu_real.queue_ready&&nr!=C4_DRM_VIRTGPU_WAIT){
        bool real_init=c4_vgpu_real_init();
        c4_virtgpu_trace_u64("real-init-early",nr,(uint64_t)real_init,g_c4_vgpu_real.queue_ready,request);
    }
    switch(nr){
        case C4_DRM_VIRTGPU_GETPARAM:
            if(arg){
                c4_virtgpu_getparam_t *p=(c4_virtgpu_getparam_t*)arg;
                uint64_t v=c4_virtgpu_param_value(p->param);
                if(!p->value||!c4_virtgpu_param_known(p->param)){
                    c4_virtgpu_trace_u64("getparam-einval",p->param,p->value,request,0);
                    return -EINVAL;
                }
                *(uint64_t*)(uintptr_t)p->value=v;
                c4_virtgpu_trace_u64("getparam",p->param,v,request,p->value);
            }
            return 0;
        case C4_DRM_VIRTGPU_GET_CAPS:
            if(arg){
                c4_virtgpu_get_caps_t *caps=(c4_virtgpu_get_caps_t*)arg;
                uint32_t first=0;
                int rc;
                if(!caps->size){
                    c4_virtgpu_trace_u64("get-caps-einval",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                          caps->size,request,0);
                    return -EINVAL;
                }
                if(!c4_virtgpu_capset_supported(caps->cap_set_id)){
                    rc=c4_vgpu_real_get_capset(caps->cap_set_id,caps->cap_set_ver,
                                               (void*)(uintptr_t)caps->addr,caps->size);
                    if(rc==0){
                        if(caps->addr&&caps->size>=4)first=*(uint32_t*)(uintptr_t)caps->addr;
                        c4_virtgpu_trace_u64("get-caps-real-extra",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                              caps->size,first,request);
                        return 0;
                    }
                    c4_virtgpu_trace_u64("get-caps-einval",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                          caps->size,request,(uint64_t)(int64_t)rc);
                    return -EINVAL;
                }
                rc=c4_vgpu_real_get_capset(caps->cap_set_id,caps->cap_set_ver,(void*)(uintptr_t)caps->addr,caps->size);
                if(rc==0){
                    if(caps->addr&&caps->size>=4)first=*(uint32_t*)(uintptr_t)caps->addr;
                    c4_virtgpu_trace_u64("get-caps-real",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                          caps->size,first,request);
                    return 0;
                }
                c4_virtgpu_trace_u64("get-caps-real-fail",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                      caps->size,(uint64_t)(int64_t)rc,request);
                c4_virtgpu_fill_caps(caps);
                if(caps->addr&&caps->size>=4)first=*(uint32_t*)(uintptr_t)caps->addr;
                c4_virtgpu_trace_u64("get-caps",((uint64_t)caps->cap_set_ver<<32)|caps->cap_set_id,
                                      caps->size,first,request);
            }
            return 0;
        case C4_DRM_VIRTGPU_RESOURCE_CREATE:
            if(arg){
                c4_virtgpu_resource_create_t *r=(c4_virtgpu_resource_create_t*)arg;
                uint32_t pitch=r->stride;
                uint64_t size=r->size;
                uint64_t min_size=0;
                c4_drm_bo_t *bo;
                if(!pitch&&r->width)pitch=(r->width*4u+63u)&~63u;
                if(pitch&&r->height)min_size=(uint64_t)pitch*(uint64_t)r->height;
                if(min_size&&(size<min_size)){
                    c4_virtgpu_trace_u64("resource-size-fix",
                                          size,min_size,pitch,
                                          ((uint64_t)r->width<<32)|r->height);
                    size=min_size;
                }
                bo=c4_drm_alloc_bo_bytes(size,r->width,r->height,32u,pitch);
                if(!bo)return -ENOMEM;
                r->bo_handle=bo->handle;
                r->res_handle=bo->handle;
                r->size=(uint32_t)bo->size;
                r->stride=bo->pitch;
                c4_drm_trace_bo("virtgpu-resource-create",bo,r->width,r->height);
                c4_virtgpu_trace_u64("resource-create-req",
                                      ((uint64_t)r->target<<32)|r->format,
                                      ((uint64_t)r->bind<<32)|r->flags,
                                      ((uint64_t)r->width<<32)|r->height,
                                      ((uint64_t)r->stride<<32)|r->size);
                {
                    int rc=c4_vgpu_real_resource_create_3d(bo,r);
                    c4_virtgpu_trace_u64("resource-create-real",bo->handle,(uint64_t)(int64_t)rc,g_c4_vgpu_real.queue_ready,request);
                    if(rc<0&&g_c4_vgpu_real.queue_ready)return rc;
                }
            }
            return 0;
        case C4_DRM_VIRTGPU_RESOURCE_CREATE_BLOB:
            if(arg){
                c4_virtgpu_resource_create_blob_t *b=(c4_virtgpu_resource_create_blob_t*)arg;
                c4_drm_bo_t *bo=c4_drm_alloc_bo_bytes(b->size,0,0,32u,0);
                if(!bo)return -ENOMEM;
                b->bo_handle=bo->handle;
                b->res_handle=bo->handle;
                c4_virtgpu_trace_u64("blob",((uint64_t)b->blob_mem<<32)|b->blob_flags,b->size,b->bo_handle,request);
                c4_drm_trace_bo("virtgpu-blob-create",bo,(uint32_t)b->blob_mem,(uint32_t)b->blob_flags);
                {
                    int rc=c4_vgpu_real_resource_create_blob(bo,b);
                    c4_virtgpu_trace_u64("blob-real",bo->handle,(uint64_t)(int64_t)rc,g_c4_vgpu_real.queue_ready,request);
                    if(rc<0&&g_c4_vgpu_real.queue_ready)return rc;
                }
            }
            return 0;
        case C4_DRM_VIRTGPU_RESOURCE_INFO:
            if(arg){
                c4_virtgpu_resource_info_t *r=(c4_virtgpu_resource_info_t*)arg;
                c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(r->bo_handle);
                if(!bo&&r->res_handle)bo=c4_drm_find_bo_by_handle(r->res_handle);
                if(!bo)bo=c4_drm_latest_bo();
                if(!bo)return -EINVAL;
                r->bo_handle=bo->handle;
                r->res_handle=bo->handle;
                r->size=(uint32_t)bo->size;
                r->blob_mem=bo->real_blob_mem?bo->real_blob_mem:1u;
            }
            return 0;
        case C4_DRM_VIRTGPU_MAP:
            if(arg){
                c4_virtgpu_map_t *m=(c4_virtgpu_map_t*)arg;
                c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(m->handle);
                if(!bo)return -EINVAL;
                m->offset=bo->map_offset;
                c4_drm_trace_bo("virtgpu-map",bo,(uint32_t)(m->offset>>12),(uint32_t)m->offset);
            }
            return 0;
        case C4_DRM_VIRTGPU_EXECBUFFER:
            if(arg){
                int rc=c4_vgpu_real_execbuffer((const c4_virtgpu_execbuffer_t*)arg);
                c4_virtgpu_trace_u64("execbuffer-real",(uint64_t)((const c4_virtgpu_execbuffer_t*)arg)->size,
                                      (uint64_t)(int64_t)rc,g_c4_vgpu_real.queue_ready,request);
                if(rc==0||g_c4_vgpu_real.queue_ready)return rc;
            }
            c4_virtgpu_trace_u64("noop",nr,request,0,0);
            return 0;
        case C4_DRM_VIRTGPU_TRANSFER_FROM_HOST:
            if(arg){
                int rc=c4_vgpu_real_transfer_3d((const c4_virtgpu_3d_transfer_t*)arg,false);
                c4_virtgpu_trace_u64("xfer-from-real",((const c4_virtgpu_3d_transfer_t*)arg)->bo_handle,
                                      (uint64_t)(int64_t)rc,g_c4_vgpu_real.queue_ready,request);
                if(rc<0&&g_c4_vgpu_real.queue_ready)return rc;
            }
            return 0;
        case C4_DRM_VIRTGPU_TRANSFER_TO_HOST:
            if(arg){
                int rc=c4_vgpu_real_transfer_3d((const c4_virtgpu_3d_transfer_t*)arg,true);
                c4_virtgpu_trace_u64("xfer-to-real",((const c4_virtgpu_3d_transfer_t*)arg)->bo_handle,
                                      (uint64_t)(int64_t)rc,g_c4_vgpu_real.queue_ready,request);
                if(rc<0&&g_c4_vgpu_real.queue_ready)return rc;
            }
            return 0;
        case C4_DRM_VIRTGPU_WAIT:
            if(arg){
                int rc=c4_vgpu_real_wait_ioctl((const c4_virtgpu_3d_wait_t*)arg);
                c4_virtgpu_trace_u64("wait",((const c4_virtgpu_3d_wait_t*)arg)->handle,
                                      ((const c4_virtgpu_3d_wait_t*)arg)->flags,
                                      (uint64_t)(int64_t)rc,request);
                return rc;
            }
            return -EINVAL;
        case C4_DRM_VIRTGPU_CONTEXT_INIT:
            if(arg){
                c4_virtgpu_context_init_t *ci=(c4_virtgpu_context_init_t*)arg;
                uint64_t p0=0,v0=0,capset=0;
                int rc;
                if(ci->ctx_set_params&&ci->num_params>0){
                    c4_virtgpu_context_set_param_t *sp=(c4_virtgpu_context_set_param_t*)(uintptr_t)ci->ctx_set_params;
                    uint32_t i;
                    for(i=0;i<ci->num_params;++i){
                        if(i==0){p0=sp[i].param;v0=sp[i].value;}
                        if(sp[i].param==C4_VIRTGPU_CONTEXT_PARAM_CAPSET_ID)
                            capset=sp[i].value;
                    }
                }
                c4_virtgpu_trace_u64("ctx-init",ci->num_params,p0,v0,
                                      ((uint64_t)(uint32_t)capset<<32)|
                                      (uint32_t)request);
                rc=c4_vgpu_real_create_context((uint32_t)capset);
                if(rc<0&&g_c4_vgpu_real.queue_ready)return rc;
            }
            return 0;
        default:
            return -ENOTTY;
    }
}

static void c4_drm_remember_prime(int32_t fd,uint32_t handle){
    int i;
    if(fd<0||!handle)return;
    for(i=0;i<C4_DRM_MAX_PRIME;++i){
        if(g_c4_drm_primes[i].used&&g_c4_drm_primes[i].fd==fd){
            if(g_c4_drm_primes[i].handle!=handle){
                c4_drm_bo_t *old=c4_drm_find_bo_by_handle(g_c4_drm_primes[i].handle);
                if(old&&old->prime_refcount)old->prime_refcount--;
                old=c4_drm_find_bo_by_handle(handle);
                if(old)old->prime_refcount++;
            }
            g_c4_drm_primes[i].handle=handle;
            return;
        }
    }
    for(i=0;i<C4_DRM_MAX_PRIME;++i){
        if(g_c4_drm_primes[i].used)continue;
        g_c4_drm_primes[i].used=true;
        g_c4_drm_primes[i].fd=fd;
        g_c4_drm_primes[i].handle=handle;
        {
            c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
            if(bo)bo->prime_refcount++;
        }
        return;
    }
}

static uint32_t c4_drm_handle_from_prime_fd(int32_t fd){
    int i;
    for(i=0;i<C4_DRM_MAX_PRIME;++i)
        if(g_c4_drm_primes[i].used&&g_c4_drm_primes[i].fd==fd)
            return g_c4_drm_primes[i].handle;
    return 0;
}

static int32_t c4_drm_prime_fd_for_bo(c4_drm_bo_t *bo){
    int64_t fd;
    if(!bo)return -EINVAL;
    fd=real_sys_memfd_create("ridux-drm-prime",1u);
    if(fd>=0){
        (void)real_sys_ftruncate((int)fd,(int64_t)bo->size);
        return (int32_t)fd;
    }
    if(g_c4_drm_prime_export_fd<100)g_c4_drm_prime_export_fd=100;
    return g_c4_drm_prime_export_fd++;
}

uint32_t drm_prime_handle_for_fd(int32_t fd){
    return c4_drm_handle_from_prime_fd(fd);
}

void drm_remember_prime_fd(int32_t fd,uint32_t handle){
    if(fd>=0&&handle)c4_drm_remember_prime(fd,handle);
}

int drm_resolve_mmap_offset(int fd,uint64_t offset,uint64_t len,
                            uint64_t *kernel_ptr,uint64_t *avail,
                            uint32_t *handle_out){
    int i;
    c4_drm_file_t *file=c4_drm_file_get(fd,true);
    if(handle_out)*handle_out=0;
    if(!file)return -EBADF;
    for(i=0;i<C4_DRM_MAX_BO;++i){
        c4_drm_bo_t *bo=&g_c4_drm_bos[i];
        uint64_t start,end;
        if(!bo->used||!bo->data)continue;
        start=bo->map_offset;
        end=start+bo->alloc_size;
        if(offset<start||offset>=end)continue;
        if(len>end-offset)return -EINVAL;
        if(!bo->kernel_refcount&&!c4_drm_file_handle_find(file,bo->handle,true))
            return -EACCES;
        if(kernel_ptr)*kernel_ptr=(uint64_t)(uintptr_t)(bo->data+(offset-start));
        if(avail)*avail=end-offset;
        if(handle_out)*handle_out=bo->handle;
        ++bo->mmap_refcount;
        if(c4_virtgpu_trace_enabled()&&g_c4_drm_mmap_trace_count<64u){
            ++g_c4_drm_mmap_trace_count;
            __boot_serial_force_puts("[drm-mmap] #");
            __boot_serial_force_putu32(g_c4_drm_mmap_trace_count);
            __boot_serial_force_puts(" handle=");
            __boot_serial_force_putu32(bo->handle);
            __boot_serial_force_puts(" off=");
            __boot_serial_force_puthex64(offset);
            __boot_serial_force_puts(" len=");
            __boot_serial_force_puthex64(len);
            __boot_serial_force_puts(" kptr=");
            __boot_serial_force_puthex64((uint64_t)(uintptr_t)(bo->data+(offset-start)));
            __boot_serial_force_puts(" phys=");
            __boot_serial_force_puthex64(bo->phys+(offset-start));
            __boot_serial_force_puts("\n");
        }
        return 1;
    }
    return 0;
}

void drm_mmap_release_handle(uint32_t handle){
    c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(handle);
    if(!bo)return;
    if(bo->mmap_refcount)bo->mmap_refcount--;
    c4_drm_bo_try_free(bo);
}

void drm_file_close(int fd){
    c4_drm_file_t *file=c4_drm_file_get(fd,false);
    int i;
    if(!file)return;
    for(i=0;i<C4_DRM_MAX_FILE_HANDLES;++i){
        c4_drm_file_handle_t *h=&g_c4_drm_file_handles[i];
        c4_drm_bo_t *bo;
        if(!h->used||h->file_id!=file->id)continue;
        bo=c4_drm_find_bo_by_handle(h->handle);
        if(bo){
            if(bo->handle_refcount>h->refs)bo->handle_refcount-=h->refs;
            else bo->handle_refcount=0;
            c4_drm_bo_try_free(bo);
        }
        h->used=false;
    }
    if(g_c4_drm_master_file==file->id)g_c4_drm_master_file=0;
    file->used=false;
}

int drm_resolve_prime_fd(int32_t fd,uint64_t offset,uint64_t len,uint64_t *kernel_ptr,uint64_t *avail){
    uint32_t handle;
    c4_drm_bo_t *bo;
    handle=c4_drm_handle_from_prime_fd(fd);
    bo=handle?c4_drm_find_bo_by_handle(handle):0;
    if(!bo||!bo->used||!bo->data)return 0;
    if(offset>=bo->alloc_size)return 0;
    if(len&&len>bo->alloc_size-offset)return -EINVAL;
    if(kernel_ptr)*kernel_ptr=(uint64_t)(uintptr_t)(bo->data+offset);
    if(avail)*avail=bo->alloc_size-offset;
    return 1;
}

static int c4_drm_present_visible_mirror(c4_drm_bo_t *bo){
    static uint32_t mirror_count;
    static c4_drm_bo_t *mirror_bo;
    c4_drm_bo_t *dst;
    uint32_t x,y,w,h;
    uint64_t copied;
    int rc=0;
    if(!bo||!bo->real_created||!bo->data||!bo->pitch)return -EINVAL;
    w=bo->width?bo->width:1024u;
    h=bo->height?bo->height:768u;
    if(!mirror_bo){
        mirror_bo=c4_drm_alloc_bo(w,h,32u);
        if(!mirror_bo)return -ENOMEM;
        rc=c4_vgpu_real_resource_create_2d(mirror_bo);
        if(rc<0)return rc;
    }
    dst=mirror_bo;
    if(!dst||!dst->data||dst->size<(uint64_t)dst->pitch*(uint64_t)h)return -EINVAL;
    copied=c3_copy_visible_shell_frame(dst->data,dst->pitch,w,h);
    if(!copied){
        for(y=0;y<h;++y){
            uint32_t *row=(uint32_t*)(void*)(dst->data+(uint64_t)y*(uint64_t)dst->pitch);
            for(x=0;x<w;++x){
                uint32_t r=(x*255u)/(w?w:1u);
                uint32_t g=(y*255u)/(h?h:1u);
                uint32_t b=(x^y)&255u;
                row[x]=0xff000000u|(r<<16)|(g<<8)|b;
            }
        }
    }
    rc=c4_vgpu_real_transfer_2d_to_host(dst);
    c4_vgpu_real_set_scanout_bo(dst);
    c4_vgpu_real_flush_bo(dst);
    ++mirror_count;
    c4_virtgpu_trace_u64("scanout-mirror",bo->handle,
                          ((uint64_t)dst->handle<<32)|(uint32_t)rc,
                          ((uint64_t)w<<32)|h,copied?copied:dst->pitch);
    return rc;
}

static bool c4_drm_scanout_mirror_disabled(void){
    static int cached=-1;
    if(cached<0)
        cached=kvfs_exists("/etc/ridux-wayfire-scanout-mirror.disable")?1:0;
    return cached!=0;
}

static bool c4_drm_scanout_probe_enabled(void){
    static int cached=-1;
    if(cached<0)
        cached=kvfs_exists("/etc/ridux-wayfire-scanout-probe.enable")?1:0;
    return cached!=0;
}

static bool c4_drm_force_2d_rescue_on_blank(void){
    static int cached=-1;
    if(cached<0)
        cached=kvfs_exists("/etc/ridux-wayfire-present2d.enable")?1:0;
    return cached!=0;
}

static bool c4_drm_candidate_matches_output(c4_drm_bo_t *cand,c4_drm_bo_t *src){
    uint32_t w=src&&src->width?src->width:(g_fb.width?g_fb.width:1024u);
    uint32_t h=src&&src->height?src->height:(g_fb.height?g_fb.height:768u);
    if(!cand||!cand->real_created||!cand->width||!cand->height)return false;
    if(cand->width==w&&cand->height==h)return true;
    if(g_fb.width&&g_fb.height&&cand->width==g_fb.width&&cand->height==g_fb.height)return true;
    return false;
}

static bool c4_drm_try_scanout_candidate(c4_drm_bo_t *src,uint32_t handle){
    static uint32_t candidate_trace;
    c4_drm_bo_t *cand;
    uint32_t nz=0;
    int scan_rc,flush_rc;
    bool probe=c4_drm_scanout_probe_enabled();
    if(!handle)return false;
    cand=c4_drm_find_bo_by_handle(handle);
    if(!cand||cand==src||!c4_drm_candidate_matches_output(cand,src))return false;
    scan_rc=c4_vgpu_real_set_scanout_bo(cand);
    flush_rc=c4_vgpu_real_flush_bo(cand);
    if(probe){
        c4_drm_refresh_bo_from_gpu(cand,"candidate-refresh");
        c4_drm_sample_bo(cand,&nz,0,0,0);
    }
    if(candidate_trace<48u){
        ++candidate_trace;
        c4_virtgpu_trace_u64(probe?"scanout-candidate":"scanout-candidate-real",
                              cand->handle,src?src->handle:0,
                              ((uint64_t)(uint32_t)scan_rc<<32)|(uint32_t)flush_rc,
                              ((uint64_t)cand->width<<32)|nz);
    }
    return scan_rc==0&&flush_rc==0&&(!probe||nz!=0);
}

static bool c4_drm_present_render_candidate(c4_drm_bo_t *src){
    uint32_t tried[12];
    uint32_t n=0,i;
    for(i=0;i<(uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));++i){
        uint32_t idx=(g_c4_vgpu_present_candidate_next+
                      (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]))-1u-i)%
                     (uint32_t)(sizeof(g_c4_vgpu_present_candidates)/sizeof(g_c4_vgpu_present_candidates[0]));
        uint32_t h=g_c4_vgpu_present_candidates[idx];
        uint32_t j;
        bool seen=false;
        for(j=0;j<n;++j){
            if(tried[j]==h){seen=true;break;}
        }
        if(!seen&&n<(uint32_t)(sizeof(tried)/sizeof(tried[0])))tried[n++]=h;
    }
    for(i=0;i<n;++i){
        if(c4_drm_try_scanout_candidate(src,tried[i]))return true;
    }
    return false;
}

static void c4_drm_present_bo(c4_drm_bo_t *bo){
    uint32_t w,h,y;
    bool real_scanout=false;
    uint32_t bo_nz=0;
    if(bo&&bo->real_created&&!c4_drm_scanout_probe_enabled()){
        int scan_rc=c4_vgpu_real_set_scanout_bo(bo);
        int flush_rc=(scan_rc==0)?c4_vgpu_real_flush_bo(bo):scan_rc;
        static uint32_t real_direct_trace;
        if(real_direct_trace<96u){
            ++real_direct_trace;
            c4_virtgpu_trace_u64("scanout-real-direct",bo->handle,bo->fb_id,
                                  ((uint64_t)(uint32_t)scan_rc<<32)|(uint32_t)flush_rc,
                                  ((uint64_t)bo->width<<32)|bo->height);
        }
        if(scan_rc==0&&flush_rc==0)return;
        if(c4_vgpu_real_virgl_available())return;
    }
    if(!(bo&&g_c4_vmw_rescue_rt_handle==bo->handle))
        c4_drm_refresh_bo_from_gpu(bo,"present-refresh");
    c4_drm_sample_bo(bo,&bo_nz,0,0,0);
    if(bo&&bo->real_created&&!bo_nz){
        c4_drm_bo_t *visible=c4_drm_visible_bo_for_present(bo);
        uint32_t visible_nz=0;
        c4_drm_sample_bo(visible,&visible_nz,0,0,0);
        if(visible&&visible!=bo&&visible_nz){
            bo=visible;
            bo_nz=visible_nz;
        }
    }
    if(bo&&bo->real_created&&!bo_nz){
        c4_drm_bo_t *fbbo;
        int scan_rc=c4_vgpu_real_set_scanout_bo(bo);
        int flush_rc=(scan_rc==0)?c4_vgpu_real_flush_bo(bo):scan_rc;
        if(scan_rc==0&&flush_rc==0&&!c4_drm_scanout_probe_enabled()){
            static uint32_t trusted_host_scanout_trace;
            if(trusted_host_scanout_trace<64u){
                ++trusted_host_scanout_trace;
                c4_virtgpu_trace_u64("scanout-trusted-host",bo->handle,bo->fb_id,
                                      ((uint64_t)bo->width<<32)|bo->height,
                                      bo->size);
            }
            return;
        }
        if(c4_drm_present_render_candidate(bo))return;
        fbbo=c4_drm_find_bo_by_handle(g_c4_vgpu_last_fb_resource);
        if(fbbo&&fbbo!=bo&&fbbo->real_created){
            uint32_t fb_nz=0;
            int scan_fb_rc,flush_fb_rc;
            c4_drm_refresh_bo_from_gpu(fbbo,"fbres-refresh");
            c4_drm_sample_bo(fbbo,&fb_nz,0,0,0);
            scan_fb_rc=c4_vgpu_real_set_scanout_bo(fbbo);
            flush_fb_rc=c4_vgpu_real_flush_bo(fbbo);
            static uint32_t scanout_fbres_trace;
            if(scanout_fbres_trace<64u){
                ++scanout_fbres_trace;
                c4_virtgpu_trace_u64("scanout-fbres",fbbo->handle,bo->handle,
                                      ((uint64_t)fbbo->width<<32)|fb_nz,
                                      ((uint64_t)(uint32_t)scan_fb_rc<<32)|
                                      (uint32_t)flush_fb_rc);
            }
            if(scan_fb_rc==0&&flush_fb_rc==0&&fb_nz)return;
        }
        if(c4_drm_force_2d_rescue_on_blank()&&!c4_drm_scanout_mirror_disabled()&&
           c4_drm_present_visible_mirror(bo)==0)return;
        return;
    }
    if(bo&&bo->real_created){
        int scan_rc=c4_vgpu_real_set_scanout_bo(bo);
        int flush_rc=c4_vgpu_real_flush_bo(bo);
        real_scanout=(scan_rc==0&&flush_rc==0);
    }
    if(real_scanout){
        uint32_t nz=0;
        if(!c4_drm_scanout_probe_enabled()){
            static uint32_t trusted_scanout_trace;
            if(trusted_scanout_trace<64u){
                ++trusted_scanout_trace;
                c4_virtgpu_trace_u64("scanout-trusted",bo->handle,bo->fb_id,
                                      ((uint64_t)bo->width<<32)|bo->height,
                                      bo->size);
            }
            return;
        }
        c4_drm_sample_bo(bo,&nz,0,0,0);
        if(!nz){
            if(c4_drm_present_render_candidate(bo))return;
            c4_drm_bo_t *fbbo=c4_drm_find_bo_by_handle(g_c4_vgpu_last_fb_resource);
            if(fbbo&&fbbo!=bo&&fbbo->real_created){
                uint32_t fb_nz=0;
                int scan_fb_rc,flush_fb_rc;
                c4_drm_refresh_bo_from_gpu(fbbo,"fbres-refresh");
                c4_drm_sample_bo(fbbo,&fb_nz,0,0,0);
                scan_fb_rc=c4_vgpu_real_set_scanout_bo(fbbo);
                flush_fb_rc=c4_vgpu_real_flush_bo(fbbo);
                static uint32_t scanout_fbres_trace;
                if(scanout_fbres_trace<64u){
                    ++scanout_fbres_trace;
                    c4_virtgpu_trace_u64("scanout-fbres",fbbo->handle,bo->handle,
                                          ((uint64_t)fbbo->width<<32)|fb_nz,
                                          ((uint64_t)(uint32_t)scan_fb_rc<<32)|
                                          (uint32_t)flush_fb_rc);
                }
                if(scan_fb_rc==0&&flush_fb_rc==0&&fb_nz)return;
            }
            if(c4_drm_force_2d_rescue_on_blank()&&!c4_drm_scanout_mirror_disabled()&&
               c4_drm_present_visible_mirror(bo)==0)return;
            static uint32_t scanout_only_trace;
            if(scanout_only_trace<32u){
                ++scanout_only_trace;
                c4_virtgpu_trace_u64("scanout-only",bo->handle,bo->fb_id,
                                      ((uint64_t)bo->width<<32)|bo->height,
                                      bo->size);
            }
            return;
        }
    }
    if(bo&&bo->real_created&&c4_vgpu_real_virgl_available()&&!c4_drm_force_2d_rescue_on_blank()){
        static uint32_t no_cpu_trace;
        if(no_cpu_trace<64u){
            ++no_cpu_trace;
            c4_virtgpu_trace_u64("scanout-no-cpu-fallback",bo->handle,bo->fb_id,
                                  ((uint64_t)bo->width<<32)|bo->height,
                                  bo->size);
        }
        return;
    }
    bo=c4_drm_visible_bo_for_present(bo);
    if(!bo||!bo->used||!bo->data||!g_fb.ready||!g_fb.address)return;
    if(!ridux_fb_ensure_current_mapping())return;
    w=bo->width<g_fb.width?bo->width:g_fb.width;
    h=bo->height<g_fb.height?bo->height:g_fb.height;
    if(!w||!h)return;
    if(bo->pitch&&bo->size<(uint64_t)bo->pitch*(uint64_t)h){
        h=(uint32_t)(bo->size/(uint64_t)bo->pitch);
        if(!h)return;
    }
    if(bo->bpp==32&&g_fb.bpp==32&&g_fb.red_pos==16&&
       g_fb.green_pos==8&&g_fb.blue_pos==0&&
       (bo->pixel_format==C4_DRM_FORMAT_XRGB8888||
        bo->pixel_format==C4_DRM_FORMAT_ARGB8888)){
        uint32_t row_bytes=w*4u;
        for(y=0;y<h;++y){
            ulibc_memcpy(g_fb.address+(size_t)y*g_fb.pitch,
                         bo->data+(size_t)y*bo->pitch,row_bytes);
        }
    }else if(bo->bpp==32&&g_fb.bpp==32){
        for(y=0;y<h;++y){
            uint32_t x;
            const uint32_t *src=(const uint32_t*)(const void*)(bo->data+(size_t)y*bo->pitch);
            uint32_t *dst=(uint32_t*)(void*)(g_fb.address+(size_t)y*g_fb.pitch);
            for(x=0;x<w;++x){
                uint32_t v=src[x];
                uint32_t r=(bo->pixel_format==C4_DRM_FORMAT_XBGR8888||
                            bo->pixel_format==C4_DRM_FORMAT_ABGR8888)?(v&0xFFu):((v>>16)&0xFFu);
                uint32_t g=(v>>8)&0xFFu;
                uint32_t b=(bo->pixel_format==C4_DRM_FORMAT_XBGR8888||
                            bo->pixel_format==C4_DRM_FORMAT_ABGR8888)?((v>>16)&0xFFu):(v&0xFFu);
                dst[x]=(r<<g_fb.red_pos)|(g<<g_fb.green_pos)|(b<<g_fb.blue_pos);
            }
        }
    }
    {
        static uint32_t present_trace;
        if(c4_virtgpu_trace_enabled()&&present_trace<48){
            uint32_t nz=0;
            uint64_t sum=0,p0=0,p1=0;
            c4_drm_sample_bo(bo,&nz,&sum,&p0,&p1);
            ++present_trace;
            __boot_serial_force_puts("[drm-present] fb=");
            __boot_serial_force_putu32(bo->fb_id);
            __boot_serial_force_puts(" handle=");
            __boot_serial_force_putu32(bo->handle);
            __boot_serial_force_puts(" fmt=");
            __boot_serial_force_puthex64((uint64_t)bo->pixel_format);
            __boot_serial_force_puts(" pitch=");
            __boot_serial_force_putu32(bo->pitch);
            __boot_serial_force_puts(" ");
            __boot_serial_force_putu32(w);
            __boot_serial_force_puts("x");
            __boot_serial_force_putu32(h);
            __boot_serial_force_puts(" nz=");
            __boot_serial_force_putu32(nz);
            __boot_serial_force_puts(" sum=");
            __boot_serial_force_puthex64(sum);
            __boot_serial_force_puts(" p0=");
            __boot_serial_force_puthex64(p0);
            __boot_serial_force_puts(" p1=");
            __boot_serial_force_puthex64(p1);
            __boot_serial_force_puts("\n");
        }
    }
}

static bool c4_drm_present_fb_vbox_or_cpu(uint32_t fb_id){
    c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(fb_id);
    if(!bo&&fb_id==C4_DRM_ID_FB)bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
    if(!bo)return false;
    if(c4_drm_vbox_mode()){
        c4_drm_bo_t *visible=c4_drm_visible_bo_for_present(bo);
        /* Composite the cursor (real BO pixels when wlroots provided one,
           otherwise the synthetic fallback) on top of the framebuffer the
           guest is about to scan out. Wayfire/wlroots repaints the FBO
           every frame so this is safe and avoids the previous behaviour
           where the cursor was never visible inside the VirtualBox VMSVGA
           path. */
        if(visible)
            (void)c4_vmw_draw_cursor_overlay(visible);
        if(c4_svga_real_present_bo(visible))return true;
    }else if(!(bo&&bo->real_created&&c4_vgpu_real_virgl_available())){
        /* Never draw a cursor into the CPU shadow of a VirGL scanout BO.
         * Hyprland renders that resource on the host GPU; its guest CPU
         * backing can be stale/black. Uploading it on pointer motion corrupts
         * the real frame, which looked like the desktop going black on click. */
        (void)c4_vmw_draw_cursor_overlay(bo);
    }
    c4_drm_present_bo(bo);
    return true;
}

/* Standard mode list for /dev/dri/card0 connector. The first entry is always
 * the current framebuffer geometry (preferred + driver). The rest provide
 * fallback resolutions so userland (KDE Plasma, Wayfire, GNOME, weston-info)
 * sees a real "monitor" with multiple modes, like a physical display. */
typedef struct {
    uint16_t w;
    uint16_t h;
    uint16_t vrefresh;
    uint8_t  type;       /* C4_DRM_MODE_TYPE_* OR'ed bits */
    uint8_t  pad;
} c4_drm_mode_entry_t;

#define C4_DRM_MODE_TYPE_DRIVER    0x40u
#define C4_DRM_MODE_TYPE_PREFERRED 0x08u
#define C4_DRM_MODE_TYPE_USERDEF   0x20u

static const c4_drm_mode_entry_t g_c4_drm_mode_table[] = {
    { 1920, 1080, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1600, 1200, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1440,  900, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1366,  768, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1280, 1024, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1280,  720, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    { 1024,  768, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    {  800,  600, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
    {  640,  480, 60, C4_DRM_MODE_TYPE_DRIVER, 0 },
};
#define C4_DRM_MODE_TABLE_LEN ((uint32_t)(sizeof(g_c4_drm_mode_table)/sizeof(g_c4_drm_mode_table[0])))

static uint32_t c4_drm_preferred_refresh(void){
    static int cached=-1;
    if(kvfs_exists("/etc/ridux-display-refresh-60.enable"))return 60u;
    if(cached<0){
        cached=(kvfs_exists("/etc/ridux-display-refresh-144.enable")||
                kvfs_exists("/etc/ridux-wayfire-virtio-gpu.enable")||
                kvfs_exists("/etc/ridux-virtio-gpu.enable")||
                c4_vgpu_real_find_device())?144:60;
    }
    return cached>0?(uint32_t)cached:60u;
}

static void c4_drm_fill_mode_geom(c4_drm_mode_modeinfo_t *m,uint32_t w,uint32_t h,
                                  uint32_t refresh,uint32_t type){
    if(!m||!w||!h)return;
    ulibc_memset(m,0,sizeof(*m));
    if(!refresh)refresh=60u;
    m->hdisplay=(uint16_t)w;
    m->hsync_start=(uint16_t)(w+16u);
    m->hsync_end=(uint16_t)(w+16u+32u);
    m->htotal=(uint16_t)(w+16u+32u+48u);
    m->vdisplay=(uint16_t)h;
    m->vsync_start=(uint16_t)(h+10u);
    m->vsync_end=(uint16_t)(h+10u+2u);
    m->vtotal=(uint16_t)(h+10u+2u+33u);
    m->vrefresh=(uint32_t)refresh;
    m->clock=(uint32_t)(((uint64_t)m->htotal*(uint64_t)m->vtotal*(uint64_t)refresh)/1000ULL);
    if(!m->clock)m->clock=65000u;
    m->type=(uint32_t)type;
    ulibc_snprintf(m->name,sizeof(m->name),"%ux%u",(unsigned)w,(unsigned)h);
}

/* Index 0 is always the current framebuffer geometry, marked PREFERRED so
 * Wayfire/KMS userland keeps the same scanout as today; entries 1..N are the
 * standard fallbacks from g_c4_drm_mode_table. Modes that match the current
 * geometry are skipped to avoid duplicates. */
static uint32_t c4_drm_modes_count(void){
    uint32_t fb_w=(g_fb.width?g_fb.width:1024u);
    uint32_t fb_h=(g_fb.height?g_fb.height:768u);
    uint32_t i,n=1u;
    for(i=0;i<C4_DRM_MODE_TABLE_LEN;++i){
        if(g_c4_drm_mode_table[i].w==fb_w&&g_c4_drm_mode_table[i].h==fb_h)continue;
        ++n;
    }
    return n;
}

static bool c4_drm_fill_mode_at(uint32_t idx,c4_drm_mode_modeinfo_t *m){
    uint32_t fb_w=(g_fb.width?g_fb.width:1024u);
    uint32_t fb_h=(g_fb.height?g_fb.height:768u);
    uint32_t i,j=1u;
    if(!m)return false;
    if(idx==0u){
        c4_drm_fill_mode_geom(m,fb_w,fb_h,c4_drm_preferred_refresh(),
            C4_DRM_MODE_TYPE_DRIVER|C4_DRM_MODE_TYPE_PREFERRED);
        return true;
    }
    for(i=0;i<C4_DRM_MODE_TABLE_LEN;++i){
        if(g_c4_drm_mode_table[i].w==fb_w&&g_c4_drm_mode_table[i].h==fb_h)continue;
        if(j==idx){
            c4_drm_fill_mode_geom(m,
                g_c4_drm_mode_table[i].w,
                g_c4_drm_mode_table[i].h,
                g_c4_drm_mode_table[i].vrefresh,
                g_c4_drm_mode_table[i].type);
            return true;
        }
        ++j;
    }
    return false;
}

static void c4_drm_fill_mode(c4_drm_mode_modeinfo_t *m){
    uint32_t fb_w=(g_fb.width?g_fb.width:1024u);
    uint32_t fb_h=(g_fb.height?g_fb.height:768u);
    c4_drm_fill_mode_geom(m,fb_w,fb_h,c4_drm_preferred_refresh(),
        C4_DRM_MODE_TYPE_DRIVER|C4_DRM_MODE_TYPE_PREFERRED);
}

/* EDID 1.3 base block, 128 bytes, with a valid checksum byte at offset 127.
 * We synthesize an EDID for a virtual "Ridux Display" so libdrm/wayland clients
 * that probe the connector see a proper monitor identity. The preferred timing
 * (descriptor 0) is built from the current framebuffer geometry. Output buffer
 * must be at least 128 bytes. */
static void c4_drm_build_edid(uint8_t *out){
    uint32_t fb_w=(g_fb.width?g_fb.width:1024u);
    uint32_t fb_h=(g_fb.height?g_fb.height:768u);
    uint32_t refresh=c4_drm_preferred_refresh();
    uint32_t mm_w=fb_w/4u;
    uint32_t mm_h=fb_h/4u;
    uint32_t pixel_clock_10khz;
    uint32_t hblank=96u,vblank=45u;
    uint32_t hsync_off=16u,hsync_pulse=32u;
    uint32_t vsync_off=10u,vsync_pulse=2u;
    uint8_t cks=0;
    int i;
    if(!out)return;
    ulibc_memset(out,0,128);
    /* Header */
    out[0]=0x00;out[1]=0xFF;out[2]=0xFF;out[3]=0xFF;
    out[4]=0xFF;out[5]=0xFF;out[6]=0xFF;out[7]=0x00;
    /* Manufacturer "RXR" -> 5-bit packed: R=18, X=24, R=18 */
    out[8]=(uint8_t)((18u<<2)|(24u>>3));            /* 0x4B */
    out[9]=(uint8_t)(((24u&0x7u)<<5)|18u);          /* 0x12 */
    /* Product code, serial, week/year */
    out[10]=0x01;out[11]=0x00;
    out[12]=0x00;out[13]=0x00;out[14]=0x00;out[15]=0x00;
    out[16]=10;       /* week 10 */
    out[17]=33;       /* year = 1990+33 = 2023 */
    /* EDID 1.3 */
    out[18]=1;out[19]=3;
    /* Video input: digital, 8-bit color, DisplayPort */
    out[20]=0x80;
    out[21]=(uint8_t)((mm_w/10u)>255?255:(mm_w/10u));
    out[22]=(uint8_t)((mm_h/10u)>255?255:(mm_h/10u));
    out[23]=120;      /* gamma 2.2 */
    out[24]=0x0E;     /* RGB color, preferred timing in DTD#1, default GTF */
    /* Chromaticity (sRGB-ish) */
    out[25]=0xEE;out[26]=0x95;out[27]=0xA3;out[28]=0x54;out[29]=0x4C;
    out[30]=0x99;out[31]=0x26;out[32]=0x0F;out[33]=0x50;out[34]=0x54;
    /* Established + standard timings: leave zero (no extra), preferred is in DTD */
    out[35]=0x00;out[36]=0x00;out[37]=0x00;
    for(i=38;i<54;++i)out[i]=0x01; /* unused standard timings */
    /* Detailed Timing Descriptor #1 (preferred): build from fb geometry */
    pixel_clock_10khz=(uint32_t)((((uint64_t)(fb_w+hblank)*(uint64_t)(fb_h+vblank)*(uint64_t)refresh)+5000ULL)/10000ULL);
    if(!pixel_clock_10khz)pixel_clock_10khz=6500u;
    out[54]=(uint8_t)(pixel_clock_10khz&0xFFu);
    out[55]=(uint8_t)((pixel_clock_10khz>>8)&0xFFu);
    out[56]=(uint8_t)(fb_w&0xFFu);
    out[57]=(uint8_t)(hblank&0xFFu);
    out[58]=(uint8_t)((((fb_w>>8)&0x0Fu)<<4)|((hblank>>8)&0x0Fu));
    out[59]=(uint8_t)(fb_h&0xFFu);
    out[60]=(uint8_t)(vblank&0xFFu);
    out[61]=(uint8_t)((((fb_h>>8)&0x0Fu)<<4)|((vblank>>8)&0x0Fu));
    out[62]=(uint8_t)(hsync_off&0xFFu);
    out[63]=(uint8_t)(hsync_pulse&0xFFu);
    out[64]=(uint8_t)(((vsync_off&0x0Fu)<<4)|(vsync_pulse&0x0Fu));
    out[65]=(uint8_t)((((hsync_off>>8)&0x03u)<<6)|
                      (((hsync_pulse>>8)&0x03u)<<4)|
                      (((vsync_off>>4)&0x03u)<<2)|
                       ((vsync_pulse>>4)&0x03u));
    out[66]=(uint8_t)((mm_w)&0xFFu);
    out[67]=(uint8_t)((mm_h)&0xFFu);
    out[68]=(uint8_t)((((mm_w>>8)&0x0Fu)<<4)|((mm_h>>8)&0x0Fu));
    out[69]=0;out[70]=0;
    out[71]=0x18;     /* digital separate sync, +H, +V */
    /* Detailed Descriptor #2: monitor name "Ridux Display" */
    out[72]=0;out[73]=0;out[74]=0;out[75]=0xFC;out[76]=0;
    {
        const char *name="Ridux Display";
        for(i=0;i<13;++i)out[77+i]=(uint8_t)(name[i]?name[i]:0x20);
    }
    /* Descriptor #3: monitor range limits. Virtio/VirGL VM mode advertises
     * a high-refresh virtual panel so wlroots does not throttle the desktop
     * to the old 60Hz compatibility contract. */
    out[90]=0;out[91]=0;out[92]=0;out[93]=0xFD;out[94]=0;
    out[95]=50;out[96]=(uint8_t)(refresh>60u?refresh:60u);out[97]=30;out[98]=90;
    out[99]=(uint8_t)(refresh>60u?60u:20u);
    out[100]=0x00;
    out[101]=0x0A;out[102]=0x20;out[103]=0x20;out[104]=0x20;
    out[105]=0x20;out[106]=0x20;out[107]=0x20;
    /* Descriptor #4: dummy / serial */
    out[108]=0;out[109]=0;out[110]=0;out[111]=0xFE;out[112]=0;
    {
        const char *desc="RiduxOS Output";
        for(i=0;i<13;++i)out[113+i]=(uint8_t)(desc[i]?desc[i]:0x20);
    }
    /* Number of extension blocks */
    out[126]=0;
    /* Checksum so the whole 128-byte block sums to 0 mod 256 */
    cks=0;
    for(i=0;i<127;++i)cks=(uint8_t)(cks+out[i]);
    out[127]=(uint8_t)(0u-cks);
}

#define C4_DRM_EDID_LEN 128u
#define C4_DRM_IN_FORMATS_MAX 256u
static uint8_t  g_c4_drm_edid[C4_DRM_EDID_LEN];
static uint8_t  g_c4_drm_in_formats[C4_DRM_IN_FORMATS_MAX];
static uint32_t g_c4_drm_in_formats_len;
static bool     g_c4_drm_static_blobs_ready;
static uint16_t g_c4_drm_gamma_r[C4_DRM_GAMMA_LUT_SIZE];
static uint16_t g_c4_drm_gamma_g[C4_DRM_GAMMA_LUT_SIZE];
static uint16_t g_c4_drm_gamma_b[C4_DRM_GAMMA_LUT_SIZE];
static uint8_t  g_c4_drm_dpms_state = C4_DRM_DPMS_ON;
static uint32_t g_c4_drm_vblank_seq;

/* Per-connector HDR / color-mgmt scalar state. Compositors update these via
 * atomic_commit() with the matching property. We keep them queryable for
 * debugging and to feed back into connector property reads. */
static c4_hdr_output_metadata_t g_c4_drm_hdr_metadata;
static uint8_t  g_c4_drm_icc_profile[3072];   /* room for a v4 sRGB ICC */
static uint32_t g_c4_drm_icc_profile_len;
static uint8_t  g_c4_drm_tile_blob[64];
static uint32_t g_c4_drm_tile_blob_len;
static uint8_t  g_c4_drm_path_blob[128];
static uint32_t g_c4_drm_path_blob_len;
static uint32_t g_c4_drm_colorspace      = 0u;   /* 0 = Default */
static uint32_t g_c4_drm_max_bpc         = 10u;
static uint32_t g_c4_drm_broadcast_rgb   = 0u;   /* 0 = Automatic */
static uint32_t g_c4_drm_panel_orient    = 0u;   /* 0 = Normal */
static uint32_t g_c4_drm_content_type    = 0u;   /* No Data */
static uint32_t g_c4_drm_scaling_mode    = 1u;   /* Full aspect */
static uint32_t g_c4_drm_hdcp_cp         = 0u;   /* 0 Undesired */
static uint32_t g_c4_drm_hdcp_cp_type    = 0u;   /* 0 HDCP Type0 */
static uint64_t g_c4_drm_background_argb = 0xFF000000ULL;
static uint32_t g_c4_drm_privacy_screen_sw = 0u;
static uint32_t g_c4_drm_privacy_screen_hw = 0u;
static uint32_t g_c4_drm_dithering_mode    = 0u;
static uint32_t g_c4_drm_output_bpp        = 24u;

/* Monotonic ns helper used by WAIT_VBLANK and page-flip events. We forward to
 * the kernel clock_gettime path so userspace timestamps line up with the rest
 * of the system clock domain (futex deadlines, evdev events, etc.). */
static uint64_t ulibc_clock_gettime_monotonic_ns(void){
    timespec_t ts;
    if(real_sys_clock_gettime(1/*CLOCK_MONOTONIC*/,&ts)==0&&ts.tv_sec>=0&&ts.tv_nsec>=0)
        return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
    return 0ULL;
}

uint32_t ridux_drm_vblank_sequence(void){return g_c4_drm_vblank_seq;}
uint64_t ridux_drm_now_ns(void){return ulibc_clock_gettime_monotonic_ns();}

/* drm_format_modifier_blob layout, like Linux include/uapi/drm/drm_mode.h:
 *  struct drm_format_modifier_blob {
 *      u32 version; u32 flags; u32 count_formats; u32 formats_offset;
 *      u32 count_modifiers; u32 modifiers_offset;
 *      // followed by formats[] (u32) and drm_format_modifier[] entries.
 *  };
 *  struct drm_format_modifier { u64 formats; u32 offset; u32 pad; u64 modifier; };
 * We expose the full BGRA/RGBA/RGB565/2101010/16-bit-float matrix plus YUV
 * subsampled formats so wlroots/Mesa GBM/Vulkan WSI can negotiate everything
 * an Arch-class desktop expects. The modifier set is just LINEAR + INVALID,
 * which is the common baseline that maps cleanly to dumb buffers. */
static void c4_drm_build_in_formats(void){
    typedef struct __attribute__((packed)){
        uint32_t version;
        uint32_t flags;
        uint32_t count_formats;
        uint32_t formats_offset;
        uint32_t count_modifiers;
        uint32_t modifiers_offset;
    } in_formats_hdr_t;
    typedef struct __attribute__((packed)){
        uint64_t formats;
        uint32_t offset;
        uint32_t pad;
        uint64_t modifier;
    } in_formats_mod_t;
    static const uint32_t k_formats[]={
        C4_DRM_FORMAT_XRGB8888,
        C4_DRM_FORMAT_ARGB8888,
        C4_DRM_FORMAT_XBGR8888,
        C4_DRM_FORMAT_ABGR8888,
        C4_DRM_FORMAT_RGB565,
        C4_DRM_FORMAT_BGR565,
        C4_DRM_FORMAT_RGB888,
        C4_DRM_FORMAT_BGR888,
        C4_DRM_FORMAT_XRGB2101010,
        C4_DRM_FORMAT_ARGB2101010,
        C4_DRM_FORMAT_XBGR2101010,
        C4_DRM_FORMAT_ABGR2101010,
        C4_DRM_FORMAT_XBGR16161616F,
        C4_DRM_FORMAT_ABGR16161616F,
        C4_DRM_FORMAT_NV12,
        C4_DRM_FORMAT_YUV420,
    };
    static const uint64_t k_modifiers[]={
        C4_DRM_FORMAT_MOD_LINEAR,
        C4_DRM_FORMAT_MOD_INVALID,
    };
    uint32_t nfmt=(uint32_t)(sizeof(k_formats)/sizeof(k_formats[0]));
    uint32_t nmod=(uint32_t)(sizeof(k_modifiers)/sizeof(k_modifiers[0]));
    in_formats_hdr_t *hdr;
    uint32_t *formats;
    in_formats_mod_t *mods;
    uint32_t fmts_off,mods_off,total,i;
    uint64_t fmt_mask;
    fmts_off=(uint32_t)sizeof(in_formats_hdr_t);
    mods_off=fmts_off+(uint32_t)sizeof(uint32_t)*nfmt;
    total=mods_off+(uint32_t)sizeof(in_formats_mod_t)*nmod;
    if(total>sizeof(g_c4_drm_in_formats)){
        /* Truncate gracefully if we ever overflow the static buffer. */
        nmod=1u;
        total=mods_off+(uint32_t)sizeof(in_formats_mod_t);
        if(total>sizeof(g_c4_drm_in_formats))
            total=(uint32_t)sizeof(g_c4_drm_in_formats);
    }
    ulibc_memset(g_c4_drm_in_formats,0,sizeof(g_c4_drm_in_formats));
    hdr=(in_formats_hdr_t*)g_c4_drm_in_formats;
    hdr->version=1u;
    hdr->flags=0u;
    hdr->count_formats=nfmt;
    hdr->formats_offset=fmts_off;
    hdr->count_modifiers=nmod;
    hdr->modifiers_offset=mods_off;
    formats=(uint32_t*)(g_c4_drm_in_formats+fmts_off);
    for(i=0;i<nfmt;++i)formats[i]=k_formats[i];
    fmt_mask=(nfmt>=64u)?(uint64_t)-1:((1ULL<<nfmt)-1ULL);
    mods=(in_formats_mod_t*)(g_c4_drm_in_formats+mods_off);
    for(i=0;i<nmod;++i){
        mods[i].formats=fmt_mask;
        mods[i].offset=0u;
        mods[i].pad=0u;
        mods[i].modifier=k_modifiers[i];
    }
    g_c4_drm_in_formats_len=total;
}

/* Number of bytes-per-pixel for the public DRM_FORMAT_* fourcc codes we
 * advertise. Used by GBM/Vulkan WSI helpers below to size scanout buffers. */
uint32_t ridux_drm_format_bpp(uint32_t fourcc){
    switch(fourcc){
        case C4_DRM_FORMAT_XRGB8888:
        case C4_DRM_FORMAT_ARGB8888:
        case C4_DRM_FORMAT_XBGR8888:
        case C4_DRM_FORMAT_ABGR8888:
        case C4_DRM_FORMAT_XRGB2101010:
        case C4_DRM_FORMAT_ARGB2101010:
        case C4_DRM_FORMAT_XBGR2101010:
        case C4_DRM_FORMAT_ABGR2101010: return 32u;
        case C4_DRM_FORMAT_RGB565:
        case C4_DRM_FORMAT_BGR565:      return 16u;
        case C4_DRM_FORMAT_RGB888:
        case C4_DRM_FORMAT_BGR888:      return 24u;
        case C4_DRM_FORMAT_XBGR16161616F:
        case C4_DRM_FORMAT_ABGR16161616F: return 64u;
        case C4_DRM_FORMAT_NV12:
        case C4_DRM_FORMAT_NV21:
        case C4_DRM_FORMAT_YUV420:
        case C4_DRM_FORMAT_YVU420:      return 12u; /* effective per-pixel */
        default: return 32u;
    }
}

bool ridux_drm_format_is_yuv(uint32_t fourcc){
    return fourcc==C4_DRM_FORMAT_NV12||fourcc==C4_DRM_FORMAT_NV21||
           fourcc==C4_DRM_FORMAT_YUV420||fourcc==C4_DRM_FORMAT_YVU420;
}

/* ----------------------------------------------------------------------
 * Native GBM device shim. Real Linux uses libgbm.so on top of libdrm to
 * allocate scanout/render buffers. RiduxOS has a single in-tree compositor
 * that links directly against this kernel-side compat layer, so we expose
 * a small native GBM API here. The names intentionally match the public
 * gbm.h surface so the in-tree compositor can drop in a stub libgbm later.
 * ---------------------------------------------------------------------- */

#define RIDUX_GBM_BO_USE_SCANOUT      (1u<<0)
#define RIDUX_GBM_BO_USE_CURSOR       (1u<<1)
#define RIDUX_GBM_BO_USE_RENDERING    (1u<<2)
#define RIDUX_GBM_BO_USE_WRITE        (1u<<3)
#define RIDUX_GBM_BO_USE_LINEAR       (1u<<4)
#define RIDUX_GBM_BO_USE_PROTECTED    (1u<<5)
#define RIDUX_GBM_BO_USE_FRONT_RENDERING (1u<<6)

typedef struct ridux_gbm_device {
    uint32_t magic;
    int32_t  drm_fd;
    char     name[32];
} ridux_gbm_device_t;

typedef struct ridux_gbm_bo {
    uint32_t magic;
    ridux_gbm_device_t *dev;
    c4_drm_bo_t *bo;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    uint64_t modifier;
    void *map_user_ptr;
    void *user_data;
    void (*user_data_destroy)(struct ridux_gbm_bo*,void*);
} ridux_gbm_bo_t;

typedef struct ridux_gbm_surface {
    uint32_t magic;
    ridux_gbm_device_t *dev;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    uint32_t bo_count;
    ridux_gbm_bo_t *bos[3];   /* triple buffered */
    int      front_idx;
    int      back_idx;
} ridux_gbm_surface_t;

#define RIDUX_GBM_DEVICE_MAGIC 0x47424D44u    /* "GBMD" */
#define RIDUX_GBM_BO_MAGIC     0x47424D42u    /* "GBMB" */
#define RIDUX_GBM_SURF_MAGIC   0x47424D53u    /* "GBMS" */

#define RIDUX_GBM_DEVICE_MAX  4
#define RIDUX_GBM_BO_MAX      256
#define RIDUX_GBM_SURFACE_MAX 64

static ridux_gbm_device_t  g_ridux_gbm_devices[RIDUX_GBM_DEVICE_MAX];
static ridux_gbm_bo_t      g_ridux_gbm_bos[RIDUX_GBM_BO_MAX];
static ridux_gbm_surface_t g_ridux_gbm_surfaces[RIDUX_GBM_SURFACE_MAX];

static ridux_gbm_device_t *ridux_gbm_device_alloc(int32_t drm_fd,const char *name){
    int i;
    for(i=0;i<RIDUX_GBM_DEVICE_MAX;++i){
        if(g_ridux_gbm_devices[i].magic==RIDUX_GBM_DEVICE_MAGIC)continue;
        ulibc_memset(&g_ridux_gbm_devices[i],0,sizeof(g_ridux_gbm_devices[i]));
        g_ridux_gbm_devices[i].magic=RIDUX_GBM_DEVICE_MAGIC;
        g_ridux_gbm_devices[i].drm_fd=drm_fd;
        ulibc_strncpy(g_ridux_gbm_devices[i].name,name?name:"ridux-gbm",
            sizeof(g_ridux_gbm_devices[i].name)-1);
        return &g_ridux_gbm_devices[i];
    }
    return 0;
}

static ridux_gbm_bo_t *ridux_gbm_bo_alloc_slot(void){
    int i;
    for(i=0;i<RIDUX_GBM_BO_MAX;++i){
        if(g_ridux_gbm_bos[i].magic==RIDUX_GBM_BO_MAGIC)continue;
        ulibc_memset(&g_ridux_gbm_bos[i],0,sizeof(g_ridux_gbm_bos[i]));
        g_ridux_gbm_bos[i].magic=RIDUX_GBM_BO_MAGIC;
        return &g_ridux_gbm_bos[i];
    }
    return 0;
}

static ridux_gbm_surface_t *ridux_gbm_surface_alloc_slot(void){
    int i;
    for(i=0;i<RIDUX_GBM_SURFACE_MAX;++i){
        if(g_ridux_gbm_surfaces[i].magic==RIDUX_GBM_SURF_MAGIC)continue;
        ulibc_memset(&g_ridux_gbm_surfaces[i],0,sizeof(g_ridux_gbm_surfaces[i]));
        g_ridux_gbm_surfaces[i].magic=RIDUX_GBM_SURF_MAGIC;
        return &g_ridux_gbm_surfaces[i];
    }
    return 0;
}

ridux_gbm_device_t *ridux_gbm_create_device(int32_t drm_fd){
    return ridux_gbm_device_alloc(drm_fd,"ridux-gbm");
}

void ridux_gbm_device_destroy(ridux_gbm_device_t *dev){
    if(!dev||dev->magic!=RIDUX_GBM_DEVICE_MAGIC)return;
    dev->magic=0u;
}

const char *ridux_gbm_device_get_backend_name(ridux_gbm_device_t *dev){
    return (dev&&dev->magic==RIDUX_GBM_DEVICE_MAGIC)?dev->name:"ridux-gbm";
}

int32_t ridux_gbm_device_get_fd(ridux_gbm_device_t *dev){
    return (dev&&dev->magic==RIDUX_GBM_DEVICE_MAGIC)?dev->drm_fd:-1;
}

bool ridux_gbm_device_is_format_supported(ridux_gbm_device_t *dev,
                                          uint32_t format,uint32_t use){
    (void)dev;(void)use;
    if(format==C4_DRM_FORMAT_XRGB8888||format==C4_DRM_FORMAT_ARGB8888||
       format==C4_DRM_FORMAT_XBGR8888||format==C4_DRM_FORMAT_ABGR8888||
       format==C4_DRM_FORMAT_RGB565||format==C4_DRM_FORMAT_BGR565||
       format==C4_DRM_FORMAT_RGB888||format==C4_DRM_FORMAT_BGR888||
       format==C4_DRM_FORMAT_XRGB2101010||format==C4_DRM_FORMAT_ARGB2101010||
       format==C4_DRM_FORMAT_XBGR2101010||format==C4_DRM_FORMAT_ABGR2101010||
       format==C4_DRM_FORMAT_XBGR16161616F||format==C4_DRM_FORMAT_ABGR16161616F)
        return true;
    return false;
}

ridux_gbm_bo_t *ridux_gbm_bo_create(ridux_gbm_device_t *dev,
                                    uint32_t width,uint32_t height,
                                    uint32_t format,uint32_t flags){
    ridux_gbm_bo_t *gbo;
    uint32_t bpp;
    c4_drm_bo_t *bo;
    c4_drm_file_t *file;
    if(!dev||dev->magic!=RIDUX_GBM_DEVICE_MAGIC)return 0;
    bpp=ridux_drm_format_bpp(format);
    if(!bpp)bpp=32u;
    bo=c4_drm_alloc_bo(width,height,bpp);
    if(!bo)return 0;
    bo->kernel_refcount=0;
    file=c4_drm_file_get(dev->drm_fd,true);
    if(!file||!c4_drm_file_handle_add(file,bo)){
        c4_drm_bo_try_free(bo);
        return 0;
    }
    gbo=ridux_gbm_bo_alloc_slot();
    if(!gbo){
        (void)c4_drm_file_handle_remove(file,bo->handle);
        return 0;
    }
    bo->pixel_format=format;
    gbo->dev=dev;
    gbo->bo=bo;
    gbo->width=width;
    gbo->height=height;
    gbo->format=format;
    gbo->flags=flags;
    gbo->modifier=(flags&RIDUX_GBM_BO_USE_LINEAR)?C4_DRM_FORMAT_MOD_LINEAR:
                                                    C4_DRM_FORMAT_MOD_LINEAR;
    return gbo;
}

ridux_gbm_bo_t *ridux_gbm_bo_create_with_modifiers(ridux_gbm_device_t *dev,
                                                   uint32_t width,uint32_t height,
                                                   uint32_t format,
                                                   const uint64_t *modifiers,
                                                   uint32_t count){
    ridux_gbm_bo_t *gbo=ridux_gbm_bo_create(dev,width,height,format,
        RIDUX_GBM_BO_USE_SCANOUT|RIDUX_GBM_BO_USE_RENDERING);
    uint32_t i;
    if(!gbo)return 0;
    /* Pick the first known modifier; default to LINEAR if none match. */
    for(i=0;i<count&&modifiers;++i){
        if(modifiers[i]==C4_DRM_FORMAT_MOD_LINEAR){gbo->modifier=modifiers[i];break;}
        if(modifiers[i]==C4_DRM_FORMAT_MOD_INVALID){gbo->modifier=modifiers[i];break;}
    }
    return gbo;
}

ridux_gbm_bo_t *ridux_gbm_bo_import_dmabuf(ridux_gbm_device_t *dev,
                                           int32_t prime_fd,uint32_t width,
                                           uint32_t height,uint32_t format,
                                           uint32_t stride){
    ridux_gbm_bo_t *gbo;
    uint32_t handle;
    c4_drm_bo_t *bo;
    c4_drm_file_t *file;
    if(!dev||dev->magic!=RIDUX_GBM_DEVICE_MAGIC)return 0;
    handle=c4_drm_handle_from_prime_fd(prime_fd);
    bo=handle?c4_drm_find_bo_by_handle(handle):0;
    if(!bo)return 0;
    file=c4_drm_file_get(dev->drm_fd,true);
    if(file&&!c4_drm_file_handle_find(file,bo->handle,true))
        (void)c4_drm_file_handle_add(file,bo);
    gbo=ridux_gbm_bo_alloc_slot();
    if(!gbo)return 0;
    if(!bo->pitch&&stride)bo->pitch=stride;
    if(!bo->width&&width)bo->width=width;
    if(!bo->height&&height)bo->height=height;
    bo->pixel_format=format;
    gbo->dev=dev;
    gbo->bo=bo;
    gbo->width=bo->width;
    gbo->height=bo->height;
    gbo->format=format;
    gbo->flags=RIDUX_GBM_BO_USE_RENDERING|RIDUX_GBM_BO_USE_LINEAR;
    gbo->modifier=C4_DRM_FORMAT_MOD_LINEAR;
    return gbo;
}

void ridux_gbm_bo_destroy(ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC)return;
    if(gbo->user_data_destroy)
        gbo->user_data_destroy(gbo,gbo->user_data);
    if(gbo->dev&&gbo->bo){
        c4_drm_file_t *file=c4_drm_file_get(gbo->dev->drm_fd,false);
        if(file)(void)c4_drm_file_handle_remove(file,gbo->bo->handle);
    }
    gbo->magic=0u;
}

uint32_t ridux_gbm_bo_get_width(const ridux_gbm_bo_t *gbo){
    return (gbo&&gbo->magic==RIDUX_GBM_BO_MAGIC)?gbo->width:0u;
}
uint32_t ridux_gbm_bo_get_height(const ridux_gbm_bo_t *gbo){
    return (gbo&&gbo->magic==RIDUX_GBM_BO_MAGIC)?gbo->height:0u;
}
uint32_t ridux_gbm_bo_get_stride(const ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC||!gbo->bo)return 0u;
    return gbo->bo->pitch;
}
uint32_t ridux_gbm_bo_get_format(const ridux_gbm_bo_t *gbo){
    return (gbo&&gbo->magic==RIDUX_GBM_BO_MAGIC)?gbo->format:0u;
}
uint32_t ridux_gbm_bo_get_handle(const ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC||!gbo->bo)return 0u;
    return gbo->bo->handle;
}
uint64_t ridux_gbm_bo_get_modifier(const ridux_gbm_bo_t *gbo){
    return (gbo&&gbo->magic==RIDUX_GBM_BO_MAGIC)?gbo->modifier:
        C4_DRM_FORMAT_MOD_INVALID;
}
uint32_t ridux_gbm_bo_get_offset(const ridux_gbm_bo_t *gbo,uint32_t plane){
    (void)gbo;(void)plane;
    return 0u;
}
uint32_t ridux_gbm_bo_get_plane_count(const ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC)return 1u;
    return ridux_drm_format_is_yuv(gbo->format)?2u:1u;
}
uint32_t ridux_gbm_bo_get_bpp(const ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC)return 0u;
    return ridux_drm_format_bpp(gbo->format);
}

int32_t ridux_gbm_bo_get_fd(ridux_gbm_bo_t *gbo){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC||!gbo->bo)return -EINVAL;
    return c4_drm_prime_fd_for_bo(gbo->bo);
}

void *ridux_gbm_bo_map(ridux_gbm_bo_t *gbo,uint32_t x,uint32_t y,
                       uint32_t width,uint32_t height,
                       uint32_t flags,uint32_t *stride_out,void **map_data){
    (void)flags;
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC||!gbo->bo||!gbo->bo->data)return 0;
    if(stride_out)*stride_out=gbo->bo->pitch;
    if(map_data)*map_data=(void*)gbo;
    if(x>=gbo->width||y>=gbo->height)return 0;
    if(width&&height&&(x+width>gbo->width||y+height>gbo->height))return 0;
    return (void*)(gbo->bo->data+(size_t)y*(size_t)gbo->bo->pitch+
        (size_t)x*(size_t)(gbo->bo->bpp/8u));
}

void ridux_gbm_bo_unmap(ridux_gbm_bo_t *gbo,void *map_data){
    (void)gbo;(void)map_data;
}

void ridux_gbm_bo_set_user_data(ridux_gbm_bo_t *gbo,void *data,
                                void (*destroy)(ridux_gbm_bo_t*,void*)){
    if(!gbo||gbo->magic!=RIDUX_GBM_BO_MAGIC)return;
    gbo->user_data=data;
    gbo->user_data_destroy=destroy;
}

void *ridux_gbm_bo_get_user_data(ridux_gbm_bo_t *gbo){
    return (gbo&&gbo->magic==RIDUX_GBM_BO_MAGIC)?gbo->user_data:0;
}

ridux_gbm_surface_t *ridux_gbm_surface_create(ridux_gbm_device_t *dev,
                                              uint32_t width,uint32_t height,
                                              uint32_t format,uint32_t flags){
    ridux_gbm_surface_t *gs;
    uint32_t i;
    if(!dev||dev->magic!=RIDUX_GBM_DEVICE_MAGIC)return 0;
    gs=ridux_gbm_surface_alloc_slot();
    if(!gs)return 0;
    gs->dev=dev;
    gs->width=width;
    gs->height=height;
    gs->format=format;
    gs->flags=flags;
    gs->bo_count=2u;          /* default double buffering */
    for(i=0;i<gs->bo_count;++i){
        gs->bos[i]=ridux_gbm_bo_create(dev,width,height,format,
            flags|RIDUX_GBM_BO_USE_RENDERING|RIDUX_GBM_BO_USE_SCANOUT);
        if(!gs->bos[i]){
            uint32_t j;
            for(j=0;j<i;++j)ridux_gbm_bo_destroy(gs->bos[j]);
            gs->magic=0u;
            return 0;
        }
    }
    gs->front_idx=-1;
    gs->back_idx=0;
    return gs;
}

void ridux_gbm_surface_destroy(ridux_gbm_surface_t *gs){
    uint32_t i;
    if(!gs||gs->magic!=RIDUX_GBM_SURF_MAGIC)return;
    for(i=0;i<gs->bo_count;++i)
        if(gs->bos[i])ridux_gbm_bo_destroy(gs->bos[i]);
    gs->magic=0u;
}

ridux_gbm_bo_t *ridux_gbm_surface_lock_front_buffer(ridux_gbm_surface_t *gs){
    if(!gs||gs->magic!=RIDUX_GBM_SURF_MAGIC)return 0;
    /* Move back -> front for the next page-flip cycle. */
    gs->front_idx=gs->back_idx;
    gs->back_idx=(gs->back_idx+1)%(int)gs->bo_count;
    if(gs->front_idx<0||gs->front_idx>=(int)gs->bo_count)return 0;
    return gs->bos[gs->front_idx];
}

void ridux_gbm_surface_release_buffer(ridux_gbm_surface_t *gs,ridux_gbm_bo_t *gbo){
    (void)gs;(void)gbo;
}

bool ridux_gbm_surface_has_free_buffers(ridux_gbm_surface_t *gs){
    return gs&&gs->magic==RIDUX_GBM_SURF_MAGIC;
}

/* ----------------------------------------------------------------------
 * Vulkan WSI / KMS display extension shim. The in-tree compositor and any
 * future Vulkan tools call these to query the single attached connector
 * and submit Vulkan-shaped page-flips. Behind the scenes we forward to the
 * DRM/KMS path already implemented above (atomic commit + page flip).
 * ---------------------------------------------------------------------- */

typedef struct ridux_vk_display_info {
    uint32_t display_id;            /* opaque, always 1 in the single-output shim */
    uint32_t plane_index;           /* primary plane index */
    uint32_t plane_stack_index;
    uint32_t persistent_content;
    uint32_t supported_alpha;
    uint32_t min_src_pos_x;
    uint32_t min_src_pos_y;
    uint32_t max_src_pos_x;
    uint32_t max_src_pos_y;
    uint32_t min_src_extent_w;
    uint32_t min_src_extent_h;
    uint32_t max_src_extent_w;
    uint32_t max_src_extent_h;
    uint32_t min_dst_pos_x;
    uint32_t min_dst_pos_y;
    uint32_t max_dst_pos_x;
    uint32_t max_dst_pos_y;
    uint32_t min_dst_extent_w;
    uint32_t min_dst_extent_h;
    uint32_t max_dst_extent_w;
    uint32_t max_dst_extent_h;
    char     name[32];
} ridux_vk_display_info_t;

typedef struct ridux_vk_display_mode {
    uint32_t mode_id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_milli_hz;      /* e.g. 60000 for 60.000 Hz */
} ridux_vk_display_mode_t;

uint32_t ridux_vk_display_count(void){
    return 1u;
}

bool ridux_vk_display_get_info(uint32_t index,ridux_vk_display_info_t *out){
    uint32_t w,h;
    if(index!=0u||!out)return false;
    w=(g_fb.width?g_fb.width:1024u);
    h=(g_fb.height?g_fb.height:768u);
    ulibc_memset(out,0,sizeof(*out));
    out->display_id=1u;
    out->plane_index=0u;
    out->plane_stack_index=0u;
    out->persistent_content=1u;
    out->supported_alpha=0x0Fu;     /* OPAQUE|GLOBAL|PER_PIXEL|PER_PIXEL_PREMULT */
    out->max_src_pos_x=w;
    out->max_src_pos_y=h;
    out->max_src_extent_w=w;
    out->max_src_extent_h=h;
    out->max_dst_pos_x=w;
    out->max_dst_pos_y=h;
    out->max_dst_extent_w=w;
    out->max_dst_extent_h=h;
    ulibc_strncpy(out->name,"Ridux Display",sizeof(out->name)-1);
    return true;
}

uint32_t ridux_vk_display_modes(uint32_t display_index,
                                ridux_vk_display_mode_t *out,uint32_t cap){
    uint32_t i,total;
    if(display_index!=0u)return 0u;
    total=c4_drm_modes_count();
    if(out&&cap){
        uint32_t limit=cap<total?cap:total;
        for(i=0;i<limit;++i){
            c4_drm_mode_modeinfo_t m;
            c4_drm_fill_mode_at(i,&m);
            out[i].mode_id=i+1u;
            out[i].width=m.hdisplay;
            out[i].height=m.vdisplay;
            out[i].refresh_milli_hz=m.vrefresh*1000u;
        }
    }
    return total;
}

/* Vulkan WSI swapchain accept/present hook. The compositor calls this when
 * vkQueuePresentKHR is invoked and the chosen image must reach the scanout.
 * pixels points to the rendered ARGB/XRGB buffer; stride is in pixels. */
bool ridux_vk_display_present(uint32_t display_index,
                              const uint32_t *pixels,uint32_t width,
                              uint32_t height,uint32_t stride_pixels){
    if(display_index!=0u)return false;
    return ridux_gpu_present_backbuffer(pixels,width,height,stride_pixels);
}

const char *ridux_vk_present_backend(void){
    return ridux_gpu_present_backend();
}

/* ----------------------------------------------------------------------
 * Vulkan WSI swapchain object. Wraps a small ring of GBM BOs and tracks the
 * acquire/present cursor explicitly. Supports VK_PRESENT_MODE_FIFO (default)
 * and VK_PRESENT_MODE_MAILBOX (overwrite latest pending buffer).
 * ---------------------------------------------------------------------- */

#define RIDUX_VK_SWAPCHAIN_MAX 16
#define RIDUX_VK_SWAPCHAIN_BO_MAX 4

struct ridux_vk_swapchain {
    uint32_t magic;
    uint32_t image_count;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    ridux_vk_present_mode_t mode;
    ridux_gbm_bo_t *bos[RIDUX_VK_SWAPCHAIN_BO_MAX];
    uint32_t in_flight[RIDUX_VK_SWAPCHAIN_BO_MAX]; /* bitmap of acquired indices */
    uint32_t next_acquire;
    uint32_t pending_present;
    uint64_t frame_count;
    ridux_gbm_device_t *gbm;
};

#define RIDUX_VK_SWAPCHAIN_MAGIC 0x564B5343u  /* "VKSC" */

static ridux_vk_swapchain_t g_ridux_vk_swapchains[RIDUX_VK_SWAPCHAIN_MAX];

ridux_vk_swapchain_t *ridux_vk_swapchain_create(ridux_gbm_device_t *dev,
                                                uint32_t width,uint32_t height,
                                                uint32_t format,uint32_t image_count,
                                                ridux_vk_present_mode_t mode){
    int i,j;
    ridux_vk_swapchain_t *sc=0;
    if(!dev||!image_count)return 0;
    if(image_count>RIDUX_VK_SWAPCHAIN_BO_MAX)image_count=RIDUX_VK_SWAPCHAIN_BO_MAX;
    for(i=0;i<RIDUX_VK_SWAPCHAIN_MAX;++i){
        if(g_ridux_vk_swapchains[i].magic==RIDUX_VK_SWAPCHAIN_MAGIC)continue;
        sc=&g_ridux_vk_swapchains[i];
        break;
    }
    if(!sc)return 0;
    ulibc_memset(sc,0,sizeof(*sc));
    sc->magic=RIDUX_VK_SWAPCHAIN_MAGIC;
    sc->image_count=image_count;
    sc->width=width;
    sc->height=height;
    sc->format=format;
    sc->mode=mode;
    sc->gbm=dev;
    sc->next_acquire=0;
    sc->pending_present=(uint32_t)-1;
    for(j=0;j<(int)image_count;++j){
        sc->bos[j]=ridux_gbm_bo_create(dev,width,height,format,
            RIDUX_GBM_BO_USE_SCANOUT|RIDUX_GBM_BO_USE_RENDERING);
        if(!sc->bos[j]){
            int k;
            for(k=0;k<j;++k)ridux_gbm_bo_destroy(sc->bos[k]);
            sc->magic=0u;
            return 0;
        }
    }
    return sc;
}

void ridux_vk_swapchain_destroy(ridux_vk_swapchain_t *sc){
    uint32_t i;
    if(!sc||sc->magic!=RIDUX_VK_SWAPCHAIN_MAGIC)return;
    for(i=0;i<sc->image_count;++i)
        if(sc->bos[i])ridux_gbm_bo_destroy(sc->bos[i]);
    sc->magic=0u;
}

bool ridux_vk_swapchain_acquire_next(ridux_vk_swapchain_t *sc,uint32_t *out_index){
    uint32_t idx;
    if(!sc||sc->magic!=RIDUX_VK_SWAPCHAIN_MAGIC||!out_index)return false;
    idx=sc->next_acquire%sc->image_count;
    sc->next_acquire=(sc->next_acquire+1)%sc->image_count;
    sc->in_flight[idx]=1u;
    *out_index=idx;
    return true;
}

ridux_gbm_bo_t *ridux_vk_swapchain_image(ridux_vk_swapchain_t *sc,uint32_t index){
    if(!sc||sc->magic!=RIDUX_VK_SWAPCHAIN_MAGIC)return 0;
    if(index>=sc->image_count)return 0;
    return sc->bos[index];
}

bool ridux_vk_swapchain_queue_present(ridux_vk_swapchain_t *sc,uint32_t index){
    ridux_gbm_bo_t *gbo;
    if(!sc||sc->magic!=RIDUX_VK_SWAPCHAIN_MAGIC)return false;
    if(index>=sc->image_count)return false;
    gbo=sc->bos[index];
    if(!gbo||!gbo->bo||!gbo->bo->data)return false;
    /* Flip immediately by issuing a present with the BO pixels. */
    (void)ridux_gpu_present_backbuffer((const uint32_t*)gbo->bo->data,
        gbo->width,gbo->height,gbo->bo->pitch/4u);
    ++g_c4_drm_vblank_seq;
    sc->in_flight[index]=0u;
    if(sc->mode==RIDUX_VK_PRESENT_MODE_MAILBOX){
        if(sc->pending_present!=(uint32_t)-1)
            sc->in_flight[sc->pending_present]=0u;
        sc->pending_present=(uint32_t)-1;
    }else{
        sc->pending_present=index;
    }
    ++sc->frame_count;
    return true;
}

uint64_t ridux_vk_swapchain_frame_count(const ridux_vk_swapchain_t *sc){
    return (sc&&sc->magic==RIDUX_VK_SWAPCHAIN_MAGIC)?sc->frame_count:0ULL;
}

uint32_t ridux_vk_swapchain_image_count(const ridux_vk_swapchain_t *sc){
    return (sc&&sc->magic==RIDUX_VK_SWAPCHAIN_MAGIC)?sc->image_count:0u;
}

/* ----------------------------------------------------------------------
 * Compositor-facing high-level KMS helpers. The in-tree Ridux Wayland
 * compositor uses these to drive the framebuffer instead of issuing raw
 * ioctls. They map 1:1 to the equivalent libdrm calls so the same code
 * also works against a foreign DRM master (e.g. /dev/dri/card0 on Linux).
 * ---------------------------------------------------------------------- */

typedef struct ridux_drm_mode_summary {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_milli_hz;
    uint32_t flags;
} ridux_drm_mode_summary_t;

typedef struct ridux_drm_connector_summary {
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t crtc_id;
    uint32_t connection;            /* 1=connected,2=disconnected,3=unknown */
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t mode_count;
    uint32_t connector_type;
    uint32_t connector_type_id;
    char     name[32];
} ridux_drm_connector_summary_t;

uint32_t ridux_drm_connector_count(void){
    return 1u;
}

bool ridux_drm_connector_summary(uint32_t index,ridux_drm_connector_summary_t *out){
    uint32_t w,h;
    if(index!=0u||!out)return false;
    w=(g_fb.width?g_fb.width:1024u);
    h=(g_fb.height?g_fb.height:768u);
    ulibc_memset(out,0,sizeof(*out));
    out->connector_id=C4_DRM_ID_CONNECTOR;
    out->encoder_id=C4_DRM_ID_ENCODER;
    out->crtc_id=C4_DRM_ID_CRTC;
    out->connection=1u;
    out->mm_width=w/4u;
    out->mm_height=h/4u;
    out->subpixel=1u;
    out->mode_count=c4_drm_modes_count();
    out->connector_type=11u;        /* DisplayPort */
    out->connector_type_id=1u;
    ulibc_strncpy(out->name,"DP-1",sizeof(out->name)-1);
    return true;
}

uint32_t ridux_drm_connector_modes(uint32_t index,
                                   ridux_drm_mode_summary_t *out,uint32_t cap){
    uint32_t i,total;
    if(index!=0u)return 0u;
    total=c4_drm_modes_count();
    if(out&&cap){
        uint32_t limit=cap<total?cap:total;
        for(i=0;i<limit;++i){
            c4_drm_mode_modeinfo_t m;
            c4_drm_fill_mode_at(i,&m);
            out[i].width=m.hdisplay;
            out[i].height=m.vdisplay;
            out[i].refresh_milli_hz=m.vrefresh*1000u;
            out[i].flags=m.flags;
        }
    }
    return total;
}

bool ridux_drm_connector_edid(uint8_t *out,uint32_t cap,uint32_t *length){
    c4_drm_static_blobs_ensure();
    if(length)*length=C4_DRM_EDID_LEN;
    if(out&&cap>=C4_DRM_EDID_LEN){
        ulibc_memcpy(out,g_c4_drm_edid,C4_DRM_EDID_LEN);
        return true;
    }
    return false;
}

bool ridux_drm_set_dpms(uint32_t state){
    if(state>C4_DRM_DPMS_OFF)return false;
    g_c4_drm_dpms_state=(uint8_t)state;
    return true;
}
uint32_t ridux_drm_get_dpms(void){return g_c4_drm_dpms_state;}

bool ridux_drm_set_gamma(const uint16_t *r,const uint16_t *g,const uint16_t *b,
                         uint32_t count){
    uint32_t i,n;
    c4_drm_static_blobs_ensure();
    if(!count||count>C4_DRM_GAMMA_LUT_SIZE)count=C4_DRM_GAMMA_LUT_SIZE;
    n=count;
    for(i=0;i<n;++i){
        if(r)g_c4_drm_gamma_r[i]=r[i];
        if(g)g_c4_drm_gamma_g[i]=g[i];
        if(b)g_c4_drm_gamma_b[i]=b[i];
    }
    return true;
}

uint32_t ridux_drm_get_gamma_size(void){return C4_DRM_GAMMA_LUT_SIZE;}

/* HDR / color management set/get helpers. Compositors call these to drive the
 * connector state from a higher-level scene description. */
bool ridux_drm_set_hdr_metadata(const void *blob,uint32_t length){
    c4_drm_static_blobs_ensure();
    if(!blob||!length)return false;
    if(length>sizeof(g_c4_drm_hdr_metadata))length=(uint32_t)sizeof(g_c4_drm_hdr_metadata);
    ulibc_memcpy(&g_c4_drm_hdr_metadata,blob,length);
    return true;
}
uint32_t ridux_drm_get_hdr_metadata(void *out,uint32_t cap){
    uint32_t L=(uint32_t)sizeof(g_c4_drm_hdr_metadata);
    c4_drm_static_blobs_ensure();
    if(out&&cap>=L)ulibc_memcpy(out,&g_c4_drm_hdr_metadata,L);
    return L;
}

bool ridux_drm_set_icc_profile(const void *blob,uint32_t length){
    if(!blob||!length)return false;
    if(length>sizeof(g_c4_drm_icc_profile))length=(uint32_t)sizeof(g_c4_drm_icc_profile);
    ulibc_memcpy(g_c4_drm_icc_profile,blob,length);
    g_c4_drm_icc_profile_len=length;
    return true;
}
uint32_t ridux_drm_get_icc_profile(void *out,uint32_t cap){
    c4_drm_static_blobs_ensure();
    if(out&&cap>=g_c4_drm_icc_profile_len&&g_c4_drm_icc_profile_len)
        ulibc_memcpy(out,g_c4_drm_icc_profile,g_c4_drm_icc_profile_len);
    return g_c4_drm_icc_profile_len;
}

void ridux_drm_set_colorspace(uint32_t cs){g_c4_drm_colorspace=cs;}
uint32_t ridux_drm_get_colorspace(void){return g_c4_drm_colorspace;}
void ridux_drm_set_max_bpc(uint32_t bpc){g_c4_drm_max_bpc=bpc;}
uint32_t ridux_drm_get_max_bpc(void){return g_c4_drm_max_bpc;}
void ridux_drm_set_broadcast_rgb(uint32_t v){g_c4_drm_broadcast_rgb=v;}
uint32_t ridux_drm_get_broadcast_rgb(void){return g_c4_drm_broadcast_rgb;}
void ridux_drm_set_panel_orientation(uint32_t v){g_c4_drm_panel_orient=v;}
uint32_t ridux_drm_get_panel_orientation(void){return g_c4_drm_panel_orient;}
void ridux_drm_set_content_type(uint32_t v){g_c4_drm_content_type=v;}
uint32_t ridux_drm_get_content_type(void){return g_c4_drm_content_type;}
void ridux_drm_set_scaling_mode(uint32_t v){g_c4_drm_scaling_mode=v;}
uint32_t ridux_drm_get_scaling_mode(void){return g_c4_drm_scaling_mode;}
void ridux_drm_set_hdcp(uint32_t cp,uint32_t cp_type){
    g_c4_drm_hdcp_cp=cp;g_c4_drm_hdcp_cp_type=cp_type;
}
void ridux_drm_get_hdcp(uint32_t *cp,uint32_t *cp_type){
    if(cp)*cp=g_c4_drm_hdcp_cp;
    if(cp_type)*cp_type=g_c4_drm_hdcp_cp_type;
}
void ridux_drm_set_background_color(uint64_t argb){g_c4_drm_background_argb=argb;}
uint64_t ridux_drm_get_background_color(void){return g_c4_drm_background_argb;}
void ridux_drm_set_privacy_screen(uint32_t sw_state){g_c4_drm_privacy_screen_sw=sw_state;}
uint32_t ridux_drm_get_privacy_screen(uint32_t *hw_state){
    if(hw_state)*hw_state=g_c4_drm_privacy_screen_hw;
    return g_c4_drm_privacy_screen_sw;
}
void ridux_drm_set_dithering_mode(uint32_t v){g_c4_drm_dithering_mode=v;}
uint32_t ridux_drm_get_dithering_mode(void){return g_c4_drm_dithering_mode;}
void ridux_drm_set_output_bpp(uint32_t v){g_c4_drm_output_bpp=v;}
uint32_t ridux_drm_get_output_bpp(void){return g_c4_drm_output_bpp;}

/* High-level page flip helper. Caller passes a pixel buffer; we present it
 * via the GPU path if available, otherwise dirty-blit to the scanout BO.
 * Returns true on success and increments the global vblank counter so any
 * userland consumer of DRM_IOCTL_WAIT_VBLANK sees the new tick. */
bool ridux_drm_page_flip_pixels(const uint32_t *pixels,uint32_t width,
                                uint32_t height,uint32_t stride_pixels){
    bool ok=ridux_gpu_present_backbuffer(pixels,width,height,stride_pixels);
    ++g_c4_drm_vblank_seq;
    return ok;
}

uint32_t ridux_drm_primary_plane_id(void){return C4_DRM_ID_PLANE;}
uint32_t ridux_drm_cursor_plane_id(void){return C4_DRM_ID_PLANE_CURSOR;}
uint32_t ridux_drm_overlay_plane_id(void){return C4_DRM_ID_PLANE_OVERLAY;}
uint32_t ridux_drm_crtc_id(void){return C4_DRM_ID_CRTC;}
uint32_t ridux_drm_connector_id(void){return C4_DRM_ID_CONNECTOR;}
uint32_t ridux_drm_encoder_id(void){return C4_DRM_ID_ENCODER;}

/* ----------------------------------------------------------------------
 * KMS lease helpers. Wayland compositors hand out leases to VR runtimes,
 * external display servers and game launchers. We expose a tiny lease
 * registry that mirrors libdrm semantics: one lease per session, the
 * lease covers the whole single-output config. */

typedef struct {
    bool used;
    uint32_t lessee_id;
    int32_t  lease_fd;
    uint32_t object_count;
    uint32_t objects[8];
} c4_drm_lease_t;

#define C4_DRM_LEASE_MAX 16
static c4_drm_lease_t g_c4_drm_leases[C4_DRM_LEASE_MAX];
static uint32_t g_c4_drm_next_lessee=1u;

uint32_t ridux_drm_lease_create(const uint32_t *objects,uint32_t count,
                                int32_t *out_fd){
    int i;
    int fd;
    task_t *cur=task_current();
    if(!objects||!count)return 0u;
    if(!cur)return 0u;
    for(i=0;i<C4_DRM_LEASE_MAX;++i){
        c4_drm_file_t *file;
        if(g_c4_drm_leases[i].used)continue;
        fd=fd_alloc(&cur->fdt,FDKIND_DEVFB,1,FDFL_READABLE|FDFL_WRITABLE);
        if(fd<0)return 0u;
        file=c4_drm_file_get(fd,true);
        if(!file){
            fd_close(&cur->fdt,fd);
            return 0u;
        }
        g_c4_drm_leases[i].used=true;
        g_c4_drm_leases[i].lessee_id=g_c4_drm_next_lessee++;
        g_c4_drm_leases[i].lease_fd=fd;
        g_c4_drm_leases[i].object_count=count<8u?count:8u;
        ulibc_memcpy(g_c4_drm_leases[i].objects,objects,
            g_c4_drm_leases[i].object_count*sizeof(uint32_t));
        file->lease=true;
        file->authenticated=true;
        file->lessee_id=g_c4_drm_leases[i].lessee_id;
        if(out_fd)*out_fd=fd;
        return g_c4_drm_leases[i].lessee_id;
    }
    return 0u;
}

bool ridux_drm_lease_revoke(uint32_t lessee_id){
    int i;
    for(i=0;i<C4_DRM_LEASE_MAX;++i){
        if(!g_c4_drm_leases[i].used)continue;
        if(g_c4_drm_leases[i].lessee_id!=lessee_id)continue;
        if(g_c4_drm_leases[i].lease_fd>=0)real_sys_close(g_c4_drm_leases[i].lease_fd);
        g_c4_drm_leases[i].used=false;
        return true;
    }
    return false;
}

uint32_t ridux_drm_lease_count(void){
    int i;
    uint32_t n=0;
    for(i=0;i<C4_DRM_LEASE_MAX;++i)
        if(g_c4_drm_leases[i].used)++n;
    return n;
}

/* ----------------------------------------------------------------------
 * Render fence subsystem. Mesa, wlroots and Vulkan WSI all need a way to
 * signal "this render pass is done" before scanout. We expose a tiny fence
 * registry that wraps a sync_file fd and tracks signal/wait/transfer.
 * ---------------------------------------------------------------------- */

typedef struct ridux_drm_fence {
    bool used;
    uint32_t fence_id;
    int32_t  sync_fd;            /* memfd-backed sync_file fd */
    uint64_t signal_ts_ns;       /* monotonic ns when signaled, 0 if unsignaled */
    uint64_t timeline_value;     /* timeline point if this is a timeline fence */
    bool     timeline;
    bool     signaled;
    char     name[32];
} ridux_drm_fence_t;

#define RIDUX_DRM_FENCE_MAX 256
static ridux_drm_fence_t g_ridux_drm_fences[RIDUX_DRM_FENCE_MAX];
static uint32_t g_ridux_drm_fence_seq=1u;

uint32_t ridux_drm_fence_create(const char *name,bool timeline){
    int i;
    int64_t fd;
    for(i=0;i<RIDUX_DRM_FENCE_MAX;++i){
        if(g_ridux_drm_fences[i].used)continue;
        fd=real_sys_memfd_create(name?name:"ridux-fence",1u);
        if(fd<0)return 0u;
        (void)real_sys_ftruncate((int)fd,16);
        ulibc_memset(&g_ridux_drm_fences[i],0,sizeof(g_ridux_drm_fences[i]));
        g_ridux_drm_fences[i].used=true;
        g_ridux_drm_fences[i].fence_id=g_ridux_drm_fence_seq++;
        g_ridux_drm_fences[i].sync_fd=(int32_t)fd;
        g_ridux_drm_fences[i].timeline=timeline;
        ulibc_strncpy(g_ridux_drm_fences[i].name,name?name:"ridux-fence",
            sizeof(g_ridux_drm_fences[i].name)-1);
        return g_ridux_drm_fences[i].fence_id;
    }
    return 0u;
}

static ridux_drm_fence_t *ridux_drm_fence_find(uint32_t fence_id){
    int i;
    for(i=0;i<RIDUX_DRM_FENCE_MAX;++i)
        if(g_ridux_drm_fences[i].used&&g_ridux_drm_fences[i].fence_id==fence_id)
            return &g_ridux_drm_fences[i];
    return 0;
}

bool ridux_drm_fence_signal(uint32_t fence_id,uint64_t timeline_value){
    ridux_drm_fence_t *f=ridux_drm_fence_find(fence_id);
    if(!f)return false;
    f->signaled=true;
    f->timeline_value=timeline_value;
    f->signal_ts_ns=ulibc_clock_gettime_monotonic_ns();
    return true;
}

bool ridux_drm_fence_is_signaled(uint32_t fence_id){
    ridux_drm_fence_t *f=ridux_drm_fence_find(fence_id);
    return f&&f->signaled;
}

bool ridux_drm_fence_wait(uint32_t fence_id,uint64_t timeout_ns){
    ridux_drm_fence_t *f=ridux_drm_fence_find(fence_id);
    (void)timeout_ns;
    if(!f)return false;
    /* In our shim renders complete synchronously; signal lazily so the
     * caller's wait always succeeds. */
    if(!f->signaled){
        f->signaled=true;
        f->signal_ts_ns=ulibc_clock_gettime_monotonic_ns();
    }
    return true;
}

int32_t ridux_drm_fence_export_fd(uint32_t fence_id){
    ridux_drm_fence_t *f=ridux_drm_fence_find(fence_id);
    return f?f->sync_fd:-1;
}

bool ridux_drm_fence_destroy(uint32_t fence_id){
    ridux_drm_fence_t *f=ridux_drm_fence_find(fence_id);
    if(!f)return false;
    f->used=false;
    return true;
}

uint32_t ridux_drm_fence_count(void){
    int i;
    uint32_t n=0;
    for(i=0;i<RIDUX_DRM_FENCE_MAX;++i)
        if(g_ridux_drm_fences[i].used)++n;
    return n;
}

/* ----------------------------------------------------------------------
 * EGL_EXT_image_dma_buf_import_modifiers helpers. Mesa probes the supported
 * dmabuf format/modifier matrix for EGL surfaces. Expose the same matrix as
 * the DRM IN_FORMATS blob so the two paths agree. */

uint32_t ridux_egl_dmabuf_format_count(void){
    typedef struct __attribute__((packed)){
        uint32_t version,flags,count_formats,formats_offset;
        uint32_t count_modifiers,modifiers_offset;
    } in_formats_hdr_t;
    const in_formats_hdr_t *hdr;
    c4_drm_static_blobs_ensure();
    if(g_c4_drm_in_formats_len<sizeof(in_formats_hdr_t))return 0u;
    hdr=(const in_formats_hdr_t*)g_c4_drm_in_formats;
    return hdr->count_formats;
}

uint32_t ridux_egl_dmabuf_formats(uint32_t *out,uint32_t cap){
    typedef struct __attribute__((packed)){
        uint32_t version,flags,count_formats,formats_offset;
        uint32_t count_modifiers,modifiers_offset;
    } in_formats_hdr_t;
    const in_formats_hdr_t *hdr;
    const uint32_t *formats;
    uint32_t i,n;
    c4_drm_static_blobs_ensure();
    if(g_c4_drm_in_formats_len<sizeof(in_formats_hdr_t))return 0u;
    hdr=(const in_formats_hdr_t*)g_c4_drm_in_formats;
    formats=(const uint32_t*)(g_c4_drm_in_formats+hdr->formats_offset);
    n=hdr->count_formats;
    if(out&&cap){
        uint32_t limit=cap<n?cap:n;
        for(i=0;i<limit;++i)out[i]=formats[i];
    }
    return n;
}

uint32_t ridux_egl_dmabuf_modifiers(uint32_t format,uint64_t *out,uint8_t *external,
                                    uint32_t cap){
    typedef struct __attribute__((packed)){
        uint32_t version,flags,count_formats,formats_offset;
        uint32_t count_modifiers,modifiers_offset;
    } in_formats_hdr_t;
    typedef struct __attribute__((packed)){
        uint64_t formats; uint32_t offset; uint32_t pad; uint64_t modifier;
    } in_formats_mod_t;
    const in_formats_hdr_t *hdr;
    const uint32_t *formats;
    const in_formats_mod_t *mods;
    uint32_t i,j,n_out=0;
    int format_idx=-1;
    c4_drm_static_blobs_ensure();
    if(g_c4_drm_in_formats_len<sizeof(in_formats_hdr_t))return 0u;
    hdr=(const in_formats_hdr_t*)g_c4_drm_in_formats;
    formats=(const uint32_t*)(g_c4_drm_in_formats+hdr->formats_offset);
    mods=(const in_formats_mod_t*)(g_c4_drm_in_formats+hdr->modifiers_offset);
    for(i=0;i<hdr->count_formats;++i)
        if(formats[i]==format){format_idx=(int)i;break;}
    if(format_idx<0)return 0u;
    for(i=0;i<hdr->count_modifiers;++i){
        if(!(mods[i].formats&(1ULL<<format_idx)))continue;
        if(out&&n_out<cap)out[n_out]=mods[i].modifier;
        if(external&&n_out<cap)external[n_out]=0u; /* not external */
        ++n_out;
    }
    /* Iterate remaining static modifiers that may not be in the blob. */
    (void)j;
    return n_out;
}

/* ----------------------------------------------------------------------
 * Per-task GPU stats. wlroots/Mesa expose render time, presentation latency
 * and submission counts via DRM_IOCTL_GEM_VRAM_STATS-like calls. We expose
 * a lightweight in-process counter that the in-tree compositor can sample
 * each frame and forward to its IPC consumers. */

static ridux_gpu_stats_t g_ridux_gpu_stats;

void ridux_gpu_stats_record_present(uint64_t duration_ns){
    g_ridux_gpu_stats.frames_presented++;
    g_ridux_gpu_stats.last_present_ns=ulibc_clock_gettime_monotonic_ns();
    g_ridux_gpu_stats.last_present_duration_ns=duration_ns;
    g_ridux_gpu_stats.total_render_ns+=duration_ns;
}
void ridux_gpu_stats_record_drop(void){
    g_ridux_gpu_stats.frames_dropped++;
}
void ridux_gpu_stats_record_fence_signaled(void){
    g_ridux_gpu_stats.fences_signaled++;
}
void ridux_gpu_stats_record_upload(uint64_t bytes){
    g_ridux_gpu_stats.bytes_uploaded+=bytes;
}
void ridux_gpu_stats_record_download(uint64_t bytes){
    g_ridux_gpu_stats.bytes_downloaded+=bytes;
}
void ridux_gpu_stats_snapshot(ridux_gpu_stats_t *out){
    if(out)*out=g_ridux_gpu_stats;
}
void ridux_gpu_stats_reset(void){
    ulibc_memset(&g_ridux_gpu_stats,0,sizeof(g_ridux_gpu_stats));
}

/* ----------------------------------------------------------------------
 * Per-client GEM accounting (mirrors Linux drm_client_dev / drm-fdinfo).
 * Used by the in-tree compositor to surface "GPU memory usage" stats per
 * Wayland client and to enforce per-client BO quotas if the user wants. */

#define RIDUX_GEM_CLIENT_MAX 64
static ridux_gem_client_t g_ridux_gem_clients[RIDUX_GEM_CLIENT_MAX];

static ridux_gem_client_t *ridux_gem_client_get_or_alloc(uint32_t pid){
    int i,free_slot=-1;
    for(i=0;i<RIDUX_GEM_CLIENT_MAX;++i){
        if(g_ridux_gem_clients[i].used){
            if(g_ridux_gem_clients[i].client_pid==pid)
                return &g_ridux_gem_clients[i];
        }else if(free_slot<0)free_slot=i;
    }
    if(free_slot<0)return 0;
    ulibc_memset(&g_ridux_gem_clients[free_slot],0,
        sizeof(g_ridux_gem_clients[free_slot]));
    g_ridux_gem_clients[free_slot].used=true;
    g_ridux_gem_clients[free_slot].client_pid=pid;
    return &g_ridux_gem_clients[free_slot];
}

void ridux_gem_account_alloc(uint32_t pid,uint64_t bytes){
    ridux_gem_client_t *c=ridux_gem_client_get_or_alloc(pid);
    if(!c)return;
    c->bo_count++;
    c->bo_total_bytes+=bytes;
    c->bo_resident_bytes+=bytes;
    c->bo_active_bytes+=bytes;
    c->last_activity_ns=ulibc_clock_gettime_monotonic_ns();
}

void ridux_gem_account_free(uint32_t pid,uint64_t bytes){
    ridux_gem_client_t *c=ridux_gem_client_get_or_alloc(pid);
    if(!c)return;
    if(c->bo_count)c->bo_count--;
    if(c->bo_total_bytes>=bytes)c->bo_total_bytes-=bytes;else c->bo_total_bytes=0;
    if(c->bo_resident_bytes>=bytes)c->bo_resident_bytes-=bytes;else c->bo_resident_bytes=0;
    if(c->bo_active_bytes>=bytes)c->bo_active_bytes-=bytes;else c->bo_active_bytes=0;
    c->last_activity_ns=ulibc_clock_gettime_monotonic_ns();
}

void ridux_gem_account_purge(uint32_t pid,uint64_t bytes){
    ridux_gem_client_t *c=ridux_gem_client_get_or_alloc(pid);
    if(!c)return;
    if(c->bo_resident_bytes>=bytes)c->bo_resident_bytes-=bytes;else c->bo_resident_bytes=0;
    c->bo_purgeable_bytes+=bytes;
    c->last_activity_ns=ulibc_clock_gettime_monotonic_ns();
}

bool ridux_gem_account_snapshot(uint32_t pid,ridux_gem_client_t *out){
    int i;
    if(!out)return false;
    for(i=0;i<RIDUX_GEM_CLIENT_MAX;++i){
        if(g_ridux_gem_clients[i].used&&g_ridux_gem_clients[i].client_pid==pid){
            *out=g_ridux_gem_clients[i];
            return true;
        }
    }
    return false;
}

uint32_t ridux_gem_account_iterate(ridux_gem_client_t *out,uint32_t cap){
    int i;
    uint32_t n=0;
    for(i=0;i<RIDUX_GEM_CLIENT_MAX;++i){
        if(!g_ridux_gem_clients[i].used)continue;
        if(out&&n<cap)out[n]=g_ridux_gem_clients[i];
        ++n;
    }
    return n;
}

void ridux_gem_account_drop(uint32_t pid){
    int i;
    for(i=0;i<RIDUX_GEM_CLIENT_MAX;++i)
        if(g_ridux_gem_clients[i].used&&g_ridux_gem_clients[i].client_pid==pid)
            g_ridux_gem_clients[i].used=false;
}

/* ----------------------------------------------------------------------
 * Wayland wp_presentation_feedback timing helpers. The compositor publishes
 * one of these per surface presentation; consumers (audio sync, cursor
 * latency probes, sync test suites) read it to align to the scanout. */

static ridux_present_feedback_t g_ridux_present_last;

void ridux_present_feedback_record(uint64_t commit_ns,uint32_t flags){
    g_ridux_present_last.commit_ns=commit_ns;
    g_ridux_present_last.present_ns=ulibc_clock_gettime_monotonic_ns();
    /* refresh_ns from current mode (default 60Hz = 16,666,666 ns). */
    {
        c4_drm_mode_modeinfo_t m;
        c4_drm_fill_mode_at(0u,&m);
        g_ridux_present_last.refresh_ns=
            (m.vrefresh?(1000000000ULL/(uint64_t)m.vrefresh):16666666ULL);
    }
    g_ridux_present_last.seq=g_c4_drm_vblank_seq;
    g_ridux_present_last.flags=flags|RIDUX_PRESENT_FLAG_VSYNC|
                               RIDUX_PRESENT_FLAG_HW_CLOCK|
                               RIDUX_PRESENT_FLAG_HW_COMPLETION;
}

void ridux_present_feedback_get(ridux_present_feedback_t *out){
    if(!out)return;
    *out=g_ridux_present_last;
    if(!out->refresh_ns)out->refresh_ns=16666666ULL;
    if(!out->present_ns)out->present_ns=ulibc_clock_gettime_monotonic_ns();
    if(!out->flags)
        out->flags=RIDUX_PRESENT_FLAG_VSYNC|RIDUX_PRESENT_FLAG_HW_CLOCK|
                   RIDUX_PRESENT_FLAG_HW_COMPLETION;
}

uint64_t ridux_present_refresh_ns(void){
    c4_drm_mode_modeinfo_t m;
    c4_drm_fill_mode_at(0u,&m);
    return m.vrefresh?(1000000000ULL/(uint64_t)m.vrefresh):16666666ULL;
}

/* ----------------------------------------------------------------------
 * Variable refresh rate (VRR) range. Compositors negotiate adaptive sync
 * via wp_fifo / wp_drm_lease; we expose a sensible default range that
 * VirtualBox's emulated panel can honor. */

static uint32_t g_ridux_vrr_min_refresh=30u;
static uint32_t g_ridux_vrr_max_refresh=144u;
static bool     g_ridux_vrr_enabled=false;

bool ridux_drm_vrr_supported(void){return true;}
void ridux_drm_vrr_set_range(uint32_t min_hz,uint32_t max_hz){
    if(min_hz)g_ridux_vrr_min_refresh=min_hz;
    if(max_hz>=g_ridux_vrr_min_refresh)g_ridux_vrr_max_refresh=max_hz;
}
void ridux_drm_vrr_get_range(uint32_t *min_hz,uint32_t *max_hz){
    if(min_hz)*min_hz=g_ridux_vrr_min_refresh;
    if(max_hz)*max_hz=g_ridux_vrr_max_refresh;
}
void ridux_drm_vrr_set_enabled(bool en){g_ridux_vrr_enabled=en;}
bool ridux_drm_vrr_get_enabled(void){return g_ridux_vrr_enabled;}

/* ----------------------------------------------------------------------
 * linux-explicit-sync-v1 helpers (zwp_linux_explicit_synchronization_v1).
 * These wrap a Wayland surface's acquire/release fence into our render
 * fence subsystem. */

typedef struct {
    bool used;
    uint32_t surface_id;
    uint32_t acquire_fence_id;
    uint32_t release_fence_id;
} ridux_explicit_sync_t;

#define RIDUX_EXPLICIT_SYNC_MAX 128
static ridux_explicit_sync_t g_ridux_explicit_sync[RIDUX_EXPLICIT_SYNC_MAX];

bool ridux_explicit_sync_attach(uint32_t surface_id,
                                uint32_t acquire_fence_id,
                                uint32_t release_fence_id){
    int i;
    for(i=0;i<RIDUX_EXPLICIT_SYNC_MAX;++i){
        if(g_ridux_explicit_sync[i].used){
            if(g_ridux_explicit_sync[i].surface_id==surface_id){
                g_ridux_explicit_sync[i].acquire_fence_id=acquire_fence_id;
                g_ridux_explicit_sync[i].release_fence_id=release_fence_id;
                return true;
            }
        }
    }
    for(i=0;i<RIDUX_EXPLICIT_SYNC_MAX;++i){
        if(g_ridux_explicit_sync[i].used)continue;
        g_ridux_explicit_sync[i].used=true;
        g_ridux_explicit_sync[i].surface_id=surface_id;
        g_ridux_explicit_sync[i].acquire_fence_id=acquire_fence_id;
        g_ridux_explicit_sync[i].release_fence_id=release_fence_id;
        return true;
    }
    return false;
}

bool ridux_explicit_sync_signal_release(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_EXPLICIT_SYNC_MAX;++i){
        if(!g_ridux_explicit_sync[i].used)continue;
        if(g_ridux_explicit_sync[i].surface_id!=surface_id)continue;
        if(g_ridux_explicit_sync[i].release_fence_id)
            ridux_drm_fence_signal(g_ridux_explicit_sync[i].release_fence_id,1ULL);
        return true;
    }
    return false;
}

bool ridux_explicit_sync_wait_acquire(uint32_t surface_id,uint64_t timeout_ns){
    int i;
    for(i=0;i<RIDUX_EXPLICIT_SYNC_MAX;++i){
        if(!g_ridux_explicit_sync[i].used)continue;
        if(g_ridux_explicit_sync[i].surface_id!=surface_id)continue;
        if(g_ridux_explicit_sync[i].acquire_fence_id)
            return ridux_drm_fence_wait(g_ridux_explicit_sync[i].acquire_fence_id,
                                        timeout_ns);
        return true;
    }
    return false;
}

void ridux_explicit_sync_drop(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_EXPLICIT_SYNC_MAX;++i){
        if(!g_ridux_explicit_sync[i].used)continue;
        if(g_ridux_explicit_sync[i].surface_id!=surface_id)continue;
        g_ridux_explicit_sync[i].used=false;
        return;
    }
}

/* ----------------------------------------------------------------------
 * zwp_linux_dmabuf_v1 / linux-dmabuf-feedback helpers. The compositor
 * advertises the supported (format,modifier) tranches per-output via the
 * Wayland "feedback" event. We expose a flat tranche table that the
 * Wayland wire-protocol bridge can serialize directly.
 * ---------------------------------------------------------------------- */

uint32_t ridux_dmabuf_feedback_pair_count(void){
    uint32_t i,j,total=0;
    typedef struct __attribute__((packed)){
        uint32_t version,flags,count_formats,formats_offset;
        uint32_t count_modifiers,modifiers_offset;
    } in_formats_hdr_t;
    typedef struct __attribute__((packed)){
        uint64_t formats; uint32_t offset; uint32_t pad; uint64_t modifier;
    } in_formats_mod_t;
    const in_formats_hdr_t *hdr;
    const in_formats_mod_t *mods;
    c4_drm_static_blobs_ensure();
    if(g_c4_drm_in_formats_len<sizeof(in_formats_hdr_t))return 0u;
    hdr=(const in_formats_hdr_t*)g_c4_drm_in_formats;
    mods=(const in_formats_mod_t*)(g_c4_drm_in_formats+hdr->modifiers_offset);
    for(i=0;i<hdr->count_modifiers;++i)
        for(j=0;j<hdr->count_formats;++j)
            if(mods[i].formats&(1ULL<<j))++total;
    return total;
}

uint32_t ridux_dmabuf_feedback_fill(ridux_dmabuf_format_pair_t *out,uint32_t cap){
    uint32_t i,j,n=0;
    typedef struct __attribute__((packed)){
        uint32_t version,flags,count_formats,formats_offset;
        uint32_t count_modifiers,modifiers_offset;
    } in_formats_hdr_t;
    typedef struct __attribute__((packed)){
        uint64_t formats; uint32_t offset; uint32_t pad; uint64_t modifier;
    } in_formats_mod_t;
    const in_formats_hdr_t *hdr;
    const uint32_t *fmts;
    const in_formats_mod_t *mods;
    c4_drm_static_blobs_ensure();
    if(g_c4_drm_in_formats_len<sizeof(in_formats_hdr_t))return 0u;
    hdr=(const in_formats_hdr_t*)g_c4_drm_in_formats;
    fmts=(const uint32_t*)(g_c4_drm_in_formats+hdr->formats_offset);
    mods=(const in_formats_mod_t*)(g_c4_drm_in_formats+hdr->modifiers_offset);
    for(i=0;i<hdr->count_modifiers;++i){
        for(j=0;j<hdr->count_formats;++j){
            if(!(mods[i].formats&(1ULL<<j)))continue;
            if(out&&n<cap){
                out[n].format=fmts[j];
                out[n].modifier=mods[i].modifier;
                out[n].tranche_index=(uint16_t)i;
                out[n].flags=RIDUX_DMABUF_FLAG_SCANOUT;
                out[n].target_device=0u;
            }
            ++n;
        }
    }
    return n;
}

uint64_t ridux_dmabuf_main_device(void){
    /* Linux dev_t = MKDEV(226, 0) for /dev/dri/card0 packed as legacy 32-bit. */
    return ((uint64_t)226u<<8)|0u;
}

uint64_t ridux_dmabuf_target_device(uint32_t connector_id){
    (void)connector_id;
    return ridux_dmabuf_main_device();
}

/* ----------------------------------------------------------------------
 * wp_color_management_v1 / wp_color_representation_v1 helpers. Modern
 * compositors negotiate per-surface color spaces, transfer functions and
 * primaries. Wire the protocol enums to our static state and expose the
 * supported set so the in-tree compositor can advertise it. */

#define RIDUX_CM_IMAGE_DESC_MAGIC 0x434D4944u   /* "CMID" */
#define RIDUX_CM_IMAGE_DESC_MAX 64
static ridux_cm_image_description_t g_ridux_cm_descs[RIDUX_CM_IMAGE_DESC_MAX];
static uint32_t g_ridux_cm_seq=1u;

uint32_t ridux_cm_supported_primaries(uint32_t *out,uint32_t cap){
    static const uint32_t k[]={
        RIDUX_CM_PRIMARIES_SRGB,RIDUX_CM_PRIMARIES_BT709,
        RIDUX_CM_PRIMARIES_BT2020,RIDUX_CM_PRIMARIES_DCI_P3,
        RIDUX_CM_PRIMARIES_DISPLAY_P3,RIDUX_CM_PRIMARIES_ADOBE_RGB,
    };
    uint32_t i,n=(uint32_t)(sizeof(k)/sizeof(k[0]));
    if(out&&cap){
        uint32_t lim=cap<n?cap:n;
        for(i=0;i<lim;++i)out[i]=k[i];
    }
    return n;
}
uint32_t ridux_cm_supported_transfers(uint32_t *out,uint32_t cap){
    static const uint32_t k[]={
        RIDUX_CM_TRANSFER_SRGB,RIDUX_CM_TRANSFER_LINEAR,
        RIDUX_CM_TRANSFER_BT709,RIDUX_CM_TRANSFER_PQ,
        RIDUX_CM_TRANSFER_HLG,RIDUX_CM_TRANSFER_GAMMA22,
        RIDUX_CM_TRANSFER_ST428,
    };
    uint32_t i,n=(uint32_t)(sizeof(k)/sizeof(k[0]));
    if(out&&cap){
        uint32_t lim=cap<n?cap:n;
        for(i=0;i<lim;++i)out[i]=k[i];
    }
    return n;
}
uint32_t ridux_cm_supported_intents(uint32_t *out,uint32_t cap){
    static const uint32_t k[]={
        RIDUX_CM_RENDER_INTENT_PERCEPTUAL,RIDUX_CM_RENDER_INTENT_RELATIVE,
        RIDUX_CM_RENDER_INTENT_SATURATION,RIDUX_CM_RENDER_INTENT_ABSOLUTE,
        RIDUX_CM_RENDER_INTENT_RELATIVE_BPC,
    };
    uint32_t i,n=(uint32_t)(sizeof(k)/sizeof(k[0]));
    if(out&&cap){
        uint32_t lim=cap<n?cap:n;
        for(i=0;i<lim;++i)out[i]=k[i];
    }
    return n;
}

uint32_t ridux_cm_image_description_create(ridux_cm_primaries_t prim,
                                           ridux_cm_transfer_t  tf,
                                           uint32_t max_cll,uint32_t max_fall,
                                           uint32_t white_luminance,
                                           uint32_t black_luminance){
    int i;
    for(i=0;i<RIDUX_CM_IMAGE_DESC_MAX;++i){
        if(g_ridux_cm_descs[i].magic==RIDUX_CM_IMAGE_DESC_MAGIC)continue;
        ulibc_memset(&g_ridux_cm_descs[i],0,sizeof(g_ridux_cm_descs[i]));
        g_ridux_cm_descs[i].magic=RIDUX_CM_IMAGE_DESC_MAGIC;
        g_ridux_cm_descs[i].image_description_id=g_ridux_cm_seq++;
        g_ridux_cm_descs[i].primaries=prim;
        g_ridux_cm_descs[i].transfer=tf;
        g_ridux_cm_descs[i].max_cll=max_cll;
        g_ridux_cm_descs[i].max_fall=max_fall;
        g_ridux_cm_descs[i].white_luminance=white_luminance;
        g_ridux_cm_descs[i].black_luminance=black_luminance;
        return g_ridux_cm_descs[i].image_description_id;
    }
    return 0u;
}

bool ridux_cm_image_description_get(uint32_t image_description_id,
                                    ridux_cm_image_description_t *out){
    int i;
    if(!out)return false;
    for(i=0;i<RIDUX_CM_IMAGE_DESC_MAX;++i){
        if(g_ridux_cm_descs[i].magic!=RIDUX_CM_IMAGE_DESC_MAGIC)continue;
        if(g_ridux_cm_descs[i].image_description_id!=image_description_id)continue;
        *out=g_ridux_cm_descs[i];
        return true;
    }
    return false;
}

bool ridux_cm_image_description_destroy(uint32_t image_description_id){
    int i;
    for(i=0;i<RIDUX_CM_IMAGE_DESC_MAX;++i){
        if(g_ridux_cm_descs[i].magic!=RIDUX_CM_IMAGE_DESC_MAGIC)continue;
        if(g_ridux_cm_descs[i].image_description_id!=image_description_id)continue;
        g_ridux_cm_descs[i].magic=0u;
        return true;
    }
    return false;
}

/* ----------------------------------------------------------------------
 * Damage tracking. Wayland surfaces submit dirty regions per frame; we
 * accumulate them per-output and feed them to the next page-flip so the
 * scanout backend (VBox VMSVGA / virtio-gpu DEFINE_SCREEN+UPDATE) can
 * hand the GPU only the rectangles that actually changed. */

#define RIDUX_DAMAGE_MAX 64
static ridux_damage_rect_t g_ridux_damage_rects[RIDUX_DAMAGE_MAX];
static uint32_t g_ridux_damage_count;

void ridux_damage_reset(void){
    g_ridux_damage_count=0u;
    ulibc_memset(g_ridux_damage_rects,0,sizeof(g_ridux_damage_rects));
}

bool ridux_damage_add(int32_t x,int32_t y,int32_t w,int32_t h){
    if(w<=0||h<=0)return false;
    if(g_ridux_damage_count>=RIDUX_DAMAGE_MAX){
        /* Coalesce by extending the bounding box of the last rect. */
        ridux_damage_rect_t *last=&g_ridux_damage_rects[RIDUX_DAMAGE_MAX-1];
        int32_t x2=last->x+last->w, y2=last->y+last->h;
        int32_t nx2=x+w, ny2=y+h;
        if(x<last->x)last->x=x;
        if(y<last->y)last->y=y;
        if(nx2>x2)x2=nx2;
        if(ny2>y2)y2=ny2;
        last->w=x2-last->x;
        last->h=y2-last->y;
        return true;
    }
    g_ridux_damage_rects[g_ridux_damage_count].x=x;
    g_ridux_damage_rects[g_ridux_damage_count].y=y;
    g_ridux_damage_rects[g_ridux_damage_count].w=w;
    g_ridux_damage_rects[g_ridux_damage_count].h=h;
    ++g_ridux_damage_count;
    return true;
}

uint32_t ridux_damage_count(void){return g_ridux_damage_count;}

bool ridux_damage_get(uint32_t index,ridux_damage_rect_t *out){
    if(!out||index>=g_ridux_damage_count)return false;
    *out=g_ridux_damage_rects[index];
    return true;
}

void ridux_damage_full(int32_t w,int32_t h){
    ridux_damage_reset();
    ridux_damage_add(0,0,w,h);
}

/* ----------------------------------------------------------------------
 * Hardware video decode session API (VAAPI / VDPAU-style). Provides a
 * lightweight session registry so userland video tools can advertise
 * decode/encode capabilities consistently. Backed by software decode for
 * now, but the session/surface/buffer triplet matches the VA API surface
 * model so future GPU-accelerated codepaths slot in cleanly. */

typedef struct {
    uint32_t magic;
    uint32_t session_id;
    ridux_video_codec_t codec;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t frames_processed;
    uint64_t bytes_processed;
} ridux_video_session_t;

#define RIDUX_VIDEO_SESSION_MAGIC 0x52564944u   /* "RVID" */
#define RIDUX_VIDEO_SESSION_MAX 32
static ridux_video_session_t g_ridux_video_sessions[RIDUX_VIDEO_SESSION_MAX];
static uint32_t g_ridux_video_seq=1u;

uint32_t ridux_video_capabilities(ridux_video_capability_t *out,uint32_t cap){
    static const ridux_video_capability_t k[]={
        {RIDUX_VIDEO_CODEC_H264,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT|RIDUX_VIDEO_PROFILE_10BIT,3840,2160,32400u},
        {RIDUX_VIDEO_CODEC_H265,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT|RIDUX_VIDEO_PROFILE_10BIT|RIDUX_VIDEO_PROFILE_HDR,3840,2160,32400u},
        {RIDUX_VIDEO_CODEC_VP9 ,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT|RIDUX_VIDEO_PROFILE_10BIT,3840,2160,32400u},
        {RIDUX_VIDEO_CODEC_AV1 ,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT|RIDUX_VIDEO_PROFILE_10BIT|RIDUX_VIDEO_PROFILE_HDR,3840,2160,32400u},
        {RIDUX_VIDEO_CODEC_VP8 ,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT,1920,1080,8160u},
        {RIDUX_VIDEO_CODEC_MPEG2,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT,1920,1080,8160u},
        {RIDUX_VIDEO_CODEC_JPEG,RIDUX_VIDEO_PROFILE_DECODE|RIDUX_VIDEO_PROFILE_8BIT,16384,16384,0u},
    };
    uint32_t i,n=(uint32_t)(sizeof(k)/sizeof(k[0]));
    if(out&&cap){
        uint32_t lim=cap<n?cap:n;
        for(i=0;i<lim;++i)out[i]=k[i];
    }
    return n;
}

uint32_t ridux_video_session_create(ridux_video_codec_t codec,uint32_t flags,
                                    uint32_t width,uint32_t height,uint32_t format){
    int i;
    for(i=0;i<RIDUX_VIDEO_SESSION_MAX;++i){
        if(g_ridux_video_sessions[i].magic==RIDUX_VIDEO_SESSION_MAGIC)continue;
        ulibc_memset(&g_ridux_video_sessions[i],0,sizeof(g_ridux_video_sessions[i]));
        g_ridux_video_sessions[i].magic=RIDUX_VIDEO_SESSION_MAGIC;
        g_ridux_video_sessions[i].session_id=g_ridux_video_seq++;
        g_ridux_video_sessions[i].codec=codec;
        g_ridux_video_sessions[i].flags=flags;
        g_ridux_video_sessions[i].width=width;
        g_ridux_video_sessions[i].height=height;
        g_ridux_video_sessions[i].format=format;
        return g_ridux_video_sessions[i].session_id;
    }
    return 0u;
}

bool ridux_video_session_submit(uint32_t session_id,uint64_t input_bytes){
    int i;
    for(i=0;i<RIDUX_VIDEO_SESSION_MAX;++i){
        if(g_ridux_video_sessions[i].magic!=RIDUX_VIDEO_SESSION_MAGIC)continue;
        if(g_ridux_video_sessions[i].session_id!=session_id)continue;
        g_ridux_video_sessions[i].frames_processed++;
        g_ridux_video_sessions[i].bytes_processed+=input_bytes;
        return true;
    }
    return false;
}

bool ridux_video_session_destroy(uint32_t session_id){
    int i;
    for(i=0;i<RIDUX_VIDEO_SESSION_MAX;++i){
        if(g_ridux_video_sessions[i].magic!=RIDUX_VIDEO_SESSION_MAGIC)continue;
        if(g_ridux_video_sessions[i].session_id!=session_id)continue;
        g_ridux_video_sessions[i].magic=0u;
        return true;
    }
    return false;
}

uint32_t ridux_video_session_count(void){
    int i;
    uint32_t n=0;
    for(i=0;i<RIDUX_VIDEO_SESSION_MAX;++i)
        if(g_ridux_video_sessions[i].magic==RIDUX_VIDEO_SESSION_MAGIC)++n;
    return n;
}

/* ----------------------------------------------------------------------
 * zwlr_output_management_v1 helpers. Allows external session managers
 * (kanshi, way-displays, GNOME Settings via the protocol) to read and
 * stage output configurations atomically. Single-output, but matches the
 * full protocol surface so adding multi-monitor later is mechanical. */

uint32_t ridux_output_count(void){return 1u;}

bool ridux_output_get_state(uint32_t output_id,ridux_output_state_t *out){
    uint32_t w,h;
    if(output_id!=1u||!out)return false;
    w=(g_fb.width?g_fb.width:1024u);
    h=(g_fb.height?g_fb.height:768u);
    ulibc_memset(out,0,sizeof(*out));
    out->output_id=1u;
    out->connector_id=C4_DRM_ID_CONNECTOR;
    out->crtc_id=C4_DRM_ID_CRTC;
    out->position_x=0;
    out->position_y=0;
    out->width_mm=w/4u;
    out->height_mm=h/4u;
    out->scale_milli=1000u;
    out->transform=0u;
    out->enabled=1u;
    out->refresh_milli_hz=c4_drm_preferred_refresh()*1000u;
    out->mode_width=w;
    out->mode_height=h;
    out->adaptive_sync=g_ridux_vrr_enabled?1u:0u;
    out->image_description_id=0u;
    ulibc_strncpy(out->name,"Ridux-1",sizeof(out->name)-1);
    ulibc_strncpy(out->description,"Ridux Virtual Display",sizeof(out->description)-1);
    ulibc_strncpy(out->make,"Ridux",sizeof(out->make)-1);
    ulibc_strncpy(out->model,"VBox-VMSVGA",sizeof(out->model)-1);
    return true;
}

bool ridux_output_apply_state(uint32_t output_id,const ridux_output_state_t *in){
    if(output_id!=1u||!in)return false;
    /* Single-output shim: scale, transform, mode change all forward to VRR
     * + DPMS state so subsequent ridux_drm_get_* see the new values. */
    g_ridux_vrr_enabled=(in->adaptive_sync!=0u);
    return true;
}

bool ridux_output_test_state(const ridux_output_state_t *in){
    if(!in)return false;
    if(in->mode_width==0u||in->mode_height==0u)return false;
    if(in->scale_milli<250u||in->scale_milli>5000u)return false;
    if(in->transform>7u)return false;
    return true;
}

/* ----------------------------------------------------------------------
 * Idle inhibit + keyboard shortcuts inhibit. Applications such as media
 * players (video, presentations) inhibit idle to keep the display awake;
 * fullscreen games inhibit keyboard shortcuts so the compositor does not
 * intercept Alt+Tab. We expose a tiny refcount registry per scope. */

typedef struct {
    bool used;
    uint32_t inhibitor_id;
    uint32_t surface_id;
    uint32_t scope;          /* 0=idle, 1=kbd_shortcuts */
    uint32_t client_pid;
} ridux_inhibit_t;

#define RIDUX_INHIBIT_MAX 64
static ridux_inhibit_t g_ridux_inhibits[RIDUX_INHIBIT_MAX];
static uint32_t g_ridux_inhibit_seq=1u;

uint32_t ridux_idle_inhibit_create(uint32_t surface_id,uint32_t client_pid){
    int i;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i){
        if(g_ridux_inhibits[i].used)continue;
        ulibc_memset(&g_ridux_inhibits[i],0,sizeof(g_ridux_inhibits[i]));
        g_ridux_inhibits[i].used=true;
        g_ridux_inhibits[i].inhibitor_id=g_ridux_inhibit_seq++;
        g_ridux_inhibits[i].surface_id=surface_id;
        g_ridux_inhibits[i].scope=0u;
        g_ridux_inhibits[i].client_pid=client_pid;
        return g_ridux_inhibits[i].inhibitor_id;
    }
    return 0u;
}

uint32_t ridux_kbd_shortcuts_inhibit_create(uint32_t surface_id,uint32_t client_pid){
    int i;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i){
        if(g_ridux_inhibits[i].used)continue;
        ulibc_memset(&g_ridux_inhibits[i],0,sizeof(g_ridux_inhibits[i]));
        g_ridux_inhibits[i].used=true;
        g_ridux_inhibits[i].inhibitor_id=g_ridux_inhibit_seq++;
        g_ridux_inhibits[i].surface_id=surface_id;
        g_ridux_inhibits[i].scope=1u;
        g_ridux_inhibits[i].client_pid=client_pid;
        return g_ridux_inhibits[i].inhibitor_id;
    }
    return 0u;
}

bool ridux_inhibit_destroy(uint32_t inhibitor_id){
    int i;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i){
        if(!g_ridux_inhibits[i].used)continue;
        if(g_ridux_inhibits[i].inhibitor_id!=inhibitor_id)continue;
        g_ridux_inhibits[i].used=false;
        return true;
    }
    return false;
}

bool ridux_idle_is_inhibited(void){
    int i;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i)
        if(g_ridux_inhibits[i].used&&g_ridux_inhibits[i].scope==0u)return true;
    return false;
}

bool ridux_kbd_shortcuts_is_inhibited(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i){
        if(!g_ridux_inhibits[i].used)continue;
        if(g_ridux_inhibits[i].scope!=1u)continue;
        if(g_ridux_inhibits[i].surface_id==surface_id)return true;
    }
    return false;
}

uint32_t ridux_inhibit_count_for(uint32_t scope){
    int i;
    uint32_t n=0;
    for(i=0;i<RIDUX_INHIBIT_MAX;++i)
        if(g_ridux_inhibits[i].used&&g_ridux_inhibits[i].scope==scope)++n;
    return n;
}

/* ----------------------------------------------------------------------
 * Pointer constraints + relative pointer. Implements
 * zwp_pointer_constraints_v1 (locked / confined) and the relative pointer
 * delta state used by FPS games. */

typedef struct {
    bool used;
    uint32_t constraint_id;
    uint32_t surface_id;
    uint32_t pointer_id;
    bool     locked;            /* false = confined */
    bool     active;
    ridux_pointer_lifetime_t lifetime;
    int32_t  region_x, region_y, region_w, region_h;
    int32_t  cursor_hint_x, cursor_hint_y;
    bool     has_cursor_hint;
} ridux_pointer_constraint_t;

#define RIDUX_POINTER_CONSTRAINT_MAX 32
static ridux_pointer_constraint_t g_ridux_pointer_constraints[RIDUX_POINTER_CONSTRAINT_MAX];
static uint32_t g_ridux_pointer_constraint_seq=1u;
static int32_t  g_ridux_relative_dx_acc;
static int32_t  g_ridux_relative_dy_acc;

uint32_t ridux_pointer_lock_create(uint32_t surface_id,uint32_t pointer_id,
                                   ridux_pointer_lifetime_t lifetime){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(g_ridux_pointer_constraints[i].used)continue;
        ulibc_memset(&g_ridux_pointer_constraints[i],0,
            sizeof(g_ridux_pointer_constraints[i]));
        g_ridux_pointer_constraints[i].used=true;
        g_ridux_pointer_constraints[i].constraint_id=g_ridux_pointer_constraint_seq++;
        g_ridux_pointer_constraints[i].surface_id=surface_id;
        g_ridux_pointer_constraints[i].pointer_id=pointer_id;
        g_ridux_pointer_constraints[i].locked=true;
        g_ridux_pointer_constraints[i].lifetime=lifetime;
        return g_ridux_pointer_constraints[i].constraint_id;
    }
    return 0u;
}

uint32_t ridux_pointer_confine_create(uint32_t surface_id,uint32_t pointer_id,
                                      int32_t x,int32_t y,int32_t w,int32_t h,
                                      ridux_pointer_lifetime_t lifetime){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(g_ridux_pointer_constraints[i].used)continue;
        ulibc_memset(&g_ridux_pointer_constraints[i],0,
            sizeof(g_ridux_pointer_constraints[i]));
        g_ridux_pointer_constraints[i].used=true;
        g_ridux_pointer_constraints[i].constraint_id=g_ridux_pointer_constraint_seq++;
        g_ridux_pointer_constraints[i].surface_id=surface_id;
        g_ridux_pointer_constraints[i].pointer_id=pointer_id;
        g_ridux_pointer_constraints[i].locked=false;
        g_ridux_pointer_constraints[i].lifetime=lifetime;
        g_ridux_pointer_constraints[i].region_x=x;
        g_ridux_pointer_constraints[i].region_y=y;
        g_ridux_pointer_constraints[i].region_w=w;
        g_ridux_pointer_constraints[i].region_h=h;
        return g_ridux_pointer_constraints[i].constraint_id;
    }
    return 0u;
}

bool ridux_pointer_constraint_set_cursor_hint(uint32_t constraint_id,
                                              int32_t hx,int32_t hy){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(!g_ridux_pointer_constraints[i].used)continue;
        if(g_ridux_pointer_constraints[i].constraint_id!=constraint_id)continue;
        g_ridux_pointer_constraints[i].cursor_hint_x=hx;
        g_ridux_pointer_constraints[i].cursor_hint_y=hy;
        g_ridux_pointer_constraints[i].has_cursor_hint=true;
        return true;
    }
    return false;
}

bool ridux_pointer_constraint_activate(uint32_t constraint_id,bool active){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(!g_ridux_pointer_constraints[i].used)continue;
        if(g_ridux_pointer_constraints[i].constraint_id!=constraint_id)continue;
        g_ridux_pointer_constraints[i].active=active;
        return true;
    }
    return false;
}

bool ridux_pointer_constraint_destroy(uint32_t constraint_id){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(!g_ridux_pointer_constraints[i].used)continue;
        if(g_ridux_pointer_constraints[i].constraint_id!=constraint_id)continue;
        g_ridux_pointer_constraints[i].used=false;
        return true;
    }
    return false;
}

uint32_t ridux_pointer_active_constraint(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_POINTER_CONSTRAINT_MAX;++i){
        if(!g_ridux_pointer_constraints[i].used)continue;
        if(g_ridux_pointer_constraints[i].surface_id!=surface_id)continue;
        if(!g_ridux_pointer_constraints[i].active)continue;
        return g_ridux_pointer_constraints[i].constraint_id;
    }
    return 0u;
}

void ridux_pointer_relative_accumulate(int32_t dx,int32_t dy){
    g_ridux_relative_dx_acc+=dx;
    g_ridux_relative_dy_acc+=dy;
}
void ridux_pointer_relative_drain(int32_t *dx,int32_t *dy){
    if(dx)*dx=g_ridux_relative_dx_acc;
    if(dy)*dy=g_ridux_relative_dy_acc;
    g_ridux_relative_dx_acc=0;
    g_ridux_relative_dy_acc=0;
}

/* ----------------------------------------------------------------------
 * Tablet manager v2. Pen/touchscreen tablets and Wacom-style pads. */

typedef struct {
    bool used;
    uint32_t tablet_id;
    uint32_t vid;
    uint32_t pid;
    char     name[32];
    char     path[64];
} ridux_tablet_t;

typedef struct {
    bool used;
    uint32_t tool_id;
    uint32_t tablet_id;
    ridux_tablet_tool_type_t type;
    uint64_t serial;
    uint64_t hw_id;
    bool     supports_pressure;
    bool     supports_tilt;
    bool     supports_rotation;
    bool     supports_distance;
    bool     supports_slider;
    bool     supports_wheel;
    bool     in_proximity;
    int32_t  last_x_milli;
    int32_t  last_y_milli;
    uint32_t last_pressure;
    int32_t  last_tilt_x;
    int32_t  last_tilt_y;
} ridux_tablet_tool_t;

#define RIDUX_TABLET_MAX 4
#define RIDUX_TABLET_TOOL_MAX 8
static ridux_tablet_t g_ridux_tablets[RIDUX_TABLET_MAX];
static ridux_tablet_tool_t g_ridux_tablet_tools[RIDUX_TABLET_TOOL_MAX];
static uint32_t g_ridux_tablet_seq=1u;
static uint32_t g_ridux_tablet_tool_seq=1u;

uint32_t ridux_tablet_register(uint32_t vid,uint32_t pid,
                               const char *name,const char *path){
    int i;
    for(i=0;i<RIDUX_TABLET_MAX;++i){
        if(g_ridux_tablets[i].used)continue;
        ulibc_memset(&g_ridux_tablets[i],0,sizeof(g_ridux_tablets[i]));
        g_ridux_tablets[i].used=true;
        g_ridux_tablets[i].tablet_id=g_ridux_tablet_seq++;
        g_ridux_tablets[i].vid=vid;
        g_ridux_tablets[i].pid=pid;
        if(name)ulibc_strncpy(g_ridux_tablets[i].name,name,sizeof(g_ridux_tablets[i].name)-1);
        if(path)ulibc_strncpy(g_ridux_tablets[i].path,path,sizeof(g_ridux_tablets[i].path)-1);
        return g_ridux_tablets[i].tablet_id;
    }
    return 0u;
}

uint32_t ridux_tablet_tool_register(uint32_t tablet_id,
                                    ridux_tablet_tool_type_t type,
                                    uint64_t serial,uint64_t hw_id){
    int i;
    for(i=0;i<RIDUX_TABLET_TOOL_MAX;++i){
        if(g_ridux_tablet_tools[i].used)continue;
        ulibc_memset(&g_ridux_tablet_tools[i],0,sizeof(g_ridux_tablet_tools[i]));
        g_ridux_tablet_tools[i].used=true;
        g_ridux_tablet_tools[i].tool_id=g_ridux_tablet_tool_seq++;
        g_ridux_tablet_tools[i].tablet_id=tablet_id;
        g_ridux_tablet_tools[i].type=type;
        g_ridux_tablet_tools[i].serial=serial;
        g_ridux_tablet_tools[i].hw_id=hw_id;
        g_ridux_tablet_tools[i].supports_pressure=true;
        g_ridux_tablet_tools[i].supports_tilt=true;
        g_ridux_tablet_tools[i].supports_distance=true;
        return g_ridux_tablet_tools[i].tool_id;
    }
    return 0u;
}

bool ridux_tablet_tool_motion(uint32_t tool_id,int32_t x_milli,int32_t y_milli,
                              uint32_t pressure,int32_t tilt_x,int32_t tilt_y){
    int i;
    for(i=0;i<RIDUX_TABLET_TOOL_MAX;++i){
        if(!g_ridux_tablet_tools[i].used)continue;
        if(g_ridux_tablet_tools[i].tool_id!=tool_id)continue;
        g_ridux_tablet_tools[i].last_x_milli=x_milli;
        g_ridux_tablet_tools[i].last_y_milli=y_milli;
        g_ridux_tablet_tools[i].last_pressure=pressure;
        g_ridux_tablet_tools[i].last_tilt_x=tilt_x;
        g_ridux_tablet_tools[i].last_tilt_y=tilt_y;
        return true;
    }
    return false;
}

bool ridux_tablet_tool_proximity(uint32_t tool_id,bool in_prox){
    int i;
    for(i=0;i<RIDUX_TABLET_TOOL_MAX;++i){
        if(!g_ridux_tablet_tools[i].used)continue;
        if(g_ridux_tablet_tools[i].tool_id!=tool_id)continue;
        g_ridux_tablet_tools[i].in_proximity=in_prox;
        return true;
    }
    return false;
}

uint32_t ridux_tablet_count(void){
    int i; uint32_t n=0;
    for(i=0;i<RIDUX_TABLET_MAX;++i)if(g_ridux_tablets[i].used)++n;
    return n;
}
uint32_t ridux_tablet_tool_count(void){
    int i; uint32_t n=0;
    for(i=0;i<RIDUX_TABLET_TOOL_MAX;++i)if(g_ridux_tablet_tools[i].used)++n;
    return n;
}

/* ----------------------------------------------------------------------
 * Data device / clipboard / DnD. The compositor publishes the current
 * selection (PRIMARY and CLIPBOARD) so cross-app paste works regardless of
 * toolkit. The DnD source chain tracks a single in-flight drag operation. */

#define RIDUX_DATA_OFFER_MIME_MAX  16
#define RIDUX_DATA_OFFER_MIME_LEN  64
#define RIDUX_DATA_OFFER_BUF       (1u<<20)   /* 1 MiB clipboard buffer */
typedef struct {
    bool active;
    char mime[RIDUX_DATA_OFFER_MIME_MAX][RIDUX_DATA_OFFER_MIME_LEN];
    uint32_t mime_count;
    uint8_t  data[RIDUX_DATA_OFFER_BUF];
    uint32_t data_len;
    uint32_t source_pid;
    uint32_t serial;
} ridux_data_offer_t;

static ridux_data_offer_t g_ridux_clipboard;
static ridux_data_offer_t g_ridux_primary;
static ridux_data_offer_t g_ridux_dnd;
static uint32_t g_ridux_dnd_serial=1u;

static void c4_clip_clear(ridux_data_offer_t *o){
    uint32_t i;
    if(!o)return;
    o->active=false;
    o->mime_count=0;
    o->data_len=0;
    o->source_pid=0;
    o->serial=0;
    for(i=0;i<RIDUX_DATA_OFFER_MIME_MAX;++i)o->mime[i][0]=0;
}

static bool c4_clip_offer_set(ridux_data_offer_t *o,const char *const *mimes,
                              uint32_t mime_count,const void *data,uint32_t data_len,
                              uint32_t source_pid){
    uint32_t i;
    if(!o)return false;
    c4_clip_clear(o);
    if(mime_count>RIDUX_DATA_OFFER_MIME_MAX)mime_count=RIDUX_DATA_OFFER_MIME_MAX;
    if(data_len>RIDUX_DATA_OFFER_BUF)data_len=RIDUX_DATA_OFFER_BUF;
    for(i=0;i<mime_count&&mimes;++i)
        if(mimes[i])ulibc_strncpy(o->mime[i],mimes[i],RIDUX_DATA_OFFER_MIME_LEN-1);
    o->mime_count=mime_count;
    if(data&&data_len)ulibc_memcpy(o->data,data,data_len);
    o->data_len=data_len;
    o->source_pid=source_pid;
    o->serial=g_ridux_dnd_serial++;
    o->active=true;
    return true;
}

bool ridux_clipboard_set(const char *const *mimes,uint32_t mime_count,
                         const void *data,uint32_t data_len,uint32_t source_pid){
    return c4_clip_offer_set(&g_ridux_clipboard,mimes,mime_count,data,data_len,source_pid);
}
bool ridux_primary_selection_set(const char *const *mimes,uint32_t mime_count,
                                 const void *data,uint32_t data_len,uint32_t source_pid){
    return c4_clip_offer_set(&g_ridux_primary,mimes,mime_count,data,data_len,source_pid);
}
bool ridux_dnd_set_source(const char *const *mimes,uint32_t mime_count,
                          const void *data,uint32_t data_len,uint32_t source_pid){
    return c4_clip_offer_set(&g_ridux_dnd,mimes,mime_count,data,data_len,source_pid);
}

uint32_t ridux_clipboard_mime_count(void){return g_ridux_clipboard.mime_count;}
const char *ridux_clipboard_mime_at(uint32_t i){
    if(i>=g_ridux_clipboard.mime_count)return 0;
    return g_ridux_clipboard.mime[i];
}
uint32_t ridux_clipboard_read(void *out,uint32_t cap){
    uint32_t L=g_ridux_clipboard.data_len;
    if(out&&cap){
        uint32_t lim=cap<L?cap:L;
        ulibc_memcpy(out,g_ridux_clipboard.data,lim);
    }
    return L;
}

uint32_t ridux_primary_selection_read(void *out,uint32_t cap){
    uint32_t L=g_ridux_primary.data_len;
    if(out&&cap){
        uint32_t lim=cap<L?cap:L;
        ulibc_memcpy(out,g_ridux_primary.data,lim);
    }
    return L;
}

uint32_t ridux_dnd_mime_count(void){return g_ridux_dnd.mime_count;}
const char *ridux_dnd_mime_at(uint32_t i){
    if(i>=g_ridux_dnd.mime_count)return 0;
    return g_ridux_dnd.mime[i];
}
uint32_t ridux_dnd_read(void *out,uint32_t cap){
    uint32_t L=g_ridux_dnd.data_len;
    if(out&&cap){
        uint32_t lim=cap<L?cap:L;
        ulibc_memcpy(out,g_ridux_dnd.data,lim);
    }
    return L;
}
void ridux_dnd_finish(void){c4_clip_clear(&g_ridux_dnd);}

/* ----------------------------------------------------------------------
 * Input method v2 + virtual keyboard. Lets IBus/fcitx/QtVirtualKeyboard
 * inject text and key events into the focused surface. The compositor
 * routes these through evdev. */

typedef struct {
    bool active;
    uint32_t serial;
    uint32_t focused_surface_id;
    char preedit_text[256];
    char commit_text[256];
    int32_t preedit_cursor_begin;
    int32_t preedit_cursor_end;
} ridux_input_method_state_t;

static ridux_input_method_state_t g_ridux_im_state;

bool ridux_im_activate(uint32_t surface_id){
    g_ridux_im_state.active=true;
    g_ridux_im_state.focused_surface_id=surface_id;
    g_ridux_im_state.serial++;
    return true;
}
bool ridux_im_deactivate(void){
    g_ridux_im_state.active=false;
    g_ridux_im_state.focused_surface_id=0;
    g_ridux_im_state.serial++;
    return true;
}
bool ridux_im_set_preedit(const char *text,int32_t cursor_begin,int32_t cursor_end){
    if(!g_ridux_im_state.active)return false;
    if(text)ulibc_strncpy(g_ridux_im_state.preedit_text,text,
        sizeof(g_ridux_im_state.preedit_text)-1);
    else g_ridux_im_state.preedit_text[0]=0;
    g_ridux_im_state.preedit_cursor_begin=cursor_begin;
    g_ridux_im_state.preedit_cursor_end=cursor_end;
    g_ridux_im_state.serial++;
    return true;
}
bool ridux_im_commit_text(const char *text){
    if(!g_ridux_im_state.active)return false;
    if(text)ulibc_strncpy(g_ridux_im_state.commit_text,text,
        sizeof(g_ridux_im_state.commit_text)-1);
    else g_ridux_im_state.commit_text[0]=0;
    g_ridux_im_state.serial++;
    return true;
}
const char *ridux_im_get_preedit(void){return g_ridux_im_state.preedit_text;}
const char *ridux_im_get_commit (void){return g_ridux_im_state.commit_text;}
uint32_t    ridux_im_serial    (void){return g_ridux_im_state.serial;}
uint32_t    ridux_im_focused_surface(void){return g_ridux_im_state.focused_surface_id;}
bool        ridux_im_is_active (void){return g_ridux_im_state.active;}

/* Virtual keyboard injection: forwards to the kernel-side evdev push
 * helper declared in src/compat/base.h so injected keys appear on the
 * focused surface as real key events with proper SYN_REPORT framing. */
void ridux_virtual_keyboard_press_key(uint32_t keycode){
    evdev_push_key((uint16_t)keycode,1);
}
void ridux_virtual_keyboard_release_key(uint32_t keycode){
    evdev_push_key((uint16_t)keycode,0);
}
void ridux_virtual_keyboard_press_modifier(uint32_t keycode){
    evdev_push_key((uint16_t)keycode,1);
}
void ridux_virtual_keyboard_release_modifier(uint32_t keycode){
    evdev_push_key((uint16_t)keycode,0);
}

/* ----------------------------------------------------------------------
 * xdg_toplevel state + zwlr_foreign_toplevel_management_v1. Lets task
 * switchers (wofi, fuzzel, plasma taskbars) enumerate every open window
 * and trigger maximize/minimize/close/activate without going through
 * each toolkit's own protocol. */

typedef enum {
    RIDUX_TOPLEVEL_STATE_MAXIMIZED  = (1u<<0),
    RIDUX_TOPLEVEL_STATE_MINIMIZED  = (1u<<1),
    RIDUX_TOPLEVEL_STATE_FULLSCREEN = (1u<<2),
    RIDUX_TOPLEVEL_STATE_ACTIVATED  = (1u<<3),
    RIDUX_TOPLEVEL_STATE_RESIZING   = (1u<<4),
    RIDUX_TOPLEVEL_STATE_TILED_LEFT = (1u<<5),
    RIDUX_TOPLEVEL_STATE_TILED_RIGHT= (1u<<6),
    RIDUX_TOPLEVEL_STATE_TILED_TOP  = (1u<<7),
    RIDUX_TOPLEVEL_STATE_TILED_BOTTOM=(1u<<8),
    RIDUX_TOPLEVEL_STATE_SUSPENDED  = (1u<<9),
} ridux_toplevel_state_flags_t;

typedef struct {
    bool used;
    uint32_t toplevel_id;
    uint32_t surface_id;
    uint32_t output_id;
    uint32_t workspace_id;
    uint32_t state_flags;
    uint32_t client_pid;
    int32_t  width;
    int32_t  height;
    char     title[128];
    char     app_id[128];
    char     parent_app_id[128];
} ridux_toplevel_t;

#define RIDUX_TOPLEVEL_MAX 256
static ridux_toplevel_t g_ridux_toplevels[RIDUX_TOPLEVEL_MAX];
static uint32_t g_ridux_toplevel_seq=1u;
static uint32_t g_ridux_toplevel_active_id;

uint32_t ridux_toplevel_register(uint32_t surface_id,uint32_t client_pid,
                                 const char *title,const char *app_id){
    int i;
    for(i=0;i<RIDUX_TOPLEVEL_MAX;++i){
        if(g_ridux_toplevels[i].used)continue;
        ulibc_memset(&g_ridux_toplevels[i],0,sizeof(g_ridux_toplevels[i]));
        g_ridux_toplevels[i].used=true;
        g_ridux_toplevels[i].toplevel_id=g_ridux_toplevel_seq++;
        g_ridux_toplevels[i].surface_id=surface_id;
        g_ridux_toplevels[i].client_pid=client_pid;
        g_ridux_toplevels[i].output_id=1u;
        if(title)ulibc_strncpy(g_ridux_toplevels[i].title,title,
            sizeof(g_ridux_toplevels[i].title)-1);
        if(app_id)ulibc_strncpy(g_ridux_toplevels[i].app_id,app_id,
            sizeof(g_ridux_toplevels[i].app_id)-1);
        return g_ridux_toplevels[i].toplevel_id;
    }
    return 0u;
}

static ridux_toplevel_t *ridux_toplevel_find(uint32_t toplevel_id){
    int i;
    for(i=0;i<RIDUX_TOPLEVEL_MAX;++i)
        if(g_ridux_toplevels[i].used&&g_ridux_toplevels[i].toplevel_id==toplevel_id)
            return &g_ridux_toplevels[i];
    return 0;
}

bool ridux_toplevel_set_title (uint32_t id,const char *title){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    if(title)ulibc_strncpy(t->title,title,sizeof(t->title)-1);
    else t->title[0]=0;
    return true;
}
bool ridux_toplevel_set_app_id(uint32_t id,const char *app_id){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    if(app_id)ulibc_strncpy(t->app_id,app_id,sizeof(t->app_id)-1);
    else t->app_id[0]=0;
    return true;
}
bool ridux_toplevel_set_geometry(uint32_t id,int32_t w,int32_t h){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    t->width=w;
    t->height=h;
    return true;
}
bool ridux_toplevel_set_state_flags(uint32_t id,uint32_t flags){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    t->state_flags=flags;
    if(flags&RIDUX_TOPLEVEL_STATE_ACTIVATED)g_ridux_toplevel_active_id=id;
    return true;
}
bool ridux_toplevel_request_maximize(uint32_t id,bool on){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    if(on)t->state_flags|=RIDUX_TOPLEVEL_STATE_MAXIMIZED;
    else  t->state_flags&=~RIDUX_TOPLEVEL_STATE_MAXIMIZED;
    return true;
}
bool ridux_toplevel_request_minimize(uint32_t id,bool on){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    if(on)t->state_flags|=RIDUX_TOPLEVEL_STATE_MINIMIZED;
    else  t->state_flags&=~RIDUX_TOPLEVEL_STATE_MINIMIZED;
    return true;
}
bool ridux_toplevel_request_fullscreen(uint32_t id,bool on,uint32_t output_id){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    if(on){
        t->state_flags|=RIDUX_TOPLEVEL_STATE_FULLSCREEN;
        if(output_id)t->output_id=output_id;
    }else t->state_flags&=~RIDUX_TOPLEVEL_STATE_FULLSCREEN;
    return true;
}
bool ridux_toplevel_request_activate(uint32_t id){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    int i;
    if(!t)return false;
    /* Deactivate all others. */
    for(i=0;i<RIDUX_TOPLEVEL_MAX;++i)
        if(g_ridux_toplevels[i].used)
            g_ridux_toplevels[i].state_flags&=~RIDUX_TOPLEVEL_STATE_ACTIVATED;
    t->state_flags|=RIDUX_TOPLEVEL_STATE_ACTIVATED;
    g_ridux_toplevel_active_id=id;
    return true;
}
bool ridux_toplevel_request_close(uint32_t id){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t)return false;
    t->used=false;
    if(g_ridux_toplevel_active_id==id)g_ridux_toplevel_active_id=0u;
    return true;
}
uint32_t ridux_toplevel_count(void){
    int i; uint32_t n=0;
    for(i=0;i<RIDUX_TOPLEVEL_MAX;++i)if(g_ridux_toplevels[i].used)++n;
    return n;
}
bool ridux_toplevel_get(uint32_t id,ridux_toplevel_t *out){
    ridux_toplevel_t *t=ridux_toplevel_find(id);
    if(!t||!out)return false;
    *out=*t;
    return true;
}
uint32_t ridux_toplevel_iterate(ridux_toplevel_t *out,uint32_t cap){
    int i; uint32_t n=0;
    for(i=0;i<RIDUX_TOPLEVEL_MAX;++i){
        if(!g_ridux_toplevels[i].used)continue;
        if(out&&n<cap)out[n]=g_ridux_toplevels[i];
        ++n;
    }
    return n;
}
uint32_t ridux_toplevel_active(void){return g_ridux_toplevel_active_id;}

/* ----------------------------------------------------------------------
 * Screen capture: zwlr_screencopy_v1 + ext_image_copy_capture_v1. The
 * compositor binds a destination buffer then asks the renderer to fill
 * it with the current scanout contents. We expose a single-shot blocking
 * helper that copies the last presented frame from g_fb. */

bool ridux_screencopy_capture_to(void *dst,uint32_t dst_pitch_bytes,
                                 uint32_t dst_width,uint32_t dst_height){
    uint32_t y,row_bytes,sw,sh,sp;
    uint8_t *d;
    const uint8_t *s;
    if(!dst||!dst_pitch_bytes||!g_fb.address)return false;
    sw=g_fb.width?g_fb.width:1024u;
    sh=g_fb.height?g_fb.height:768u;
    sp=g_fb.pitch?g_fb.pitch:(sw*4u);
    if(dst_width>sw)dst_width=sw;
    if(dst_height>sh)dst_height=sh;
    row_bytes=dst_width*4u;
    if(row_bytes>dst_pitch_bytes)row_bytes=dst_pitch_bytes;
    d=(uint8_t*)dst;
    s=(const uint8_t*)g_fb.address;
    for(y=0;y<dst_height;++y)
        ulibc_memcpy(d+y*dst_pitch_bytes,s+y*sp,row_bytes);
    ridux_gpu_stats_record_download((uint64_t)dst_pitch_bytes*dst_height);
    return true;
}

bool ridux_screencopy_capture_region(int32_t x,int32_t y,int32_t w,int32_t h,
                                     void *dst,uint32_t dst_pitch_bytes){
    uint32_t row,sw,sh,sp,row_bytes;
    uint8_t *d;
    const uint8_t *s;
    if(!dst||!dst_pitch_bytes||!g_fb.address)return false;
    if(x<0||y<0||w<=0||h<=0)return false;
    sw=g_fb.width?g_fb.width:1024u;
    sh=g_fb.height?g_fb.height:768u;
    sp=g_fb.pitch?g_fb.pitch:(sw*4u);
    if((uint32_t)(x+w)>sw)w=(int32_t)sw-x;
    if((uint32_t)(y+h)>sh)h=(int32_t)sh-y;
    row_bytes=(uint32_t)w*4u;
    if(row_bytes>dst_pitch_bytes)row_bytes=dst_pitch_bytes;
    d=(uint8_t*)dst;
    s=(const uint8_t*)g_fb.address;
    for(row=0;row<(uint32_t)h;++row)
        ulibc_memcpy(d+row*dst_pitch_bytes,s+(y+(int32_t)row)*sp+x*4,row_bytes);
    ridux_gpu_stats_record_download((uint64_t)dst_pitch_bytes*(uint32_t)h);
    return true;
}

uint32_t ridux_screencopy_format(void){return C4_DRM_FORMAT_XRGB8888;}

/* ----------------------------------------------------------------------
 * zwlr_layer_shell_v1 + ext_session_lock_v1. Tracks layer surfaces (panels,
 * docks, lock screens) so the input router knows which surfaces are above
 * the normal stack and the renderer knows which depth to composite at. */

typedef enum {
    RIDUX_LAYER_BACKGROUND = 0,
    RIDUX_LAYER_BOTTOM     = 1,
    RIDUX_LAYER_TOP        = 2,
    RIDUX_LAYER_OVERLAY    = 3,
} ridux_layer_t;

typedef struct {
    bool used;
    uint32_t layer_id;
    uint32_t surface_id;
    uint32_t output_id;
    ridux_layer_t layer;
    char     namespace_name[64];
    int32_t  desired_w;
    int32_t  desired_h;
    int32_t  margin_top, margin_right, margin_bottom, margin_left;
    uint32_t anchor_mask;       /* TOP/BOTTOM/LEFT/RIGHT bit-mask */
    uint32_t exclusive_zone;
    bool     keyboard_interactive;
    bool     locked_by_session;
} ridux_layer_surface_t;

#define RIDUX_LAYER_SURFACE_MAX 64
static ridux_layer_surface_t g_ridux_layer_surfaces[RIDUX_LAYER_SURFACE_MAX];
static uint32_t g_ridux_layer_seq=1u;

uint32_t ridux_layer_surface_create(uint32_t surface_id,uint32_t output_id,
                                    ridux_layer_t layer,const char *namespace_name){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(g_ridux_layer_surfaces[i].used)continue;
        ulibc_memset(&g_ridux_layer_surfaces[i],0,
            sizeof(g_ridux_layer_surfaces[i]));
        g_ridux_layer_surfaces[i].used=true;
        g_ridux_layer_surfaces[i].layer_id=g_ridux_layer_seq++;
        g_ridux_layer_surfaces[i].surface_id=surface_id;
        g_ridux_layer_surfaces[i].output_id=output_id?output_id:1u;
        g_ridux_layer_surfaces[i].layer=layer;
        if(namespace_name)
            ulibc_strncpy(g_ridux_layer_surfaces[i].namespace_name,namespace_name,
                sizeof(g_ridux_layer_surfaces[i].namespace_name)-1);
        return g_ridux_layer_surfaces[i].layer_id;
    }
    return 0u;
}

bool ridux_layer_surface_set_size(uint32_t layer_id,int32_t w,int32_t h){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].desired_w=w;
        g_ridux_layer_surfaces[i].desired_h=h;
        return true;
    }
    return false;
}
bool ridux_layer_surface_set_anchor(uint32_t layer_id,uint32_t anchor_mask){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].anchor_mask=anchor_mask;
        return true;
    }
    return false;
}
bool ridux_layer_surface_set_margin(uint32_t layer_id,int32_t t,int32_t r,
                                    int32_t b,int32_t l){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].margin_top=t;
        g_ridux_layer_surfaces[i].margin_right=r;
        g_ridux_layer_surfaces[i].margin_bottom=b;
        g_ridux_layer_surfaces[i].margin_left=l;
        return true;
    }
    return false;
}
bool ridux_layer_surface_set_exclusive_zone(uint32_t layer_id,uint32_t zone){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].exclusive_zone=zone;
        return true;
    }
    return false;
}
bool ridux_layer_surface_set_keyboard_interactive(uint32_t layer_id,bool on){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].keyboard_interactive=on;
        return true;
    }
    return false;
}
bool ridux_layer_surface_destroy(uint32_t layer_id){
    int i;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i){
        if(!g_ridux_layer_surfaces[i].used)continue;
        if(g_ridux_layer_surfaces[i].layer_id!=layer_id)continue;
        g_ridux_layer_surfaces[i].used=false;
        return true;
    }
    return false;
}
uint32_t ridux_layer_surface_count(void){
    int i; uint32_t n=0;
    for(i=0;i<RIDUX_LAYER_SURFACE_MAX;++i)if(g_ridux_layer_surfaces[i].used)++n;
    return n;
}

/* Session lock (ext_session_lock_v1): blocks input + black-out screen. */
static bool g_ridux_session_locked;
static uint32_t g_ridux_session_lock_layer_id;

bool ridux_session_lock(uint32_t lock_surface_layer_id){
    g_ridux_session_locked=true;
    g_ridux_session_lock_layer_id=lock_surface_layer_id;
    return true;
}
bool ridux_session_unlock(void){
    g_ridux_session_locked=false;
    g_ridux_session_lock_layer_id=0u;
    return true;
}
bool ridux_session_is_locked(void){return g_ridux_session_locked;}
uint32_t ridux_session_lock_surface(void){return g_ridux_session_lock_layer_id;}

/* ----------------------------------------------------------------------
 * cursor-shape-v1 + xdg-decoration-v1 + wp-fractional-scale +
 * wp-tearing-control. */

typedef enum {
    RIDUX_CURSOR_SHAPE_DEFAULT      = 1,
    RIDUX_CURSOR_SHAPE_CONTEXT_MENU = 2,
    RIDUX_CURSOR_SHAPE_HELP         = 3,
    RIDUX_CURSOR_SHAPE_POINTER      = 4,
    RIDUX_CURSOR_SHAPE_PROGRESS     = 5,
    RIDUX_CURSOR_SHAPE_WAIT         = 6,
    RIDUX_CURSOR_SHAPE_CELL         = 7,
    RIDUX_CURSOR_SHAPE_CROSSHAIR    = 8,
    RIDUX_CURSOR_SHAPE_TEXT         = 9,
    RIDUX_CURSOR_SHAPE_VERTICAL_TEXT=10,
    RIDUX_CURSOR_SHAPE_ALIAS        =11,
    RIDUX_CURSOR_SHAPE_COPY         =12,
    RIDUX_CURSOR_SHAPE_MOVE         =13,
    RIDUX_CURSOR_SHAPE_NO_DROP      =14,
    RIDUX_CURSOR_SHAPE_NOT_ALLOWED  =15,
    RIDUX_CURSOR_SHAPE_GRAB         =16,
    RIDUX_CURSOR_SHAPE_GRABBING     =17,
    RIDUX_CURSOR_SHAPE_E_RESIZE     =18,
    RIDUX_CURSOR_SHAPE_N_RESIZE     =19,
    RIDUX_CURSOR_SHAPE_NE_RESIZE    =20,
    RIDUX_CURSOR_SHAPE_NW_RESIZE    =21,
    RIDUX_CURSOR_SHAPE_S_RESIZE     =22,
    RIDUX_CURSOR_SHAPE_SE_RESIZE    =23,
    RIDUX_CURSOR_SHAPE_SW_RESIZE    =24,
    RIDUX_CURSOR_SHAPE_W_RESIZE     =25,
    RIDUX_CURSOR_SHAPE_EW_RESIZE    =26,
    RIDUX_CURSOR_SHAPE_NS_RESIZE    =27,
    RIDUX_CURSOR_SHAPE_NESW_RESIZE  =28,
    RIDUX_CURSOR_SHAPE_NWSE_RESIZE  =29,
    RIDUX_CURSOR_SHAPE_COL_RESIZE   =30,
    RIDUX_CURSOR_SHAPE_ROW_RESIZE   =31,
    RIDUX_CURSOR_SHAPE_ALL_SCROLL   =32,
    RIDUX_CURSOR_SHAPE_ZOOM_IN      =33,
    RIDUX_CURSOR_SHAPE_ZOOM_OUT     =34,
} ridux_cursor_shape_t;

static ridux_cursor_shape_t g_ridux_cursor_shape=RIDUX_CURSOR_SHAPE_DEFAULT;

void ridux_cursor_shape_set(ridux_cursor_shape_t shape){g_ridux_cursor_shape=shape;}
ridux_cursor_shape_t ridux_cursor_shape_get(void){return g_ridux_cursor_shape;}
const char *ridux_cursor_shape_name(ridux_cursor_shape_t shape){
    switch(shape){
        case RIDUX_CURSOR_SHAPE_DEFAULT: return "default";
        case RIDUX_CURSOR_SHAPE_CONTEXT_MENU: return "context-menu";
        case RIDUX_CURSOR_SHAPE_HELP: return "help";
        case RIDUX_CURSOR_SHAPE_POINTER: return "pointer";
        case RIDUX_CURSOR_SHAPE_PROGRESS: return "progress";
        case RIDUX_CURSOR_SHAPE_WAIT: return "wait";
        case RIDUX_CURSOR_SHAPE_CELL: return "cell";
        case RIDUX_CURSOR_SHAPE_CROSSHAIR: return "crosshair";
        case RIDUX_CURSOR_SHAPE_TEXT: return "text";
        case RIDUX_CURSOR_SHAPE_VERTICAL_TEXT: return "vertical-text";
        case RIDUX_CURSOR_SHAPE_ALIAS: return "alias";
        case RIDUX_CURSOR_SHAPE_COPY: return "copy";
        case RIDUX_CURSOR_SHAPE_MOVE: return "move";
        case RIDUX_CURSOR_SHAPE_NO_DROP: return "no-drop";
        case RIDUX_CURSOR_SHAPE_NOT_ALLOWED: return "not-allowed";
        case RIDUX_CURSOR_SHAPE_GRAB: return "grab";
        case RIDUX_CURSOR_SHAPE_GRABBING: return "grabbing";
        case RIDUX_CURSOR_SHAPE_E_RESIZE: return "e-resize";
        case RIDUX_CURSOR_SHAPE_N_RESIZE: return "n-resize";
        case RIDUX_CURSOR_SHAPE_NE_RESIZE: return "ne-resize";
        case RIDUX_CURSOR_SHAPE_NW_RESIZE: return "nw-resize";
        case RIDUX_CURSOR_SHAPE_S_RESIZE: return "s-resize";
        case RIDUX_CURSOR_SHAPE_SE_RESIZE: return "se-resize";
        case RIDUX_CURSOR_SHAPE_SW_RESIZE: return "sw-resize";
        case RIDUX_CURSOR_SHAPE_W_RESIZE: return "w-resize";
        case RIDUX_CURSOR_SHAPE_EW_RESIZE: return "ew-resize";
        case RIDUX_CURSOR_SHAPE_NS_RESIZE: return "ns-resize";
        case RIDUX_CURSOR_SHAPE_NESW_RESIZE: return "nesw-resize";
        case RIDUX_CURSOR_SHAPE_NWSE_RESIZE: return "nwse-resize";
        case RIDUX_CURSOR_SHAPE_COL_RESIZE: return "col-resize";
        case RIDUX_CURSOR_SHAPE_ROW_RESIZE: return "row-resize";
        case RIDUX_CURSOR_SHAPE_ALL_SCROLL: return "all-scroll";
        case RIDUX_CURSOR_SHAPE_ZOOM_IN: return "zoom-in";
        case RIDUX_CURSOR_SHAPE_ZOOM_OUT: return "zoom-out";
        default: return "default";
    }
}

typedef enum {
    RIDUX_DECORATION_MODE_CLIENT_SIDE = 1,
    RIDUX_DECORATION_MODE_SERVER_SIDE = 2,
} ridux_decoration_mode_t;

static ridux_decoration_mode_t g_ridux_decoration_mode=RIDUX_DECORATION_MODE_CLIENT_SIDE;
void ridux_decoration_mode_set(ridux_decoration_mode_t m){g_ridux_decoration_mode=m;}
ridux_decoration_mode_t ridux_decoration_mode_get(void){return g_ridux_decoration_mode;}

/* wp-fractional-scale-v1 (per-surface preferred scale, 1/120 units). */
typedef struct {
    bool used;
    uint32_t object_id;
    uint32_t surface_id;
    uint32_t scale_120;     /* 1.0 = 120 */
} ridux_fractional_scale_t;

#define RIDUX_FRACTIONAL_SCALE_MAX 64
static ridux_fractional_scale_t g_ridux_fractional[RIDUX_FRACTIONAL_SCALE_MAX];
static uint32_t g_ridux_fractional_seq=1u;

uint32_t ridux_fractional_scale_attach(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_FRACTIONAL_SCALE_MAX;++i){
        if(g_ridux_fractional[i].used)continue;
        g_ridux_fractional[i].used=true;
        g_ridux_fractional[i].object_id=g_ridux_fractional_seq++;
        g_ridux_fractional[i].surface_id=surface_id;
        g_ridux_fractional[i].scale_120=120u; /* 1.0 default */
        return g_ridux_fractional[i].object_id;
    }
    return 0u;
}
bool ridux_fractional_scale_set(uint32_t object_id,uint32_t scale_120){
    int i;
    for(i=0;i<RIDUX_FRACTIONAL_SCALE_MAX;++i){
        if(!g_ridux_fractional[i].used)continue;
        if(g_ridux_fractional[i].object_id!=object_id)continue;
        g_ridux_fractional[i].scale_120=scale_120;
        return true;
    }
    return false;
}
uint32_t ridux_fractional_scale_get(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_FRACTIONAL_SCALE_MAX;++i){
        if(!g_ridux_fractional[i].used)continue;
        if(g_ridux_fractional[i].surface_id!=surface_id)continue;
        return g_ridux_fractional[i].scale_120;
    }
    return 120u;
}
bool ridux_fractional_scale_destroy(uint32_t object_id){
    int i;
    for(i=0;i<RIDUX_FRACTIONAL_SCALE_MAX;++i){
        if(!g_ridux_fractional[i].used)continue;
        if(g_ridux_fractional[i].object_id!=object_id)continue;
        g_ridux_fractional[i].used=false;
        return true;
    }
    return false;
}

/* wp-tearing-control-v1: applications that want immediate present can hint
 * the compositor to allow tearing on a per-surface basis. */
typedef enum {
    RIDUX_TEARING_VSYNC = 0,
    RIDUX_TEARING_ASYNC = 1,
} ridux_tearing_t;

typedef struct {
    bool used;
    uint32_t surface_id;
    ridux_tearing_t hint;
} ridux_tearing_state_t;

#define RIDUX_TEARING_MAX 64
static ridux_tearing_state_t g_ridux_tearing[RIDUX_TEARING_MAX];

bool ridux_tearing_set(uint32_t surface_id,ridux_tearing_t hint){
    int i,free_slot=-1;
    for(i=0;i<RIDUX_TEARING_MAX;++i){
        if(g_ridux_tearing[i].used){
            if(g_ridux_tearing[i].surface_id==surface_id){
                g_ridux_tearing[i].hint=hint;
                return true;
            }
        }else if(free_slot<0)free_slot=i;
    }
    if(free_slot<0)return false;
    g_ridux_tearing[free_slot].used=true;
    g_ridux_tearing[free_slot].surface_id=surface_id;
    g_ridux_tearing[free_slot].hint=hint;
    return true;
}
ridux_tearing_t ridux_tearing_get(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_TEARING_MAX;++i){
        if(!g_ridux_tearing[i].used)continue;
        if(g_ridux_tearing[i].surface_id!=surface_id)continue;
        return g_ridux_tearing[i].hint;
    }
    return RIDUX_TEARING_VSYNC;
}
void ridux_tearing_clear(uint32_t surface_id){
    int i;
    for(i=0;i<RIDUX_TEARING_MAX;++i){
        if(!g_ridux_tearing[i].used)continue;
        if(g_ridux_tearing[i].surface_id!=surface_id)continue;
        g_ridux_tearing[i].used=false;
        return;
    }
}

/* Default HDR_OUTPUT_METADATA blob (BT.2020 + PQ EOTF). Compositors that
 * negotiate HDR replace this via atomic_commit, but we always keep a
 * sensible default so probing succeeds even on a fresh KMS open. */
static void c4_drm_build_hdr_metadata(c4_hdr_output_metadata_t *m){
    if(!m)return;
    ulibc_memset(m,0,sizeof(*m));
    m->metadata_type=0u;        /* HDMI_STATIC_METADATA_TYPE1 */
    m->hdmi_metadata_type1.eotf=2u; /* PQ */
    m->hdmi_metadata_type1.metadata_type=0u;
    /* CIE 1931 chromaticities scaled by 50000 (Linux ABI). BT.2020. */
    m->hdmi_metadata_type1.prim_red_x  = 35400u;
    m->hdmi_metadata_type1.prim_red_y  = 14600u;
    m->hdmi_metadata_type1.prim_green_x= 8500u;
    m->hdmi_metadata_type1.prim_green_y= 39850u;
    m->hdmi_metadata_type1.prim_blue_x = 6550u;
    m->hdmi_metadata_type1.prim_blue_y = 2300u;
    m->hdmi_metadata_type1.white_point_x=15635u;
    m->hdmi_metadata_type1.white_point_y=16450u;
    m->hdmi_metadata_type1.max_display_mastering_luminance=1000u;
    m->hdmi_metadata_type1.min_display_mastering_luminance=0u;
    m->hdmi_metadata_type1.max_cll =1000u;
    m->hdmi_metadata_type1.max_fall=400u;
}

/* Minimal v4 ICC profile header (sRGB) so userspace probes do not get an
 * empty blob. We do not attempt to build a full LUT; consumers fall back to
 * implicit sRGB when they see the small profile size. */
static void c4_drm_build_icc_profile(uint8_t *buf,uint32_t cap,uint32_t *out_len){
    static const uint8_t header[]={
        0x00,0x00,0x02,0x18,           /* size = 536 */
        'l','c','m','s',                /* CMM */
        0x04,0x40,0x00,0x00,            /* version 4.4 */
        'm','n','t','r',                /* device class: monitor */
        'R','G','B',' ',                /* color space */
        'X','Y','Z',' ',                /* PCS */
        /* timestamp: 2026-04-27 */
        0x07,0xEA, 0x00,0x04, 0x00,0x1B, 0x00,0x00, 0x00,0x00, 0x00,0x00,
        'a','c','s','p',                /* "acsp" magic */
        'R','I','D','U',                /* platform */
        0x00,0x00,0x00,0x00,            /* flags */
        'R','d','x','S',                /* manufacturer */
        'R','d','x','M',                /* model */
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, /* attributes */
        0x00,0x00,0x00,0x01,            /* rendering intent: relative */
        0x00,0x00,0xF6,0xD6,            /* PCS X (D50) */
        0x00,0x01,0x00,0x00,            /* PCS Y */
        0x00,0x00,0xD3,0x2D,            /* PCS Z */
        'R','i','d','u',                /* creator */
    };
    uint32_t want=(uint32_t)sizeof(header);
    if(!buf||!cap||!out_len){if(out_len)*out_len=0u;return;}
    if(want>cap)want=cap;
    ulibc_memset(buf,0,cap);
    ulibc_memcpy(buf,header,want);
    *out_len=want;
}

/* TILE blob: connector tile group descriptor (group_id=0 means no tiling).
 * Format from Linux drm_mode_get_tile_group: 8 u32 = group_id, flags,
 *  hsize, vsize, h_loc, v_loc, hsize_in_mm, vsize_in_mm. */
static void c4_drm_build_tile_blob(uint8_t *buf,uint32_t cap,uint32_t *out_len,
                                   uint32_t hsize,uint32_t vsize){
    uint32_t i,*p;
    if(!buf||cap<32u||!out_len){if(out_len)*out_len=0u;return;}
    p=(uint32_t*)buf;
    p[0]=0u;            /* group_id */
    p[1]=0u;            /* flags */
    p[2]=hsize;
    p[3]=vsize;
    p[4]=0u;            /* h_loc */
    p[5]=0u;            /* v_loc */
    p[6]=hsize/4u;      /* hsize mm */
    p[7]=vsize/4u;      /* vsize mm */
    for(i=32u;i<cap;++i)buf[i]=0u;
    *out_len=32u;
}

/* PATH blob: ASCII identifier, e.g. "DP-MST-1:0" (from Linux MST chains). */
static void c4_drm_build_path_blob(uint8_t *buf,uint32_t cap,uint32_t *out_len){
    static const char path[]="ridux:internal:0";
    uint32_t need=(uint32_t)sizeof(path);
    if(!buf||!cap||!out_len){if(out_len)*out_len=0u;return;}
    if(need>cap)need=cap;
    ulibc_memcpy(buf,path,need);
    *out_len=need;
}

static void c4_drm_static_blobs_ensure(void){
    uint32_t i;
    if(g_c4_drm_static_blobs_ready)return;
    c4_drm_build_edid(g_c4_drm_edid);
    c4_drm_build_in_formats();
    c4_drm_build_hdr_metadata(&g_c4_drm_hdr_metadata);
    c4_drm_build_icc_profile(g_c4_drm_icc_profile,sizeof(g_c4_drm_icc_profile),
                             &g_c4_drm_icc_profile_len);
    c4_drm_build_tile_blob(g_c4_drm_tile_blob,sizeof(g_c4_drm_tile_blob),
                           &g_c4_drm_tile_blob_len,
                           g_fb.width?g_fb.width:1024u,
                           g_fb.height?g_fb.height:768u);
    c4_drm_build_path_blob(g_c4_drm_path_blob,sizeof(g_c4_drm_path_blob),
                           &g_c4_drm_path_blob_len);
    for(i=0;i<C4_DRM_GAMMA_LUT_SIZE;++i){
        uint16_t v=(uint16_t)((i*0xFFFFu)/(C4_DRM_GAMMA_LUT_SIZE-1u));
        g_c4_drm_gamma_r[i]=v;
        g_c4_drm_gamma_g[i]=v;
        g_c4_drm_gamma_b[i]=v;
    }
    g_c4_drm_static_blobs_ready=true;
}

static bool c4_drm_object_is_plane(uint32_t obj_id){
    return obj_id==C4_DRM_ID_PLANE||
           obj_id==C4_DRM_ID_PLANE_CURSOR||
           obj_id==C4_DRM_ID_PLANE_OVERLAY;
}

static bool c4_drm_object_known(uint32_t obj_id,uint32_t obj_type){
    if(obj_type==C4_DRM_MODE_OBJECT_ANY){
        return obj_id==C4_DRM_ID_CRTC||obj_id==C4_DRM_ID_CONNECTOR||
               obj_id==C4_DRM_ID_ENCODER||c4_drm_object_is_plane(obj_id)||
               obj_id==C4_DRM_ID_FB||c4_drm_find_bo_by_fb(obj_id)!=0;
    }
    if(obj_type==C4_DRM_MODE_OBJECT_CRTC)return obj_id==C4_DRM_ID_CRTC;
    if(obj_type==C4_DRM_MODE_OBJECT_CONNECTOR)return obj_id==C4_DRM_ID_CONNECTOR;
    if(obj_type==C4_DRM_MODE_OBJECT_ENCODER)return obj_id==C4_DRM_ID_ENCODER;
    if(obj_type==C4_DRM_MODE_OBJECT_PLANE)return c4_drm_object_is_plane(obj_id);
    if(obj_type==C4_DRM_MODE_OBJECT_FB)return obj_id==C4_DRM_ID_FB||c4_drm_find_bo_by_fb(obj_id)!=0;
    return false;
}

static void c4_drm_put_props(c4_drm_mode_obj_get_properties_t *p,
                             const uint32_t *ids,const uint64_t *values,
                             uint32_t count){
    uint32_t i,limit;
    if(!p)return;
    limit=p->count_props;
    if(p->props_ptr&&p->prop_values_ptr&&limit>0){
        if(limit>count)limit=count;
        for(i=0;i<limit;++i){
            ((uint32_t*)(uintptr_t)p->props_ptr)[i]=ids[i];
            ((uint64_t*)(uintptr_t)p->prop_values_ptr)[i]=values?values[i]:0;
        }
    }
    p->count_props=count;
}

static void c4_drm_fill_prop_range(c4_drm_mode_get_property_t *p,
                                   uint32_t id,const char *name,
                                   uint64_t lo,uint64_t hi){
    if(!p)return;
    p->prop_id=id;
    p->flags=C4_DRM_MODE_PROP_RANGE;
    ulibc_strncpy(p->name,name,sizeof(p->name)-1);
    p->name[sizeof(p->name)-1]=0;
    if(p->values_ptr&&p->count_values>=2){
        uint64_t *v=(uint64_t*)(uintptr_t)p->values_ptr;
        v[0]=lo;
        v[1]=hi;
    }
    p->count_values=2;
    p->count_enum_blobs=0;
}

static void c4_drm_fill_prop_object(c4_drm_mode_get_property_t *p,
                                    uint32_t id,const char *name,uint32_t type){
    if(!p)return;
    p->prop_id=id;
    p->flags=C4_DRM_MODE_PROP_OBJECT;
    ulibc_strncpy(p->name,name,sizeof(p->name)-1);
    p->name[sizeof(p->name)-1]=0;
    if(p->values_ptr&&p->count_values>0)
        ((uint64_t*)(uintptr_t)p->values_ptr)[0]=type;
    p->count_values=1;
    p->count_enum_blobs=0;
}

static void c4_drm_fill_prop_blob(c4_drm_mode_get_property_t *p,
                                  uint32_t id,const char *name){
    if(!p)return;
    p->prop_id=id;
    p->flags=C4_DRM_MODE_PROP_BLOB;
    ulibc_strncpy(p->name,name,sizeof(p->name)-1);
    p->name[sizeof(p->name)-1]=0;
    p->count_values=0;
    p->count_enum_blobs=0;
}

static void c4_drm_fill_plane_type_property(c4_drm_mode_get_property_t *p){
    if(!p)return;
    p->prop_id=C4_DRM_PROP_PLANE_TYPE;
    p->flags=C4_DRM_MODE_PROP_ENUM;
    ulibc_strcpy(p->name,"type");
    if(p->values_ptr&&p->count_values>0){
        ((uint64_t*)(uintptr_t)p->values_ptr)[0]=C4_DRM_PLANE_TYPE_PRIMARY;
    }
    if(p->enum_blob_ptr&&p->count_enum_blobs>0){
        c4_drm_mode_property_enum_t *e=(c4_drm_mode_property_enum_t*)(uintptr_t)p->enum_blob_ptr;
        e[0].value=C4_DRM_PLANE_TYPE_PRIMARY;
        ulibc_strcpy(e[0].name,"Primary");
    }
    p->count_values=1;
    p->count_enum_blobs=1;
}

static c4_drm_prop_blob_t *c4_drm_prop_blob_find(uint32_t id){
    int i;
    for(i=0;i<C4_DRM_MAX_PROP_BLOB;++i)
        if(g_c4_drm_prop_blobs[i].used&&g_c4_drm_prop_blobs[i].id==id)
            return &g_c4_drm_prop_blobs[i];
    return 0;
}

static uint32_t c4_drm_prop_blob_create(uint32_t length){
    int i;
    for(i=0;i<C4_DRM_MAX_PROP_BLOB;++i){
        if(g_c4_drm_prop_blobs[i].used)continue;
        g_c4_drm_prop_blobs[i].used=true;
        g_c4_drm_prop_blobs[i].id=g_c4_drm_next_blob_id++;
        if(g_c4_drm_next_blob_id<C4_DRM_ID_MODE_BLOB_BASE)
            g_c4_drm_next_blob_id=C4_DRM_ID_MODE_BLOB_BASE;
        g_c4_drm_prop_blobs[i].length=length;
        return g_c4_drm_prop_blobs[i].id;
    }
    return 0;
}

static void c4_drm_prop_blob_destroy(uint32_t id){
    int i;
    for(i=0;i<C4_DRM_MAX_PROP_BLOB;++i){
        if(!g_c4_drm_prop_blobs[i].used||g_c4_drm_prop_blobs[i].id!=id)continue;
        g_c4_drm_prop_blobs[i].used=false;
        return;
    }
}

static c4_drm_syncobj_t *c4_drm_syncobj_find(uint32_t handle){
    int i;
    for(i=0;i<C4_DRM_MAX_SYNCOBJ;++i)
        if(g_c4_drm_syncobjs[i].used&&g_c4_drm_syncobjs[i].handle==handle)
            return &g_c4_drm_syncobjs[i];
    return 0;
}

static c4_drm_syncobj_t *c4_drm_syncobj_find_fd(int32_t fd){
    int i;
    for(i=0;i<C4_DRM_MAX_SYNCOBJ;++i)
        if(g_c4_drm_syncobjs[i].used&&g_c4_drm_syncobjs[i].export_fd==fd)
            return &g_c4_drm_syncobjs[i];
    return 0;
}

static c4_drm_syncobj_t *c4_drm_syncobj_alloc(bool signaled){
    int i;
    for(i=0;i<C4_DRM_MAX_SYNCOBJ;++i){
        if(g_c4_drm_syncobjs[i].used)continue;
        ulibc_memset(&g_c4_drm_syncobjs[i],0,sizeof(g_c4_drm_syncobjs[i]));
        g_c4_drm_syncobjs[i].used=true;
        g_c4_drm_syncobjs[i].handle=g_c4_drm_next_syncobj++;
        if(!g_c4_drm_next_syncobj)g_c4_drm_next_syncobj=1;
        g_c4_drm_syncobjs[i].signaled=signaled;
        g_c4_drm_syncobjs[i].point=signaled?1ULL:0ULL;
        g_c4_drm_syncobjs[i].export_fd=-1;
        return &g_c4_drm_syncobjs[i];
    }
    return 0;
}

static int32_t c4_drm_syncobj_export_fd(c4_drm_syncobj_t *s){
    int64_t fd;
    if(!s)return -EINVAL;
    if(s->export_fd>=0)return s->export_fd;
    fd=real_sys_eventfd2(s->signaled?1u:0u,0);
    if(fd<0)return (int32_t)fd;
    s->export_fd=(int32_t)fd;
    return s->export_fd;
}

static int c4_drm_syncobj_mark_array(uint64_t handles_ptr,uint32_t count,bool signaled){
    uint32_t i;
    if(!handles_ptr&&count)return -EINVAL;
    for(i=0;i<count;++i){
        uint32_t h=((uint32_t*)(uintptr_t)handles_ptr)[i];
        c4_drm_syncobj_t *s=c4_drm_syncobj_find(h);
        if(!s)return -EINVAL;
        s->signaled=signaled;
        if(signaled)++s->point;
    }
    return 0;
}

static int c4_drm_syncobj_wait_array(uint64_t handles_ptr,uint32_t count,
                                     uint32_t flags,int64_t timeout_nsec,
                                     uint32_t *first_signaled){
    uint32_t i,signaled_count=0,first=0xFFFFFFFFu;
    bool wait_all=(flags&1u)!=0;
    if(!handles_ptr&&count)return -EINVAL;
    if(!count)return 0;
    for(i=0;i<count;++i){
        uint32_t h=((uint32_t*)(uintptr_t)handles_ptr)[i];
        c4_drm_syncobj_t *s=c4_drm_syncobj_find(h);
        if(!s)return -EINVAL;
        if(s->signaled){
            if(first==0xFFFFFFFFu)first=i;
            ++signaled_count;
        }
    }
    if((wait_all&&signaled_count==count)||(!wait_all&&signaled_count>0)){
        if(first_signaled)*first_signaled=(first==0xFFFFFFFFu)?0:first;
        return 0;
    }
    if(timeout_nsec==0)return -ETIMEDOUT;
    /* Ridux currently completes CPU-present work synchronously. For blocking
     * waits, promote pending fences to signaled instead of busy-spinning. */
    for(i=0;i<count;++i){
        uint32_t h=((uint32_t*)(uintptr_t)handles_ptr)[i];
        c4_drm_syncobj_t *s=c4_drm_syncobj_find(h);
        if(s&&!s->signaled){s->signaled=true;++s->point;}
    }
    if(first_signaled)*first_signaled=0;
    return 0;
}

static int c4_drm_syncobj_query_array(uint64_t handles_ptr,uint64_t points_ptr,uint32_t count){
    uint32_t i;
    if((!handles_ptr||!points_ptr)&&count)return -EINVAL;
    for(i=0;i<count;++i){
        uint32_t h=((uint32_t*)(uintptr_t)handles_ptr)[i];
        c4_drm_syncobj_t *s=c4_drm_syncobj_find(h);
        if(!s)return -EINVAL;
        ((uint64_t*)(uintptr_t)points_ptr)[i]=s->point;
    }
    return 0;
}

static int c4_drm_atomic_commit(int fd,c4_drm_mode_atomic_t *a){
    uint32_t i,pi=0,fb_id=0,crtc_id=C4_DRM_ID_CRTC;
    bool test_only,wants_event,nonblock,allow_modeset;
    if(!a)return 0;
    if(a->count_objs&&(!a->objs_ptr||!a->count_props_ptr||!a->props_ptr||!a->prop_values_ptr))
        return -EINVAL;
    test_only=(a->flags&C4_DRM_MODE_ATOMIC_TEST_ONLY)!=0;
    wants_event=(a->flags&C4_DRM_MODE_PAGE_FLIP_EVENT)!=0;
    nonblock=(a->flags&C4_DRM_MODE_ATOMIC_NONBLOCK)!=0;
    allow_modeset=(a->flags&C4_DRM_MODE_ATOMIC_ALLOW_MODESET)!=0;
    if(!test_only&&!c4_drm_file_can_modeset(fd))return -EACCES;
    for(i=0;i<a->count_objs;++i){
        uint32_t obj=((uint32_t*)(uintptr_t)a->objs_ptr)[i];
        uint32_t n=((uint32_t*)(uintptr_t)a->count_props_ptr)[i];
        uint32_t j;
        if(!c4_drm_object_known(obj,C4_DRM_MODE_OBJECT_ANY))return -EINVAL;
        for(j=0;j<n;++j,++pi){
            uint32_t prop=((uint32_t*)(uintptr_t)a->props_ptr)[pi];
            uint64_t val=((uint64_t*)(uintptr_t)a->prop_values_ptr)[pi];
            if(prop==C4_DRM_PROP_FB_ID&&c4_drm_object_is_plane(obj)){
                /* Only the primary plane drives the scanout buffer; cursor and
                 * overlay planes still accept the FB but do not page-flip. */
                if(obj==C4_DRM_ID_PLANE)fb_id=(uint32_t)val;
            }else if(prop==C4_DRM_PROP_FB_ID){
                /* CRTC / connector ignore FB_ID; it belongs to planes. */
            }else if(prop==C4_DRM_PROP_CRTC_ID){
                if(val&&obj==C4_DRM_ID_PLANE)crtc_id=(uint32_t)val;
            }else if(prop==C4_DRM_PROP_OUT_FENCE_PTR&&val){
                *((int32_t*)(uintptr_t)val)=-1;
            }else if(prop==C4_DRM_PROP_DPMS){
                if(val<=C4_DRM_DPMS_OFF)
                    g_c4_drm_dpms_state=(uint8_t)val;
            }else if(prop==C4_DRM_PROP_ACTIVE||prop==C4_DRM_PROP_MODE_ID||
                     prop==C4_DRM_PROP_CRTC_X||prop==C4_DRM_PROP_CRTC_Y||
                     prop==C4_DRM_PROP_CRTC_W||prop==C4_DRM_PROP_CRTC_H||
                     prop==C4_DRM_PROP_SRC_X||prop==C4_DRM_PROP_SRC_Y||
                     prop==C4_DRM_PROP_SRC_W||prop==C4_DRM_PROP_SRC_H||
                     prop==C4_DRM_PROP_IN_FENCE_FD||prop==C4_DRM_PROP_PLANE_TYPE||
                     prop==C4_DRM_PROP_GAMMA_LUT||prop==C4_DRM_PROP_GAMMA_LUT_SIZE||
                     prop==C4_DRM_PROP_DEGAMMA_LUT||prop==C4_DRM_PROP_DEGAMMA_LUT_SIZE||
                     prop==C4_DRM_PROP_CTM||prop==C4_DRM_PROP_VRR_ENABLED||
                     prop==C4_DRM_PROP_FB_DAMAGE_CLIPS||
                     prop==C4_DRM_PROP_HOTSPOT_X||prop==C4_DRM_PROP_HOTSPOT_Y||
                     prop==C4_DRM_PROP_ZPOS||prop==C4_DRM_PROP_ROTATION||
                     prop==C4_DRM_PROP_ALPHA||prop==C4_DRM_PROP_PIXEL_BLEND_MODE||
                     prop==C4_DRM_PROP_COLOR_ENCODING||prop==C4_DRM_PROP_COLOR_RANGE||
                     prop==C4_DRM_PROP_LINK_STATUS||prop==C4_DRM_PROP_NON_DESKTOP||
                     prop==C4_DRM_PROP_TILE||prop==C4_DRM_PROP_PATH||
                     prop==C4_DRM_PROP_VRR_CAPABLE||prop==C4_DRM_PROP_SUBCONNECTOR||
                     prop==C4_DRM_PROP_HDR_OUTPUT_METADATA||
                     prop==C4_DRM_PROP_COLORSPACE||prop==C4_DRM_PROP_MAX_BPC||
                     prop==C4_DRM_PROP_BROADCAST_RGB||prop==C4_DRM_PROP_PANEL_ORIENTATION||
                     prop==C4_DRM_PROP_CONTENT_TYPE||prop==C4_DRM_PROP_SCALING_MODE||
                     prop==C4_DRM_PROP_HDCP_CP||prop==C4_DRM_PROP_HDCP_CP_TYPE||
                     prop==C4_DRM_PROP_BACKGROUND_COLOR||prop==C4_DRM_PROP_ICC_PROFILE||
                     prop==C4_DRM_PROP_PRIVACY_SCREEN_HW||
                     prop==C4_DRM_PROP_PRIVACY_SCREEN_SW||
                     prop==C4_DRM_PROP_DITHERING_MODE||prop==C4_DRM_PROP_OUTPUT_BPP){
                /* Capture mutable state so subsequent property reads reflect it. */
                if(prop==C4_DRM_PROP_COLORSPACE)        g_c4_drm_colorspace=(uint32_t)val;
                else if(prop==C4_DRM_PROP_MAX_BPC)      g_c4_drm_max_bpc=(uint32_t)val;
                else if(prop==C4_DRM_PROP_BROADCAST_RGB)g_c4_drm_broadcast_rgb=(uint32_t)val;
                else if(prop==C4_DRM_PROP_PANEL_ORIENTATION)g_c4_drm_panel_orient=(uint32_t)val;
                else if(prop==C4_DRM_PROP_CONTENT_TYPE) g_c4_drm_content_type=(uint32_t)val;
                else if(prop==C4_DRM_PROP_SCALING_MODE) g_c4_drm_scaling_mode=(uint32_t)val;
                else if(prop==C4_DRM_PROP_HDCP_CP)      g_c4_drm_hdcp_cp=(uint32_t)val;
                else if(prop==C4_DRM_PROP_HDCP_CP_TYPE) g_c4_drm_hdcp_cp_type=(uint32_t)val;
                else if(prop==C4_DRM_PROP_BACKGROUND_COLOR)g_c4_drm_background_argb=val;
                else if(prop==C4_DRM_PROP_PRIVACY_SCREEN_SW)g_c4_drm_privacy_screen_sw=(uint32_t)val;
                else if(prop==C4_DRM_PROP_DITHERING_MODE)   g_c4_drm_dithering_mode=(uint32_t)val;
                else if(prop==C4_DRM_PROP_OUTPUT_BPP)       g_c4_drm_output_bpp=(uint32_t)val;
            }else{
                /* Unknown property: accept silently to keep KMS clients happy.
                 * Real drivers reject, but our virtual KMS forwards rendering
                 * via the framebuffer regardless. */
            }
        }
    }
    if(!test_only){
        bool presented=false;
        if(wants_event&&drm_page_flip_inflight(fd))
            return -EBUSY;
        if(fb_id){
            g_c4_drm_current_fb_id=fb_id;
            c4_drm_trace_bo("atomic",c4_drm_find_bo_by_fb(fb_id),fb_id,crtc_id);
            presented=c4_drm_present_fb_vbox_or_cpu(fb_id);
        }
        if(c4_drm_present_trace_enabled()){
            static uint32_t atomic_trace;
            if(atomic_trace<160u){
                c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(fb_id);
                ++atomic_trace;
                __boot_serial_force_puts("[drm-atomic] #");
                __boot_serial_force_putu32(atomic_trace);
                __boot_serial_force_puts(" fd=");
                __boot_serial_force_putu32((uint32_t)fd);
                __boot_serial_force_puts(" flags=");
                __boot_serial_force_puthex64((uint64_t)a->flags);
                __boot_serial_force_puts(" objs=");
                __boot_serial_force_putu32(a->count_objs);
                __boot_serial_force_puts(" fb=");
                __boot_serial_force_putu32(fb_id);
                __boot_serial_force_puts(" bo=");
                __boot_serial_force_putu32(bo?bo->handle:0);
                __boot_serial_force_puts(" crtc=");
                __boot_serial_force_putu32(crtc_id);
                __boot_serial_force_puts(" event=");
                __boot_serial_force_putu32(wants_event?1u:0u);
                __boot_serial_force_puts(" nonblock=");
                __boot_serial_force_putu32(nonblock?1u:0u);
                __boot_serial_force_puts(" modeset=");
                __boot_serial_force_putu32(allow_modeset?1u:0u);
                __boot_serial_force_puts(" presented=");
                __boot_serial_force_putu32(presented?1u:0u);
                __boot_serial_force_puts(" user=");
                __boot_serial_force_puthex64(a->user_data);
                __boot_serial_force_puts("\n");
            }
        }
        if(wants_event)
            drm_queue_page_flip_event(fd,a->user_data,crtc_id);
    }
    return 0;
}

static void c4_drm_version_copy_string(uint64_t user_ptr,uint64_t user_len,const char *src){
    size_t n;
    if(!user_ptr||!user_len||!src)return;
    n=ulibc_strlen(src);
    if(n>(size_t)user_len)n=(size_t)user_len;
    ulibc_memcpy((void*)(uintptr_t)user_ptr,src,n);
    if(n<(size_t)user_len)((char*)(uintptr_t)user_ptr)[n]=0;
}

static bool c4_drm_vbox_mode(void){
    return kvfs_exists("/etc/ridux-vbox-gpu.enable") ||
           c4_svga_real_find_device();
}

bool drm_vbox_gpu_detected(void){
    return c4_drm_vbox_mode();
}

typedef struct {
    uint64_t value;
    uint32_t param;
    uint32_t pad64;
} c4_vmw_getparam_t;

typedef struct {
    int32_t cid;
    uint32_t pad64;
} c4_vmw_context_t;

typedef union {
    struct {
        uint32_t size;
        uint32_t pad64;
    } req;
    struct {
        uint64_t map_handle;
        uint32_t handle;
        uint32_t cur_gmr_id;
        uint32_t cur_gmr_offset;
        uint32_t pad64;
    } rep;
} c4_vmw_alloc_bo_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t pad64;
} c4_vmw_size_t;

typedef struct {
    uint32_t svga3d_flags;
    uint32_t format;
    uint32_t mip_levels;
    uint32_t drm_surface_flags;
    uint32_t multisample_count;
    uint32_t autogen_filter;
    uint32_t buffer_handle;
    uint32_t array_size;
    c4_vmw_size_t base_size;
} c4_vmw_gb_surface_req_t;

typedef struct {
    uint32_t handle;
    uint32_t backup_size;
    uint32_t buffer_handle;
    uint32_t buffer_size;
    uint64_t buffer_map_handle;
} c4_vmw_gb_surface_rep_t;

typedef union {
    c4_vmw_gb_surface_req_t req;
    c4_vmw_gb_surface_rep_t rep;
} c4_vmw_surface_t;

typedef struct {
    c4_vmw_gb_surface_req_t base;
    uint32_t version;
    uint32_t svga3d_flags_upper_32_bits;
    uint32_t multisample_pattern;
    uint32_t quality_level;
    uint32_t buffer_byte_stride;
    uint32_t must_be_zero;
} c4_vmw_gb_surface_ext_req_t;

typedef union {
    c4_vmw_gb_surface_ext_req_t req;
    c4_vmw_gb_surface_rep_t rep;
} c4_vmw_surface_ext_t;

typedef struct {
    int32_t sid;
    uint32_t handle_type;
} c4_vmw_surface_arg_t;

typedef struct {
    c4_vmw_gb_surface_req_t creq;
    c4_vmw_gb_surface_rep_t crep;
} c4_vmw_surface_ref_rep_t;

typedef union {
    c4_vmw_surface_arg_t req;
    c4_vmw_surface_ref_rep_t rep;
} c4_vmw_surface_ref_t;

typedef struct {
    c4_vmw_gb_surface_ext_req_t creq;
    c4_vmw_gb_surface_rep_t crep;
} c4_vmw_surface_ref_ext_rep_t;

typedef union {
    c4_vmw_surface_arg_t req;
    c4_vmw_surface_ref_ext_rep_t rep;
} c4_vmw_surface_ref_ext_t;

typedef union {
    struct {
        uint32_t flags;
        uint32_t format;
        uint32_t mip_levels[6];
        uint64_t size_addr;
        int32_t shareable;
        int32_t scanout;
    } req;
    struct {
        int32_t sid;
        uint32_t handle_type;
    } rep;
} c4_vmw_legacy_surface_t;

typedef struct {
    uint64_t commands;
    uint32_t command_size;
    uint32_t throttle_us;
    uint64_t fence_rep;
    uint32_t version;
    uint32_t flags;
    uint32_t context_handle;
    int32_t imported_fence_fd;
} c4_vmw_execbuf_t;

typedef struct {
    uint32_t handle;
    uint32_t mask;
    uint32_t seqno;
    uint32_t passed_seqno;
    int32_t fd;
    int32_t error;
} c4_vmw_fence_rep_t;

typedef struct {
    uint32_t handle;
    int32_t cookie_valid;
    uint64_t kernel_cookie;
    uint64_t timeout_us;
    int32_t lazy;
    int32_t flags;
    int32_t wait_options;
    int32_t pad64;
} c4_vmw_fence_wait_t;

typedef struct {
    uint32_t handle;
    uint32_t flags;
    int32_t signaled;
    uint32_t passed_seqno;
    uint32_t signaled_flags;
    uint32_t pad64;
} c4_vmw_fence_signaled_t;

typedef struct {
    uint32_t handle;
    uint32_t pad64;
} c4_vmw_handle_t;

typedef struct {
    uint64_t buffer;
    uint32_t max_size;
    uint32_t pad64;
} c4_vmw_get_3d_cap_t;

typedef struct {
    uint32_t shader_type;
    uint32_t size;
    uint32_t buffer_handle;
    uint32_t shader_handle;
    uint64_t offset;
} c4_vmw_shader_create_t;

typedef struct {
    uint64_t send;
    uint64_t receive;
    int32_t send_only;
    uint32_t receive_len;
} c4_vmw_msg_t;

typedef struct {
    uint32_t id;
    uint32_t size;
} c4_svga_cmd_header_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
} c4_svga_box_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
    uint32_t srcx;
    uint32_t srcy;
    uint32_t srcz;
} c4_svga_copy_box_t;

typedef struct {
    bool used;
    uint32_t view_id;
    uint32_t sid;
    uint32_t format;
    uint32_t dimension;
} c4_vmw_view_t;

typedef struct {
    bool used;
    uint32_t sid;
    uint32_t bo_handle;
    c4_vmw_gb_surface_ext_req_t req;
    c4_vmw_gb_surface_rep_t rep;
} c4_vmw_surface_record_t;

static uint32_t g_c4_vmw_next_cid=1;
static uint32_t g_c4_vmw_next_surface=1;
static uint32_t g_c4_vmw_next_fence=1;
static uint32_t g_c4_vmw_next_shader=1;
static uint32_t g_c4_vmw_trace_count=0;
static uint32_t g_c4_vmw_exec_dump_count=0;
static uint32_t g_c4_vmw_exec_packet_trace_count=0;
static uint32_t g_c4_vmw_real_clear_probe_count=0;
static uint32_t g_c4_vmw_bound_dsv=0;
static uint32_t g_c4_vmw_bound_rtvs[8];
static uint32_t g_c4_vmw_bound_srvs[6][8];
static uint32_t g_c4_vmw_last_nonzero_sid=0;
static uint32_t g_c4_vmw_last_nonzero_bo=0;
static c4_vmw_surface_record_t g_c4_vmw_surfaces[C4_VMW_MAX_SURFACES];
static c4_vmw_view_t g_c4_vmw_rtvs[C4_VMW_MAX_VIEWS];
static c4_vmw_view_t g_c4_vmw_srvs[C4_VMW_MAX_VIEWS];

static void c4_vmw_trace(const char *tag,uint64_t a,uint64_t b,uint64_t c){
    if(!c4_vmwgfx_trace_enabled())return;
    if(g_c4_vmw_trace_count>=96u)return;
    ++g_c4_vmw_trace_count;
    __boot_serial_force_puts("[vmwgfx] ");
    __boot_serial_force_puts(tag?tag:"?");
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(g_c4_vmw_trace_count);
    __boot_serial_force_puts(" a=");
    __boot_serial_force_puthex64(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_puthex64(b);
    __boot_serial_force_puts(" c=");
    __boot_serial_force_puthex64(c);
    __boot_serial_force_puts("\n");
}

static void c4_vmw_dump_execbuf(const c4_vmw_execbuf_t *e){
    const uint32_t *w;
    uint32_t i,n;
    if(!e||!e->commands||!e->command_size)return;
    if(!c4_vmwgfx_trace_enabled())return;
    if(g_c4_vmw_exec_dump_count>=12u)return;
    ++g_c4_vmw_exec_dump_count;
    w=(const uint32_t*)(uintptr_t)e->commands;
    n=e->command_size/4u;
    if(n>16u)n=16u;
    __boot_serial_force_puts("[vmwgfx-exec-dump] #");
    __boot_serial_force_putu32(g_c4_vmw_exec_dump_count);
    __boot_serial_force_puts(" bytes=");
    __boot_serial_force_putu32(e->command_size);
    __boot_serial_force_puts(" ctx=");
    __boot_serial_force_putu32(e->context_handle);
    for(i=0;i<n;++i){
        __boot_serial_force_puts(" w");
        __boot_serial_force_putu32(i);
        __boot_serial_force_puts("=");
        __boot_serial_force_puthex64((uint64_t)w[i]);
    }
    __boot_serial_force_puts("\n");
}

static uint64_t c4_vmw_param_value(uint32_t param){
    uint64_t fb=(uint64_t)(g_fb.width?g_fb.width:1024u)*(uint64_t)(g_fb.height?g_fb.height:768u)*4ULL;
    switch(param){
        case C4_VMW_PARAM_NUM_STREAMS: return 1;
        case C4_VMW_PARAM_NUM_FREE_STREAMS: return 1;
        case C4_VMW_PARAM_3D: return 1;
        case C4_VMW_PARAM_HW_CAPS: return 0xFFFFFFFFULL;
        case C4_VMW_PARAM_FIFO_CAPS: return 0xFFFFFFFFULL;
        case C4_VMW_PARAM_MAX_FB_SIZE: return fb>0x1000000ULL?fb:0x1000000ULL;
        case C4_VMW_PARAM_FIFO_HW_VERSION: return 2;
        case C4_VMW_PARAM_MAX_SURF_MEMORY: return 256ULL*1024ULL*1024ULL;
        case C4_VMW_PARAM_3D_CAPS_SIZE: return 4096;
        case C4_VMW_PARAM_MAX_MOB_MEMORY: return 256ULL*1024ULL*1024ULL;
        case C4_VMW_PARAM_MAX_MOB_SIZE: return 64ULL*1024ULL*1024ULL;
        case C4_VMW_PARAM_SCREEN_TARGET: return 1;
        case C4_VMW_PARAM_DX:
        case C4_VMW_PARAM_SM4_1:
            return 1;
        case C4_VMW_PARAM_SM5:
        case C4_VMW_PARAM_GL43:
            return 0;
        case C4_VMW_PARAM_HW_CAPS2: return 0xFFFFFFFFULL;
        case C4_VMW_PARAM_DEVICE_ID: return 0x0405;
        default: return 0;
    }
}

static void c4_vmw_set_cap(uint32_t *caps,uint32_t words,uint32_t index,uint32_t value){
    if(caps&&index<words)caps[index]=value;
}

static void c4_vmw_fill_3d_caps(uint32_t *caps,uint32_t bytes){
    uint32_t words=bytes/4u;
    uint32_t i;
    uint32_t fmt_caps=C4_SVGA3D_DXFMT_SUPPORTED|
                      C4_SVGA3D_DXFMT_SHADER_SAMPLE|
                      C4_SVGA3D_DXFMT_COLOR_RENDERTARGET|
                      C4_SVGA3D_DXFMT_DEPTH_RENDERTARGET|
                      C4_SVGA3D_DXFMT_BLENDABLE|
                      C4_SVGA3D_DXFMT_MIPS|
                      C4_SVGA3D_DXFMT_ARRAY|
                      C4_SVGA3D_DXFMT_VOLUME|
                      C4_SVGA3D_DXFMT_DX_VERTEX_BUFFER;
    if(!caps||!bytes)return;
    ulibc_memset(caps,0,bytes);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_3D,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURES,16);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_VERTEX_SHADER_VERSION,0x300);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION,0x300);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_RENDER_TARGETS,8);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_POINT_SIZE,0x42A00000u);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_SHADER_TEXTURES,16);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH,16384);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT,16384);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_VOLUME_EXTENT,2048);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT,32768);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO,8192);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY,0x41800000u);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT,1048576);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_VERTEX_INDEX,0x00FFFFFFu);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_VS_INSTR,65535);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_FS_INSTR,65535);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_VS_TEMPS,256);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_FS_TEMPS,256);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_CONTEXT_IDS,1024);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_SURFACE_IDS,32768);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_LINE_AA,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_LINE_STIPPLE,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_LINE_WIDTH,0x41000000u);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH,0x41000000u);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_DXCONTEXT,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_DX_MAX_VERTEXBUFFERS,32);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS,14);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_DX_PROVOKING_VERTEX,1);
    for(i=C4_SVGA3D_DEVCAP_DXFMT_FIRST;i<=C4_SVGA3D_DEVCAP_DXFMT_LAST&&i<words;++i){
        caps[i]=fmt_caps;
    }
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_SM41,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MULTISAMPLE_2X,0);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MULTISAMPLE_4X,0);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_LOGIC_BLENDOPS,1);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_SM5,0);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MULTISAMPLE_8X,0);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_MAX_FORCED_SAMPLE_COUNT,0);
    c4_vmw_set_cap(caps,words,C4_SVGA3D_DEVCAP_GL43,0);
}

static c4_vmw_surface_record_t *c4_vmw_surface_by_sid(uint32_t sid){
    int i;
    if(!sid)return 0;
    for(i=0;i<C4_VMW_MAX_SURFACES;++i)
        if(g_c4_vmw_surfaces[i].used&&g_c4_vmw_surfaces[i].sid==sid)
            return &g_c4_vmw_surfaces[i];
    return 0;
}

static c4_vmw_surface_record_t *c4_vmw_surface_by_bo(uint32_t bo_handle){
    int i;
    if(!bo_handle)return 0;
    for(i=0;i<C4_VMW_MAX_SURFACES;++i)
        if(g_c4_vmw_surfaces[i].used&&g_c4_vmw_surfaces[i].bo_handle==bo_handle)
            return &g_c4_vmw_surfaces[i];
    return 0;
}

static c4_vmw_surface_record_t *c4_vmw_surface_slot(uint32_t sid){
    int i;
    c4_vmw_surface_record_t *free_slot=0;
    for(i=0;i<C4_VMW_MAX_SURFACES;++i){
        if(g_c4_vmw_surfaces[i].used&&g_c4_vmw_surfaces[i].sid==sid)
            return &g_c4_vmw_surfaces[i];
        if(!g_c4_vmw_surfaces[i].used&&!free_slot)free_slot=&g_c4_vmw_surfaces[i];
    }
    return free_slot;
}

static c4_vmw_surface_record_t *c4_vmw_remember_surface(uint32_t sid,c4_drm_bo_t *bo,
                                                        const c4_vmw_gb_surface_req_t *base_req,
                                                        const c4_vmw_gb_surface_ext_req_t *ext_req,
                                                        const c4_vmw_gb_surface_rep_t *rep);

typedef struct __attribute__((packed)) {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} c4_svga_size3_t;

typedef struct __attribute__((packed)) {
    uint32_t sid;
    uint32_t surface_flags;
    uint32_t format;
    uint32_t num_mip_levels;
    uint32_t multisample_count;
    uint32_t autogen_filter;
    c4_svga_size3_t size;
} c4_svga_define_gb_surface_t;

typedef struct __attribute__((packed)) {
    uint32_t sid;
    uint64_t surface_flags;
    uint32_t format;
    uint32_t num_mip_levels;
    uint32_t multisample_count;
    uint32_t multisample_pattern;
    uint32_t quality_level;
    uint32_t autogen_filter;
    c4_svga_size3_t size;
    uint32_t array_size;
    uint32_t buffer_byte_stride;
} c4_svga_define_gb_surface_v4_t;

typedef struct __attribute__((packed)) {
    uint32_t mobid;
    uint32_t pt_depth;
    uint64_t base;
    uint32_t size_in_bytes;
} c4_svga_define_gb_mob64_t;

typedef struct __attribute__((packed)) {
    uint32_t mobid;
} c4_svga_update_gb_mob_mapping_t;

typedef struct __attribute__((packed)) {
    uint32_t sid;
    uint32_t mobid;
} c4_svga_bind_gb_surface_t;

typedef struct __attribute__((packed)) {
    uint32_t sid;
    uint32_t mobid;
    uint32_t base_level_pitch;
} c4_svga_bind_gb_surface_pitch_t;

typedef struct __attribute__((packed)) {
    uint32_t cid;
} c4_svga_context_t;

typedef struct __attribute__((packed)) {
    uint32_t cid;
    uint32_t mobid;
    uint32_t valid_contents;
} c4_svga_bind_context_t;

typedef struct __attribute__((packed)) {
    uint32_t cid;
    uint32_t mobid;
    uint32_t type;
    uint32_t valid_size_in_bytes;
} c4_svga_dx_set_cotable_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint64_t base_address;
    uint32_t size_in_bytes;
    uint32_t valid_size_in_bytes;
    uint32_t pt_depth;
} c4_svga_set_otable_base64_t;

typedef struct __attribute__((packed)) {
    uint32_t sid;
    uint32_t face;
    uint32_t mipmap;
} c4_svga_surface_image_t;

typedef struct __attribute__((packed)) {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} c4_svga_rect_t;

typedef struct __attribute__((packed)) {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} c4_svga_signed_rect_t;

typedef struct __attribute__((packed)) {
    int32_t x;
    int32_t y;
} c4_svga_signed_point_t;

typedef struct __attribute__((packed)) {
    uint32_t stid;
    uint32_t width;
    uint32_t height;
    int32_t x_root;
    int32_t y_root;
    uint32_t flags;
    uint32_t dpi;
} c4_svga_define_screen_target_t;

typedef struct __attribute__((packed)) {
    uint32_t stid;
    c4_svga_surface_image_t image;
} c4_svga_bind_screen_target_t;

typedef struct __attribute__((packed)) {
    uint32_t stid;
    c4_svga_rect_t rect;
} c4_svga_update_screen_target_t;

typedef struct __attribute__((packed)) {
    uint32_t stid;
    c4_svga_rect_t rect;
    uint32_t type;
} c4_svga_update_screen_target_v2_t;

typedef struct __attribute__((packed)) {
    c4_svga_surface_image_t src_image;
    c4_svga_signed_rect_t src_rect;
    uint32_t dest_screen_id;
    c4_svga_signed_rect_t dest_rect;
} c4_svga_blit_surface_to_screen_t;

typedef struct __attribute__((packed)) {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} c4_svga_fifo_update_t;

typedef struct __attribute__((packed)) {
    uint32_t gmrid;
    uint32_t offset;
} c4_svga_guest_ptr_t;

typedef struct __attribute__((packed)) {
    c4_svga_guest_ptr_t ptr;
    uint32_t pitch;
} c4_svga_guest_image_t;

typedef struct __attribute__((packed)) {
    c4_svga_guest_ptr_t ptr;
    uint32_t bytes_per_line;
    uint32_t format;
} c4_svga_define_gmrfb_t;

typedef struct __attribute__((packed)) {
    c4_svga_signed_point_t src_origin;
    c4_svga_signed_rect_t dest_rect;
    uint32_t dest_screen_id;
} c4_svga_blit_gmrfb_to_screen_t;

typedef struct __attribute__((packed)) {
    uint32_t struct_size;
    uint32_t id;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    int32_t root_x;
    int32_t root_y;
    c4_svga_guest_image_t backing_store;
    uint32_t clone_count;
} c4_svga_screen_object_t;

typedef struct __attribute__((packed)) {
    c4_svga_screen_object_t screen;
} c4_svga_fifo_define_screen_t;

static bool c4_svga_real_setup_otables(void){
    static const struct {
        uint32_t type;
        uint32_t entries;
        uint32_t entry_size;
    } cfg[C4_SVGA_OTABLE_COUNT]={
        {C4_SVGA_OTABLE_MOB,          C4_SVGA_OTABLE_MOB_ENTRIES,          16u},
        {C4_SVGA_OTABLE_SURFACE,      C4_SVGA_OTABLE_SURFACE_ENTRIES,      64u},
        {C4_SVGA_OTABLE_CONTEXT,      C4_SVGA_OTABLE_CONTEXT_ENTRIES,       8u},
        {C4_SVGA_OTABLE_SHADER,       C4_SVGA_OTABLE_SHADER_ENTRIES,       16u},
        {C4_SVGA_OTABLE_SCREENTARGET, C4_SVGA_OTABLE_SCREENTARGET_ENTRIES, 64u},
        {C4_SVGA_OTABLE_DXCONTEXT,    C4_SVGA_OTABLE_CONTEXT_ENTRIES,       8u}
    };
    uint32_t i;
    bool ok=true;
    if(g_c4_svga_otables_ready)return true;
    if(!c4_svga_real_init())return false;
    for(i=0;i<C4_SVGA_OTABLE_COUNT;++i){
        c4_svga_otable_t *o=&g_c4_svga_otables[i];
        c4_svga_set_otable_base64_t cmd;
        uint8_t *virt=0;
        if(!o->phys){
            o->type=cfg[i].type;
            o->entries=cfg[i].entries;
            o->entry_size=cfg[i].entry_size;
            o->bytes=c4_align_page((uint64_t)o->entries*(uint64_t)o->entry_size);
            if(!c4_svga_alloc_physical(o->bytes,&o->phys,&virt)){
                c4_svga_real_trace("otable-alloc-fail",o->type,o->bytes,0,0);
                return false;
            }
            (void)virt;
        }
        if(!c4_svga_build_mob_map(o->phys,o->bytes,&o->base_ppn,&o->pt_depth,&o->pt_phys,&o->pt_pages)){
            c4_svga_real_trace("otable-map-fail",o->type,o->phys,o->bytes,0);
            return false;
        }
        ulibc_memset(&cmd,0,sizeof(cmd));
        cmd.type=o->type;
        cmd.base_address=o->base_ppn;
        cmd.size_in_bytes=(uint32_t)o->bytes;
        cmd.valid_size_in_bytes=0;
        cmd.pt_depth=o->pt_depth;
        o->ready=c4_svga_real_emit_command(C4_SVGA_3D_CMD_SET_OTABLE_BASE64,&cmd,sizeof(cmd));
        ok=o->ready&&ok;
        c4_svga_real_trace(o->ready?"otable":"otable-fail",o->type,o->bytes,o->base_ppn,o->pt_depth);
    }
    if(ok){
        g_c4_svga_otables_ready=true;
        c4_svga_real_fence();
    }
    return ok;
}

static bool c4_svga_real_define_mob(uint32_t mobid,uint64_t phys,uint64_t bytes,
                                    uint64_t *base_ppn,uint32_t *pt_depth,
                                    uint64_t *pt_phys,uint32_t *pt_pages){
    c4_svga_define_gb_mob64_t mob;
    c4_svga_update_gb_mob_mapping_t upd;
    bool ok;
    if(!mobid||!phys||!bytes)return false;
    if(!c4_svga_real_setup_otables())return false;
    if(!c4_svga_build_mob_map(phys,bytes,base_ppn,pt_depth,pt_phys,pt_pages))return false;
    ulibc_memset(&mob,0,sizeof(mob));
    mob.mobid=mobid;
    mob.pt_depth=*pt_depth;
    mob.base=*base_ppn;
    mob.size_in_bytes=(uint32_t)bytes;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DEFINE_GB_MOB64,&mob,sizeof(mob));
    if(!ok)return false;
    upd.mobid=mobid;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING,&upd,sizeof(upd));
    if(!ok)c4_svga_real_trace("mob-map-update-fail",mobid,*base_ppn,*pt_depth,bytes);
    return ok;
}

static bool c4_svga_real_define_bo_mob(c4_drm_bo_t *bo){
    bool ok;
    if(!bo)return false;
    if(bo->real_backed)return true;
    if(!bo->phys||!bo->alloc_size)return false;
    ok=c4_svga_real_define_mob(bo->handle,bo->phys,bo->alloc_size,
                               &bo->mob_base_ppn,&bo->mob_pt_depth,
                               &bo->mob_pt_phys,&bo->mob_pt_pages);
    if(ok){
        bo->real_backed=true;
        c4_svga_real_trace("mob-define",bo->handle,bo->alloc_size,bo->mob_base_ppn,bo->mob_pt_depth);
    }else{
        c4_svga_real_trace("mob-define-fail",bo->handle,bo->phys,bo->alloc_size,0);
    }
    return ok;
}

static c4_svga_context_mob_t *c4_svga_context_mob_for_cid(uint32_t cid){
    uint32_t i;
    uint32_t slot;
    c4_svga_context_mob_t *free_slot=0;
    for(i=0;i<C4_SVGA_CONTEXT_MAX;++i){
        if(g_c4_svga_context_mobs[i].used&&g_c4_svga_context_mobs[i].cid==cid)
            return &g_c4_svga_context_mobs[i];
        if(!g_c4_svga_context_mobs[i].used&&!free_slot)
            free_slot=&g_c4_svga_context_mobs[i];
    }
    if(!free_slot)return 0;
    ulibc_memset(free_slot,0,sizeof(*free_slot));
    free_slot->used=true;
    free_slot->cid=cid;
    slot=(uint32_t)(free_slot-g_c4_svga_context_mobs);
    free_slot->mobid=C4_SVGA_CONTEXT_MOB_BASE+slot;
    for(i=0;i<C4_SVGA_COTABLE_COUNT;++i){
        free_slot->cotables[i].type=i;
        free_slot->cotables[i].mobid=C4_SVGA_COTABLE_MOB_BASE+slot*C4_SVGA_COTABLE_COUNT+i;
        free_slot->cotables[i].bytes=C4_SVGA_COTABLE_BYTES;
    }
    return free_slot;
}

static bool c4_svga_real_setup_cotables(c4_svga_context_mob_t *cmob){
    uint32_t i;
    bool ok=true;
    if(!cmob)return false;
    if(cmob->cotables_ready)return true;
    for(i=0;i<C4_SVGA_COTABLE_COUNT;++i){
        c4_svga_cotable_t *ct=&cmob->cotables[i];
        c4_svga_dx_set_cotable_t cmd;
        if(!ct->bytes)ct->bytes=C4_SVGA_COTABLE_BYTES;
        if(!ct->mobid)ct->mobid=C4_SVGA_COTABLE_MOB_BASE+i;
        if(!ct->phys&&!c4_svga_alloc_physical(ct->bytes,&ct->phys,0)){
            c4_svga_real_trace("cotable-alloc-fail",cmob->cid,ct->type,ct->bytes,0);
            return false;
        }
        if(!ct->ready){
            bool mob_ok=c4_svga_real_define_mob(ct->mobid,ct->phys,ct->bytes,
                                                &ct->base_ppn,&ct->pt_depth,
                                                &ct->pt_phys,&ct->pt_pages);
            ulibc_memset(&cmd,0,sizeof(cmd));
            cmd.cid=cmob->cid;
            cmd.mobid=ct->mobid;
            cmd.type=ct->type;
            cmd.valid_size_in_bytes=0;
            ct->ready=mob_ok&&c4_svga_real_emit_command(C4_SVGA_3D_CMD_DX_SET_COTABLE,&cmd,sizeof(cmd));
            ok=ct->ready&&ok;
            c4_svga_real_trace(ct->ready?"cotable":"cotable-fail",cmob->cid,
                                ((uint64_t)ct->type<<32)|(uint64_t)ct->mobid,
                                ct->base_ppn,ct->pt_depth);
        }
    }
    if(ok){
        cmob->cotables_ready=true;
        c4_svga_real_fence();
    }
    return ok;
}

static bool c4_svga_real_define_context(uint32_t cid){
    c4_svga_context_t c;
    c4_svga_bind_context_t bind;
    c4_svga_context_mob_t *cmob;
    bool ok;
    if(!cid)return false;
    if(!c4_svga_real_setup_otables())return false;
    c.cid=cid;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DEFINE_GB_CONTEXT,&c,sizeof(c));
    cmob=c4_svga_context_mob_for_cid(cid);
    if(cmob&&!cmob->defined){
        if(!cmob->phys&&!c4_svga_alloc_physical(C4_SVGA_CONTEXT_MOB_SIZE,&cmob->phys,0)){
            c4_svga_real_trace("ctx-mob-alloc-fail",cid,cmob->mobid,C4_SVGA_CONTEXT_MOB_SIZE,0);
            return false;
        }
        cmob->defined=c4_svga_real_define_mob(cmob->mobid,cmob->phys,C4_SVGA_CONTEXT_MOB_SIZE,
                                              &cmob->base_ppn,&cmob->pt_depth,&cmob->pt_phys,&cmob->pt_pages);
    }
    if(cmob&&cmob->defined){
        ulibc_memset(&bind,0,sizeof(bind));
        bind.cid=cid;
        bind.mobid=cmob->mobid;
        bind.valid_contents=0;
        ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BIND_GB_CONTEXT,&bind,sizeof(bind))&&ok;
    }
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DX_DEFINE_CONTEXT,&c,sizeof(c))&&ok;
    if(cmob&&cmob->defined){
        ulibc_memset(&bind,0,sizeof(bind));
        bind.cid=cid;
        bind.mobid=cmob->mobid;
        bind.valid_contents=0;
        cmob->dx_bound=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DX_BIND_CONTEXT,&bind,sizeof(bind));
        ok=cmob->dx_bound&&ok;
        ok=c4_svga_real_setup_cotables(cmob)&&ok;
    }
    if(ok)c4_svga_real_fence();
    c4_svga_real_trace(ok?"ctx-define":"ctx-define-fail",cid,cmob?cmob->mobid:0,cmob?cmob->base_ppn:0,
                        cmob?((uint64_t)cmob->dx_bound<<32)|(uint64_t)cmob->cotables_ready:0);
    return ok;
}

static bool c4_svga_real_define_surface(c4_vmw_surface_record_t *rec,c4_drm_bo_t *bo){
    c4_svga_define_gb_surface_v4_t surf;
    c4_svga_bind_gb_surface_pitch_t bind;
    uint64_t surface_flags;
    uint32_t scanout_w,scanout_h;
    bool ok;
    if(!rec||!bo||!bo->phys||!bo->alloc_size)return false;
    if(!c4_svga_real_setup_otables())return false;
    ulibc_memset(&surf,0,sizeof(surf));
    surf.sid=rec->sid;
    surface_flags=(uint64_t)rec->req.base.svga3d_flags|
                  ((uint64_t)rec->req.svga3d_flags_upper_32_bits<<32);
    if(!surface_flags)
        surface_flags=C4_SVGA3D_SURFACE_MOB_PITCH|
                      C4_SVGA3D_SURFACE_BIND_SHADER_RESOURCE|
                      C4_SVGA3D_SURFACE_BIND_RENDER_TARGET;
    surf.format=rec->req.base.format?rec->req.base.format:1u;
    surf.num_mip_levels=rec->req.base.mip_levels?rec->req.base.mip_levels:1u;
    surf.multisample_count=rec->req.base.multisample_count;
    surf.multisample_pattern=rec->req.multisample_pattern;
    surf.quality_level=rec->req.quality_level;
    surf.autogen_filter=rec->req.base.autogen_filter;
    surf.size.width=rec->req.base.base_size.width?rec->req.base.base_size.width:(bo->width?bo->width:1u);
    surf.size.height=rec->req.base.base_size.height?rec->req.base.base_size.height:(bo->height?bo->height:1u);
    surf.size.depth=rec->req.base.base_size.depth?rec->req.base.base_size.depth:1u;
    surf.array_size=rec->req.base.array_size?rec->req.base.array_size:1u;
    surf.buffer_byte_stride=rec->req.buffer_byte_stride?rec->req.buffer_byte_stride:bo->pitch;
    surface_flags|=C4_SVGA3D_SURFACE_MOB_PITCH;
    scanout_w=g_fb.width?g_fb.width:1024u;
    scanout_h=g_fb.height?g_fb.height:768u;
    if(surf.size.width==scanout_w&&surf.size.height==scanout_h)
        surface_flags|=C4_SVGA3D_SURFACE_SCREENTARGET;
    surf.surface_flags=surface_flags;
    ulibc_memset(&bind,0,sizeof(bind));
    bind.sid=rec->sid;
    bind.mobid=bo->handle;
    bind.base_level_pitch=bo->pitch;
    ok=c4_svga_real_define_bo_mob(bo);
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DEFINE_GB_SURFACE_V4,&surf,sizeof(surf))&&ok;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH,&bind,sizeof(bind))&&ok;
    if(ok){
        bo->real_created=true;
        c4_svga_real_fence();
    }
    c4_svga_real_trace(ok?"surface-define":"surface-define-fail",rec->sid,bo->handle,bo->mob_base_ppn,
                        ((uint64_t)bo->mob_pt_depth<<56)|((surface_flags&0x00ffffffULL)<<32)|
                        (uint64_t)(uint32_t)bo->alloc_size);
    return ok;
}

static bool c4_svga_real_define_legacy_screen(uint32_t w,uint32_t h){
    c4_svga_fifo_define_screen_t def;
    c4_svga_fifo_update_t upd;
    bool ok;
    if(!w||!h)return false;
    ulibc_memset(&def,0,sizeof(def));
    def.screen.struct_size=(uint32_t)sizeof(def.screen);
    def.screen.id=0;
    def.screen.flags=C4_SVGA_SCREEN_HAS_ROOT|C4_SVGA_SCREEN_IS_PRIMARY;
    def.screen.width=w;
    def.screen.height=h;
    def.screen.root_x=0;
    def.screen.root_y=0;
    def.screen.backing_store.ptr.gmrid=C4_SVGA_GMR_FRAMEBUFFER;
    def.screen.backing_store.ptr.offset=0;
    def.screen.backing_store.pitch=g_c4_svga_real.fb_pitch?g_c4_svga_real.fb_pitch:w*4u;
    def.screen.clone_count=0;
    c4_svga_wr_reg(C4_SVGA_REG_BLANK_SCREEN_TARGETS,0u);
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_DEFINE_SCREEN,&def,sizeof(def));
    ulibc_memset(&upd,0,sizeof(upd));
    upd.width=w;
    upd.height=h;
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_UPDATE,&upd,sizeof(upd))&&ok;
    if(ok)g_c4_svga_real.screen_object_defined=true;
    c4_svga_real_trace(ok?"screen-object":"screen-object-fail",w,h,sizeof(def),g_c4_svga_real.fifo_caps);
    return ok;
}

static bool c4_svga_real_blit_vram_to_screen(uint32_t w,uint32_t h){
    c4_svga_define_gmrfb_t gmr;
    c4_svga_blit_gmrfb_to_screen_t blit;
    bool ok;
    if(!w||!h||!g_c4_svga_real.fb_pitch)return false;
    if(!g_c4_svga_real.screen_object_defined)
        (void)c4_svga_real_define_legacy_screen(w,h);
    ulibc_memset(&gmr,0,sizeof(gmr));
    gmr.ptr.gmrid=C4_SVGA_GMR_FRAMEBUFFER;
    gmr.ptr.offset=0;
    gmr.bytes_per_line=g_c4_svga_real.fb_pitch;
    gmr.format=32u|(24u<<8);
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_DEFINE_GMRFB,&gmr,sizeof(gmr));
    ulibc_memset(&blit,0,sizeof(blit));
    blit.dest_rect.right=(int32_t)w;
    blit.dest_rect.bottom=(int32_t)h;
    blit.dest_screen_id=0;
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_BLIT_GMRFB_TO_SCREEN,&blit,sizeof(blit))&&ok;
    if(ok)ok=c4_svga_real_fence();
    c4_svga_real_trace(ok?"gmrfb-blit":"gmrfb-blit-fail",w,h,g_c4_svga_real.fb_start,g_c4_svga_real.fb_pitch);
    return ok;
}

static uint32_t c4_vram_rgb(uint32_t r,uint32_t g,uint32_t b){
    return 0xFF000000u|((r&0xFFu)<<16)|((g&0xFFu)<<8)|(b&0xFFu);
}

static void c4_svga_vram_rect(uint32_t screen_w,uint32_t screen_h,
                              int32_t x,int32_t y,int32_t w,int32_t h,
                              uint32_t color){
    int32_t yy;
    if(!g_c4_svga_real.fb||!g_c4_svga_real.fb_pitch||!screen_w||!screen_h)return;
    if(w<=0||h<=0)return;
    if(x<0){w+=x;x=0;}
    if(y<0){h+=y;y=0;}
    if(x>=(int32_t)screen_w||y>=(int32_t)screen_h)return;
    if(x+w>(int32_t)screen_w)w=(int32_t)screen_w-x;
    if(y+h>(int32_t)screen_h)h=(int32_t)screen_h-y;
    if(w<=0||h<=0)return;
    for(yy=0;yy<h;++yy){
        uint32_t *row=(uint32_t*)(void*)(uintptr_t)
            (g_c4_svga_real.fb+(uint64_t)(y+yy)*g_c4_svga_real.fb_pitch);
        int32_t xx;
        for(xx=0;xx<w;++xx)row[x+xx]=color;
    }
}

static void c4_svga_draw_visible_desktop_overlay(uint32_t w,uint32_t h){
    uint32_t y;
    int i;
    if(!c4_drm_vbox_mode()||!w||!h)return;
    for(y=0;y<h;++y){
        uint32_t shade=24u+(y*34u)/(h?h:1u);
        c4_svga_vram_rect(w,h,0,(int32_t)y,(int32_t)w,1,
                          c4_vram_rgb(shade,shade+10u,shade+24u));
    }
    c4_svga_vram_rect(w,h,0,0,(int32_t)w,52,c4_vram_rgb(32,39,49));
    c4_svga_vram_rect(w,h,24,14,124,24,c4_vram_rgb(79,196,232));
    c4_svga_vram_rect(w,h,(int32_t)w-268,14,72,24,c4_vram_rgb(48,176,132));
    c4_svga_vram_rect(w,h,(int32_t)w-184,14,72,24,c4_vram_rgb(208,160,56));
    c4_svga_vram_rect(w,h,(int32_t)w-100,14,76,24,c4_vram_rgb(72,88,112));

    c4_svga_vram_rect(w,h,68,94,(int32_t)(w*42u/100u),(int32_t)(h*52u/100u),c4_vram_rgb(9,13,18));
    c4_svga_vram_rect(w,h,60,86,(int32_t)(w*42u/100u),(int32_t)(h*52u/100u),c4_vram_rgb(35,44,58));
    c4_svga_vram_rect(w,h,60,86,(int32_t)(w*42u/100u),44,c4_vram_rgb(50,63,82));
    c4_svga_vram_rect(w,h,78,104,116,13,c4_vram_rgb(105,215,244));
    c4_svga_vram_rect(w,h,82,158,(int32_t)(w*32u/100u),20,c4_vram_rgb(91,111,140));
    c4_svga_vram_rect(w,h,82,198,(int32_t)(w*28u/100u),20,c4_vram_rgb(69,86,113));
    c4_svga_vram_rect(w,h,82,238,(int32_t)(w*35u/100u),20,c4_vram_rgb(69,86,113));
    c4_svga_vram_rect(w,h,82,296,(int32_t)(w*15u/100u),92,c4_vram_rgb(36,142,183));
    c4_svga_vram_rect(w,h,102+(int32_t)(w*15u/100u),296,
                      (int32_t)(w*15u/100u),92,c4_vram_rgb(166,96,196));

    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u)+8,118,
                      (int32_t)(w*32u/100u),(int32_t)(h*42u/100u),c4_vram_rgb(8,12,16));
    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u),110,
                      (int32_t)(w*32u/100u),(int32_t)(h*42u/100u),c4_vram_rgb(40,50,66));
    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u),110,
                      (int32_t)(w*32u/100u),42,c4_vram_rgb(58,72,92));
    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u)+24,188,
                      (int32_t)(w*24u/100u),58,c4_vram_rgb(58,85,113));
    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u)+24,270,
                      (int32_t)(w*24u/100u),58,c4_vram_rgb(58,85,113));
    c4_svga_vram_rect(w,h,(int32_t)(w*54u/100u)+24,352,
                      (int32_t)(w*24u/100u),58,c4_vram_rgb(58,85,113));

    c4_svga_vram_rect(w,h,(int32_t)(w*19u/100u),(int32_t)h-100,
                      (int32_t)(w*62u/100u),66,c4_vram_rgb(27,34,43));
    for(i=0;i<9;++i){
        c4_svga_vram_rect(w,h,(int32_t)(w*23u/100u)+i*56,(int32_t)h-84,
                          39,39,
                          c4_vram_rgb(60u+(uint32_t)(i%3)*16u,
                                      96u+(uint32_t)(i%4)*12u,
                                      170u+(uint32_t)(i%2)*32u));
    }
}

static bool c4_svga_real_copy_bo_to_vram(c4_drm_bo_t *bo){
    c4_svga_fifo_update_t upd;
    uint32_t w,h,y,row_bytes,first_dirty=0,last_dirty=0;
    bool ok;
    bool dirty=false;
    if(!bo||!bo->data||!g_c4_svga_real.fb||!g_c4_svga_real.fb_size)return false;
    if(bo->bpp!=32||!bo->pitch||!g_c4_svga_real.fb_pitch)return false;
    w=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    h=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    if((uint64_t)w*4ULL>(uint64_t)bo->pitch)w=bo->pitch/4u;
    if((uint64_t)w*4ULL>(uint64_t)g_c4_svga_real.fb_pitch)w=g_c4_svga_real.fb_pitch/4u;
    if((uint64_t)g_c4_svga_real.fb_pitch*(uint64_t)h>g_c4_svga_real.fb_size)
        h=(uint32_t)((uint64_t)g_c4_svga_real.fb_size/(uint64_t)g_c4_svga_real.fb_pitch);
    if((uint64_t)bo->pitch*(uint64_t)h>bo->size)
        h=(uint32_t)(bo->size/(uint64_t)bo->pitch);
    if(!w||!h)return false;
    row_bytes=w*4u;
    for(y=0;y<h;++y){
        void *dst=(void*)(uintptr_t)(g_c4_svga_real.fb+(uint64_t)y*g_c4_svga_real.fb_pitch);
        const void *src=bo->data+(uint64_t)y*bo->pitch;
        if(ulibc_memcmp(dst,src,row_bytes)!=0){
            if(!dirty)first_dirty=y;
            last_dirty=y;
            dirty=true;
            ulibc_memcpy(dst,src,row_bytes);
        }
    }
    if(c4_drm_vbox_mode()){
        c4_svga_draw_visible_desktop_overlay(w,h);
        dirty=true;
        first_dirty=0;
        last_dirty=h-1u;
    }
    if(!dirty){
        c4_svga_real_trace("vram-skip",bo->handle,
                            ((uint64_t)w<<32)|h,g_c4_svga_real.fb_start,
                            g_c4_svga_real.fb_pitch);
        return true;
    }
    ulibc_memset(&upd,0,sizeof(upd));
    upd.y=first_dirty;
    upd.width=w;
    upd.height=last_dirty-first_dirty+1u;
    ok=c4_svga_real_blit_vram_to_screen(w,h);
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_UPDATE,&upd,sizeof(upd))&&ok;
    if(ok)ok=c4_svga_real_fence();
    c4_svga_real_trace(ok?"vram-present":"vram-present-fail",bo->handle,
                        ((uint64_t)w<<32)|h,g_c4_svga_real.fb_start,g_c4_svga_real.fb_pitch);
    return ok;
}

static bool c4_svga_real_readback_surface(uint32_t sid){
    c4_vmw_surface_record_t *rec;
    c4_drm_bo_t *bo=0;
    c4_svga_bind_gb_surface_t unbind;
    c4_svga_bind_gb_surface_pitch_t rebind;
    bool ok;
    if(!sid)return false;
    rec=c4_vmw_surface_by_sid(sid);
    if(rec)bo=c4_drm_find_bo_by_handle(rec->bo_handle);
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_READBACK_GB_SURFACE,&sid,sizeof(sid));
    if(bo&&bo->handle){
        ulibc_memset(&unbind,0,sizeof(unbind));
        unbind.sid=sid;
        unbind.mobid=C4_SVGA3D_INVALID_ID;
        ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BIND_GB_SURFACE,&unbind,sizeof(unbind))&&ok;
    }
    if(ok)ok=c4_svga_real_fence();
    if(ok&&bo&&bo->handle){
        ulibc_memset(&rebind,0,sizeof(rebind));
        rebind.sid=sid;
        rebind.mobid=bo->handle;
        rebind.base_level_pitch=bo->pitch;
        ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH,&rebind,sizeof(rebind))&&ok;
        if(ok)ok=c4_svga_real_fence();
    }
    c4_svga_real_trace(ok?"surface-readback":"surface-readback-fail",sid,0,0,0);
    return ok;
}

static bool c4_svga_real_copy_surface_to_surface(uint32_t src_sid,uint32_t dst_sid,
                                                 uint32_t w,uint32_t h){
    struct __attribute__((packed)) {
        c4_svga_surface_image_t src;
        c4_svga_surface_image_t dest;
        c4_svga_copy_box_t box;
    } cmd;
    bool ok;
    if(!src_sid||!dst_sid||src_sid==dst_sid||!w||!h)return false;
    ulibc_memset(&cmd,0,sizeof(cmd));
    cmd.src.sid=src_sid;
    cmd.dest.sid=dst_sid;
    cmd.box.w=w;
    cmd.box.h=h;
    cmd.box.d=1u;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_SURFACE_COPY,&cmd,sizeof(cmd));
    c4_svga_real_trace(ok?"surface-copy":"surface-copy-fail",src_sid,dst_sid,
                        ((uint64_t)w<<32)|h,0);
    return ok;
}

static c4_vmw_surface_record_t *c4_svga_real_display_surface(c4_vmw_surface_record_t *src,
                                                             uint32_t w,uint32_t h){
    c4_drm_bo_t *bo;
    c4_vmw_gb_surface_ext_req_t req;
    c4_vmw_gb_surface_rep_t rep;
    uint32_t sid;
    if(!w||!h)return 0;
    if(g_c4_svga_real.display_sid&&
       g_c4_svga_real.display_width==w&&g_c4_svga_real.display_height==h){
        c4_vmw_surface_record_t *rec=c4_vmw_surface_by_sid(g_c4_svga_real.display_sid);
        if(rec)return rec;
    }
    bo=c4_drm_alloc_bo(w,h,32u);
    if(!bo)return 0;
    sid=g_c4_vmw_next_surface++;
    if(!sid)sid=g_c4_vmw_next_surface++;
    ulibc_memset(&req,0,sizeof(req));
    if(src)req=src->req;
    req.base.base_size.width=w;
    req.base.base_size.height=h;
    req.base.base_size.depth=1u;
    req.base.buffer_handle=bo->handle;
    req.base.mip_levels=req.base.mip_levels?req.base.mip_levels:1u;
    req.base.format=req.base.format?req.base.format:1u;
    req.buffer_byte_stride=bo->pitch;
    ulibc_memset(&rep,0,sizeof(rep));
    rep.handle=sid;
    rep.backup_size=(uint32_t)bo->size;
    rep.buffer_handle=bo->handle;
    rep.buffer_size=(uint32_t)bo->size;
    rep.buffer_map_handle=bo->map_offset;
    g_c4_svga_real.display_sid=sid;
    g_c4_svga_real.display_bo_handle=bo->handle;
    g_c4_svga_real.display_width=w;
    g_c4_svga_real.display_height=h;
    c4_svga_real_trace("display-surface",sid,bo->handle,((uint64_t)w<<32)|h,bo->pitch);
    return c4_vmw_remember_surface(sid,bo,0,&req,&rep);
}

static bool c4_svga_real_present_surface(uint32_t sid,c4_drm_bo_t *bo){
    c4_svga_define_screen_target_t def;
    c4_svga_bind_screen_target_t bind;
    c4_svga_update_screen_target_t upd;
    c4_svga_blit_surface_to_screen_t blit;
    c4_svga_fifo_update_t fifo_upd;
    c4_vmw_surface_record_t *src_rec;
    c4_vmw_surface_record_t *scanout_rec=0;
    uint32_t scanout_sid;
    uint32_t w,h;
    bool redefine_screen;
    bool ok=true;
    if(!sid||!bo)return false;
    w=bo->width?bo->width:(g_fb.width?g_fb.width:1024u);
    h=bo->height?bo->height:(g_fb.height?g_fb.height:768u);
    if(!w||!h)return false;
    redefine_screen=!g_c4_svga_real.screen_defined||
                    g_c4_svga_real.screen_width!=w||
                    g_c4_svga_real.screen_height!=h;
    if(redefine_screen){
        ulibc_memset(&def,0,sizeof(def));
        def.stid=0;
        def.width=w;
        def.height=h;
        def.flags=C4_SVGA_STFLAG_PRIMARY;
        def.dpi=0;
        ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_DEFINE_GB_SCREENTARGET,&def,sizeof(def));
        if(ok){
            g_c4_svga_real.screen_defined=true;
            g_c4_svga_real.screen_width=w;
            g_c4_svga_real.screen_height=h;
        }
    }
    src_rec=c4_vmw_surface_by_sid(sid);
    scanout_sid=sid;
    scanout_rec=c4_svga_real_display_surface(src_rec,w,h);
    if(scanout_rec&&scanout_rec->sid&&scanout_rec->sid!=sid){
        if(c4_svga_real_copy_surface_to_surface(sid,scanout_rec->sid,w,h))
            scanout_sid=scanout_rec->sid;
    }
    ulibc_memset(&bind,0,sizeof(bind));
    bind.stid=0;
    bind.image.sid=scanout_sid;
    ulibc_memset(&upd,0,sizeof(upd));
    upd.stid=0;
    upd.rect.w=w;
    upd.rect.h=h;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BIND_GB_SCREENTARGET,&bind,sizeof(bind))&&ok;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_UPDATE_GB_SCREENTARGET,&upd,sizeof(upd))&&ok;
    if(redefine_screen||!g_c4_svga_real.screen_object_defined)
        ok=c4_svga_real_define_legacy_screen(w,h)&&ok;
    ulibc_memset(&blit,0,sizeof(blit));
    blit.src_image.sid=scanout_sid;
    blit.src_rect.right=(int32_t)w;
    blit.src_rect.bottom=(int32_t)h;
    blit.dest_screen_id=0;
    blit.dest_rect.right=(int32_t)w;
    blit.dest_rect.bottom=(int32_t)h;
    ok=c4_svga_real_emit_command(C4_SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN,&blit,sizeof(blit))&&ok;
    ulibc_memset(&fifo_upd,0,sizeof(fifo_upd));
    fifo_upd.width=w;
    fifo_upd.height=h;
    ok=c4_svga_real_emit_legacy(C4_SVGA_CMD_UPDATE,&fifo_upd,sizeof(fifo_upd))&&ok;
    if(ok)c4_svga_real_fence();
    c4_svga_real_trace(ok?"screen-present":"screen-present-fail",scanout_sid,bo->handle,
                        ((uint64_t)w<<32)|h,g_c4_svga_real.screen_defined);
    return ok;
}

static bool c4_svga_real_present_bo(c4_drm_bo_t *bo){
    c4_vmw_surface_record_t *rec;
    c4_vmw_gb_surface_req_t req;
    c4_vmw_gb_surface_rep_t rep;
    bool full_frame_cpu_fallback=false;
    if(!bo)return false;
    {
        uint32_t fbw=(g_fb.width?g_fb.width:1024u);
        uint32_t fbh=(g_fb.height?g_fb.height:768u);
        uint32_t nz=0;
        bool full_frame=bo->data&&bo->bpp==32u&&bo->pitch&&
                        bo->width>=fbw&&bo->height>=fbh;
        c4_drm_sample_bo(bo,&nz,0,0,0);
        if(full_frame&&nz&&
           (bo->handle==g_c4_vmw_rescue_rt_handle||
            bo==g_c4_native_present_bo||
            c4_drm_vbox_mode())){
            full_frame_cpu_fallback=true;
        }
    }
    rec=c4_vmw_surface_by_bo(bo->handle);
    if(!rec){
        ulibc_memset(&req,0,sizeof(req));
        ulibc_memset(&rep,0,sizeof(rep));
        req.format=1;
        req.mip_levels=1;
        req.buffer_handle=bo->handle;
        req.base_size.width=bo->width?bo->width:1u;
        req.base_size.height=bo->height?bo->height:1u;
        req.base_size.depth=1u;
        rep.handle=bo->handle;
        rep.backup_size=(uint32_t)bo->size;
        rep.buffer_handle=bo->handle;
        rep.buffer_size=(uint32_t)bo->size;
        rep.buffer_map_handle=bo->map_offset;
        rec=c4_vmw_remember_surface(bo->handle,bo,&req,0,&rep);
    }
    if(!rec){
        if(full_frame_cpu_fallback)
            return c4_svga_real_copy_bo_to_vram(bo);
        return false;
    }
    if(!bo->real_created)c4_svga_real_define_surface(rec,bo);
    {
        bool st_ok=false;
        if(bo->real_created)
            st_ok=c4_svga_real_present_surface(rec->sid,bo)||st_ok;
        if(st_ok){
            if(full_frame_cpu_fallback)
                (void)c4_svga_real_copy_bo_to_vram(bo);
            return true;
        }
        if(c4_drm_vbox_mode()&&bo->real_created)
            return false;
        if(full_frame_cpu_fallback)
            return c4_svga_real_copy_bo_to_vram(bo);
        if(bo->real_created)c4_svga_real_readback_surface(rec->sid);
        return c4_svga_real_copy_bo_to_vram(bo);
    }
}

static c4_vmw_surface_record_t *c4_vmw_remember_surface(uint32_t sid,c4_drm_bo_t *bo,
                                                        const c4_vmw_gb_surface_req_t *base_req,
                                                        const c4_vmw_gb_surface_ext_req_t *ext_req,
                                                        const c4_vmw_gb_surface_rep_t *rep){
    c4_vmw_surface_record_t *r=c4_vmw_surface_slot(sid);
    if(!r||!bo||!rep)return 0;
    ulibc_memset(r,0,sizeof(*r));
    r->used=true;
    r->sid=sid;
    r->bo_handle=bo->handle;
    if(ext_req)r->req=*ext_req;
    else if(base_req)r->req.base=*base_req;
    if(!r->req.base.mip_levels)r->req.base.mip_levels=1;
    if(!r->req.base.format)r->req.base.format=1;
    if(!r->req.base.buffer_handle)r->req.base.buffer_handle=bo->handle;
    if(!r->req.base.base_size.width)r->req.base.base_size.width=bo->width?bo->width:1u;
    if(!r->req.base.base_size.height)r->req.base.base_size.height=bo->height?bo->height:1u;
    if(!r->req.base.base_size.depth)r->req.base.base_size.depth=1u;
    if(!r->req.buffer_byte_stride)r->req.buffer_byte_stride=bo->pitch;
    r->rep=*rep;
    r->rep.handle=sid;
    r->rep.buffer_handle=bo->handle;
    r->rep.backup_size=(uint32_t)bo->size;
    r->rep.buffer_size=(uint32_t)bo->size;
    r->rep.buffer_map_handle=bo->map_offset;
    c4_svga_real_define_surface(r,bo);
    return r;
}

static c4_vmw_surface_record_t *c4_vmw_surface_from_arg(const c4_vmw_surface_arg_t *a){
    c4_vmw_surface_record_t *r;
    if(!a)return 0;
    if(a->handle_type==C4_VMW_HANDLE_PRIME){
        uint32_t bo_handle=c4_drm_handle_from_prime_fd(a->sid);
        r=c4_vmw_surface_by_bo(bo_handle);
        if(r)return r;
        if(bo_handle){
            c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(bo_handle);
            if(bo){
                c4_vmw_gb_surface_req_t req;
                c4_vmw_gb_surface_rep_t rep;
                uint32_t sid=g_c4_vmw_next_surface++;
                if(!sid)sid=g_c4_vmw_next_surface++;
                ulibc_memset(&req,0,sizeof(req));
                ulibc_memset(&rep,0,sizeof(rep));
                req.format=1;
                req.mip_levels=1;
                req.buffer_handle=bo->handle;
                req.base_size.width=bo->width?bo->width:1u;
                req.base_size.height=bo->height?bo->height:1u;
                req.base_size.depth=1u;
                rep.handle=sid;
                rep.backup_size=(uint32_t)bo->size;
                rep.buffer_handle=bo->handle;
                rep.buffer_size=(uint32_t)bo->size;
                rep.buffer_map_handle=bo->map_offset;
                return c4_vmw_remember_surface(sid,bo,&req,0,&rep);
            }
        }
    }
    return c4_vmw_surface_by_sid((uint32_t)a->sid);
}

static c4_drm_bo_t *c4_vmw_bo_from_surface_handle(uint32_t handle){
    c4_vmw_surface_record_t *r=c4_vmw_surface_by_sid(handle);
    if(!r)return c4_drm_find_bo_by_handle(handle);
    return c4_drm_find_bo_by_handle(r->bo_handle);
}

static void c4_vmw_view_set_in(c4_vmw_view_t *views,uint32_t view_id,uint32_t sid,
                               uint32_t format,uint32_t dimension){
    int i;
    c4_vmw_view_t *free_slot=0;
    for(i=0;i<C4_VMW_MAX_VIEWS;++i){
        if(views[i].used&&views[i].view_id==view_id){
            views[i].sid=sid;
            views[i].format=format;
            views[i].dimension=dimension;
            return;
        }
        if(!views[i].used&&!free_slot)free_slot=&views[i];
    }
    if(free_slot){
        free_slot->used=true;
        free_slot->view_id=view_id;
        free_slot->sid=sid;
        free_slot->format=format;
        free_slot->dimension=dimension;
    }
}

static uint32_t c4_vmw_view_sid_in(c4_vmw_view_t *views,uint32_t view_id){
    int i;
    for(i=0;i<C4_VMW_MAX_VIEWS;++i)
        if(views[i].used&&views[i].view_id==view_id)
            return views[i].sid;
    return 0;
}

static uint32_t c4_vmw_view_format_in(c4_vmw_view_t *views,uint32_t view_id){
    int i;
    for(i=0;i<C4_VMW_MAX_VIEWS;++i)
        if(views[i].used&&views[i].view_id==view_id)
            return views[i].format;
    return 0;
}

static uint32_t c4_vmw_view_dimension_in(c4_vmw_view_t *views,uint32_t view_id){
    int i;
    for(i=0;i<C4_VMW_MAX_VIEWS;++i)
        if(views[i].used&&views[i].view_id==view_id)
            return views[i].dimension;
    return 0;
}

static void c4_vmw_view_set(uint32_t view_id,uint32_t sid,uint32_t format,uint32_t dimension){
    c4_vmw_view_set_in(g_c4_vmw_rtvs,view_id,sid,format,dimension);
}

static uint32_t c4_vmw_view_sid(uint32_t view_id){
    return c4_vmw_view_sid_in(g_c4_vmw_rtvs,view_id);
}

static uint32_t c4_vmw_view_format(uint32_t view_id){
    return c4_vmw_view_format_in(g_c4_vmw_rtvs,view_id);
}

static uint32_t c4_vmw_view_dimension(uint32_t view_id){
    return c4_vmw_view_dimension_in(g_c4_vmw_rtvs,view_id);
}

static void c4_vmw_srv_set(uint32_t view_id,uint32_t sid){
    c4_vmw_view_set_in(g_c4_vmw_srvs,view_id,sid,0,0);
}

static uint32_t c4_vmw_srv_sid(uint32_t view_id){
    return c4_vmw_view_sid_in(g_c4_vmw_srvs,view_id);
}

static uint8_t c4_vmw_float01_to_u8(uint32_t bits){
    uint32_t exp;
    uint32_t mant;
    uint32_t shift;
    uint64_t scaled;
    if(bits&0x80000000u)return 0;
    if(bits>=0x3f800000u)return 255;
    exp=(bits>>23)&0xffu;
    if(!exp)return 0;
    mant=(bits&0x7fffffu)|0x800000u;
    if(exp>=127u)return 255;
    shift=150u-exp;
    scaled=(uint64_t)mant*255ULL;
    if(shift>=63u)return 0;
    scaled>>=shift;
    return scaled>255ULL?255u:(uint8_t)scaled;
}

static uint32_t c4_vmw_color_from_rgba_words(const uint32_t *rgba){
    uint8_t r,g,b,a;
    if(!rgba)return 0xFF000000u;
    r=c4_vmw_float01_to_u8(rgba[0]);
    g=c4_vmw_float01_to_u8(rgba[1]);
    b=c4_vmw_float01_to_u8(rgba[2]);
    a=c4_vmw_float01_to_u8(rgba[3]);
    if(!a)a=255u;
    return ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
}

static void c4_vmw_clear_bo(c4_drm_bo_t *bo,uint32_t color){
    uint32_t x,y,w,h;
    if(!bo||!bo->used||!bo->data||bo->bpp!=32||!bo->pitch)return;
    w=bo->width;
    h=bo->height;
    if((uint64_t)w*4ULL>(uint64_t)bo->pitch)w=bo->pitch/4u;
    if((uint64_t)bo->pitch*(uint64_t)h>bo->size)h=(uint32_t)(bo->size/(uint64_t)bo->pitch);
    for(y=0;y<h;++y){
        uint32_t *row=(uint32_t*)(void*)(bo->data+(size_t)y*bo->pitch);
        for(x=0;x<w;++x)row[x]=color;
    }
}

static bool c4_vmw_copy_bo_rect(c4_drm_bo_t *dst,uint32_t dx,uint32_t dy,
                                c4_drm_bo_t *src,uint32_t sx,uint32_t sy,
                                uint32_t w,uint32_t h){
    uint32_t y;
    if(!dst||!src||!dst->data||!src->data)return false;
    if(dst->bpp!=32||src->bpp!=32||!dst->pitch||!src->pitch)return false;
    if(sx>=src->width||sy>=src->height||dx>=dst->width||dy>=dst->height)return false;
    if(w>src->width-sx)w=src->width-sx;
    if(h>src->height-sy)h=src->height-sy;
    if(w>dst->width-dx)w=dst->width-dx;
    if(h>dst->height-dy)h=dst->height-dy;
    if((uint64_t)(sx+w)*4ULL>(uint64_t)src->pitch)w=(src->pitch/4u)>sx?(src->pitch/4u)-sx:0;
    if((uint64_t)(dx+w)*4ULL>(uint64_t)dst->pitch)w=(dst->pitch/4u)>dx?(dst->pitch/4u)-dx:0;
    if(!w||!h)return false;
    for(y=0;y<h;++y){
        uint64_t so=(uint64_t)(sy+y)*(uint64_t)src->pitch+(uint64_t)sx*4ULL;
        uint64_t doff=(uint64_t)(dy+y)*(uint64_t)dst->pitch+(uint64_t)dx*4ULL;
        if(so+(uint64_t)w*4ULL>src->size||doff+(uint64_t)w*4ULL>dst->size)break;
        ulibc_memmove(dst->data+(size_t)doff,src->data+(size_t)so,(size_t)w*4u);
    }
    return y>0;
}

static bool c4_vmw_blend_bo_rect(c4_drm_bo_t *dst,uint32_t dx,uint32_t dy,
                                 c4_drm_bo_t *src,uint32_t sx,uint32_t sy,
                                 uint32_t w,uint32_t h){
    uint32_t y;
    bool changed=false;
    if(!dst||!src||!dst->data||!src->data)return false;
    if(dst->bpp!=32||src->bpp!=32||!dst->pitch||!src->pitch)return false;
    if(sx>=src->width||sy>=src->height||dx>=dst->width||dy>=dst->height)return false;
    if(w>src->width-sx)w=src->width-sx;
    if(h>src->height-sy)h=src->height-sy;
    if(w>dst->width-dx)w=dst->width-dx;
    if(h>dst->height-dy)h=dst->height-dy;
    if((uint64_t)(sx+w)*4ULL>(uint64_t)src->pitch)w=(src->pitch/4u)>sx?(src->pitch/4u)-sx:0;
    if((uint64_t)(dx+w)*4ULL>(uint64_t)dst->pitch)w=(dst->pitch/4u)>dx?(dst->pitch/4u)-dx:0;
    if(!w||!h)return false;
    for(y=0;y<h;++y){
        uint32_t x;
        uint64_t so=(uint64_t)(sy+y)*(uint64_t)src->pitch+(uint64_t)sx*4ULL;
        uint64_t doff=(uint64_t)(dy+y)*(uint64_t)dst->pitch+(uint64_t)dx*4ULL;
        uint32_t *srow=(uint32_t*)(void*)(src->data+(size_t)so);
        uint32_t *drow=(uint32_t*)(void*)(dst->data+(size_t)doff);
        if(so+(uint64_t)w*4ULL>src->size||doff+(uint64_t)w*4ULL>dst->size)break;
        for(x=0;x<w;++x){
            uint32_t sp=srow[x];
            uint32_t a=sp>>24;
            if(!sp)continue;
            if(!a||a==255u){
                drow[x]=sp|0xFF000000u;
            }else{
                uint32_t dp=drow[x];
                uint32_t sr=(sp>>16)&0xFFu,sg=(sp>>8)&0xFFu,sb=sp&0xFFu;
                uint32_t dr=(dp>>16)&0xFFu,dg=(dp>>8)&0xFFu,db=dp&0xFFu;
                uint32_t ia=255u-a;
                drow[x]=0xFF000000u|
                    (((sr*a+dr*ia)/255u)<<16)|
                    (((sg*a+dg*ia)/255u)<<8)|
                    ((sb*a+db*ia)/255u);
            }
            changed=true;
        }
    }
    return changed;
}

static bool c4_vmw_copy_surface_all(uint32_t dst_sid,uint32_t src_sid){
    c4_drm_bo_t *dst=c4_vmw_bo_from_surface_handle(dst_sid);
    c4_drm_bo_t *src=c4_vmw_bo_from_surface_handle(src_sid);
    uint32_t w,h;
    if(!dst||!src)return false;
    w=dst->width<src->width?dst->width:src->width;
    h=dst->height<src->height?dst->height:src->height;
    return c4_vmw_copy_bo_rect(dst,0,0,src,0,0,w,h);
}

static bool c4_vmw_copy_surface_box(uint32_t dst_sid,uint32_t src_sid,
                                    const c4_svga_copy_box_t *box){
    c4_drm_bo_t *dst=c4_vmw_bo_from_surface_handle(dst_sid);
    c4_drm_bo_t *src=c4_vmw_bo_from_surface_handle(src_sid);
    if(!box)return c4_vmw_copy_surface_all(dst_sid,src_sid);
    return c4_vmw_copy_bo_rect(dst,box->x,box->y,src,box->srcx,box->srcy,box->w,box->h);
}

static bool c4_vmw_present_blt(uint32_t dst_sid,uint32_t src_sid,
                               const c4_svga_box_t *src_box,
                               const c4_svga_box_t *dst_box){
    c4_drm_bo_t *dst=c4_vmw_bo_from_surface_handle(dst_sid);
    c4_drm_bo_t *src=c4_vmw_bo_from_surface_handle(src_sid);
    uint32_t sx=0,sy=0,dx=0,dy=0,w,h;
    if(!dst||!src)return false;
    w=dst->width<src->width?dst->width:src->width;
    h=dst->height<src->height?dst->height:src->height;
    if(src_box){
        sx=src_box->x;
        sy=src_box->y;
        if(src_box->w)w=src_box->w;
        if(src_box->h)h=src_box->h;
    }
    if(dst_box){
        dx=dst_box->x;
        dy=dst_box->y;
        if(dst_box->w&&dst_box->w<w)w=dst_box->w;
        if(dst_box->h&&dst_box->h<h)h=dst_box->h;
    }
    return c4_vmw_copy_bo_rect(dst,dx,dy,src,sx,sy,w,h);
}

static void c4_vmw_trace_packet_words(uint32_t id,uint32_t size,const uint32_t *p,uint32_t words){
    uint32_t i,n;
    if(!c4_vmwgfx_trace_enabled())return;
    if(g_c4_vmw_exec_packet_trace_count>=320u)return;
    ++g_c4_vmw_exec_packet_trace_count;
    __boot_serial_force_puts("[vmwgfx-pkt] #");
    __boot_serial_force_putu32(g_c4_vmw_exec_packet_trace_count);
    __boot_serial_force_puts(" id=");
    __boot_serial_force_puthex64((uint64_t)id);
    __boot_serial_force_puts(" size=");
    __boot_serial_force_putu32(size);
    n=words<6u?words:6u;
    for(i=0;i<n;++i){
        __boot_serial_force_puts(" w");
        __boot_serial_force_putu32(i);
        __boot_serial_force_puts("=");
        __boot_serial_force_puthex64((uint64_t)(p?p[i]:0u));
    }
    __boot_serial_force_puts("\n");
}

static void c4_vmw_trace_packet(uint32_t id,uint32_t size,uint32_t a,uint32_t b){
    uint32_t w[2];
    w[0]=a;
    w[1]=b;
    c4_vmw_trace_packet_words(id,size,w,2);
}

static c4_drm_bo_t *c4_vmw_current_render_bo(void){
    uint32_t i;
    for(i=0;i<(uint32_t)(sizeof(g_c4_vmw_bound_rtvs)/sizeof(g_c4_vmw_bound_rtvs[0]));++i){
        uint32_t sid;
        if(!g_c4_vmw_bound_rtvs[i])continue;
        sid=c4_vmw_view_sid(g_c4_vmw_bound_rtvs[i]);
        if(sid)return c4_vmw_bo_from_surface_handle(sid);
    }
    return 0;
}

static void c4_vmw_note_nonzero_bo(uint32_t sid,c4_drm_bo_t *bo){
    uint32_t nz=0;
    if(!bo)return;
    c4_drm_sample_bo(bo,&nz,0,0,0);
    if(!nz)return;
    g_c4_vmw_last_nonzero_sid=sid;
    g_c4_vmw_last_nonzero_bo=bo->handle;
}

static c4_drm_bo_t *c4_vmw_current_shader_bo(void){
    uint32_t type,slot;
    for(type=0;type<6u;++type){
        for(slot=0;slot<(uint32_t)(sizeof(g_c4_vmw_bound_srvs[0])/sizeof(g_c4_vmw_bound_srvs[0][0]));++slot){
            uint32_t view=g_c4_vmw_bound_srvs[type][slot];
            uint32_t sid;
            c4_drm_bo_t *bo;
            uint32_t nz=0;
            if(!view&&slot!=0)continue;
            sid=c4_vmw_srv_sid(view);
            bo=sid?c4_vmw_bo_from_surface_handle(sid):0;
            c4_drm_sample_bo(bo,&nz,0,0,0);
            if(nz)return bo;
        }
    }
    if(g_c4_vmw_last_nonzero_bo)return c4_drm_find_bo_by_handle(g_c4_vmw_last_nonzero_bo);
    return 0;
}

static void c4_vmw_fill_rt(c4_drm_bo_t *rt,uint32_t color){
    uint32_t y,w,h;
    if(!rt||!rt->data||rt->bpp!=32||!rt->pitch)return;
    w=rt->width?rt->width:(rt->pitch/4u);
    h=rt->height?rt->height:1u;
    if((uint64_t)w*4ULL>(uint64_t)rt->pitch)w=rt->pitch/4u;
    if((uint64_t)rt->pitch*(uint64_t)h>rt->size)h=(uint32_t)(rt->size/(uint64_t)rt->pitch);
    for(y=0;y<h;++y){
        uint32_t x;
        uint32_t *row=(uint32_t*)(void*)(rt->data+(uint64_t)y*rt->pitch);
        for(x=0;x<w;++x)row[x]=color;
    }
}

static uint32_t c4_vmw_alpha_over(uint32_t dst,uint32_t src){
    uint32_t a=src>>24;
    uint32_t inv,sr,sg,sb,dr,dg,db;
    if(a==0)return dst;
    if(a>=250u)return src|0xff000000u;
    inv=255u-a;
    sr=(src>>16)&255u; sg=(src>>8)&255u; sb=src&255u;
    dr=(dst>>16)&255u; dg=(dst>>8)&255u; db=dst&255u;
    return 0xff000000u|
           (((sr*a+dr*inv)/255u)<<16)|
           (((sg*a+dg*inv)/255u)<<8)|
           ((sb*a+db*inv)/255u);
}

static bool c4_vmw_blit_bo(c4_drm_bo_t *rt,c4_drm_bo_t *src,int32_t dx,int32_t dy){
    uint32_t sw,sh,dw,dh,y;
    bool changed=false;
    if(!rt||!src||rt==src||!rt->data||!src->data)return false;
    if(rt->bpp!=32||src->bpp!=32||!rt->pitch||!src->pitch)return false;
    sw=src->width?src->width:(src->pitch/4u);
    sh=src->height?src->height:1u;
    dw=rt->width?rt->width:(rt->pitch/4u);
    dh=rt->height?rt->height:1u;
    if((uint64_t)sw*4ULL>(uint64_t)src->pitch)sw=src->pitch/4u;
    if((uint64_t)dw*4ULL>(uint64_t)rt->pitch)dw=rt->pitch/4u;
    if((uint64_t)src->pitch*(uint64_t)sh>src->size)sh=(uint32_t)(src->size/(uint64_t)src->pitch);
    if((uint64_t)rt->pitch*(uint64_t)dh>rt->size)dh=(uint32_t)(rt->size/(uint64_t)rt->pitch);
    if(!sw||!sh||!dw||!dh)return false;
    for(y=0;y<sh;++y){
        uint32_t x;
        int32_t ty=dy+(int32_t)y;
        const uint32_t *srow;
        uint32_t *drow;
        if(ty<0||ty>=(int32_t)dh)continue;
        srow=(const uint32_t*)(const void*)(src->data+(uint64_t)y*src->pitch);
        drow=(uint32_t*)(void*)(rt->data+(uint64_t)(uint32_t)ty*rt->pitch);
        for(x=0;x<sw;++x){
            int32_t tx=dx+(int32_t)x;
            uint32_t sp;
            if(tx<0||tx>=(int32_t)dw)continue;
            sp=srow[x];
            if(!sp)continue;
            drow[(uint32_t)tx]=c4_vmw_alpha_over(drow[(uint32_t)tx],sp);
            changed=true;
        }
    }
    return changed;
}

static void c4_vmw_put_cursor_px(c4_drm_bo_t *rt,int32_t x,int32_t y,uint32_t px){
    uint32_t *drow;
    uint32_t dw,dh;
    if(!rt||!rt->data||rt->bpp!=32||!rt->pitch)return;
    dw=rt->width?rt->width:(rt->pitch/4u);
    dh=rt->height?rt->height:1u;
    if(x<0||y<0||x>=(int32_t)dw||y>=(int32_t)dh)return;
    if((uint64_t)(uint32_t)y*(uint64_t)rt->pitch+(uint64_t)(uint32_t)x*4ULL+4ULL>rt->size)return;
    drow=(uint32_t*)(void*)(rt->data+(uint64_t)(uint32_t)y*rt->pitch);
    drow[(uint32_t)x]=c4_vmw_alpha_over(drow[(uint32_t)x],px);
}

static bool c4_vmw_draw_fallback_cursor(c4_drm_bo_t *rt,int32_t x,int32_t y){
    int row,col;
    static const char *shape[]={
        "BSS........................",
        "BWSS.......................",
        "BWWSS......................",
        "BWWWSS.....................",
        "BWWWWSS....................",
        "BWWWWWSS...................",
        "BWWWWWWSS..................",
        "BWWWWWWWSS.................",
        "BWWWWWWWWSS................",
        "BWWWWWWWWWSS...............",
        "BWWWWWWWWWWSS..............",
        "BWWWWWWWWWWWSS.............",
        "BWWWWWWWWWWWWSS............",
        "BWWWWWWWWWWWWWSS...........",
        "BWWWWWWWWWWWWWWSS..........",
        "BWWWWWWBBBBBBBBSS..........",
        "BWWWWBWSS..................",
        "BWWB.BWSS..................",
        "BWB..BWWSS.................",
        "BB...BWWSS.................",
        ".....BWWSS.................",
        ".....BWWSS.................",
        "......BBSS.................",
        ".......SS.................."
    };
    for(row=0;row<(int)(sizeof(shape)/sizeof(shape[0]));++row){
        const char *s=shape[row];
        for(col=0;s[col];++col){
            if(s[col]=='B'){
                c4_vmw_put_cursor_px(rt,x+col,y+row,0xff111318u);
            }else if(s[col]=='W'){
                c4_vmw_put_cursor_px(rt,x+col,y+row,0xfffbfbfbu);
            }else if(s[col]=='S'){
                c4_vmw_put_cursor_px(rt,x+col,y+row,0x66000000u);
            }
        }
    }
    return true;
}

static bool c4_vmw_draw_cursor_overlay(c4_drm_bo_t *rt){
    int32_t cx,cy,dx,dy;
    c4_drm_bo_t *cur;
    if(!rt||!rt->data||rt->bpp!=32||!rt->pitch)return false;
    /* When wlroots renders the cursor in software the cursor pixels are
       already inside the framebuffer Wayfire just scanned out, so do not
       draw any kernel-side cursor on top of it. */
    if(g_c4_drm_cursor_swcomp)return false;
    if(!g_c4_drm_cursor_visible)return false;
    cx=(g_c4_drm_cursor_x>=0)?g_c4_drm_cursor_x:(int32_t)((rt->width?rt->width:1024u)/2u);
    cy=(g_c4_drm_cursor_y>=0)?g_c4_drm_cursor_y:(int32_t)((rt->height?rt->height:768u)/2u);
    cur=g_c4_drm_cursor_handle?c4_drm_find_bo_by_handle(g_c4_drm_cursor_handle):0;
    if(cur&&cur!=rt&&cur->data&&cur->bpp==32&&cur->pitch){
        uint32_t nz=0;
        c4_drm_sample_bo(cur,&nz,0,0,0);
        if(nz){
            dx=cx-g_c4_drm_cursor_hot_x;
            dy=cy-g_c4_drm_cursor_hot_y;
            return c4_vmw_blit_bo(rt,cur,dx,dy);
        }
    }
    return c4_vmw_draw_fallback_cursor(rt,cx,cy);
}

/* The 2D rescue compositor paints a synthetic background plus surface
   thumbnails when the real GLES2 path produced no pixels. It used to be on
   by default, which made the desktop look "weird" because users saw the
   synthetic widgets layered over (or instead of) the real Wayfire dock.
   Now it is opt-in: only enabled when the operator drops the explicit
   /etc/ridux-wayfire-present2d.enable marker (the same one that already
   gated the front-buffer scanout fallback). */
static bool c4_vmw_rescue_compositor_enabled(void){
    static int cached=-1;
    if(cached<0)
        cached=kvfs_exists("/etc/ridux-wayfire-present2d.enable")?1:0;
    return cached!=0;
}

static bool c4_vmw_layer_overlay_draw_to_rt(c4_drm_bo_t *rt){
    uint32_t dw,dh,i,drew=0;
    bool changed=false;
    if(!c4_drm_vbox_mode()||c4_vmw_rescue_compositor_enabled())return false;
    if(!rt||!rt->data||rt->bpp!=32||!rt->pitch)return false;
    dw=rt->width?rt->width:(rt->pitch/4u);
    dh=rt->height?rt->height:1u;
    if((uint64_t)dw*4ULL>(uint64_t)rt->pitch)dw=rt->pitch/4u;
    if((uint64_t)rt->pitch*(uint64_t)dh>rt->size)dh=(uint32_t)(rt->size/(uint64_t)rt->pitch);
    if(!dw||!dh)return false;
    for(i=0;i<C4_VMW_MAX_SURFACES&&drew<12u;++i){
        c4_vmw_surface_record_t *rec=&g_c4_vmw_surfaces[i];
        c4_drm_bo_t *src;
        uint32_t sw,sh,nz=0;
        int32_t dx=0,dy=0;
        bool draw=false;
        if(!rec->used||!rec->bo_handle)continue;
        src=c4_drm_find_bo_by_handle(rec->bo_handle);
        if(!src||src==rt||!src->data||src->bpp!=32||!src->pitch)continue;
        sw=src->width?src->width:(src->pitch/4u);
        sh=src->height?src->height:1u;
        if(!sw||!sh||sw>4096u||sh>4096u)continue;
        if((uint64_t)sw*4ULL>(uint64_t)src->pitch)sw=src->pitch/4u;
        if((uint64_t)src->pitch*(uint64_t)sh>src->size)sh=(uint32_t)(src->size/(uint64_t)src->pitch);
        if(!sw||!sh)continue;
        if(sw>=dw&&sh>=dh)continue;
        if(sw>=(dw*3u)/4u&&sh>=(dh*3u)/4u)continue;
        if(sw<=80u&&sh<=80u)continue;
        c4_drm_sample_bo(src,&nz,0,0,0);
        if(!nz)continue;
        if(sh<=96u&&sw>=dw/2u){
            dx=(int32_t)((dw>sw)?((dw-sw)/2u):0u);
            dy=0;
            draw=true;
        }else if(sh<=180u&&sw>=96u){
            dx=(int32_t)((dw>sw)?((dw-sw)/2u):0u);
            dy=(int32_t)((dh>sh+24u)?(dh-sh-24u):0u);
            draw=true;
        }else if(sw>=180u&&sh>=120u&&sw<dw&&sh<dh){
            dx=(int32_t)((dw-sw)/2u);
            dy=(int32_t)((dh-sh)/2u);
            draw=true;
        }
        if(draw&&c4_vmw_blit_bo(rt,src,dx,dy)){
            ++drew;
            changed=true;
        }
    }
    /* This overlay is written back into the VMSVGA surface. Drawing the
       fallback cursor here burns old cursor positions into later readbacks, so
       leave cursor rendering to the normal cursor path and only merge real
       layer-shell/window buffers in this pass. */
    if(changed&&drew){
        static uint32_t overlay_trace;
        g_c4_vmw_rescue_rt_handle=rt->handle;
        if(c4_vmwgfx_trace_enabled()&&overlay_trace<64u){
            ++overlay_trace;
            __boot_serial_force_puts("[vmw-layer-overlay!] #");
            __boot_serial_force_putu32(overlay_trace);
            __boot_serial_force_puts(" rt=");
            __boot_serial_force_putu32(rt->handle);
            __boot_serial_force_puts(" ");
            __boot_serial_force_putu32(dw);
            __boot_serial_force_puts("x");
            __boot_serial_force_putu32(dh);
            __boot_serial_force_puts(" drew=");
            __boot_serial_force_putu32(drew);
            __boot_serial_force_puts("\n");
        }
    }
    return changed;
}

static bool c4_vmw_rescue_draw_to_rt(c4_drm_bo_t *rt){
    uint32_t dw,dh,i;
    bool changed=false;
    if(!c4_vmw_rescue_compositor_enabled())return false;
    if(!rt||!rt->data||rt->bpp!=32||!rt->pitch)return false;
    dw=rt->width?rt->width:(rt->pitch/4u);
    dh=rt->height?rt->height:1u;
    if((uint64_t)dw*4ULL>(uint64_t)rt->pitch)dw=rt->pitch/4u;
    if((uint64_t)rt->pitch*(uint64_t)dh>rt->size)dh=(uint32_t)(rt->size/(uint64_t)rt->pitch);
    if(!dw||!dh)return false;
    c4_vmw_fill_rt(rt,0xff171a20u);
    changed=true;

    for(i=0;i<C4_VMW_MAX_SURFACES;++i){
        c4_vmw_surface_record_t *rec=&g_c4_vmw_surfaces[i];
        c4_drm_bo_t *src;
        uint32_t sw,sh,nz=0;
        int32_t dx,dy;
        if(!rec->used||!rec->bo_handle)continue;
        src=c4_drm_find_bo_by_handle(rec->bo_handle);
        if(!src||src==rt||!src->data||src->bpp!=32||!src->pitch)continue;
        sw=src->width?src->width:(src->pitch/4u);
        sh=src->height?src->height:1u;
        if(!sw||!sh||sw>4096u||sh>4096u)continue;
        if((uint64_t)sw*4ULL>(uint64_t)src->pitch)sw=src->pitch/4u;
        if((uint64_t)src->pitch*(uint64_t)sh>src->size)sh=(uint32_t)(src->size/(uint64_t)src->pitch);
        c4_drm_sample_bo(src,&nz,0,0,0);
        if(!nz)continue;

        if(sw<=72u&&sh<=72u){
            continue;
        }
        if(sw>=dw/2u&&sh>=dh/2u){
            if(c4_vmw_blit_bo(rt,src,0,0))changed=true;
            continue;
        }
        if(sh<=120u&&sw>=400u){
            dx=(int32_t)((dw>sw)?((dw-sw)/2u):0u);
            dy=(sw>=(dw*4u)/5u)?18:(int32_t)((dh>sh+28u)?(dh-sh-28u):0u);
            if(c4_vmw_blit_bo(rt,src,dx,dy))changed=true;
            continue;
        }
        if(sw>=180u&&sh>=120u&&sw<dw&&sh<dh){
            dx=(int32_t)((dw-sw)/2u);
            dy=(int32_t)((dh-sh)/2u);
            if(c4_vmw_blit_bo(rt,src,dx,dy))changed=true;
        }
    }

    if(g_c4_drm_cursor_visible){
        int32_t cx=(g_c4_drm_cursor_visible&&g_c4_drm_cursor_x>=0)?
                   g_c4_drm_cursor_x:(int32_t)(dw/2u);
        int32_t cy=(g_c4_drm_cursor_visible&&g_c4_drm_cursor_y>=0)?
                   g_c4_drm_cursor_y:(int32_t)(dh/2u);
        if(c4_vmw_draw_fallback_cursor(rt,cx,cy))changed=true;
    }
    if(changed){
        g_c4_vmw_rescue_rt_handle=rt->handle;
        c4_drm_trace_bo("vmw-draw-rescue-src",c4_vmw_current_shader_bo(),
                        g_c4_vmw_last_nonzero_sid,g_c4_vmw_last_nonzero_bo);
        c4_drm_trace_bo("vmw-draw-rescue-rt",rt,rt->width,rt->height);
    }
    return changed;
}

static bool c4_vmw_execbuf_process(const c4_vmw_execbuf_t *e){
    const uint8_t *base;
    uint32_t off=0;
    bool changed=false;
    if(!e||!e->commands||!e->command_size)return false;
    base=(const uint8_t*)(uintptr_t)e->commands;
    while(off+sizeof(c4_svga_cmd_header_t)<=e->command_size){
        const c4_svga_cmd_header_t *h=(const c4_svga_cmd_header_t*)(const void*)(base+off);
        const uint32_t *p=(const uint32_t*)(const void*)(base+off+sizeof(*h));
        uint32_t next=off+sizeof(*h)+h->size;
        uint32_t words=h->size/4u;
        if(h->size>(e->command_size-off-sizeof(*h)))break;
        c4_vmw_trace_packet_words(h->id,h->size,p,words);
        switch(h->id){
            case C4_SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW:
                if(words>=2u){
                    c4_drm_bo_t *bo=c4_vmw_bo_from_surface_handle(p[1]);
                    c4_vmw_srv_set(p[0],p[1]);
                    c4_vmw_note_nonzero_bo(p[1],bo);
                    c4_vmw_trace("srv",p[0],p[1],bo?bo->handle:0u);
                }
                break;
            case C4_SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW:
                if(words>=2u){
                    c4_vmw_view_set(p[0],p[1],words>2u?p[2]:0u,words>3u?p[3]:0u);
                    c4_vmw_trace_packet(h->id,h->size,p[0],p[1]);
                }
                break;
            case C4_SVGA_3D_CMD_DX_SET_SHADER_RESOURCES:
                if(words>=2u){
                    uint32_t i,type=p[1];
                    if(type<6u){
                        uint32_t n=words-2u;
                        if(n>(uint32_t)(sizeof(g_c4_vmw_bound_srvs[0])/sizeof(g_c4_vmw_bound_srvs[0][0])))
                            n=(uint32_t)(sizeof(g_c4_vmw_bound_srvs[0])/sizeof(g_c4_vmw_bound_srvs[0][0]));
                        for(i=0;i<n;++i){
                            uint32_t view=p[2u+i];
                            uint32_t sid=c4_vmw_srv_sid(view);
                            c4_drm_bo_t *bo=c4_vmw_bo_from_surface_handle(sid);
                            if(p[0]+i<(uint32_t)(sizeof(g_c4_vmw_bound_srvs[0])/sizeof(g_c4_vmw_bound_srvs[0][0])))
                                g_c4_vmw_bound_srvs[type][p[0]+i]=view;
                            c4_vmw_note_nonzero_bo(sid,bo);
                        }
                    }
                    c4_vmw_trace("set-srv",p[0],p[1],words>2u?p[2]:0u);
                }
                break;
            case C4_SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW:
                if(words>=5u){
                    uint32_t sid=c4_vmw_view_sid(p[0]);
                    c4_drm_bo_t *bo=c4_vmw_bo_from_surface_handle(sid);
                    if(bo){
                        c4_vmw_clear_bo(bo,c4_vmw_color_from_rgba_words(&p[1]));
                        c4_drm_trace_bo("vmw-clear-rtv",bo,p[0],sid);
                        changed=true;
                    }
                }
                break;
            case C4_SVGA_3D_CMD_DX_PRESENTBLT:
                if(words>=17u){
                    const c4_svga_box_t *src_box=(const c4_svga_box_t*)(const void*)&p[4];
                    const c4_svga_box_t *dst_box=(const c4_svga_box_t*)(const void*)&p[10];
                    if(c4_vmw_present_blt(p[2],p[0],src_box,dst_box)){
                        c4_drm_trace_bo("vmw-presentblt",c4_vmw_bo_from_surface_handle(p[2]),p[0],p[2]);
                        changed=true;
                    }
                }else if(words>=4u&&c4_vmw_copy_surface_all(p[2],p[0])){
                    c4_drm_trace_bo("vmw-presentblt",c4_vmw_bo_from_surface_handle(p[2]),p[0],p[2]);
                    changed=true;
                }
                break;
            case C4_SVGA_3D_CMD_DX_PRED_COPY_REGION:
                if(words>=13u){
                    const c4_svga_copy_box_t *box=(const c4_svga_copy_box_t*)(const void*)&p[4];
                    if(c4_vmw_copy_surface_box(p[0],p[2],box)){
                        c4_drm_trace_bo("vmw-copy-region",c4_vmw_bo_from_surface_handle(p[0]),p[2],p[0]);
                        changed=true;
                    }
                }
                break;
            case C4_SVGA_3D_CMD_DX_PRED_COPY:
                if(words>=2u&&c4_vmw_copy_surface_all(p[0],p[1])){
                    c4_drm_trace_bo("vmw-copy",c4_vmw_bo_from_surface_handle(p[0]),p[1],p[0]);
                    changed=true;
                }
                break;
            case C4_SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE:
                if(words>=8u){
                    c4_drm_bo_t *src=c4_vmw_bo_from_surface_handle(p[0]);
                    c4_drm_trace_bo("vmw-update-subres",src,p[6],p[7]);
                    c4_vmw_note_nonzero_bo(p[0],src);
                    if(src&&src->width>=512u&&src->height>=256u){
                        c4_drm_bo_t *dst=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
                        uint32_t nz=0,w,h;
                        c4_drm_sample_bo(src,&nz,0,0,0);
                        if(dst&&dst!=src&&nz){
                            w=dst->width<src->width?dst->width:src->width;
                            h=dst->height<src->height?dst->height:src->height;
                            if(c4_vmw_blend_bo_rect(dst,0,0,src,0,0,w,h)){
                                c4_drm_trace_bo("vmw-update-to-fb",dst,p[0],dst->handle);
                                changed=true;
                            }
                        }
                    }
                }
                break;
            case C4_SVGA_3D_CMD_DX_DRAW:
            case C4_SVGA_3D_CMD_DX_DRAW_INDEXED:
                {
                    c4_drm_bo_t *rt=c4_vmw_current_render_bo();
                    if(rt){
                        c4_vmw_surface_record_t *rec=c4_vmw_surface_by_bo(rt->handle);
                        uint32_t nz=0;
                        c4_drm_trace_bo("vmw-draw-rt",rt,h->id,words?p[0]:0);
                        if(g_c4_vmw_real_clear_probe_count<2u){
                            uint32_t i,view=0;
                            for(i=0;i<(uint32_t)(sizeof(g_c4_vmw_bound_rtvs)/sizeof(g_c4_vmw_bound_rtvs[0]));++i){
                                if(g_c4_vmw_bound_rtvs[i]){view=g_c4_vmw_bound_rtvs[i];break;}
                            }
                            if(view&&rec&&rt->real_created&&
                               c4_svga_real_clear_readback_rtv((uint32_t)e->context_handle,view,rec->sid,
                                                               c4_vmw_view_format(view),
                                                               c4_vmw_view_dimension(view),
                                                               g_c4_vmw_real_clear_probe_count))
                                ++g_c4_vmw_real_clear_probe_count;
                            else if(view&&c4_svga_real_clear_rtv((uint32_t)e->context_handle,view,
                                                                 g_c4_vmw_real_clear_probe_count))
                                ++g_c4_vmw_real_clear_probe_count;
                        }
                        if(rec&&rt->real_created)
                            (void)c4_svga_real_dx_readback_surface((uint32_t)e->context_handle,rec->sid);
                        if(rec&&rt->real_created&&c4_svga_real_readback_surface(rec->sid)){
                            c4_drm_sample_bo(rt,&nz,0,0,0);
                            c4_drm_trace_bo("vmw-draw-readback",rt,rec->sid,nz);
                            if(nz&&rt->width>=512u&&rt->height>=256u){
                                bool rescue_backing=c4_vmw_rescue_compositor_enabled()&&
                                                    (g_c4_vmw_rescue_rt_handle==rt->handle);
                                if((rescue_backing&&c4_vmw_rescue_draw_to_rt(rt))||
                                   (!rescue_backing&&c4_vmw_layer_overlay_draw_to_rt(rt)))
                                    (void)c4_svga_real_update_surface(rec->sid);
                                if(c4_drm_scanout_probe_enabled()){
                                    (void)c4_svga_real_present_bo(rt);
                                    c4_drm_present_bo(rt);
                                }
                                changed=true;
                            }else if(rt->width>=512u&&rt->height>=256u){
                                if(c4_vmw_rescue_draw_to_rt(rt)){
                                    if(rt->real_created)(void)c4_svga_real_update_surface(rec->sid);
                                    if(c4_drm_scanout_probe_enabled()){
                                        (void)c4_svga_real_present_bo(rt);
                                        c4_drm_present_bo(rt);
                                    }
                                    changed=true;
                                }else{
                                    if(c4_drm_scanout_probe_enabled())
                                        (void)c4_svga_real_present_surface(rec->sid,rt);
                                }
                            }
                        }
                    }
                }
                break;
            case C4_SVGA_3D_CMD_DX_SET_RENDERTARGETS:
                if(words>=1u){
                    uint32_t i,n=words-1u;
                    g_c4_vmw_bound_dsv=p[0];
                    ulibc_memset(g_c4_vmw_bound_rtvs,0,sizeof(g_c4_vmw_bound_rtvs));
                    if(n>(uint32_t)(sizeof(g_c4_vmw_bound_rtvs)/sizeof(g_c4_vmw_bound_rtvs[0])))
                        n=(uint32_t)(sizeof(g_c4_vmw_bound_rtvs)/sizeof(g_c4_vmw_bound_rtvs[0]));
                    for(i=0;i<n;++i)g_c4_vmw_bound_rtvs[i]=p[1u+i];
                    c4_vmw_trace("set-rt",g_c4_vmw_bound_dsv,
                                 n?g_c4_vmw_bound_rtvs[0]:0u,
                                 n?c4_vmw_view_sid(g_c4_vmw_bound_rtvs[0]):0u);
                }
                break;
            case C4_SVGA_3D_CMD_DX_BUFFER_COPY:
                break;
            default:
                break;
        }
        if(next<=off)break;
        off=next;
        if(off&3u)off=(off+3u)&~3u;
    }
    return changed;
}

static void c4_vmw_fill_fence(uint64_t ptr){
    c4_vmw_fence_rep_t *r;
    uint32_t id;
    if(!ptr)return;
    r=(c4_vmw_fence_rep_t*)(uintptr_t)ptr;
    id=g_c4_vmw_next_fence++;
    if(!id)id=g_c4_vmw_next_fence++;
    r->handle=id;
    r->mask=1;
    r->seqno=id;
    r->passed_seqno=id;
    r->fd=-1;
    r->error=0;
}

static int c4_vmwgfx_ioctl(uint64_t request,void *arg){
    uint32_t nr=(uint32_t)(request&0xFFu);
    switch(nr){
        case C4_DRM_VMW_GET_PARAM:
            if(arg){
                c4_vmw_getparam_t *p=(c4_vmw_getparam_t*)arg;
                p->value=c4_vmw_param_value(p->param);
                c4_vmw_trace("get-param",p->param,p->value,request);
            }
            return 0;
        case C4_DRM_VMW_ALLOC_BO:
            if(arg){
                c4_vmw_alloc_bo_t *a=(c4_vmw_alloc_bo_t*)arg;
                uint32_t size=a->req.size?a->req.size:PAGE_SIZE;
                c4_drm_bo_t *bo=c4_drm_alloc_bo_bytes(size,0,0,32u,0);
                if(!bo)return -ENOMEM;
                (void)c4_svga_real_define_bo_mob(bo);
                a->rep.map_handle=bo->map_offset;
                a->rep.handle=bo->handle;
                a->rep.cur_gmr_id=bo->handle;
                a->rep.cur_gmr_offset=0;
                a->rep.pad64=0;
                c4_drm_trace_bo("vmw-alloc-bo",bo,size,0);
            }
            return 0;
        case C4_DRM_VMW_CREATE_CONTEXT:
        case C4_DRM_VMW_CREATE_EXTENDED_CONTEXT:
            if(arg){
                c4_vmw_context_t *c=(c4_vmw_context_t*)arg;
                c->cid=(int32_t)g_c4_vmw_next_cid++;
                if(!c->cid)c->cid=(int32_t)g_c4_vmw_next_cid++;
                c4_svga_real_define_context((uint32_t)c->cid);
                c4_vmw_trace("ctx-create",(uint32_t)c->cid,nr,request);
            }
            return 0;
        case C4_DRM_VMW_CREATE_SURFACE:
            if(arg){
                c4_vmw_legacy_surface_t *s=(c4_vmw_legacy_surface_t*)arg;
                int32_t sid=(int32_t)g_c4_vmw_next_surface++;
                if(!sid)sid=(int32_t)g_c4_vmw_next_surface++;
                s->rep.sid=sid;
                s->rep.handle_type=0;
                c4_vmw_trace("legacy-surface",(uint32_t)sid,nr,request);
            }
            return 0;
        case C4_DRM_VMW_GB_SURFACE_CREATE:
        case C4_DRM_VMW_GB_SURFACE_CREATE_EXT:
            if(arg){
                c4_vmw_gb_surface_req_t req;
                c4_vmw_gb_surface_ext_req_t ext_req;
                c4_vmw_gb_surface_rep_t rep;
                uint32_t w,h,sid;
                uint64_t bytes;
                c4_drm_bo_t *bo;
                if(nr==C4_DRM_VMW_GB_SURFACE_CREATE_EXT){
                    c4_vmw_surface_ext_t *se=(c4_vmw_surface_ext_t*)arg;
                    ext_req=se->req;
                    req=ext_req.base;
                }else{
                    c4_vmw_surface_t *s=(c4_vmw_surface_t*)arg;
                    req=s->req;
                    ulibc_memset(&ext_req,0,sizeof(ext_req));
                    ext_req.base=req;
                }
                if(!req.mip_levels)req.mip_levels=1;
                w=req.base_size.width?req.base_size.width:(g_fb.width?g_fb.width:1024u);
                h=req.base_size.height?req.base_size.height:(g_fb.height?g_fb.height:768u);
                bytes=(uint64_t)w*(uint64_t)h*4ULL;
                bo=req.buffer_handle?c4_drm_find_bo_by_handle(req.buffer_handle):0;
                if(!bo)bo=c4_drm_alloc_bo_bytes(bytes,w,h,32u,0);
                if(!bo)return -ENOMEM;
                if(!bo->width)bo->width=w;
                if(!bo->height)bo->height=h;
                if(!bo->bpp)bo->bpp=32u;
                if(!bo->pitch)bo->pitch=(uint32_t)((((uint64_t)w*32ULL)+7ULL)/8ULL);
                sid=g_c4_vmw_next_surface++;
                if(!sid)sid=g_c4_vmw_next_surface++;
                ulibc_memset(&rep,0,sizeof(rep));
                rep.handle=sid;
                rep.backup_size=(uint32_t)bo->size;
                rep.buffer_handle=bo->handle;
                rep.buffer_size=(uint32_t)bo->size;
                rep.buffer_map_handle=bo->map_offset;
                if(nr==C4_DRM_VMW_GB_SURFACE_CREATE_EXT)
                    ((c4_vmw_surface_ext_t*)arg)->rep=rep;
                else
                    ((c4_vmw_surface_t*)arg)->rep=rep;
                (void)c4_vmw_remember_surface(sid,bo,&req,
                    (nr==C4_DRM_VMW_GB_SURFACE_CREATE_EXT)?&ext_req:0,&rep);
                c4_vmw_trace("gb-surface",sid,bo->handle,((uint64_t)w<<32)|h);
                c4_drm_trace_bo("vmw-surface",bo,w,h);
            }
            return 0;
        case C4_DRM_VMW_GB_SURFACE_REF:
            if(arg){
                c4_vmw_surface_ref_t *r=(c4_vmw_surface_ref_t*)arg;
                c4_vmw_surface_arg_t req=r->req;
                c4_vmw_surface_record_t *rec=c4_vmw_surface_from_arg(&req);
                if(!rec)return -EINVAL;
                ulibc_memset(&r->rep,0,sizeof(r->rep));
                r->rep.creq=rec->req.base;
                r->rep.crep=rec->rep;
                c4_vmw_trace("gb-ref",req.handle_type,(uint32_t)req.sid,rec->bo_handle);
            }
            return 0;
        case C4_DRM_VMW_GB_SURFACE_REF_EXT:
            if(arg){
                c4_vmw_surface_ref_ext_t *r=(c4_vmw_surface_ref_ext_t*)arg;
                c4_vmw_surface_arg_t req=r->req;
                c4_vmw_surface_record_t *rec=c4_vmw_surface_from_arg(&req);
                if(!rec)return -EINVAL;
                ulibc_memset(&r->rep,0,sizeof(r->rep));
                r->rep.creq=rec->req;
                r->rep.crep=rec->rep;
                c4_vmw_trace("gb-ref-ext",req.handle_type,(uint32_t)req.sid,rec->bo_handle);
            }
            return 0;
        case C4_DRM_VMW_EXECBUF:
            if(arg){
                c4_vmw_execbuf_t *e=(c4_vmw_execbuf_t*)arg;
                bool real_sent=false;
                bool sw_changed=false;
                c4_drm_bo_t *cur_bo;
                if(e->commands&&e->command_size)
                    real_sent=c4_svga_real_submit_dx_commands((const void*)(uintptr_t)e->commands,
                                                              e->command_size,
                                                              (uint32_t)e->context_handle);
                if(real_sent)c4_svga_real_fence();
                sw_changed=c4_vmw_execbuf_process(e);
                cur_bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
                if((sw_changed||real_sent)&&cur_bo)
                    (void)c4_drm_present_fb_vbox_or_cpu(g_c4_drm_current_fb_id);
                c4_vmw_dump_execbuf(e);
                c4_vmw_fill_fence(e->fence_rep);
                c4_vmw_trace("execbuf",e->command_size,e->context_handle,
                              (real_sent?1u:0u)|(sw_changed?2u:0u));
            }
            return 0;
        case C4_DRM_VMW_GET_3D_CAP:
            if(arg){
                c4_vmw_get_3d_cap_t *c=(c4_vmw_get_3d_cap_t*)arg;
                if(c->buffer&&c->max_size)c4_vmw_fill_3d_caps((uint32_t*)(uintptr_t)c->buffer,c->max_size);
                c4_vmw_trace("3d-cap",c->max_size,c->buffer,request);
            }
            return 0;
        case C4_DRM_VMW_CREATE_SHADER:
            if(arg){
                c4_vmw_shader_create_t *s=(c4_vmw_shader_create_t*)arg;
                c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(s->buffer_handle);
                if(bo)(void)c4_svga_real_define_bo_mob(bo);
                s->shader_handle=g_c4_vmw_next_shader++;
                if(!s->shader_handle)s->shader_handle=g_c4_vmw_next_shader++;
                if(bo)c4_drm_trace_bo("vmw-shader-bo",bo,s->shader_type,s->size);
                c4_vmw_trace("shader-create",s->shader_type,s->size,
                              ((uint64_t)s->buffer_handle<<32)|(uint64_t)s->shader_handle);
            }
            return 0;
        case C4_DRM_VMW_FENCE_SIGNALED:
            if(arg){
                c4_vmw_fence_signaled_t *f=(c4_vmw_fence_signaled_t*)arg;
                f->signaled=1;
                f->passed_seqno=f->handle;
                f->signaled_flags=f->flags?f->flags:1u;
            }
            return 0;
        case C4_DRM_VMW_FENCE_EVENT:
            if(arg)c4_vmw_fill_fence(((uint64_t*)arg)[0]);
            return 0;
        case C4_DRM_VMW_FENCE_WAIT:
            if(arg){
                c4_vmw_fence_wait_t *w=(c4_vmw_fence_wait_t*)arg;
                w->cookie_valid=0;
                w->kernel_cookie=0;
            }
            return 0;
        case C4_DRM_VMW_HANDLE_CLOSE:
        case C4_DRM_VMW_UNREF_CONTEXT:
        case C4_DRM_VMW_UNREF_SURFACE:
        case C4_DRM_VMW_REF_SURFACE:
        case C4_DRM_VMW_FENCE_UNREF:
        case C4_DRM_VMW_PRESENT:
        case C4_DRM_VMW_PRESENT_READBACK:
        case C4_DRM_VMW_UPDATE_LAYOUT:
        case C4_DRM_VMW_UNREF_SHADER:
        case C4_DRM_VMW_CURSOR_BYPASS:
        case C4_DRM_VMW_CONTROL_STREAM:
        case C4_DRM_VMW_CLAIM_STREAM:
        case C4_DRM_VMW_UNREF_STREAM:
        case C4_DRM_VMW_SYNCCPU:
        case C4_DRM_VMW_MKSSTAT_RESET:
        case C4_DRM_VMW_MKSSTAT_ADD:
        case C4_DRM_VMW_MKSSTAT_REMOVE:
            c4_vmw_trace("noop",nr,request,(uint64_t)(uintptr_t)arg);
            return 0;
        case C4_DRM_VMW_MSG:
            if(arg){
                c4_vmw_msg_t *m=(c4_vmw_msg_t*)arg;
                if(!m->send_only&&m->receive&&m->receive_len){
                    const char *ok="OK";
                    uint32_t n=2;
                    if(n>=m->receive_len)n=m->receive_len-1u;
                    ulibc_memcpy((void*)(uintptr_t)m->receive,ok,n);
                    ((char*)(uintptr_t)m->receive)[n]=0;
                }
                c4_vmw_trace("msg",m->send_only,m->receive_len,m->send);
            }
            return 0;
        default:
            return -ENOTTY;
    }
}

int drm_ioctl_handler(int fd, uint64_t request, void *arg) {
    c4_drm_file_t *drm_file=c4_drm_file_get(fd,true);
    if(request == DRM_IOCTL_VERSION) {
        bool vbox_mode = c4_drm_vbox_mode();
        if(!vbox_mode&&!g_c4_vgpu_real.queue_ready){
            bool real_init=c4_vgpu_real_init();
            c4_virtgpu_trace_u64("real-init-version",request,(uint64_t)real_init,g_c4_vgpu_real.queue_ready,(uint64_t)fd);
        }
        if(arg) {
            const char *name=vbox_mode?"vmwgfx":"virtio_gpu";
            const char *date="20260510";
            const char *desc="RiduxOS KMS/DRM compatible display";
            c4_drm_version_t *v=(c4_drm_version_t*)arg;
            uint64_t in_name_len=v->name_len;
            (void)fd;
            v->version_major=vbox_mode?2:0;
            v->version_minor=vbox_mode?20:1;
            v->version_patchlevel=0;
            c4_drm_version_copy_string(v->name,v->name_len,name);
            c4_drm_version_copy_string(v->date,v->date_len,date);
            c4_drm_version_copy_string(v->desc,v->desc_len,desc);
            v->name_len=ulibc_strlen(name);
            v->date_len=ulibc_strlen(date);
            v->desc_len=ulibc_strlen(desc);
            c4_virtgpu_trace_u64("version-ret",
                                  ((uint64_t)v->version_major<<32)|
                                  (uint64_t)v->version_minor,
                                  ((in_name_len&0xFFFFFFFFULL)<<32)|
                                  (uint64_t)(v->name_len&0xFFFFFFFFu),
                                  drm_file?(uint64_t)drm_file->render_node:0ULL,
                                  (uint64_t)fd);
        }
        return 0;
    }
    if(request == DRM_IOCTL_GET_MAGIC) {
        if(!drm_file||drm_file->render_node)return -EACCES;
        if(!drm_file->magic){
            drm_file->magic=g_c4_drm_next_magic++;
            if(g_c4_drm_next_magic<0x52445840u)g_c4_drm_next_magic=0x52445840u;
        }
        if(arg)*(uint32_t*)arg=drm_file->magic;
        return 0;
    }
    if(request == DRM_IOCTL_AUTH_MAGIC) {
        uint32_t magic=arg?*(uint32_t*)arg:0;
        int i;
        if(!drm_file||drm_file->render_node)return -EACCES;
        if(g_c4_drm_master_file&&g_c4_drm_master_file!=drm_file->id)
            return -EACCES;
        for(i=0;i<C4_DRM_MAX_FILES;++i){
            if(!g_c4_drm_files[i].used||g_c4_drm_files[i].magic!=magic)continue;
            g_c4_drm_files[i].authenticated=true;
            return 0;
        }
        if(!magic)return -EINVAL;
        return 0;
    }
    if(request == DRM_IOCTL_GET_UNIQUE) {
        if(arg){
            uint64_t *u=(uint64_t*)arg;
            const char *name="ridux-drm-card0";
            if(u[1]&&u[0]>0){
                char *dst=(char*)(uintptr_t)u[1];
                size_t n=ulibc_strlen(name);
                if(n>u[0])n=(size_t)u[0];
                ulibc_memcpy(dst,name,n);
            }
            u[0]=ulibc_strlen(name);
        }
        return 0;
    }
    if(request == DRM_IOCTL_GET_CAP) {
        if(arg) {
            c4_drm_get_cap_t *cap=(c4_drm_get_cap_t*)arg;
            switch(cap->capability){
                case C4_DRM_CAP_DUMB_BUFFER:
                case C4_DRM_CAP_TIMESTAMP_MONOTONIC:
                case C4_DRM_CAP_CRTC_IN_VBLANK_EVENT:
                case C4_DRM_CAP_SYNCOBJ:
                    cap->value=1;
                    break;
                case C4_DRM_CAP_SYNCOBJ_TIMELINE:
                    cap->value=0;
                    break;
                case C4_DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP:
                    cap->value=1;
                    break;
                case C4_DRM_CAP_PRIME:
                    cap->value=C4_DRM_PRIME_CAP_IMPORT|C4_DRM_PRIME_CAP_EXPORT;
                    break;
                case C4_DRM_CAP_CURSOR_WIDTH:
                case C4_DRM_CAP_CURSOR_HEIGHT:
                    /* Advertise the largest cursor we can composite. wlroots
                     * uses this to clamp cursor allocation. 256x256 covers
                     * GNOME/Plasma HiDPI cursor themes. */
                    cap->value=256;
                    break;
                case C4_DRM_CAP_ADDFB2_MODIFIERS:
                    /* We expose a real IN_FORMATS blob now, so set this. */
                    cap->value=1;
                    break;
                /* Newer libdrm caps that downstream Mesa/wlroots probe.
                 * 0x0F=DRM_CAP_PAGE_FLIP_TARGET, 0x11=DRM_CAP_DUMB_PREFERRED_DEPTH,
                 * 0x07=DRM_CAP_ASYNC_PAGE_FLIP, 0x16=DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP. */
                case 0x0FULL: cap->value=0; break;          /* PAGE_FLIP_TARGET */
                case 0x07ULL: cap->value=1; break;          /* ASYNC_PAGE_FLIP */
                case 0x11ULL: cap->value=24; break;         /* PREFERRED_DEPTH */
                case 0xAULL:  cap->value=32; break;         /* DUMB_PREFER_SHADOW */
                default:
                    cap->value=0;
                    break;
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_SET_CLIENT_CAP) {
        if(arg) {
            c4_drm_set_client_cap_t *cap=(c4_drm_set_client_cap_t*)arg;
            switch(cap->capability){
                case C4_DRM_CLIENT_CAP_UNIVERSAL_PLANES:
                case C4_DRM_CLIENT_CAP_STEREO_3D:
                case C4_DRM_CLIENT_CAP_ASPECT_RATIO:
                case C4_DRM_CLIENT_CAP_ATOMIC:
                case C4_DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT:
                    /* CURSOR_PLANE_HOTSPOT is a libdrm 2.4.117+ cap that
                     * compositors enable to discover the HOTSPOT_X/HOTSPOT_Y
                     * plane properties. We expose those properties already. */
                    return 0;
                case C4_DRM_CLIENT_CAP_WRITEBACK_CONNECTORS:
                    /* No writeback connector implemented; reject enable. */
                    return cap->value?-EINVAL:0;
                default:
                    return cap->value?-EINVAL:0;
            }
        }
        return 0;
    }
    if(c4_drm_vbox_mode()&&
       (request&0xFFu)>=C4_DRM_COMMAND_BASE&&
       (request&0xFFu)<=C4_DRM_VMW_MKSSTAT_REMOVE) {
        return c4_vmwgfx_ioctl(request,arg);
    }
    if((request&0xFFu)>=C4_DRM_VIRTGPU_MAP&&
       (request&0xFFu)<=C4_DRM_VIRTGPU_CONTEXT_INIT) {
        return c4_virtgpu_ioctl(request,arg);
    }
    if(request == DRM_IOCTL_SET_MASTER) {
        if(!drm_file||drm_file->render_node)return -EACCES;
        if(g_c4_drm_master_file&&g_c4_drm_master_file!=drm_file->id)return -EBUSY;
        g_c4_drm_master_file=drm_file->id;
        drm_file->master=true;
        drm_file->authenticated=true;
        return 0;
    }
    if(request == DRM_IOCTL_DROP_MASTER) {
        if(!drm_file||drm_file->render_node)return -EACCES;
        if(g_c4_drm_master_file==drm_file->id)g_c4_drm_master_file=0;
        drm_file->master=false;
        return 0;
    }
    if(request == DRM_IOCTL_GEM_CLOSE || (request&0xFFu)==0x09u) {
        if(arg){
            c4_drm_gem_close_t *c=(c4_drm_gem_close_t*)arg;
            if(!c4_drm_file_handle_remove(drm_file,c->handle))return -EINVAL;
            c->handle=0;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_CREATE) {
        if(arg){
            c4_drm_syncobj_create_t *c=(c4_drm_syncobj_create_t*)arg;
            c4_drm_syncobj_t *s=c4_drm_syncobj_alloc((c->flags&1u)!=0);
            if(!s)return -ENOMEM;
            c->handle=s->handle;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_DESTROY) {
        if(arg){
            c4_drm_syncobj_destroy_t *d=(c4_drm_syncobj_destroy_t*)arg;
            c4_drm_syncobj_t *s=c4_drm_syncobj_find(d->handle);
            if(s){
                if(s->export_fd>=0)real_sys_close(s->export_fd);
                s->used=false;
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD) {
        if(arg){
            c4_drm_syncobj_handle_t *h=(c4_drm_syncobj_handle_t*)arg;
            c4_drm_syncobj_t *s=c4_drm_syncobj_find(h->handle);
            if(!s)return -EINVAL;
            h->fd=c4_drm_syncobj_export_fd(s);
            if(h->fd<0)return h->fd;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE) {
        if(arg){
            c4_drm_syncobj_handle_t *h=(c4_drm_syncobj_handle_t*)arg;
            c4_drm_syncobj_t *s=c4_drm_syncobj_find_fd(h->fd);
            if(!s)s=c4_drm_syncobj_alloc(true);
            if(!s)return -ENOMEM;
            if(s->export_fd<0)s->export_fd=h->fd;
            h->handle=s->handle;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_WAIT) {
        if(arg){
            c4_drm_syncobj_wait_t *w=(c4_drm_syncobj_wait_t*)arg;
            int rc=c4_drm_syncobj_wait_array(w->handles,w->count_handles,
                                             w->flags,w->timeout_nsec,
                                             &w->first_signaled);
            if(rc<0)return rc;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_RESET || request == DRM_IOCTL_SYNCOBJ_SIGNAL) {
        if(arg){
            c4_drm_syncobj_array_t *a=(c4_drm_syncobj_array_t*)arg;
            return c4_drm_syncobj_mark_array(a->handles,a->count_handles,
                                             request==DRM_IOCTL_SYNCOBJ_SIGNAL);
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT) {
        if(arg){
            c4_drm_syncobj_timeline_wait_t *w=(c4_drm_syncobj_timeline_wait_t*)arg;
            int rc=c4_drm_syncobj_wait_array(w->handles,w->count_handles,
                                             w->flags,w->timeout_nsec,
                                             &w->first_signaled);
            if(rc<0)return rc;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_QUERY) {
        if(arg){
            c4_drm_syncobj_timeline_array_t *q=(c4_drm_syncobj_timeline_array_t*)arg;
            return c4_drm_syncobj_query_array(q->handles,q->points,q->count_handles);
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_TRANSFER) {
        if(arg){
            c4_drm_syncobj_transfer_t *t=(c4_drm_syncobj_transfer_t*)arg;
            c4_drm_syncobj_t *src=c4_drm_syncobj_find(t->src_handle);
            c4_drm_syncobj_t *dst=c4_drm_syncobj_find(t->dst_handle);
            if(!src||!dst)return -EINVAL;
            dst->signaled=src->signaled;
            dst->point=t->dst_point?t->dst_point:src->point;
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL) {
        if(arg){
            c4_drm_syncobj_timeline_array_t *q=(c4_drm_syncobj_timeline_array_t*)arg;
            uint32_t i;
            if((!q->handles||!q->points)&&q->count_handles)return -EINVAL;
            for(i=0;i<q->count_handles;++i){
                uint32_t h=((uint32_t*)(uintptr_t)q->handles)[i];
                c4_drm_syncobj_t *s=c4_drm_syncobj_find(h);
                if(!s)return -EINVAL;
                s->signaled=true;
                s->point=((uint64_t*)(uintptr_t)q->points)[i];
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_SYNCOBJ_EVENTFD) {
        if(arg){
            c4_drm_syncobj_eventfd_t *e=(c4_drm_syncobj_eventfd_t*)arg;
            c4_drm_syncobj_t *s=c4_drm_syncobj_find(e->handle);
            if(!s)return -EINVAL;
            s->signaled=true;
            if(e->fd>=0){
                uint64_t one=1;
                (void)real_sys_write(e->fd,&one,sizeof(one));
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_PRIME_HANDLE_TO_FD || (request&0xFFu)==0x2Du) {
        if(arg){
            c4_drm_prime_handle_t *p=(c4_drm_prime_handle_t*)arg;
            c4_drm_bo_t *bo=0;
            if(c4_drm_vbox_mode())bo=c4_vmw_bo_from_surface_handle(p->handle);
            if(!bo)bo=c4_drm_bo_for_fd_handle(fd,p->handle);
            if(!bo&&!p->handle)bo=c4_drm_latest_bo();
            if(!bo)return -EINVAL;
            p->handle=bo->handle;
            p->fd=c4_drm_prime_fd_for_bo(bo);
            if(p->fd<0)return p->fd;
            c4_drm_remember_prime(p->fd,p->handle);
        }
        return 0;
    }
    if(request == DRM_IOCTL_PRIME_FD_TO_HANDLE || (request&0xFFu)==0x2Eu) {
        if(arg){
            c4_drm_prime_handle_t *p=(c4_drm_prime_handle_t*)arg;
            uint32_t h=c4_drm_handle_from_prime_fd(p->fd);
            if(!h)return -EINVAL;
            if(!c4_drm_file_handle_find(drm_file,h,true)){
                c4_drm_bo_t *bo=c4_drm_find_bo_by_handle(h);
                if(!bo||!c4_drm_file_handle_add(drm_file,bo))return -EINVAL;
            }
            p->handle=h?h:C4_DRM_ID_FB;
            (void)p->flags;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETRESOURCES) {
        c4_drm_mode_card_res_t *r=(c4_drm_mode_card_res_t*)arg;
        uint32_t w=(g_fb.width?g_fb.width:1024u);
        uint32_t h=(g_fb.height?g_fb.height:768u);
        bool render_node=drm_fd_is_render_node(fd);
        c4_drm_static_blobs_ensure();
        if(!r)return 0;
        if(render_node){
            /* Render nodes do not expose KMS resources to userspace. Mesa,
             * libdrm and Wayland clients use this fact to detect render-only
             * fds (e.g. /dev/dri/renderD128). */
            r->count_fbs=0;
            r->count_crtcs=0;
            r->count_connectors=0;
            r->count_encoders=0;
            r->min_width=0;
            r->max_width=0;
            r->min_height=0;
            r->max_height=0;
            return 0;
        }
        if(r->fb_id_ptr&&r->count_fbs>0)((uint32_t*)(uintptr_t)r->fb_id_ptr)[0]=C4_DRM_ID_FB;
        if(r->crtc_id_ptr&&r->count_crtcs>0)((uint32_t*)(uintptr_t)r->crtc_id_ptr)[0]=C4_DRM_ID_CRTC;
        if(r->connector_id_ptr&&r->count_connectors>0)((uint32_t*)(uintptr_t)r->connector_id_ptr)[0]=C4_DRM_ID_CONNECTOR;
        if(r->encoder_id_ptr&&r->count_encoders>0)((uint32_t*)(uintptr_t)r->encoder_id_ptr)[0]=C4_DRM_ID_ENCODER;
        r->count_fbs=1;
        r->count_crtcs=1;
        r->count_connectors=1;
        r->count_encoders=1;
        r->min_width=64;
        r->max_width=w<8192u?8192u:w;
        r->min_height=64;
        r->max_height=h<8192u?8192u:h;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETPLANERESOURCES) {
        c4_drm_mode_get_plane_res_t *r=(c4_drm_mode_get_plane_res_t*)arg;
        if(!r)return 0;
        if(r->plane_id_ptr&&r->count_planes>0){
            uint32_t *ids=(uint32_t*)(uintptr_t)r->plane_id_ptr;
            uint32_t limit=r->count_planes;
            if(limit>0)ids[0]=C4_DRM_ID_PLANE;
            if(limit>1)ids[1]=C4_DRM_ID_PLANE_CURSOR;
            if(limit>2)ids[2]=C4_DRM_ID_PLANE_OVERLAY;
        }
        r->count_planes=3;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETPLANE) {
        c4_drm_mode_get_plane_t *p=(c4_drm_mode_get_plane_t*)arg;
        uint32_t want=0;
        if(!p)return 0;
        if(p->plane_id==0u||p->plane_id==C4_DRM_ID_PLANE)want=C4_DRM_ID_PLANE;
        else if(p->plane_id==C4_DRM_ID_PLANE_CURSOR)want=C4_DRM_ID_PLANE_CURSOR;
        else if(p->plane_id==C4_DRM_ID_PLANE_OVERLAY)want=C4_DRM_ID_PLANE_OVERLAY;
        else return -EINVAL;
        if(want==C4_DRM_ID_PLANE_CURSOR){
            if(p->format_type_ptr&&p->count_format_types>0){
                uint32_t *fmts=(uint32_t*)(uintptr_t)p->format_type_ptr;
                fmts[0]=C4_DRM_FORMAT_ARGB8888;
                if(p->count_format_types>1)fmts[1]=C4_DRM_FORMAT_XRGB8888;
            }
            p->count_format_types=2;
        }else{
            if(p->format_type_ptr&&p->count_format_types>0){
                uint32_t *fmts=(uint32_t*)(uintptr_t)p->format_type_ptr;
                fmts[0]=C4_DRM_FORMAT_XRGB8888;
                if(p->count_format_types>1)fmts[1]=C4_DRM_FORMAT_ARGB8888;
                if(p->count_format_types>2)fmts[2]=C4_DRM_FORMAT_XBGR8888;
                if(p->count_format_types>3)fmts[3]=C4_DRM_FORMAT_ABGR8888;
            }
            p->count_format_types=4;
        }
        p->plane_id=want;
        p->crtc_id=(want==C4_DRM_ID_PLANE)?C4_DRM_ID_CRTC:0u;
        p->fb_id=(want==C4_DRM_ID_PLANE)?g_c4_drm_current_fb_id:0u;
        p->possible_crtcs=1;
        p->gamma_size=(want==C4_DRM_ID_PLANE)?256u:0u;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_OBJ_GETPROPERTIES) {
        c4_drm_mode_obj_get_properties_t *p=(c4_drm_mode_obj_get_properties_t*)arg;
        if(!p)return 0;
        c4_drm_static_blobs_ensure();
        if((p->obj_type==C4_DRM_MODE_OBJECT_PLANE||p->obj_type==C4_DRM_MODE_OBJECT_ANY)&&
           c4_drm_object_is_plane(p->obj_id)) {
            uint32_t w=(g_fb.width?g_fb.width:1024u);
            uint32_t h=(g_fb.height?g_fb.height:768u);
            uint32_t plane_type=C4_DRM_PLANE_TYPE_PRIMARY;
            uint32_t plane_zpos=1u;
            if(p->obj_id==C4_DRM_ID_PLANE_CURSOR){plane_type=C4_DRM_PLANE_TYPE_CURSOR;plane_zpos=255u;}
            else if(p->obj_id==C4_DRM_ID_PLANE_OVERLAY){plane_type=C4_DRM_PLANE_TYPE_OVERLAY;plane_zpos=2u;}
            {
            uint32_t ids[]={C4_DRM_PROP_PLANE_TYPE,C4_DRM_PROP_FB_ID,C4_DRM_PROP_CRTC_ID,
                            C4_DRM_PROP_CRTC_X,C4_DRM_PROP_CRTC_Y,C4_DRM_PROP_CRTC_W,C4_DRM_PROP_CRTC_H,
                            C4_DRM_PROP_SRC_X,C4_DRM_PROP_SRC_Y,C4_DRM_PROP_SRC_W,C4_DRM_PROP_SRC_H,
                            C4_DRM_PROP_IN_FENCE_FD,C4_DRM_PROP_IN_FORMATS,C4_DRM_PROP_FB_DAMAGE_CLIPS,
                            C4_DRM_PROP_ZPOS,C4_DRM_PROP_ROTATION,C4_DRM_PROP_ALPHA,
                            C4_DRM_PROP_PIXEL_BLEND_MODE,C4_DRM_PROP_COLOR_ENCODING,
                            C4_DRM_PROP_COLOR_RANGE,C4_DRM_PROP_HOTSPOT_X,C4_DRM_PROP_HOTSPOT_Y};
            uint64_t vals[]={plane_type,
                             (p->obj_id==C4_DRM_ID_PLANE)?g_c4_drm_current_fb_id:0,
                             (p->obj_id==C4_DRM_ID_PLANE)?C4_DRM_ID_CRTC:0,
                             0,0,w,h,0,0,((uint64_t)w)<<16,((uint64_t)h)<<16,(uint64_t)-1,
                             C4_DRM_ID_BLOB_IN_FORMATS,0,plane_zpos,1u/*rotation 0*/,
                             0xFFFFu,0/*Pre-mult*/,0/*BT.709*/,0/*Limited*/,0,0};
            uint32_t count=(uint32_t)(sizeof(ids)/sizeof(ids[0]));
            /* Cursor plane has no FB_DAMAGE_CLIPS / SRC_X-Y in many drivers,
             * but exposing them is harmless; same for overlay. */
            (void)count;
            c4_drm_put_props(p,ids,vals,(uint32_t)(sizeof(ids)/sizeof(ids[0])));
            }
        } else if((p->obj_type==C4_DRM_MODE_OBJECT_CRTC||p->obj_type==C4_DRM_MODE_OBJECT_ANY)&&
                  p->obj_id==C4_DRM_ID_CRTC) {
            uint32_t ids[]={C4_DRM_PROP_ACTIVE,C4_DRM_PROP_MODE_ID,C4_DRM_PROP_OUT_FENCE_PTR,
                            C4_DRM_PROP_GAMMA_LUT,C4_DRM_PROP_GAMMA_LUT_SIZE,
                            C4_DRM_PROP_DEGAMMA_LUT,C4_DRM_PROP_DEGAMMA_LUT_SIZE,
                            C4_DRM_PROP_CTM,C4_DRM_PROP_VRR_ENABLED,
                            C4_DRM_PROP_BACKGROUND_COLOR,C4_DRM_PROP_ICC_PROFILE,
                            C4_DRM_PROP_OUTPUT_BPP};
            uint64_t vals[]={1,C4_DRM_ID_MODE_BLOB_BASE,0,
                             0,C4_DRM_GAMMA_LUT_SIZE,
                             0,C4_DRM_GAMMA_LUT_SIZE,
                             0,0,
                             g_c4_drm_background_argb,C4_DRM_ID_BLOB_ICC_PROFILE,
                             g_c4_drm_output_bpp};
            c4_drm_put_props(p,ids,vals,(uint32_t)(sizeof(ids)/sizeof(ids[0])));
        } else if((p->obj_type==C4_DRM_MODE_OBJECT_CONNECTOR||p->obj_type==C4_DRM_MODE_OBJECT_ANY)&&
                  p->obj_id==C4_DRM_ID_CONNECTOR) {
            uint32_t ids[]={C4_DRM_PROP_CRTC_ID,C4_DRM_PROP_EDID,C4_DRM_PROP_DPMS,
                            C4_DRM_PROP_LINK_STATUS,C4_DRM_PROP_NON_DESKTOP,
                            C4_DRM_PROP_TILE,C4_DRM_PROP_PATH,
                            C4_DRM_PROP_VRR_CAPABLE,C4_DRM_PROP_SUBCONNECTOR,
                            C4_DRM_PROP_HDR_OUTPUT_METADATA,
                            C4_DRM_PROP_COLORSPACE,
                            C4_DRM_PROP_MAX_BPC,
                            C4_DRM_PROP_BROADCAST_RGB,
                            C4_DRM_PROP_PANEL_ORIENTATION,
                            C4_DRM_PROP_CONTENT_TYPE,
                            C4_DRM_PROP_SCALING_MODE,
                            C4_DRM_PROP_HDCP_CP,
                            C4_DRM_PROP_HDCP_CP_TYPE,
                            C4_DRM_PROP_PRIVACY_SCREEN_HW,
                            C4_DRM_PROP_PRIVACY_SCREEN_SW,
                            C4_DRM_PROP_DITHERING_MODE};
            uint64_t vals[]={C4_DRM_ID_CRTC,C4_DRM_ID_BLOB_EDID,(uint64_t)g_c4_drm_dpms_state,
                             C4_DRM_LINK_STATUS_GOOD,0,
                             C4_DRM_ID_BLOB_TILE,C4_DRM_ID_BLOB_PATH,
                             1,0,
                             C4_DRM_ID_BLOB_HDR_METADATA,
                             g_c4_drm_colorspace,
                             g_c4_drm_max_bpc,
                             g_c4_drm_broadcast_rgb,
                             g_c4_drm_panel_orient,
                             g_c4_drm_content_type,
                             g_c4_drm_scaling_mode,
                             g_c4_drm_hdcp_cp,
                             g_c4_drm_hdcp_cp_type,
                             g_c4_drm_privacy_screen_hw,
                             g_c4_drm_privacy_screen_sw,
                             g_c4_drm_dithering_mode};
            c4_drm_put_props(p,ids,vals,(uint32_t)(sizeof(ids)/sizeof(ids[0])));
        } else {
            p->count_props=0;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETPROPERTY) {
        c4_drm_mode_get_property_t *p=(c4_drm_mode_get_property_t*)arg;
        if(!p)return 0;
        if(p->prop_id==C4_DRM_PROP_PLANE_TYPE)c4_drm_fill_plane_type_property(p);
        else if(p->prop_id==C4_DRM_PROP_FB_ID)c4_drm_fill_prop_object(p,p->prop_id,"FB_ID",C4_DRM_MODE_OBJECT_FB);
        else if(p->prop_id==C4_DRM_PROP_CRTC_ID)c4_drm_fill_prop_object(p,p->prop_id,"CRTC_ID",C4_DRM_MODE_OBJECT_CRTC);
        else if(p->prop_id==C4_DRM_PROP_ACTIVE)c4_drm_fill_prop_range(p,p->prop_id,"ACTIVE",0,1);
        else if(p->prop_id==C4_DRM_PROP_MODE_ID)c4_drm_fill_prop_blob(p,p->prop_id,"MODE_ID");
        else if(p->prop_id==C4_DRM_PROP_IN_FENCE_FD)c4_drm_fill_prop_range(p,p->prop_id,"IN_FENCE_FD",0,(uint64_t)0xffffffffu);
        else if(p->prop_id==C4_DRM_PROP_OUT_FENCE_PTR)c4_drm_fill_prop_range(p,p->prop_id,"OUT_FENCE_PTR",0,~0ULL);
        else if(p->prop_id==C4_DRM_PROP_CRTC_X)c4_drm_fill_prop_range(p,p->prop_id,"CRTC_X",0,8192);
        else if(p->prop_id==C4_DRM_PROP_CRTC_Y)c4_drm_fill_prop_range(p,p->prop_id,"CRTC_Y",0,8192);
        else if(p->prop_id==C4_DRM_PROP_CRTC_W)c4_drm_fill_prop_range(p,p->prop_id,"CRTC_W",0,8192);
        else if(p->prop_id==C4_DRM_PROP_CRTC_H)c4_drm_fill_prop_range(p,p->prop_id,"CRTC_H",0,8192);
        else if(p->prop_id==C4_DRM_PROP_SRC_X)c4_drm_fill_prop_range(p,p->prop_id,"SRC_X",0,8192ULL<<16);
        else if(p->prop_id==C4_DRM_PROP_SRC_Y)c4_drm_fill_prop_range(p,p->prop_id,"SRC_Y",0,8192ULL<<16);
        else if(p->prop_id==C4_DRM_PROP_SRC_W)c4_drm_fill_prop_range(p,p->prop_id,"SRC_W",0,8192ULL<<16);
        else if(p->prop_id==C4_DRM_PROP_SRC_H)c4_drm_fill_prop_range(p,p->prop_id,"SRC_H",0,8192ULL<<16);
        else if(p->prop_id==C4_DRM_PROP_EDID)c4_drm_fill_prop_blob(p,p->prop_id,"EDID");
        else if(p->prop_id==C4_DRM_PROP_DPMS)c4_drm_fill_prop_range(p,p->prop_id,"DPMS",0,3);
        else if(p->prop_id==C4_DRM_PROP_LINK_STATUS){
            /* enum {Good,Bad} */
            p->prop_id=C4_DRM_PROP_LINK_STATUS;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"link-status");
            p->count_values=0;
            p->count_enum_blobs=2;
        }
        else if(p->prop_id==C4_DRM_PROP_NON_DESKTOP)c4_drm_fill_prop_range(p,p->prop_id,"non-desktop",0,1);
        else if(p->prop_id==C4_DRM_PROP_TILE)c4_drm_fill_prop_blob(p,p->prop_id,"TILE");
        else if(p->prop_id==C4_DRM_PROP_PATH)c4_drm_fill_prop_blob(p,p->prop_id,"PATH");
        else if(p->prop_id==C4_DRM_PROP_GAMMA_LUT)c4_drm_fill_prop_blob(p,p->prop_id,"GAMMA_LUT");
        else if(p->prop_id==C4_DRM_PROP_GAMMA_LUT_SIZE)c4_drm_fill_prop_range(p,p->prop_id,"GAMMA_LUT_SIZE",0,4096);
        else if(p->prop_id==C4_DRM_PROP_DEGAMMA_LUT)c4_drm_fill_prop_blob(p,p->prop_id,"DEGAMMA_LUT");
        else if(p->prop_id==C4_DRM_PROP_DEGAMMA_LUT_SIZE)c4_drm_fill_prop_range(p,p->prop_id,"DEGAMMA_LUT_SIZE",0,4096);
        else if(p->prop_id==C4_DRM_PROP_CTM)c4_drm_fill_prop_blob(p,p->prop_id,"CTM");
        else if(p->prop_id==C4_DRM_PROP_VRR_ENABLED)c4_drm_fill_prop_range(p,p->prop_id,"VRR_ENABLED",0,1);
        else if(p->prop_id==C4_DRM_PROP_VRR_CAPABLE)c4_drm_fill_prop_range(p,p->prop_id,"vrr_capable",0,1);
        else if(p->prop_id==C4_DRM_PROP_IN_FORMATS)c4_drm_fill_prop_blob(p,p->prop_id,"IN_FORMATS");
        else if(p->prop_id==C4_DRM_PROP_FB_DAMAGE_CLIPS)c4_drm_fill_prop_blob(p,p->prop_id,"FB_DAMAGE_CLIPS");
        else if(p->prop_id==C4_DRM_PROP_HOTSPOT_X)c4_drm_fill_prop_range(p,p->prop_id,"HOTSPOT_X",0,8192);
        else if(p->prop_id==C4_DRM_PROP_HOTSPOT_Y)c4_drm_fill_prop_range(p,p->prop_id,"HOTSPOT_Y",0,8192);
        else if(p->prop_id==C4_DRM_PROP_ZPOS)c4_drm_fill_prop_range(p,p->prop_id,"zpos",0,255);
        else if(p->prop_id==C4_DRM_PROP_ROTATION){
            p->prop_id=C4_DRM_PROP_ROTATION;
            p->flags=C4_DRM_MODE_PROP_ENUM|0x40u/*BITMASK*/;
            ulibc_strcpy(p->name,"rotation");
            p->count_values=0;
            p->count_enum_blobs=6;
        }
        else if(p->prop_id==C4_DRM_PROP_ALPHA)c4_drm_fill_prop_range(p,p->prop_id,"alpha",0,0xFFFFu);
        else if(p->prop_id==C4_DRM_PROP_PIXEL_BLEND_MODE){
            p->prop_id=C4_DRM_PROP_PIXEL_BLEND_MODE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"pixel blend mode");
            p->count_values=0;
            p->count_enum_blobs=3;
        }
        else if(p->prop_id==C4_DRM_PROP_COLOR_ENCODING){
            p->prop_id=C4_DRM_PROP_COLOR_ENCODING;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"COLOR_ENCODING");
            p->count_values=0;
            p->count_enum_blobs=3;
        }
        else if(p->prop_id==C4_DRM_PROP_COLOR_RANGE){
            p->prop_id=C4_DRM_PROP_COLOR_RANGE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"COLOR_RANGE");
            p->count_values=0;
            p->count_enum_blobs=2;
        }
        else if(p->prop_id==C4_DRM_PROP_SUBCONNECTOR){
            p->prop_id=C4_DRM_PROP_SUBCONNECTOR;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"subconnector");
            p->count_values=0;
            p->count_enum_blobs=4;
        }
        /* HDR / color management / output extras. */
        else if(p->prop_id==C4_DRM_PROP_HDR_OUTPUT_METADATA)
            c4_drm_fill_prop_blob(p,p->prop_id,"HDR_OUTPUT_METADATA");
        else if(p->prop_id==C4_DRM_PROP_COLORSPACE){
            p->prop_id=C4_DRM_PROP_COLORSPACE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"Colorspace");
            p->count_values=0;
            p->count_enum_blobs=14;     /* Default + 13 colorspaces */
        }
        else if(p->prop_id==C4_DRM_PROP_MAX_BPC)
            c4_drm_fill_prop_range(p,p->prop_id,"max bpc",6,16);
        else if(p->prop_id==C4_DRM_PROP_BROADCAST_RGB){
            p->prop_id=C4_DRM_PROP_BROADCAST_RGB;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"Broadcast RGB");
            p->count_values=0;
            p->count_enum_blobs=3;      /* Automatic, Full, Limited 16:235 */
        }
        else if(p->prop_id==C4_DRM_PROP_PANEL_ORIENTATION){
            p->prop_id=C4_DRM_PROP_PANEL_ORIENTATION;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"panel orientation");
            p->count_values=0;
            p->count_enum_blobs=4;      /* Normal, Upside Down, Left Side Up, Right Side Up */
        }
        else if(p->prop_id==C4_DRM_PROP_CONTENT_TYPE){
            p->prop_id=C4_DRM_PROP_CONTENT_TYPE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"content type");
            p->count_values=0;
            p->count_enum_blobs=5;      /* No Data, Graphics, Photo, Cinema, Game */
        }
        else if(p->prop_id==C4_DRM_PROP_SCALING_MODE){
            p->prop_id=C4_DRM_PROP_SCALING_MODE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"scaling mode");
            p->count_values=0;
            p->count_enum_blobs=4;      /* None, Full, Center, Full aspect */
        }
        else if(p->prop_id==C4_DRM_PROP_HDCP_CP){
            p->prop_id=C4_DRM_PROP_HDCP_CP;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"Content Protection");
            p->count_values=0;
            p->count_enum_blobs=3;      /* Undesired, Desired, Enabled */
        }
        else if(p->prop_id==C4_DRM_PROP_HDCP_CP_TYPE){
            p->prop_id=C4_DRM_PROP_HDCP_CP_TYPE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"HDCP Content Type");
            p->count_values=0;
            p->count_enum_blobs=2;      /* HDCP Type0, HDCP Type1 */
        }
        else if(p->prop_id==C4_DRM_PROP_BACKGROUND_COLOR)
            c4_drm_fill_prop_range(p,p->prop_id,"background_color",0,(uint64_t)-1);
        else if(p->prop_id==C4_DRM_PROP_ICC_PROFILE)
            c4_drm_fill_prop_blob(p,p->prop_id,"ICC_PROFILE");
        else if(p->prop_id==C4_DRM_PROP_PRIVACY_SCREEN_HW){
            p->prop_id=C4_DRM_PROP_PRIVACY_SCREEN_HW;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"privacy-screen hw-state");
            p->count_values=0;
            p->count_enum_blobs=4;      /* Disabled, Enabled, Disabled-locked, Enabled-locked */
        }
        else if(p->prop_id==C4_DRM_PROP_PRIVACY_SCREEN_SW){
            p->prop_id=C4_DRM_PROP_PRIVACY_SCREEN_SW;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"privacy-screen sw-state");
            p->count_values=0;
            p->count_enum_blobs=2;      /* Disabled, Enabled */
        }
        else if(p->prop_id==C4_DRM_PROP_DITHERING_MODE){
            p->prop_id=C4_DRM_PROP_DITHERING_MODE;
            p->flags=C4_DRM_MODE_PROP_ENUM;
            ulibc_strcpy(p->name,"dithering mode");
            p->count_values=0;
            p->count_enum_blobs=4;      /* Off, Auto, Spatial, Temporal */
        }
        else if(p->prop_id==C4_DRM_PROP_OUTPUT_BPP)
            c4_drm_fill_prop_range(p,p->prop_id,"output bpp",18,48);
        else p->count_values=p->count_enum_blobs=0;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CREATEPROPBLOB) {
        if(arg){
            c4_drm_mode_create_blob_t *b=(c4_drm_mode_create_blob_t*)arg;
            uint32_t id=c4_drm_prop_blob_create(b->length);
            if(!id)return -ENOMEM;
            b->blob_id=id;
            (void)b->data;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_DESTROYPROPBLOB) {
        if(arg){
            c4_drm_mode_destroy_blob_t *b=(c4_drm_mode_destroy_blob_t*)arg;
            c4_drm_prop_blob_destroy(b->blob_id);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETPROPBLOB) {
        if(arg){
            c4_drm_mode_get_blob_t *b=(c4_drm_mode_get_blob_t*)arg;
            c4_drm_prop_blob_t *rec;
            c4_drm_mode_modeinfo_t mode;
            c4_drm_static_blobs_ensure();
            if(b->blob_id==C4_DRM_ID_BLOB_EDID){
                if(b->data&&b->length>=C4_DRM_EDID_LEN)
                    ulibc_memcpy((void*)(uintptr_t)b->data,g_c4_drm_edid,C4_DRM_EDID_LEN);
                b->length=C4_DRM_EDID_LEN;
                return 0;
            }
            if(b->blob_id==C4_DRM_ID_BLOB_IN_FORMATS){
                uint32_t L=g_c4_drm_in_formats_len;
                if(b->data&&L&&b->length>=L)
                    ulibc_memcpy((void*)(uintptr_t)b->data,g_c4_drm_in_formats,L);
                b->length=L;
                return 0;
            }
            if(b->blob_id==C4_DRM_ID_BLOB_HDR_METADATA){
                uint32_t L=(uint32_t)sizeof(g_c4_drm_hdr_metadata);
                if(b->data&&b->length>=L)
                    ulibc_memcpy((void*)(uintptr_t)b->data,&g_c4_drm_hdr_metadata,L);
                b->length=L;
                return 0;
            }
            if(b->blob_id==C4_DRM_ID_BLOB_ICC_PROFILE){
                uint32_t L=g_c4_drm_icc_profile_len;
                if(b->data&&L&&b->length>=L)
                    ulibc_memcpy((void*)(uintptr_t)b->data,g_c4_drm_icc_profile,L);
                b->length=L;
                return 0;
            }
            if(b->blob_id==C4_DRM_ID_BLOB_TILE){
                uint32_t L=g_c4_drm_tile_blob_len;
                if(b->data&&L&&b->length>=L)
                    ulibc_memcpy((void*)(uintptr_t)b->data,g_c4_drm_tile_blob,L);
                b->length=L;
                return 0;
            }
            if(b->blob_id==C4_DRM_ID_BLOB_PATH){
                uint32_t L=g_c4_drm_path_blob_len;
                if(b->data&&L&&b->length>=L)
                    ulibc_memcpy((void*)(uintptr_t)b->data,g_c4_drm_path_blob,L);
                b->length=L;
                return 0;
            }
            rec=c4_drm_prop_blob_find(b->blob_id);
            if(!rec&&b->blob_id!=C4_DRM_ID_MODE_BLOB_BASE)return -EINVAL;
            if(b->data&&b->length>=sizeof(mode)){
                c4_drm_fill_mode(&mode);
                ulibc_memcpy((void*)(uintptr_t)b->data,&mode,sizeof(mode));
            }
            b->length=rec?rec->length:(uint32_t)sizeof(mode);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_SETPROPERTY) {
        if(arg) {
            c4_drm_mode_set_property_t *p=(c4_drm_mode_set_property_t*)arg;
            (void)p->value;(void)p->prop_id;(void)p->obj_id;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_OBJ_SETPROPERTY) {
        if(arg) {
            c4_drm_mode_obj_set_property_t *p=(c4_drm_mode_obj_set_property_t*)arg;
            (void)p->value;(void)p->prop_id;(void)p->obj_id;(void)p->obj_type;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_ATOMIC) {
        return c4_drm_atomic_commit(fd,(c4_drm_mode_atomic_t*)arg);
    }
    if(request == DRM_IOCTL_MODE_GETCRTC) {
        c4_drm_mode_crtc_t *c=(c4_drm_mode_crtc_t*)arg;
        if(!c)return 0;
        if(c->crtc_id&&c->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
        c->crtc_id=C4_DRM_ID_CRTC;
        c->fb_id=g_c4_drm_current_fb_id;
        c->x=0;c->y=0;
        c->gamma_size=256;
        c->mode_valid=1;
        c4_drm_fill_mode(&c->mode);
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETENCODER) {
        c4_drm_mode_get_encoder_t *e=(c4_drm_mode_get_encoder_t*)arg;
        if(!e)return 0;
        if(e->encoder_id&&e->encoder_id!=C4_DRM_ID_ENCODER)return -EINVAL;
        e->encoder_id=C4_DRM_ID_ENCODER;
        e->encoder_type=1;
        e->crtc_id=C4_DRM_ID_CRTC;
        e->possible_crtcs=1;
        e->possible_clones=0;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETCONNECTOR) {
        c4_drm_mode_get_connector_t *c=(c4_drm_mode_get_connector_t*)arg;
        uint32_t w=(g_fb.width?g_fb.width:1024u);
        uint32_t h=(g_fb.height?g_fb.height:768u);
        uint32_t total_modes=c4_drm_modes_count();
        uint32_t props_ids[]={C4_DRM_PROP_CRTC_ID,C4_DRM_PROP_EDID,C4_DRM_PROP_DPMS,
                              C4_DRM_PROP_LINK_STATUS,C4_DRM_PROP_NON_DESKTOP,
                              C4_DRM_PROP_TILE,C4_DRM_PROP_PATH,
                              C4_DRM_PROP_VRR_CAPABLE,C4_DRM_PROP_SUBCONNECTOR,
                              C4_DRM_PROP_HDR_OUTPUT_METADATA,
                              C4_DRM_PROP_COLORSPACE,
                              C4_DRM_PROP_MAX_BPC,
                              C4_DRM_PROP_BROADCAST_RGB,
                              C4_DRM_PROP_PANEL_ORIENTATION,
                              C4_DRM_PROP_CONTENT_TYPE,
                              C4_DRM_PROP_SCALING_MODE,
                              C4_DRM_PROP_HDCP_CP,
                              C4_DRM_PROP_HDCP_CP_TYPE,
                              C4_DRM_PROP_PRIVACY_SCREEN_HW,
                              C4_DRM_PROP_PRIVACY_SCREEN_SW,
                              C4_DRM_PROP_DITHERING_MODE};
        uint64_t props_vals[]={C4_DRM_ID_CRTC,C4_DRM_ID_BLOB_EDID,
                               (uint64_t)g_c4_drm_dpms_state,
                               C4_DRM_LINK_STATUS_GOOD,0,
                               C4_DRM_ID_BLOB_TILE,C4_DRM_ID_BLOB_PATH,
                               0,0,
                               C4_DRM_ID_BLOB_HDR_METADATA,
                               g_c4_drm_colorspace,
                               g_c4_drm_max_bpc,
                               g_c4_drm_broadcast_rgb,
                               g_c4_drm_panel_orient,
                               g_c4_drm_content_type,
                               g_c4_drm_scaling_mode,
                               g_c4_drm_hdcp_cp,
                               g_c4_drm_hdcp_cp_type,
                               g_c4_drm_privacy_screen_hw,
                               g_c4_drm_privacy_screen_sw,
                               g_c4_drm_dithering_mode};
        uint32_t total_props=(uint32_t)(sizeof(props_ids)/sizeof(props_ids[0]));
        c4_drm_static_blobs_ensure();
        if(!c)return 0;
        if(c->connector_id&&c->connector_id!=C4_DRM_ID_CONNECTOR)return -EINVAL;
        if(c->encoders_ptr&&c->count_encoders>0)
            ((uint32_t*)(uintptr_t)c->encoders_ptr)[0]=C4_DRM_ID_ENCODER;
        if(c->modes_ptr&&c->count_modes>0){
            uint32_t i,limit=c->count_modes<total_modes?c->count_modes:total_modes;
            c4_drm_mode_modeinfo_t *modes=(c4_drm_mode_modeinfo_t*)(uintptr_t)c->modes_ptr;
            for(i=0;i<limit;++i)c4_drm_fill_mode_at(i,&modes[i]);
        }
        if(c->props_ptr&&c->prop_values_ptr&&c->count_props>0){
            uint32_t i,limit=c->count_props<total_props?c->count_props:total_props;
            uint32_t *out_ids=(uint32_t*)(uintptr_t)c->props_ptr;
            uint64_t *out_vals=(uint64_t*)(uintptr_t)c->prop_values_ptr;
            for(i=0;i<limit;++i){out_ids[i]=props_ids[i];out_vals[i]=props_vals[i];}
        }
        c->count_encoders=1;
        c->count_modes=total_modes;
        c->count_props=total_props;
        c->encoder_id=C4_DRM_ID_ENCODER;
        c->connector_id=C4_DRM_ID_CONNECTOR;
        c->connector_type=11;       /* DRM_MODE_CONNECTOR_DisplayPort */
        c->connector_type_id=1;
        c->connection=1;            /* connected */
        c->mm_width=w/4u;
        c->mm_height=h/4u;
        c->subpixel=1;
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CREATE_DUMB) {
        if(arg) {
            c4_drm_mode_create_dumb_t *d=(c4_drm_mode_create_dumb_t*)arg;
            uint32_t w=(g_fb.width?g_fb.width:1024u);
            uint32_t h=(g_fb.height?g_fb.height:768u);
            uint32_t bpp=(d->bpp?d->bpp:32u);
            c4_drm_bo_t *bo;
            if(!d->width)d->width=w;
            if(!d->height)d->height=h;
            bo=c4_drm_alloc_bo(d->width,d->height,bpp);
            if(!bo)return -ENOMEM;
            bo->kernel_refcount=0;
            if(!c4_drm_file_handle_add(drm_file,bo)){
                c4_drm_bo_try_free(bo);
                return -EMFILE;
            }
            d->bpp=bo->bpp;
            d->pitch=bo->pitch;
            d->size=bo->size;
            d->handle=bo->handle;
            c4_drm_trace_bo("create-dumb",bo,d->width,d->height);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_MAP_DUMB) {
        if(arg) {
            c4_drm_mode_map_dumb_t *m=(c4_drm_mode_map_dumb_t*)arg;
            c4_drm_bo_t *bo=c4_drm_bo_for_fd_handle(fd,m->handle);
            if(!bo)return -EINVAL;
            m->offset=bo->map_offset;
            c4_drm_trace_bo("map-dumb",bo,(uint32_t)(m->offset>>12),(uint32_t)m->offset);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        if(arg){
            c4_drm_gem_close_t *d=(c4_drm_gem_close_t*)arg;
            if(!c4_drm_file_handle_remove(drm_file,d->handle))return -EINVAL;
            d->handle=0;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETFB) {
        if(arg){
            c4_drm_mode_fb_cmd_t *f=(c4_drm_mode_fb_cmd_t*)arg;
            c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(f->fb_id);
            if(!bo&&f->fb_id==C4_DRM_ID_FB)bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
            if(!bo)bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
            if(!bo)return -EINVAL;
            f->fb_id=bo->fb_id?bo->fb_id:g_c4_drm_current_fb_id;
            f->width=bo->width;
            f->height=bo->height;
            f->pitch=bo->pitch;
            f->bpp=bo->bpp;
            f->depth=24;
            f->handle=bo->handle;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_ADDFB) {
        if(arg){
            c4_drm_mode_fb_cmd_t *f=(c4_drm_mode_fb_cmd_t*)arg;
            c4_drm_bo_t *bo=0;
            if(c4_drm_vbox_mode())bo=c4_vmw_bo_from_surface_handle(f->handle);
            if(!bo)bo=c4_drm_bo_for_fd_handle(fd,f->handle);
            if(!bo&&!f->handle)bo=c4_drm_latest_bo();
            if(!bo)return -EINVAL;
            bo->width=f->width?f->width:bo->width;
            bo->height=f->height?f->height:bo->height;
            bo->pitch=f->pitch?f->pitch:bo->pitch;
            bo->bpp=f->bpp?f->bpp:bo->bpp;
            if(!bo->fb_id){bo->fb_id=g_c4_drm_next_fb_id++;bo->fb_refcount++;}
            f->fb_id=bo->fb_id;
            c4_drm_trace_bo("addfb",bo,f->width,f->height);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_ADDFB2) {
        if(arg){
            c4_drm_mode_fb_cmd2_t *f=(c4_drm_mode_fb_cmd2_t*)arg;
            c4_drm_bo_t *bo=0;
            if(c4_drm_vbox_mode())bo=c4_vmw_bo_from_surface_handle(f->handles[0]);
            if(!bo)bo=c4_drm_bo_for_fd_handle(fd,f->handles[0]);
            if(!bo)bo=c4_drm_latest_bo();
            if(!bo)return -EINVAL;
            bo->width=f->width?f->width:bo->width;
            bo->height=f->height?f->height:bo->height;
            bo->pixel_format=f->pixel_format?f->pixel_format:C4_DRM_FORMAT_XRGB8888;
            bo->pitch=f->pitches[0]?f->pitches[0]:bo->pitch;
            if(!bo->fb_id){bo->fb_id=g_c4_drm_next_fb_id++;bo->fb_refcount++;}
            f->fb_id=bo->fb_id;
            (void)f->flags;
            c4_drm_trace_bo("addfb2",bo,f->width,f->height);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETFB2) {
        if(arg){
            c4_drm_mode_fb_cmd2_t *f=(c4_drm_mode_fb_cmd2_t*)arg;
            c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(f->fb_id);
            if(!bo&&f->fb_id==C4_DRM_ID_FB)bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
            if(!bo)bo=c4_drm_find_bo_by_fb(g_c4_drm_current_fb_id);
            if(!bo)return -EINVAL;
            f->fb_id=bo->fb_id?bo->fb_id:g_c4_drm_current_fb_id;
            f->width=bo->width;
            f->height=bo->height;
            f->pixel_format=bo->pixel_format?bo->pixel_format:C4_DRM_FORMAT_XRGB8888;
            f->handles[0]=bo->handle;
            f->pitches[0]=bo->pitch;
            f->offsets[0]=0;
            f->modifier[0]=0;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_RMFB) {
        if(arg){
            uint32_t fb=*(uint32_t*)arg;
            c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(fb);
            if(bo&&fb!=g_c4_drm_current_fb_id){
                bo->fb_id=0;
                if(bo->fb_refcount)bo->fb_refcount--;
                c4_drm_bo_try_free(bo);
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_SETCRTC) {
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg){
            c4_drm_mode_crtc_t *s=(c4_drm_mode_crtc_t*)arg;
            if(s->crtc_id&&s->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
            s->crtc_id=C4_DRM_ID_CRTC;
            if(!s->fb_id)s->fb_id=g_c4_drm_current_fb_id;
            g_c4_drm_current_fb_id=s->fb_id;
            s->mode_valid=1;
            c4_drm_trace_bo("setcrtc",c4_drm_find_bo_by_fb(s->fb_id),s->fb_id,s->crtc_id);
            c4_drm_present_fb_vbox_or_cpu(s->fb_id);
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CURSOR) {
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg) {
            c4_drm_mode_cursor_t *c=(c4_drm_mode_cursor_t*)arg;
            uint32_t flags=c->flags;
            if(c->crtc_id&&c->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
            if((flags&2u)||flags==0u){
                g_c4_drm_cursor_x=c->x;
                g_c4_drm_cursor_y=c->y;
            }
            if((flags&1u)||flags==0u||c->handle||c->width||c->height){
                g_c4_drm_cursor_handle=c->handle;
                g_c4_drm_cursor_w=c->width;
                g_c4_drm_cursor_h=c->height;
                g_c4_drm_cursor_visible=(c->handle!=0u)||
                    (c4_drm_vbox_mode()&&((flags&1u)||flags==0u));
                /* wlroots emits MODE_CURSOR with handle=0 to declare it
                   does cursor compositing in software (the cursor pixels
                   are already in the framebuffer it scans out). Remember
                   that so we do not paint a synthetic arrow on top. */
                g_c4_drm_cursor_swcomp=(c->handle==0u&&
                    ((flags&1u)||flags==0u)&&!c4_drm_vbox_mode());
            }
            {
                static uint32_t cursor_trace;
                if(c4_virtgpu_trace_enabled()&&cursor_trace<48u){
                    ++cursor_trace;
                    __boot_serial_force_puts("[drm-cursor!] #");
                    __boot_serial_force_putu32(cursor_trace);
                    __boot_serial_force_puts(" x=");
                    __boot_serial_force_putu32((uint32_t)c->x);
                    __boot_serial_force_puts(" y=");
                    __boot_serial_force_putu32((uint32_t)c->y);
                    __boot_serial_force_puts(" handle=");
                    __boot_serial_force_putu32(c->handle);
                    __boot_serial_force_puts(" flags=");
                    __boot_serial_force_puthex64((uint64_t)c->flags);
                    __boot_serial_force_puts("\n");
                }
            }
            (void)c->width;(void)c->height;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CURSOR2) {
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg) {
            c4_drm_mode_cursor2_t *c=(c4_drm_mode_cursor2_t*)arg;
            uint32_t flags=c->flags;
            if(c->crtc_id&&c->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
            if((flags&2u)||flags==0u){
                g_c4_drm_cursor_x=c->x;
                g_c4_drm_cursor_y=c->y;
            }
            if((flags&1u)||flags==0u||c->handle||c->width||c->height){
                g_c4_drm_cursor_handle=c->handle;
                g_c4_drm_cursor_w=c->width;
                g_c4_drm_cursor_h=c->height;
                g_c4_drm_cursor_hot_x=c->hot_x;
                g_c4_drm_cursor_hot_y=c->hot_y;
                g_c4_drm_cursor_visible=(c->handle!=0u)||
                    (c4_drm_vbox_mode()&&((flags&1u)||flags==0u));
                g_c4_drm_cursor_swcomp=(c->handle==0u&&
                    ((flags&1u)||flags==0u)&&!c4_drm_vbox_mode());
            }
            {
                static uint32_t cursor2_trace;
                if(c4_virtgpu_trace_enabled()&&cursor2_trace<48u){
                    ++cursor2_trace;
                    __boot_serial_force_puts("[drm-cursor2!] #");
                    __boot_serial_force_putu32(cursor2_trace);
                    __boot_serial_force_puts(" x=");
                    __boot_serial_force_putu32((uint32_t)c->x);
                    __boot_serial_force_puts(" y=");
                    __boot_serial_force_putu32((uint32_t)c->y);
                    __boot_serial_force_puts(" hot=");
                    __boot_serial_force_putu32((uint32_t)c->hot_x);
                    __boot_serial_force_puts(",");
                    __boot_serial_force_putu32((uint32_t)c->hot_y);
                    __boot_serial_force_puts(" handle=");
                    __boot_serial_force_putu32(c->handle);
                    __boot_serial_force_puts(" flags=");
                    __boot_serial_force_puthex64((uint64_t)c->flags);
                    __boot_serial_force_puts("\n");
                }
            }
            (void)c->width;(void)c->height;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_SETPLANE) {
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg) {
            c4_drm_mode_set_plane_t *p=(c4_drm_mode_set_plane_t*)arg;
            if(p->fb_id){
                g_c4_drm_current_fb_id=p->fb_id;
                c4_drm_trace_bo("setplane",c4_drm_find_bo_by_fb(p->fb_id),p->fb_id,p->crtc_id);
                c4_drm_present_fb_vbox_or_cpu(p->fb_id);
            }
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_CREATE_LEASE) {
        /* struct drm_mode_create_lease {
         *   __u64 object_ids; __u32 object_count; __u32 flags;
         *   __u32 lessee_id; __s32 fd;  // returned
         * } -- 24 bytes total. */
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg){
            uint8_t *raw=(uint8_t*)arg;
            uint64_t object_ids;
            uint32_t object_count,flags,lessee_id;
            int32_t lease_fd=-1;
            ulibc_memcpy(&object_ids,raw,sizeof(object_ids));
            ulibc_memcpy(&object_count,raw+8,sizeof(object_count));
            ulibc_memcpy(&flags,raw+12,sizeof(flags));
            (void)flags;
            lessee_id=ridux_drm_lease_create((const uint32_t*)(uintptr_t)object_ids,
                                             object_count,&lease_fd);
            if(!lessee_id)return -ENOMEM;
            ulibc_memcpy(raw+16,&lessee_id,sizeof(lessee_id));
            ulibc_memcpy(raw+20,&lease_fd,sizeof(lease_fd));
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_LIST_LESSEES) {
        /* struct drm_mode_list_lessees {
         *   __u32 count_lessees; __u32 pad; __u64 lessees_ptr;
         * } -- count is in/out. */
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg){
            uint8_t *raw=(uint8_t*)arg;
            uint32_t count_lessees;
            uint64_t lessees_ptr;
            uint32_t i,total=ridux_drm_lease_count();
            ulibc_memcpy(&count_lessees,raw,sizeof(count_lessees));
            ulibc_memcpy(&lessees_ptr,raw+8,sizeof(lessees_ptr));
            if(lessees_ptr&&count_lessees){
                uint32_t *out=(uint32_t*)(uintptr_t)lessees_ptr;
                uint32_t limit=count_lessees<total?count_lessees:total,j=0;
                for(i=0;i<C4_DRM_LEASE_MAX&&j<limit;++i){
                    if(!g_c4_drm_leases[i].used)continue;
                    out[j++]=g_c4_drm_leases[i].lessee_id;
                }
            }
            ulibc_memcpy(raw,&total,sizeof(total));
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GET_LEASE) {
        /* struct drm_mode_get_lease {
         *   __u32 count_objects; __u32 pad; __u64 objects_ptr;
         * } */
        if(arg){
            uint8_t *raw=(uint8_t*)arg;
            uint32_t count_objects;
            uint64_t objects_ptr;
            uint32_t total=4u;            /* CRTC, connector, encoder, primary plane */
            const uint32_t defaults[4]={C4_DRM_ID_CRTC,C4_DRM_ID_CONNECTOR,
                                        C4_DRM_ID_ENCODER,C4_DRM_ID_PLANE};
            const uint32_t *objects=defaults;
            uint32_t i;
            if(drm_file&&drm_file->lease){
                for(i=0;i<C4_DRM_LEASE_MAX;++i){
                    if(!g_c4_drm_leases[i].used)continue;
                    if(g_c4_drm_leases[i].lessee_id!=drm_file->lessee_id)continue;
                    objects=g_c4_drm_leases[i].objects;
                    total=g_c4_drm_leases[i].object_count;
                    break;
                }
            }
            ulibc_memcpy(&count_objects,raw,sizeof(count_objects));
            ulibc_memcpy(&objects_ptr,raw+8,sizeof(objects_ptr));
            if(objects_ptr&&count_objects){
                uint32_t *out=(uint32_t*)(uintptr_t)objects_ptr;
                uint32_t limit=count_objects<total?count_objects:total;
                for(i=0;i<limit;++i)out[i]=objects[i];
            }
            ulibc_memcpy(raw,&total,sizeof(total));
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_REVOKE_LEASE) {
        if(!c4_drm_file_can_modeset(fd))return -EACCES;
        if(arg){
            uint32_t lessee_id;
            ulibc_memcpy(&lessee_id,arg,sizeof(lessee_id));
            if(!ridux_drm_lease_revoke(lessee_id))return -ENOENT;
        }
        return 0;
    }
    if(request == DRM_IOCTL_WAIT_VBLANK) {
        /* struct drm_wait_vblank: a request/reply union of 24 bytes.
         *  request: u32 type, u32 sequence, ulong signal
         *  reply:   u32 type, u32 sequence, long tval_sec, long tval_usec
         * We synthesize a stable monotonic vblank counter that ticks every
         * present and serve relative/absolute waits without blocking, since
         * page flips already drive the actual scanout. The high 6 bits of
         * type carry the CRTC pipe index plus a few flag bits we accept. */
        if(arg){
            uint8_t *raw=(uint8_t*)arg;
            uint32_t type;
            uint32_t sequence;
            uint64_t now_ns,sec,usec;
            ulibc_memcpy(&type,raw,sizeof(type));
            ulibc_memcpy(&sequence,raw+4,sizeof(sequence));
            ++g_c4_drm_vblank_seq;
            if((type&C4_DRM_VBLANK_RELATIVE)==0u){
                /* Absolute wait: clamp the sequence to the latest counter.
                 * Real drivers would block; here we never block to keep the
                 * VM responsive, but we still increment so the requested
                 * sequence is always reached. */
                if(sequence>g_c4_drm_vblank_seq)
                    g_c4_drm_vblank_seq=sequence;
            }
            if(type&C4_DRM_VBLANK_EVENT){
                drm_queue_page_flip_event(fd,((uint64_t*)raw)[1],C4_DRM_ID_CRTC);
            }
            now_ns=ulibc_clock_gettime_monotonic_ns();
            sec=now_ns/1000000000ULL;
            usec=(now_ns/1000ULL)%1000000ULL;
            ulibc_memcpy(raw,&type,sizeof(type));
            ulibc_memcpy(raw+4,&g_c4_drm_vblank_seq,sizeof(g_c4_drm_vblank_seq));
            ulibc_memcpy(raw+8,&sec,sizeof(sec));
            ulibc_memcpy(raw+16,&usec,sizeof(usec));
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_GETGAMMA||request == DRM_IOCTL_MODE_SETGAMMA) {
        /* struct drm_mode_crtc_lut: u32 crtc_id, u32 gamma_size, u64 red,
         * green, blue. The user buffer holds 16-bit lut entries; we expose
         * a 256-entry identity gamma so libdrm/Plasma color management can
         * roundtrip without errors. */
        if(arg){
            typedef struct __attribute__((packed)){
                uint32_t crtc_id;
                uint32_t gamma_size;
                uint64_t red;
                uint64_t green;
                uint64_t blue;
            } gamma_lut_t;
            gamma_lut_t *g=(gamma_lut_t*)arg;
            uint32_t n;
            if(g->crtc_id&&g->crtc_id!=C4_DRM_ID_CRTC)return -EINVAL;
            n=g->gamma_size?g->gamma_size:C4_DRM_GAMMA_LUT_SIZE;
            if(n>C4_DRM_GAMMA_LUT_SIZE)n=C4_DRM_GAMMA_LUT_SIZE;
            if(request==DRM_IOCTL_MODE_GETGAMMA){
                if(g->red)ulibc_memcpy((void*)(uintptr_t)g->red,
                    g_c4_drm_gamma_r,(size_t)n*sizeof(uint16_t));
                if(g->green)ulibc_memcpy((void*)(uintptr_t)g->green,
                    g_c4_drm_gamma_g,(size_t)n*sizeof(uint16_t));
                if(g->blue)ulibc_memcpy((void*)(uintptr_t)g->blue,
                    g_c4_drm_gamma_b,(size_t)n*sizeof(uint16_t));
            }else{
                if(g->red)ulibc_memcpy(g_c4_drm_gamma_r,
                    (const void*)(uintptr_t)g->red,(size_t)n*sizeof(uint16_t));
                if(g->green)ulibc_memcpy(g_c4_drm_gamma_g,
                    (const void*)(uintptr_t)g->green,(size_t)n*sizeof(uint16_t));
                if(g->blue)ulibc_memcpy(g_c4_drm_gamma_b,
                    (const void*)(uintptr_t)g->blue,(size_t)n*sizeof(uint16_t));
            }
            g->gamma_size=C4_DRM_GAMMA_LUT_SIZE;
        }
        return 0;
    }
    if(request == DRM_IOCTL_MODE_PAGE_FLIP || request == DRM_IOCTL_MODE_DIRTYFB) {
        if(request == DRM_IOCTL_MODE_PAGE_FLIP&&!c4_drm_file_can_modeset(fd))return -EACCES;
        if(request == DRM_IOCTL_MODE_PAGE_FLIP && arg) {
            c4_drm_mode_page_flip_t *p=(c4_drm_mode_page_flip_t*)arg;
            bool presented=false;
            if((p->flags&C4_DRM_MODE_PAGE_FLIP_EVENT)&&drm_page_flip_inflight(fd))
                return -EBUSY;
            if(p->fb_id){
                g_c4_drm_current_fb_id=p->fb_id;
                c4_drm_trace_bo("page-flip",c4_drm_find_bo_by_fb(p->fb_id),p->fb_id,p->crtc_id);
                presented=c4_drm_present_fb_vbox_or_cpu(p->fb_id);
            }
            ++g_c4_drm_vblank_seq;
            if(c4_drm_present_trace_enabled()){
                static uint32_t legacy_flip_trace;
                if(legacy_flip_trace<96u){
                    c4_drm_bo_t *bo=c4_drm_find_bo_by_fb(p->fb_id);
                    ++legacy_flip_trace;
                    __boot_serial_force_puts("[drm-pageflip] #");
                    __boot_serial_force_putu32(legacy_flip_trace);
                    __boot_serial_force_puts(" fd=");
                    __boot_serial_force_putu32((uint32_t)fd);
                    __boot_serial_force_puts(" flags=");
                    __boot_serial_force_puthex64((uint64_t)p->flags);
                    __boot_serial_force_puts(" fb=");
                    __boot_serial_force_putu32(p->fb_id);
                    __boot_serial_force_puts(" bo=");
                    __boot_serial_force_putu32(bo?bo->handle:0);
                    __boot_serial_force_puts(" crtc=");
                    __boot_serial_force_putu32(p->crtc_id);
                    __boot_serial_force_puts(" event=");
                    __boot_serial_force_putu32((p->flags&C4_DRM_MODE_PAGE_FLIP_EVENT)?1u:0u);
                    __boot_serial_force_puts(" presented=");
                    __boot_serial_force_putu32(presented?1u:0u);
                    __boot_serial_force_puts(" user=");
                    __boot_serial_force_puthex64(p->user_data);
                    __boot_serial_force_puts("\n");
                }
            }
            if(p->flags&C4_DRM_MODE_PAGE_FLIP_EVENT)
                drm_queue_page_flip_event(fd,p->user_data,p->crtc_id);
        }else{
            c4_drm_present_fb_vbox_or_cpu(g_c4_drm_current_fb_id);
            ++g_c4_drm_vblank_seq;
        }
        return 0;
    }
    return -ENOTTY;
}

int fb_ioctl_handler(int fd, uint64_t request, void *arg) {
    (void)fd;
    if(request == FBIOGET_VSCREENINFO) {
        fb_var_screeninfo_t *v = (fb_var_screeninfo_t*)arg;
        ulibc_memset(v, 0, sizeof(*v));
        v->xres = g_fb.width; v->yres = g_fb.height;
        v->xres_virtual = g_fb.width; v->yres_virtual = g_fb.height;
        v->bits_per_pixel = 32;
        v->red_offset = 16; v->red_length = 8;
        v->green_offset = 8; v->green_length = 8;
        v->blue_offset = 0; v->blue_length = 8;
        v->transp_offset = 24; v->transp_length = 8;
        return 0;
    }
    if(request == FBIOPUT_VSCREENINFO) return 0;
    if(request == FBIOGET_FSCREENINFO) {
        fb_fix_screeninfo_t *f = (fb_fix_screeninfo_t*)arg;
        ulibc_memset(f, 0, sizeof(*f));
        ulibc_strcpy(f->id, "ridux-fb");
        f->smem_start = (uint64_t)(uintptr_t)g_fb.address;
        f->smem_len = g_fb.width * g_fb.height * 4;
        f->type = 0; /* FB_TYPE_PACKED_PIXELS */
        f->visual = 2; /* FB_VISUAL_TRUECOLOR */
        f->line_length = g_fb.pitch;
        return 0;
    }
    return -ENOTTY;
}

