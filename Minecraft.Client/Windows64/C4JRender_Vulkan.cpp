// ============================================================================
// C4JRender_Vulkan.cpp - Windows Vulkan backend for C4JRender.
// ============================================================================
#include "stdafx.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <wincodec.h>

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "VulkanTriangleShaders.inl"
#include "VulkanUIShaders.inl"
#include "VulkanUIBridge.h"

// ---- Global singleton ----
C4JRender RenderManager;

// ============================================================================
//  Internal state
// ============================================================================

static const int MATRIX_STACK_DEPTH = 32;
static const int NUM_MATRIX_MODES = 3; // modelview, projection, texture

struct MatrixStack {
  float stack[MATRIX_STACK_DEPTH][16];
  int top;
};

static thread_local MatrixStack g_matStacks[NUM_MATRIX_MODES];
static thread_local int g_curMatrixMode = 0; // GL_MODELVIEW
static thread_local bool g_matrixDirty = true;
static thread_local bool g_matrixStacksInitialised = false;
static thread_local float g_cachedMvp[16];
static thread_local bool g_cachedMvpValid = false;
static bool g_vkEnableDrawMerge = false;
static bool g_vkEnableMvpCache = false;

static void ensureThreadLocalMatrixStacksInitialised() {
  if (g_matrixStacksInitialised)
    return;
  for (int i = 0; i < NUM_MATRIX_MODES; ++i) {
    std::memset(g_matStacks[i].stack[0], 0, 16 * sizeof(float));
    g_matStacks[i].stack[0][0] = 1.0f;
    g_matStacks[i].stack[0][5] = 1.0f;
    g_matStacks[i].stack[0][10] = 1.0f;
    g_matStacks[i].stack[0][15] = 1.0f;
    g_matStacks[i].top = 0;
  }
  g_curMatrixMode = 0;
  g_matrixDirty = true;
  g_cachedMvpValid = false;
  g_matrixStacksInitialised = true;
}

// Vulkan objects
static VkInstance g_vkInstance = VK_NULL_HANDLE;
static VkPhysicalDevice g_vkPhysicalDevice = VK_NULL_HANDLE;
static VkDevice g_vkDevice = VK_NULL_HANDLE;
static VkQueue g_vkGraphicsQueue = VK_NULL_HANDLE;
static VkSurfaceKHR g_vkSurface = VK_NULL_HANDLE;
static HWND g_vkWindow = NULL;
static VkSwapchainKHR g_vkSwapchain = VK_NULL_HANDLE;
static VkRenderPass g_vkRenderPass = VK_NULL_HANDLE;
static VkPipelineLayout g_vkPipelineLayout = VK_NULL_HANDLE;
static VkPipeline g_vkTrianglePipeline = VK_NULL_HANDLE;
static std::unordered_map<uint64_t, VkPipeline> g_vkTrianglePipelines;
static VkDescriptorSetLayout g_vkTriangleDescriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool g_vkTriangleDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_vkUiDescriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool g_vkUiDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet g_vkUiDescriptorSet = VK_NULL_HANDLE;
static VkPipelineLayout g_vkUiPipelineLayout = VK_NULL_HANDLE;
static VkPipeline g_vkUiPipeline = VK_NULL_HANDLE;
static VkCommandPool g_vkCommandPool = VK_NULL_HANDLE;
static VkCommandBuffer g_vkCommandBuffer = VK_NULL_HANDLE;
static VkFence g_vkFence = VK_NULL_HANDLE;
static VkSemaphore g_vkImageAvailable = VK_NULL_HANDLE;
static VkSemaphore g_vkRenderFinished = VK_NULL_HANDLE;
static uint32_t g_vkGraphicsFamily = 0;
static std::vector<VkImage> g_vkSwapImages;
static std::vector<VkImageView> g_vkSwapImageViews;
static std::vector<VkFramebuffer> g_vkFramebuffers;
static std::vector<VkImageLayout> g_vkSwapImageLayouts;
static VkFormat g_vkSwapFormat = VK_FORMAT_B8G8R8A8_UNORM;
static VkExtent2D g_vkSwapExtent = {0, 0};
static VkImage g_vkDepthImage = VK_NULL_HANDLE;
static VkDeviceMemory g_vkDepthMemory = VK_NULL_HANDLE;
static VkImageView g_vkDepthImageView = VK_NULL_HANDLE;
static VkFormat g_vkDepthFormat = VK_FORMAT_UNDEFINED;
static VkBuffer g_vkDynamicVertexBuffer = VK_NULL_HANDLE;
static VkDeviceMemory g_vkDynamicVertexMemory = VK_NULL_HANDLE;
static size_t g_vkDynamicVertexCapacity = 0;
static bool g_vkDynamicVertexHostCoherent = true;
static uint8_t *g_vkDynamicVertexMapped = nullptr;
static VkBuffer g_vkUiStagingBuffer = VK_NULL_HANDLE;
static VkDeviceMemory g_vkUiStagingMemory = VK_NULL_HANDLE;
static size_t g_vkUiStagingCapacity = 0;
static bool g_vkUiStagingHostCoherent = true;
static uint8_t *g_vkUiStagingMapped = nullptr;
static VkImage g_vkUiImage = VK_NULL_HANDLE;
static VkDeviceMemory g_vkUiImageMemory = VK_NULL_HANDLE;
static VkImageView g_vkUiImageView = VK_NULL_HANDLE;
static VkSampler g_vkUiSampler = VK_NULL_HANDLE;
static VkImageLayout g_vkUiImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
static uint32_t g_vkUiImageWidth = 0;
static uint32_t g_vkUiImageHeight = 0;
static bool g_vkUiImageReady = false;
static bool g_vkInitialized = false;
static int g_nextCommandBufferId = 1;
static int g_nextTextureId = 1;

static const uint32_t kVertexStridePF3TF2CB4NB4XW1 = 32;
static const uint32_t kMaxTriangleTextureDescriptors = 4096;

struct TrianglePushConstants {
  float mvp[16];
  float alphaState[4]; // x=enable, y=ref, z=func, w=unused
};

struct VulkanTextureLevelData {
  uint32_t width;
  uint32_t height;
  std::vector<uint8_t> pixels;
  bool valid;
};

struct VulkanTexture {
  int id;
  uint32_t width;
  uint32_t height;
  uint32_t mipLevels;
  VkImage image;
  VkDeviceMemory memory;
  VkImageView imageView;
  VkSampler sampler;
  VkDescriptorSet descriptorSet;
  VkImageLayout imageLayout;
  int minFilter;
  int magFilter;
  int wrapS;
  int wrapT;
  uint32_t requestedMipLevels;
  bool pendingUpload;
  std::vector<VulkanTextureLevelData> levelData;
};

static std::unordered_map<int, VulkanTexture> g_vkTextures;
static VulkanTexture g_vkWhiteTexture = {};
static bool g_vkWhiteTextureReady = false;

struct VulkanQueuedDraw {
  VkBuffer vertexBuffer;
  uint32_t firstVertex;
  uint32_t vertexCount;
  float mvp[16];
  bool depthTestEnable;
  bool depthWriteEnable;
  VkCompareOp depthCompareOp;
  bool blendEnable;
  VkBlendFactor srcBlendFactor;
  VkBlendFactor dstBlendFactor;
  VkColorComponentFlags colorWriteMask;
  bool cullEnable;
  bool cullClockwise;
  float blendConstants[4];
  VkDescriptorSet descriptorSet;
  bool alphaTestEnable;
  int alphaFunc;
  float alphaRef;
};

static std::vector<uint8_t> g_vkFrameVertexData;
static std::vector<VulkanQueuedDraw> g_vkQueuedDraws;
static bool g_vkShowCornerOverlay = true;
static bool g_vkShowPerfOverlay = true;
static LARGE_INTEGER g_vkPerfFreq = {};
static LARGE_INTEGER g_vkPerfLastPresent = {};
static float g_vkPerfFpsSmoothed = 0.0f;
static uint32_t g_vkPerfDrawCount = 0;
static uint32_t g_vkPerfVertexCount = 0;
static uint32_t g_vkPerfUploadBytes = 0;
static size_t g_vkFrameVertexReserveBytes = 1u << 20;
static size_t g_vkFrameDrawReserveCount = 4096;

struct VulkanUiUploadFrame {
  std::vector<uint8_t> pixelsBGRA;
  uint32_t width;
  uint32_t height;
  bool pending;
};
static VulkanUiUploadFrame g_vkUiUpload = {};

static bool updateUiDescriptorSet();
static void destroyAllTextures(bool preserveCpuCache);

struct RecordedDrawCall {
  C4JRender::ePrimitiveType primitiveType;
  int count;
  C4JRender::eVertexType vType;
  C4JRender::ePixelShaderType psType;
  std::vector<uint8_t> vertexData;
  // Pre-expanded to BootstrapVertex layout (RGBA + triangle list) for fast replay.
  std::vector<uint8_t> preparedVertexData;
  uint32_t preparedVertexCount;
  uint32_t gpuFirstVertex;
  uint32_t gpuVertexCount;
  bool fullStateList;
  bool hasLocalModelMatrix;
  float localModelMatrix[16];
  bool useCapturedState;
  bool depthTestEnable;
  bool depthWriteEnable;
  VkCompareOp depthCompareOp;
  bool blendEnable;
  VkBlendFactor srcBlendFactor;
  VkBlendFactor dstBlendFactor;
  VkColorComponentFlags colorWriteMask;
  bool cullEnable;
  bool cullClockwise;
  float blendConstants[4];
  // texture state is tracked separately; replay should not clobber runtime binds.
  bool captureTextureState;
  int textureId;
  // same idea for alpha test state.
  bool captureAlphaState;
  bool alphaTestEnable;
  int alphaFunc;
  float alphaRef;
};

static std::unordered_map<int, std::shared_ptr<std::vector<RecordedDrawCall>>>
    g_vkCommandLists;
struct VulkanCommandListGpuData {
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexMemory;
  uint8_t *mapped;
  size_t capacityBytes;
  size_t usedBytes;
  bool hostCoherent;
  bool uploadPending;
};
static std::unordered_map<int, VulkanCommandListGpuData> g_vkCommandListGpuData;
static std::mutex g_vkCommandListsMutex;
static thread_local bool g_vkIsRecordingCommandList = false;
static thread_local int g_vkRecordingCommandListIndex = -1;
static thread_local std::vector<RecordedDrawCall> g_vkRecordingScratch;
static thread_local bool g_vkRecordingHasStateChanges = false;
static thread_local bool g_vkRecordingHasTextureStateChanges = false;
static thread_local bool g_vkRecordingHasAlphaStateChanges = false;
static thread_local bool g_vkRecordingFullStateList = false;
static thread_local bool g_vkRecordingBaseModelValid = false;
static thread_local float g_vkRecordingBaseModelInv[16];

struct BootstrapVertex {
  float x, y, z;
  float u, v;
  uint8_t r, g, b, a;
  uint8_t nx, ny, nz, nw;
  uint32_t pad;
};
static_assert(sizeof(BootstrapVertex) == kVertexStridePF3TF2CB4NB4XW1,
              "BootstrapVertex must be 32 bytes.");

static float g_clearColour[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static bool g_isWidescreen = true;
static thread_local bool g_vkStateDepthTestEnable = true;
static thread_local bool g_vkStateDepthWriteEnable = true;
static thread_local VkCompareOp g_vkStateDepthCompareOp =
    VK_COMPARE_OP_LESS_OR_EQUAL;
static thread_local bool g_vkStateBlendEnable = false;
static thread_local VkBlendFactor g_vkStateSrcBlendFactor = VK_BLEND_FACTOR_ONE;
static thread_local VkBlendFactor g_vkStateDstBlendFactor = VK_BLEND_FACTOR_ZERO;
static thread_local bool g_vkStateCullEnable = false;
static thread_local bool g_vkStateCullClockwise = true;
static thread_local VkColorComponentFlags g_vkStateColorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
static thread_local float g_vkStateColour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static thread_local float g_vkStateBlendConstants[4] = {1.0f, 1.0f, 1.0f,
                                                         1.0f};
static thread_local int g_vkStateTextureId = -1;
static thread_local int g_vkStateVertexTextureId = -1;
static thread_local bool g_vkStateAlphaTestEnable = false;
static thread_local int g_vkStateAlphaFunc = GL_ALWAYS;
static thread_local float g_vkStateAlphaRef = 0.0f;
static int g_vkPendingTextureLevels = 1;

static void debugVk(const char *msg) { OutputDebugStringA(msg); }

static bool getWindowClientSize(HWND hWnd, int &width, int &height) {
  width = 0;
  height = 0;
  if (hWnd == NULL)
    return false;
  RECT rect = {};
  if (!GetClientRect(hWnd, &rect))
    return false;
  width = rect.right - rect.left;
  height = rect.bottom - rect.top;
  return (width > 0 && height > 0);
}

static void debugVkResult(const char *what, VkResult result) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "C4JRender_Vulkan: %s (VkResult=%d)\n",
                what, (int)result);
  OutputDebugStringA(buffer);
}

static uint64_t makeTrianglePipelineKey(bool depthTestEnable,
                                        bool depthWriteEnable,
                                        VkCompareOp depthCompareOp,
                                        bool blendEnable,
                                        VkBlendFactor srcBlendFactor,
                                        VkBlendFactor dstBlendFactor,
                                        VkColorComponentFlags colorWriteMask,
                                        bool cullEnable,
                                        bool cullClockwise) {
  uint64_t key = 0;
  key |= (depthTestEnable ? 1ull : 0ull) << 0;
  key |= (depthWriteEnable ? 1ull : 0ull) << 1;
  key |= (blendEnable ? 1ull : 0ull) << 2;
  key |= (cullEnable ? 1ull : 0ull) << 3;
  key |= (cullClockwise ? 1ull : 0ull) << 4;
  key |= (static_cast<uint64_t>(depthCompareOp) & 0xffull) << 8;
  key |= (static_cast<uint64_t>(srcBlendFactor) & 0xffull) << 16;
  key |= (static_cast<uint64_t>(dstBlendFactor) & 0xffull) << 24;
  key |= (static_cast<uint64_t>(colorWriteMask) & 0xffull) << 32;
  return key;
}

static VkCompareOp mapDepthFuncToVk(int func) {
  switch (func) {
  case 1:
    return VK_COMPARE_OP_NEVER;
  case 2:
    return VK_COMPARE_OP_LESS;
  case 3:
    return VK_COMPARE_OP_EQUAL;
  case 4:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case 5:
    return VK_COMPARE_OP_GREATER;
  case 6:
    return VK_COMPARE_OP_NOT_EQUAL;
  case 7:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case 8:
    return VK_COMPARE_OP_ALWAYS;
  default:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  }
}

static VkBlendFactor mapBlendFactorToVk(int factor) {
  switch (factor) {
  case 1:
    return VK_BLEND_FACTOR_ZERO;
  case 2:
    return VK_BLEND_FACTOR_ONE;
  case 3:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case 4:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case 5:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case 6:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case 7:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case 8:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case 9:
    return VK_BLEND_FACTOR_DST_COLOR;
  case 10:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case 17:
    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
  case 18:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  default:
    return VK_BLEND_FACTOR_ONE;
  }
}

static void resetThreadLocalRenderState() {
  g_vkStateDepthTestEnable = true;
  g_vkStateDepthWriteEnable = true;
  g_vkStateDepthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  g_vkStateBlendEnable = false;
  g_vkStateSrcBlendFactor = VK_BLEND_FACTOR_ONE;
  g_vkStateDstBlendFactor = VK_BLEND_FACTOR_ZERO;
  g_vkStateCullEnable = false;
  g_vkStateCullClockwise = true;
  g_vkStateColorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;
  g_vkStateColour[0] = 1.0f;
  g_vkStateColour[1] = 1.0f;
  g_vkStateColour[2] = 1.0f;
  g_vkStateColour[3] = 1.0f;
  g_vkStateBlendConstants[0] = 1.0f;
  g_vkStateBlendConstants[1] = 1.0f;
  g_vkStateBlendConstants[2] = 1.0f;
  g_vkStateBlendConstants[3] = 1.0f;
  g_vkStateTextureId = -1;
  g_vkStateVertexTextureId = -1;
  g_vkStateAlphaTestEnable = false;
  g_vkStateAlphaFunc = GL_ALWAYS;
  g_vkStateAlphaRef = 0.0f;
  g_cachedMvpValid = false;
}

void VulkanSubmitIggyOverlayBGRA(int width, int height, const void *pixels,
                                 size_t bytes) {
  if (width <= 0 || height <= 0 || pixels == nullptr)
    return;
  const size_t expectedBytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  if (bytes < expectedBytes)
    return;

  g_vkUiUpload.width = static_cast<uint32_t>(width);
  g_vkUiUpload.height = static_cast<uint32_t>(height);
  g_vkUiUpload.pixelsBGRA.resize(expectedBytes);
  std::memcpy(g_vkUiUpload.pixelsBGRA.data(), pixels, expectedBytes);
  g_vkUiUpload.pending = true;
}

static bool hasValidationLayer(const char *layerName) {
  uint32_t layerCount = 0;
  VkResult res = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  if (res != VK_SUCCESS || layerCount == 0)
    return false;

  std::vector<VkLayerProperties> props(layerCount);
  res = vkEnumerateInstanceLayerProperties(&layerCount, props.data());
  if (res != VK_SUCCESS)
    return false;

  for (const auto &p : props) {
    if (std::strcmp(p.layerName, layerName) == 0)
      return true;
  }
  return false;
}

template <typename T> static void SafeReleaseCOM(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

static std::wstring toWidePath(const char *path) {
  if (!path)
    return std::wstring();

  int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
  UINT codePage = CP_UTF8;
  if (len <= 0) {
    codePage = CP_ACP;
    len = MultiByteToWideChar(codePage, 0, path, -1, nullptr, 0);
  }
  if (len <= 0)
    return std::wstring();

  std::wstring out(len, L'\0');
  MultiByteToWideChar(codePage, 0, path, -1, &out[0], len);
  if (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return out;
}

static HRESULT decodeFrameToArgb(IWICBitmapFrameDecode *frame,
                                 D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut) {
  if (!frame || !pSrcInfo || !ppDataOut)
    return E_INVALIDARG;

  UINT width = 0;
  UINT height = 0;
  HRESULT hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0)
    return FAILED(hr) ? hr : E_FAIL;

  IWICImagingFactory *factory = nullptr;
  IWICFormatConverter *converter = nullptr;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
  if (FAILED(hr))
    return hr;

  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    SafeReleaseCOM(factory);
    return hr;
  }

  hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0f,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    SafeReleaseCOM(converter);
    SafeReleaseCOM(factory);
    return hr;
  }

  const UINT stride = width * 4;
  const UINT byteCount = stride * height;
  std::vector<unsigned char> pixels(byteCount);
  hr = converter->CopyPixels(nullptr, stride, byteCount, pixels.data());
  if (FAILED(hr)) {
    SafeReleaseCOM(converter);
    SafeReleaseCOM(factory);
    return hr;
  }

  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  int *out = new int[pixelCount];
  for (size_t i = 0; i < pixelCount; ++i) {
    const unsigned char b = pixels[i * 4 + 0];
    const unsigned char g = pixels[i * 4 + 1];
    const unsigned char r = pixels[i * 4 + 2];
    const unsigned char a = pixels[i * 4 + 3];
    out[i] = (static_cast<int>(a) << 24) | (static_cast<int>(r) << 16) |
             (static_cast<int>(g) << 8) | static_cast<int>(b);
  }

  pSrcInfo->Width = static_cast<int>(width);
  pSrcInfo->Height = static_cast<int>(height);
  *ppDataOut = out;

  SafeReleaseCOM(converter);
  SafeReleaseCOM(factory);
  return S_OK;
}

static uint32_t findMemoryTypeIndex(uint32_t typeBits,
                                    VkMemoryPropertyFlags requiredProperties,
                                    bool &foundOut) {
  foundOut = false;
  if (g_vkPhysicalDevice == VK_NULL_HANDLE)
    return 0;

  VkPhysicalDeviceMemoryProperties memProps = {};
  vkGetPhysicalDeviceMemoryProperties(g_vkPhysicalDevice, &memProps);
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags & requiredProperties) ==
            requiredProperties) {
      foundOut = true;
      return i;
    }
  }
  return 0;
}

static bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D24_UNORM_S8_UINT ||
         format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static VkFormat chooseDepthFormat() {
  if (g_vkPhysicalDevice == VK_NULL_HANDLE)
    return VK_FORMAT_UNDEFINED;

  const VkFormat candidates[] = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM,
  };

  for (VkFormat format : candidates) {
    VkFormatProperties props = {};
    vkGetPhysicalDeviceFormatProperties(g_vkPhysicalDevice, format, &props);
    if (props.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return format;
    }
  }
  return VK_FORMAT_UNDEFINED;
}

static VkFilter mapTextureFilterToVk(int filter) {
  return (filter == GL_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerAddressMode mapTextureWrapToVk(int wrap) {
  return (wrap == GL_REPEAT) ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                             : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

static bool hasTextureUploadContext() {
  return g_vkInitialized && g_vkDevice != VK_NULL_HANDLE &&
         g_vkGraphicsQueue != VK_NULL_HANDLE &&
         g_vkCommandPool != VK_NULL_HANDLE &&
         g_vkTriangleDescriptorPool != VK_NULL_HANDLE &&
         g_vkTriangleDescriptorSetLayout != VK_NULL_HANDLE;
}

static void cacheTextureLevelPixels(VulkanTexture &tex, uint32_t level,
                                    uint32_t width, uint32_t height,
                                    const void *data) {
  if (data == nullptr || width == 0 || height == 0)
    return;
  if (tex.levelData.size() <= level) {
    tex.levelData.resize(level + 1);
  }
  VulkanTextureLevelData &dst = tex.levelData[level];
  dst.width = width;
  dst.height = height;
  const size_t bytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  dst.pixels.resize(bytes);
  std::memcpy(dst.pixels.data(), data, bytes);
  dst.valid = true;
  tex.pendingUpload = true;
}

static bool patchTextureLevelPixels(VulkanTexture &tex, uint32_t level,
                                    uint32_t xoffset, uint32_t yoffset,
                                    uint32_t width, uint32_t height,
                                    const void *data) {
  if (data == nullptr || width == 0 || height == 0)
    return false;
  if (level >= tex.levelData.size())
    return false;
  VulkanTextureLevelData &dst = tex.levelData[level];
  if (!dst.valid || dst.width == 0 || dst.height == 0)
    return false;
  if (xoffset + width > dst.width || yoffset + height > dst.height)
    return false;

  const uint8_t *src = static_cast<const uint8_t *>(data);
  uint8_t *dstBase = dst.pixels.data();
  const size_t srcRowBytes = static_cast<size_t>(width) * 4u;
  const size_t dstRowBytes = static_cast<size_t>(dst.width) * 4u;
  for (uint32_t y = 0; y < height; ++y) {
    uint8_t *dstRow = dstBase +
                      (static_cast<size_t>(yoffset + y) * dstRowBytes) +
                      static_cast<size_t>(xoffset) * 4u;
    const uint8_t *srcRow = src + static_cast<size_t>(y) * srcRowBytes;
    std::memcpy(dstRow, srcRow, srcRowBytes);
  }
  tex.pendingUpload = true;
  return true;
}

static bool computeTextureDimensionsFromCache(const VulkanTexture &tex,
                                              uint32_t &baseWidthOut,
                                              uint32_t &baseHeightOut,
                                              uint32_t &mipLevelsOut) {
  bool haveAny = false;
  uint32_t baseWidth = 0;
  uint32_t baseHeight = 0;
  uint32_t maxLevel = 0;
  for (uint32_t level = 0; level < static_cast<uint32_t>(tex.levelData.size());
       ++level) {
    const VulkanTextureLevelData &ld = tex.levelData[level];
    if (!ld.valid || ld.width == 0 || ld.height == 0)
      continue;
    uint32_t candidateWidth = ld.width;
    uint32_t candidateHeight = ld.height;
    if (level > 0) {
      candidateWidth <<= level;
      candidateHeight <<= level;
    }
    if (!haveAny) {
      baseWidth = candidateWidth;
      baseHeight = candidateHeight;
      haveAny = true;
    } else {
      if (candidateWidth > baseWidth)
        baseWidth = candidateWidth;
      if (candidateHeight > baseHeight)
        baseHeight = candidateHeight;
    }
    if (level > maxLevel)
      maxLevel = level;
  }
  if (!haveAny || baseWidth == 0 || baseHeight == 0)
    return false;

  uint32_t levels = maxLevel + 1;
  if (tex.requestedMipLevels > levels)
    levels = tex.requestedMipLevels;

  baseWidthOut = baseWidth;
  baseHeightOut = baseHeight;
  mipLevelsOut = levels;
  return true;
}

static bool allocateTextureDescriptorSetIfNeeded(VulkanTexture &tex) {
  if (tex.descriptorSet != VK_NULL_HANDLE)
    return true;
  if (g_vkDevice == VK_NULL_HANDLE ||
      g_vkTriangleDescriptorPool == VK_NULL_HANDLE ||
      g_vkTriangleDescriptorSetLayout == VK_NULL_HANDLE)
    return false;

  VkDescriptorSetAllocateInfo dsAI = {};
  dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsAI.descriptorPool = g_vkTriangleDescriptorPool;
  dsAI.descriptorSetCount = 1;
  dsAI.pSetLayouts = &g_vkTriangleDescriptorSetLayout;
  VkResult result = vkAllocateDescriptorSets(g_vkDevice, &dsAI, &tex.descriptorSet);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to allocate triangle texture descriptor set", result);
    tex.descriptorSet = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

static bool updateTextureDescriptorSet(VulkanTexture &tex) {
  if (!allocateTextureDescriptorSetIfNeeded(tex))
    return false;
  if (g_vkDevice == VK_NULL_HANDLE || tex.descriptorSet == VK_NULL_HANDLE ||
      tex.imageView == VK_NULL_HANDLE || tex.sampler == VK_NULL_HANDLE ||
      tex.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return false;
  }

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.sampler = tex.sampler;
  imageInfo.imageView = tex.imageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = tex.descriptorSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(g_vkDevice, 1, &write, 0, nullptr);
  return true;
}

static void destroyTextureGpuResources(VulkanTexture &tex) {
  if (g_vkDevice != VK_NULL_HANDLE && tex.descriptorSet != VK_NULL_HANDLE &&
      g_vkTriangleDescriptorPool != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(g_vkDevice, g_vkTriangleDescriptorPool, 1,
                         &tex.descriptorSet);
  }
  tex.descriptorSet = VK_NULL_HANDLE;

  if (g_vkDevice != VK_NULL_HANDLE && tex.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(g_vkDevice, tex.sampler, nullptr);
  }
  tex.sampler = VK_NULL_HANDLE;

  if (g_vkDevice != VK_NULL_HANDLE && tex.imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(g_vkDevice, tex.imageView, nullptr);
  }
  tex.imageView = VK_NULL_HANDLE;

  if (g_vkDevice != VK_NULL_HANDLE && tex.image != VK_NULL_HANDLE) {
    vkDestroyImage(g_vkDevice, tex.image, nullptr);
  }
  tex.image = VK_NULL_HANDLE;

  if (g_vkDevice != VK_NULL_HANDLE && tex.memory != VK_NULL_HANDLE) {
    vkFreeMemory(g_vkDevice, tex.memory, nullptr);
  }
  tex.memory = VK_NULL_HANDLE;
  tex.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  tex.width = 0;
  tex.height = 0;
  tex.mipLevels = 1;
}

static bool recreateTextureSampler(VulkanTexture &tex) {
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;

  if (tex.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(g_vkDevice, tex.sampler, nullptr);
    tex.sampler = VK_NULL_HANDLE;
  }

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = mapTextureFilterToVk(tex.magFilter);
  samplerCI.minFilter = mapTextureFilterToVk(tex.minFilter);
  samplerCI.mipmapMode =
      (tex.minFilter == GL_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                   : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerCI.addressModeU = mapTextureWrapToVk(tex.wrapS);
  samplerCI.addressModeV = mapTextureWrapToVk(tex.wrapT);
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.mipLodBias = 0.0f;
  samplerCI.anisotropyEnable = VK_FALSE;
  samplerCI.maxAnisotropy = 1.0f;
  samplerCI.compareEnable = VK_FALSE;
  samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCI.minLod = 0.0f;
  samplerCI.maxLod = (tex.mipLevels > 1) ? static_cast<float>(tex.mipLevels - 1)
                                         : 0.0f;
  samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerCI.unnormalizedCoordinates = VK_FALSE;

  VkResult result = vkCreateSampler(g_vkDevice, &samplerCI, nullptr, &tex.sampler);
  if (result != VK_SUCCESS || tex.sampler == VK_NULL_HANDLE) {
    debugVkResult("Failed to create texture sampler", result);
    tex.sampler = VK_NULL_HANDLE;
    return false;
  }

  if (tex.imageView != VK_NULL_HANDLE &&
      tex.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return updateTextureDescriptorSet(tex);
  }
  return true;
}

static bool createTextureImageAndView(VulkanTexture &tex, uint32_t width,
                                      uint32_t height, uint32_t mipLevels) {
  if (g_vkDevice == VK_NULL_HANDLE || width == 0 || height == 0 ||
      mipLevels == 0)
    return false;

  destroyTextureGpuResources(tex);

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageCI.extent = {width, height, 1};
  imageCI.mipLevels = mipLevels;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkResult result = vkCreateImage(g_vkDevice, &imageCI, nullptr, &tex.image);
  if (result != VK_SUCCESS || tex.image == VK_NULL_HANDLE) {
    debugVkResult("Failed to create texture image", result);
    destroyTextureGpuResources(tex);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetImageMemoryRequirements(g_vkDevice, tex.image, &memReq);

  bool foundMemory = false;
  const uint32_t memoryTypeIndex = findMemoryTypeIndex(
      memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, foundMemory);
  if (!foundMemory) {
    debugVk("C4JRender_Vulkan: No device-local memory type for texture image.\n");
    destroyTextureGpuResources(tex);
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &tex.memory);
  if (result != VK_SUCCESS || tex.memory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate texture image memory", result);
    destroyTextureGpuResources(tex);
    return false;
  }

  result = vkBindImageMemory(g_vkDevice, tex.image, tex.memory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind texture image memory", result);
    destroyTextureGpuResources(tex);
    return false;
  }

  VkImageViewCreateInfo viewCI = {};
  viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCI.image = tex.image;
  viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewCI.subresourceRange.baseMipLevel = 0;
  viewCI.subresourceRange.levelCount = mipLevels;
  viewCI.subresourceRange.baseArrayLayer = 0;
  viewCI.subresourceRange.layerCount = 1;
  result = vkCreateImageView(g_vkDevice, &viewCI, nullptr, &tex.imageView);
  if (result != VK_SUCCESS || tex.imageView == VK_NULL_HANDLE) {
    debugVkResult("Failed to create texture image view", result);
    destroyTextureGpuResources(tex);
    return false;
  }

  tex.width = width;
  tex.height = height;
  tex.mipLevels = mipLevels;
  tex.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!recreateTextureSampler(tex))
    return false;
  return true;
}

static bool uploadTextureRegionImmediate(VulkanTexture &tex, uint32_t level,
                                         uint32_t xoffset, uint32_t yoffset,
                                         uint32_t width, uint32_t height,
                                         const void *data);

static bool ensureTextureUploadedFromCache(VulkanTexture &tex) {
  if (!tex.pendingUpload)
    return true;
  if (!hasTextureUploadContext())
    return false;

  uint32_t baseWidth = 0;
  uint32_t baseHeight = 0;
  uint32_t mipLevels = 1;
  if (!computeTextureDimensionsFromCache(tex, baseWidth, baseHeight, mipLevels))
    return false;

  const bool needsRecreate = (tex.image == VK_NULL_HANDLE) ||
                             (tex.width != baseWidth) ||
                             (tex.height != baseHeight) ||
                             (tex.mipLevels != mipLevels);
  if (needsRecreate) {
    if (!createTextureImageAndView(tex, baseWidth, baseHeight, mipLevels))
      return false;
  }

  for (uint32_t level = 0; level < static_cast<uint32_t>(tex.levelData.size());
       ++level) {
    const VulkanTextureLevelData &ld = tex.levelData[level];
    if (!ld.valid || ld.pixels.empty())
      continue;
    if (level >= tex.mipLevels)
      continue;
    if (!uploadTextureRegionImmediate(tex, level, 0, 0, ld.width, ld.height,
                                      ld.pixels.data())) {
      return false;
    }
  }

  tex.pendingUpload = false;
  return true;
}

static void processPendingTextureUploads() {
  if (!hasTextureUploadContext())
    return;
  int uploadsThisFrame = 0;
  const int kMaxUploadsPerFrame = 1;
  for (auto &kv : g_vkTextures) {
    VulkanTexture &tex = kv.second;
    if (tex.pendingUpload) {
      ensureTextureUploadedFromCache(tex);
      ++uploadsThisFrame;
      if (uploadsThisFrame >= kMaxUploadsPerFrame) {
        break;
      }
    }
  }
}

static bool uploadTextureRegionImmediate(VulkanTexture &tex, uint32_t level,
                                         uint32_t xoffset, uint32_t yoffset,
                                         uint32_t width, uint32_t height,
                                         const void *data) {
  if (g_vkDevice == VK_NULL_HANDLE || g_vkGraphicsQueue == VK_NULL_HANDLE ||
      g_vkCommandPool == VK_NULL_HANDLE || tex.image == VK_NULL_HANDLE ||
      data == nullptr || width == 0 || height == 0 ||
      level >= tex.mipLevels) {
    return false;
  }

  const size_t uploadBytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  if (uploadBytes == 0)
    return false;

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  bool stagingHostCoherent = true;

  VkBufferCreateInfo bufCI = {};
  bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufCI.size = static_cast<VkDeviceSize>(uploadBytes);
  bufCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result = vkCreateBuffer(g_vkDevice, &bufCI, nullptr, &stagingBuffer);
  if (result != VK_SUCCESS || stagingBuffer == VK_NULL_HANDLE) {
    debugVkResult("Failed to create texture staging buffer", result);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetBufferMemoryRequirements(g_vkDevice, stagingBuffer, &memReq);

  bool foundMemory = false;
  VkMemoryPropertyFlags memFlags =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  uint32_t memoryTypeIndex =
      findMemoryTypeIndex(memReq.memoryTypeBits, memFlags, foundMemory);
  if (!foundMemory) {
    memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memoryTypeIndex =
        findMemoryTypeIndex(memReq.memoryTypeBits, memFlags, foundMemory);
    stagingHostCoherent = false;
  }
  if (!foundMemory) {
    debugVk("C4JRender_Vulkan: No host-visible memory type for texture staging.\n");
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &stagingMemory);
  if (result != VK_SUCCESS || stagingMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate texture staging memory", result);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  result = vkBindBufferMemory(g_vkDevice, stagingBuffer, stagingMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind texture staging memory", result);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  void *mapped = nullptr;
  result = vkMapMemory(g_vkDevice, stagingMemory, 0, uploadBytes, 0, &mapped);
  if (result != VK_SUCCESS || mapped == nullptr) {
    debugVkResult("Failed to map texture staging memory", result);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }
  std::memcpy(mapped, data, uploadBytes);
  if (!stagingHostCoherent) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = stagingMemory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    result = vkFlushMappedMemoryRanges(g_vkDevice, 1, &range);
    if (result != VK_SUCCESS) {
      debugVkResult("Failed to flush texture staging memory", result);
      vkUnmapMemory(g_vkDevice, stagingMemory);
      vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
      vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
      return false;
    }
  }
  vkUnmapMemory(g_vkDevice, stagingMemory);

  VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbAI = {};
  cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbAI.commandPool = g_vkCommandPool;
  cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbAI.commandBufferCount = 1;
  result = vkAllocateCommandBuffers(g_vkDevice, &cbAI, &uploadCmd);
  if (result != VK_SUCCESS || uploadCmd == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate texture upload command buffer", result);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(uploadCmd, &beginInfo);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to begin texture upload command buffer", result);
    vkFreeCommandBuffers(g_vkDevice, g_vkCommandPool, 1, &uploadCmd);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  VkImageMemoryBarrier toTransfer = {};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = tex.imageLayout;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = tex.image;
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.baseMipLevel = level;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.baseArrayLayer = 0;
  toTransfer.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (tex.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (tex.imageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  } else {
    toTransfer.srcAccessMask = 0;
  }
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(uploadCmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &toTransfer);

  VkBufferImageCopy copyRegion = {};
  copyRegion.bufferOffset = 0;
  copyRegion.bufferRowLength = 0;
  copyRegion.bufferImageHeight = 0;
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.mipLevel = level;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageOffset = {static_cast<int32_t>(xoffset),
                            static_cast<int32_t>(yoffset), 0};
  copyRegion.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(uploadCmd, stagingBuffer, tex.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

  VkImageMemoryBarrier toRead = {};
  toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.image = tex.image;
  toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toRead.subresourceRange.baseMipLevel = level;
  toRead.subresourceRange.levelCount = 1;
  toRead.subresourceRange.baseArrayLayer = 0;
  toRead.subresourceRange.layerCount = 1;
  toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(uploadCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toRead);

  result = vkEndCommandBuffer(uploadCmd);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to end texture upload command buffer", result);
    vkFreeCommandBuffers(g_vkDevice, g_vkCommandPool, 1, &uploadCmd);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &uploadCmd;
  result = vkQueueSubmit(g_vkGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to submit texture upload", result);
    vkFreeCommandBuffers(g_vkDevice, g_vkCommandPool, 1, &uploadCmd);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  result = vkQueueWaitIdle(g_vkGraphicsQueue);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed waiting for texture upload queue idle", result);
    vkFreeCommandBuffers(g_vkDevice, g_vkCommandPool, 1, &uploadCmd);
    vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
    vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);
    return false;
  }

  vkFreeCommandBuffers(g_vkDevice, g_vkCommandPool, 1, &uploadCmd);
  vkFreeMemory(g_vkDevice, stagingMemory, nullptr);
  vkDestroyBuffer(g_vkDevice, stagingBuffer, nullptr);

  tex.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  updateTextureDescriptorSet(tex);
  return true;
}

static VkDescriptorSet resolveTextureDescriptorSet(int textureId) {
  if (textureId >= 0) {
    auto it = g_vkTextures.find(textureId);
    if (it != g_vkTextures.end()) {
      VulkanTexture &tex = it->second;
      if (tex.pendingUpload) {
        ensureTextureUploadedFromCache(tex);
      } else if (tex.descriptorSet == VK_NULL_HANDLE &&
                 tex.image != VK_NULL_HANDLE && hasTextureUploadContext()) {
        updateTextureDescriptorSet(tex);
      }
      if (tex.descriptorSet != VK_NULL_HANDLE) {
        return tex.descriptorSet;
      }
    }
  }
  if (g_vkWhiteTextureReady &&
      g_vkWhiteTexture.descriptorSet != VK_NULL_HANDLE) {
    return g_vkWhiteTexture.descriptorSet;
  }
  return VK_NULL_HANDLE;
}

static bool ensureWhiteTexture() {
  if (g_vkWhiteTextureReady &&
      g_vkWhiteTexture.descriptorSet != VK_NULL_HANDLE) {
    return true;
  }
  g_vkWhiteTexture.id = 0;
  g_vkWhiteTexture.width = 0;
  g_vkWhiteTexture.height = 0;
  g_vkWhiteTexture.mipLevels = 1;
  g_vkWhiteTexture.image = VK_NULL_HANDLE;
  g_vkWhiteTexture.memory = VK_NULL_HANDLE;
  g_vkWhiteTexture.imageView = VK_NULL_HANDLE;
  g_vkWhiteTexture.sampler = VK_NULL_HANDLE;
  g_vkWhiteTexture.descriptorSet = VK_NULL_HANDLE;
  g_vkWhiteTexture.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  g_vkWhiteTexture.minFilter = GL_NEAREST;
  g_vkWhiteTexture.magFilter = GL_NEAREST;
  g_vkWhiteTexture.wrapS = GL_REPEAT;
  g_vkWhiteTexture.wrapT = GL_REPEAT;
  g_vkWhiteTexture.requestedMipLevels = 1;
  g_vkWhiteTexture.pendingUpload = false;
  g_vkWhiteTexture.levelData.clear();
  if (!createTextureImageAndView(g_vkWhiteTexture, 1, 1, 1))
    return false;
  const uint8_t white[4] = {255, 255, 255, 255};
  if (!uploadTextureRegionImmediate(g_vkWhiteTexture, 0, 0, 0, 1, 1, white))
    return false;
  g_vkWhiteTextureReady = true;
  return true;
}

static void destroyAllTextures(bool preserveCpuCache) {
  for (auto it = g_vkTextures.begin(); it != g_vkTextures.end();) {
    destroyTextureGpuResources(it->second);
    if (preserveCpuCache) {
      bool hasCachedData = false;
      for (const auto &level : it->second.levelData) {
        if (level.valid && !level.pixels.empty()) {
          hasCachedData = true;
          break;
        }
      }
      it->second.pendingUpload = hasCachedData;
      ++it;
    } else {
      it = g_vkTextures.erase(it);
    }
  }
  destroyTextureGpuResources(g_vkWhiteTexture);
  g_vkWhiteTexture = {};
  g_vkWhiteTextureReady = false;
  if (!preserveCpuCache) {
    g_vkPendingTextureLevels = 1;
  }
}

static void destroyDepthResources() {
  if (g_vkDevice == VK_NULL_HANDLE)
    return;
  if (g_vkDepthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(g_vkDevice, g_vkDepthImageView, nullptr);
    g_vkDepthImageView = VK_NULL_HANDLE;
  }
  if (g_vkDepthImage != VK_NULL_HANDLE) {
    vkDestroyImage(g_vkDevice, g_vkDepthImage, nullptr);
    g_vkDepthImage = VK_NULL_HANDLE;
  }
  if (g_vkDepthMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g_vkDevice, g_vkDepthMemory, nullptr);
    g_vkDepthMemory = VK_NULL_HANDLE;
  }
  g_vkDepthFormat = VK_FORMAT_UNDEFINED;
}

static bool createDepthResources(uint32_t width, uint32_t height) {
  if (g_vkDevice == VK_NULL_HANDLE || width == 0 || height == 0)
    return false;

  destroyDepthResources();

  g_vkDepthFormat = chooseDepthFormat();
  if (g_vkDepthFormat == VK_FORMAT_UNDEFINED) {
    debugVk("C4JRender_Vulkan: No supported depth format.\n");
    return false;
  }

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = g_vkDepthFormat;
  imageCI.extent = {width, height, 1};
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkResult result = vkCreateImage(g_vkDevice, &imageCI, nullptr, &g_vkDepthImage);
  if (result != VK_SUCCESS || g_vkDepthImage == VK_NULL_HANDLE) {
    debugVkResult("Failed to create depth image", result);
    destroyDepthResources();
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetImageMemoryRequirements(g_vkDevice, g_vkDepthImage, &memReq);

  bool found = false;
  uint32_t memoryTypeIndex = findMemoryTypeIndex(
      memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, found);
  if (!found) {
    debugVk("C4JRender_Vulkan: No device-local memory type for depth image.\n");
    destroyDepthResources();
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &g_vkDepthMemory);
  if (result != VK_SUCCESS || g_vkDepthMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate depth image memory", result);
    destroyDepthResources();
    return false;
  }

  result = vkBindImageMemory(g_vkDevice, g_vkDepthImage, g_vkDepthMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind depth image memory", result);
    destroyDepthResources();
    return false;
  }

  VkImageViewCreateInfo viewCI = {};
  viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCI.image = g_vkDepthImage;
  viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCI.format = g_vkDepthFormat;
  viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  if (hasStencilComponent(g_vkDepthFormat))
    viewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  viewCI.subresourceRange.baseMipLevel = 0;
  viewCI.subresourceRange.levelCount = 1;
  viewCI.subresourceRange.baseArrayLayer = 0;
  viewCI.subresourceRange.layerCount = 1;
  result = vkCreateImageView(g_vkDevice, &viewCI, nullptr, &g_vkDepthImageView);
  if (result != VK_SUCCESS || g_vkDepthImageView == VK_NULL_HANDLE) {
    debugVkResult("Failed to create depth image view", result);
    destroyDepthResources();
    return false;
  }

  return true;
}

static void destroyDynamicVertexBuffer() {
  if (g_vkDevice == VK_NULL_HANDLE)
    return;
  if (g_vkDynamicVertexMemory != VK_NULL_HANDLE && g_vkDynamicVertexMapped != nullptr) {
    vkUnmapMemory(g_vkDevice, g_vkDynamicVertexMemory);
    g_vkDynamicVertexMapped = nullptr;
  }
  if (g_vkDynamicVertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(g_vkDevice, g_vkDynamicVertexBuffer, nullptr);
    g_vkDynamicVertexBuffer = VK_NULL_HANDLE;
  }
  if (g_vkDynamicVertexMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g_vkDevice, g_vkDynamicVertexMemory, nullptr);
    g_vkDynamicVertexMemory = VK_NULL_HANDLE;
  }
  g_vkDynamicVertexCapacity = 0;
  g_vkDynamicVertexHostCoherent = true;
}

static void destroyCommandListGpuBuffer(VulkanCommandListGpuData &gpuData) {
  if (g_vkDevice != VK_NULL_HANDLE) {
    if (gpuData.vertexMemory != VK_NULL_HANDLE && gpuData.mapped != nullptr) {
      vkUnmapMemory(g_vkDevice, gpuData.vertexMemory);
    }
    if (gpuData.vertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(g_vkDevice, gpuData.vertexBuffer, nullptr);
    }
    if (gpuData.vertexMemory != VK_NULL_HANDLE) {
      vkFreeMemory(g_vkDevice, gpuData.vertexMemory, nullptr);
    }
  }
  gpuData.vertexBuffer = VK_NULL_HANDLE;
  gpuData.vertexMemory = VK_NULL_HANDLE;
  gpuData.mapped = nullptr;
  gpuData.capacityBytes = 0;
  gpuData.usedBytes = 0;
  gpuData.hostCoherent = true;
  gpuData.uploadPending = false;
}

static void destroyAllCommandListGpuBuffers() {
  for (auto &kv : g_vkCommandListGpuData) {
    destroyCommandListGpuBuffer(kv.second);
  }
  g_vkCommandListGpuData.clear();
}

static bool ensureCommandListGpuBufferCapacity(VulkanCommandListGpuData &gpuData,
                                               size_t minBytes) {
  if (minBytes == 0)
    return true;
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;
  if (gpuData.vertexBuffer != VK_NULL_HANDLE && gpuData.capacityBytes >= minBytes)
    return true;

  size_t newCapacity = 1u << 20; // 1MB default for chunk command lists.
  if (newCapacity < minBytes)
    newCapacity = minBytes;
  if (gpuData.capacityBytes > 0 && newCapacity < gpuData.capacityBytes * 2) {
    newCapacity = gpuData.capacityBytes * 2;
    if (newCapacity < minBytes)
      newCapacity = minBytes;
  }

  destroyCommandListGpuBuffer(gpuData);

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = static_cast<VkDeviceSize>(newCapacity);
  bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result =
      vkCreateBuffer(g_vkDevice, &bufferCI, nullptr, &gpuData.vertexBuffer);
  if (result != VK_SUCCESS || gpuData.vertexBuffer == VK_NULL_HANDLE) {
    debugVkResult("Failed to create command-list vertex buffer", result);
    destroyCommandListGpuBuffer(gpuData);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetBufferMemoryRequirements(g_vkDevice, gpuData.vertexBuffer, &memReq);

  bool found = false;
  uint32_t memoryTypeIndex = findMemoryTypeIndex(
      memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      found);
  gpuData.hostCoherent = true;
  if (!found) {
    memoryTypeIndex = findMemoryTypeIndex(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, found);
    gpuData.hostCoherent = false;
  }
  if (!found) {
    debugVk("C4JRender_Vulkan: No host-visible memory for command-list buffer.\n");
    destroyCommandListGpuBuffer(gpuData);
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &gpuData.vertexMemory);
  if (result != VK_SUCCESS || gpuData.vertexMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate command-list vertex memory", result);
    destroyCommandListGpuBuffer(gpuData);
    return false;
  }

  result =
      vkBindBufferMemory(g_vkDevice, gpuData.vertexBuffer, gpuData.vertexMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind command-list vertex memory", result);
    destroyCommandListGpuBuffer(gpuData);
    return false;
  }

  void *mapped = nullptr;
  result = vkMapMemory(g_vkDevice, gpuData.vertexMemory, 0, VK_WHOLE_SIZE, 0,
                       &mapped);
  if (result != VK_SUCCESS || mapped == nullptr) {
    debugVkResult("Failed to map command-list vertex memory", result);
    destroyCommandListGpuBuffer(gpuData);
    return false;
  }

  gpuData.mapped = static_cast<uint8_t *>(mapped);
  gpuData.capacityBytes = newCapacity;
  gpuData.usedBytes = 0;
  gpuData.uploadPending = false;
  return true;
}

static bool uploadCommandListGpuData(
    int index, const std::shared_ptr<std::vector<RecordedDrawCall>> &calls) {
  if (!calls)
    return false;
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;

  size_t totalBytes = 0;
  for (RecordedDrawCall &call : *calls) {
    call.gpuFirstVertex = 0;
    call.gpuVertexCount = 0;
    if (call.preparedVertexCount == 0 || call.preparedVertexData.empty())
      continue;
    totalBytes += call.preparedVertexData.size();
  }

  auto it = g_vkCommandListGpuData.find(index);
  if (totalBytes == 0) {
    if (it != g_vkCommandListGpuData.end()) {
      destroyCommandListGpuBuffer(it->second);
      g_vkCommandListGpuData.erase(it);
    }
    return true;
  }

  VulkanCommandListGpuData &gpuData = g_vkCommandListGpuData[index];
  if (!ensureCommandListGpuBufferCapacity(gpuData, totalBytes))
    return false;
  if (gpuData.mapped == nullptr)
    return false;

  size_t offsetBytes = 0;
  for (RecordedDrawCall &call : *calls) {
    if (call.preparedVertexCount == 0 || call.preparedVertexData.empty())
      continue;
    const size_t bytes = call.preparedVertexData.size();
    if (offsetBytes + bytes > gpuData.capacityBytes)
      return false;
    std::memcpy(gpuData.mapped + offsetBytes, call.preparedVertexData.data(),
                bytes);
    call.gpuFirstVertex =
        static_cast<uint32_t>(offsetBytes / kVertexStridePF3TF2CB4NB4XW1);
    call.gpuVertexCount = call.preparedVertexCount;
    offsetBytes += bytes;
  }

  if (!gpuData.hostCoherent) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = gpuData.vertexMemory;
    range.offset = 0;
    range.size = static_cast<VkDeviceSize>(offsetBytes);
    VkResult result = vkFlushMappedMemoryRanges(g_vkDevice, 1, &range);
    if (result != VK_SUCCESS) {
      debugVkResult("Failed to flush command-list vertex memory", result);
      return false;
    }
  }

  gpuData.usedBytes = offsetBytes;
  gpuData.uploadPending = false;
  return true;
}

static bool ensureDynamicVertexBuffer(size_t minBytes) {
  if (minBytes == 0)
    return true;
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;
  if (g_vkDynamicVertexBuffer != VK_NULL_HANDLE &&
      g_vkDynamicVertexCapacity >= minBytes) {
    return true;
  }

  size_t newCapacity = 1u << 20; // 1MB default.
  if (newCapacity < minBytes)
    newCapacity = minBytes;
  if (g_vkDynamicVertexCapacity > 0 &&
      newCapacity < g_vkDynamicVertexCapacity * 2) {
    newCapacity = g_vkDynamicVertexCapacity * 2;
    if (newCapacity < minBytes)
      newCapacity = minBytes;
  }

  destroyDynamicVertexBuffer();

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = static_cast<VkDeviceSize>(newCapacity);
  bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result =
      vkCreateBuffer(g_vkDevice, &bufferCI, nullptr, &g_vkDynamicVertexBuffer);
  if (result != VK_SUCCESS || g_vkDynamicVertexBuffer == VK_NULL_HANDLE) {
    debugVkResult("Failed to create dynamic vertex buffer", result);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetBufferMemoryRequirements(g_vkDevice, g_vkDynamicVertexBuffer, &memReq);

  bool found = false;
  uint32_t memoryTypeIndex =
      findMemoryTypeIndex(memReq.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          found);
  g_vkDynamicVertexHostCoherent = true;
  if (!found) {
    memoryTypeIndex = findMemoryTypeIndex(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, found);
    g_vkDynamicVertexHostCoherent = false;
  }
  if (!found) {
    debugVk("C4JRender_Vulkan: No host-visible memory type for vertex buffer.\n");
    destroyDynamicVertexBuffer();
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &g_vkDynamicVertexMemory);
  if (result != VK_SUCCESS || g_vkDynamicVertexMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate dynamic vertex buffer memory", result);
    destroyDynamicVertexBuffer();
    return false;
  }

  result = vkBindBufferMemory(g_vkDevice, g_vkDynamicVertexBuffer,
                              g_vkDynamicVertexMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind dynamic vertex buffer memory", result);
    destroyDynamicVertexBuffer();
    return false;
  }

  g_vkDynamicVertexCapacity = newCapacity;
  void *mapped = nullptr;
  result = vkMapMemory(g_vkDevice, g_vkDynamicVertexMemory, 0, VK_WHOLE_SIZE, 0,
                       &mapped);
  if (result != VK_SUCCESS || mapped == nullptr) {
    debugVkResult("Failed to map dynamic vertex buffer memory", result);
    destroyDynamicVertexBuffer();
    return false;
  }
  g_vkDynamicVertexMapped = static_cast<uint8_t *>(mapped);
  return true;
}

static void destroyUiStagingBuffer() {
  if (g_vkDevice == VK_NULL_HANDLE)
    return;
  if (g_vkUiStagingMemory != VK_NULL_HANDLE && g_vkUiStagingMapped != nullptr) {
    vkUnmapMemory(g_vkDevice, g_vkUiStagingMemory);
    g_vkUiStagingMapped = nullptr;
  }
  if (g_vkUiStagingBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(g_vkDevice, g_vkUiStagingBuffer, nullptr);
    g_vkUiStagingBuffer = VK_NULL_HANDLE;
  }
  if (g_vkUiStagingMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g_vkDevice, g_vkUiStagingMemory, nullptr);
    g_vkUiStagingMemory = VK_NULL_HANDLE;
  }
  g_vkUiStagingCapacity = 0;
  g_vkUiStagingHostCoherent = true;
}

static bool ensureUiStagingBuffer(size_t minBytes) {
  if (minBytes == 0)
    return true;
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;
  if (g_vkUiStagingBuffer != VK_NULL_HANDLE && g_vkUiStagingCapacity >= minBytes)
    return true;

  size_t newCapacity = 1u << 20; // 1MB default.
  if (newCapacity < minBytes)
    newCapacity = minBytes;

  destroyUiStagingBuffer();

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = static_cast<VkDeviceSize>(newCapacity);
  bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result =
      vkCreateBuffer(g_vkDevice, &bufferCI, nullptr, &g_vkUiStagingBuffer);
  if (result != VK_SUCCESS || g_vkUiStagingBuffer == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI staging buffer", result);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetBufferMemoryRequirements(g_vkDevice, g_vkUiStagingBuffer, &memReq);

  bool found = false;
  uint32_t memoryTypeIndex =
      findMemoryTypeIndex(memReq.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          found);
  g_vkUiStagingHostCoherent = true;
  if (!found) {
    memoryTypeIndex = findMemoryTypeIndex(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, found);
    g_vkUiStagingHostCoherent = false;
  }
  if (!found) {
    debugVk("C4JRender_Vulkan: No host-visible memory type for UI staging buffer.\n");
    destroyUiStagingBuffer();
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &g_vkUiStagingMemory);
  if (result != VK_SUCCESS || g_vkUiStagingMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate UI staging buffer memory", result);
    destroyUiStagingBuffer();
    return false;
  }

  result =
      vkBindBufferMemory(g_vkDevice, g_vkUiStagingBuffer, g_vkUiStagingMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind UI staging buffer memory", result);
    destroyUiStagingBuffer();
    return false;
  }

  g_vkUiStagingCapacity = newCapacity;
  void *mapped = nullptr;
  result = vkMapMemory(g_vkDevice, g_vkUiStagingMemory, 0, VK_WHOLE_SIZE, 0,
                       &mapped);
  if (result != VK_SUCCESS || mapped == nullptr) {
    debugVkResult("Failed to map UI staging buffer memory", result);
    destroyUiStagingBuffer();
    return false;
  }
  g_vkUiStagingMapped = static_cast<uint8_t *>(mapped);
  return true;
}

static void destroyUiImageResources() {
  if (g_vkDevice == VK_NULL_HANDLE)
    return;
  if (g_vkUiSampler != VK_NULL_HANDLE) {
    vkDestroySampler(g_vkDevice, g_vkUiSampler, nullptr);
    g_vkUiSampler = VK_NULL_HANDLE;
  }
  if (g_vkUiImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(g_vkDevice, g_vkUiImageView, nullptr);
    g_vkUiImageView = VK_NULL_HANDLE;
  }
  if (g_vkUiImage != VK_NULL_HANDLE) {
    vkDestroyImage(g_vkDevice, g_vkUiImage, nullptr);
    g_vkUiImage = VK_NULL_HANDLE;
  }
  if (g_vkUiImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g_vkDevice, g_vkUiImageMemory, nullptr);
    g_vkUiImageMemory = VK_NULL_HANDLE;
  }
  g_vkUiImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  g_vkUiImageWidth = 0;
  g_vkUiImageHeight = 0;
  g_vkUiImageReady = false;
}

static bool createOrResizeUiImageResources(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || g_vkDevice == VK_NULL_HANDLE)
    return false;

  if (g_vkUiImage != VK_NULL_HANDLE && g_vkUiImageWidth == width &&
      g_vkUiImageHeight == height && g_vkUiImageView != VK_NULL_HANDLE &&
      g_vkUiSampler != VK_NULL_HANDLE) {
    return true;
  }

  destroyUiImageResources();

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = VK_FORMAT_B8G8R8A8_UNORM;
  imageCI.extent.width = width;
  imageCI.extent.height = height;
  imageCI.extent.depth = 1;
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkResult result = vkCreateImage(g_vkDevice, &imageCI, nullptr, &g_vkUiImage);
  if (result != VK_SUCCESS || g_vkUiImage == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI image", result);
    return false;
  }

  VkMemoryRequirements memReq = {};
  vkGetImageMemoryRequirements(g_vkDevice, g_vkUiImage, &memReq);
  bool found = false;
  uint32_t memoryTypeIndex =
      findMemoryTypeIndex(memReq.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, found);
  if (!found) {
    debugVk("C4JRender_Vulkan: No device-local memory type for UI image.\n");
    destroyUiImageResources();
    return false;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(g_vkDevice, &allocInfo, nullptr, &g_vkUiImageMemory);
  if (result != VK_SUCCESS || g_vkUiImageMemory == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate UI image memory", result);
    destroyUiImageResources();
    return false;
  }

  result = vkBindImageMemory(g_vkDevice, g_vkUiImage, g_vkUiImageMemory, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to bind UI image memory", result);
    destroyUiImageResources();
    return false;
  }

  VkImageViewCreateInfo viewCI = {};
  viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCI.image = g_vkUiImage;
  viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCI.format = VK_FORMAT_B8G8R8A8_UNORM;
  viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewCI.subresourceRange.baseMipLevel = 0;
  viewCI.subresourceRange.levelCount = 1;
  viewCI.subresourceRange.baseArrayLayer = 0;
  viewCI.subresourceRange.layerCount = 1;
  result = vkCreateImageView(g_vkDevice, &viewCI, nullptr, &g_vkUiImageView);
  if (result != VK_SUCCESS || g_vkUiImageView == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI image view", result);
    destroyUiImageResources();
    return false;
  }

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = VK_FILTER_LINEAR;
  samplerCI.minFilter = VK_FILTER_LINEAR;
  samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.minLod = 0.0f;
  samplerCI.maxLod = 0.0f;
  samplerCI.maxAnisotropy = 1.0f;
  result = vkCreateSampler(g_vkDevice, &samplerCI, nullptr, &g_vkUiSampler);
  if (result != VK_SUCCESS || g_vkUiSampler == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI sampler", result);
    destroyUiImageResources();
    return false;
  }

  g_vkUiImageWidth = width;
  g_vkUiImageHeight = height;
  g_vkUiImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  g_vkUiImageReady = false;
  return true;
}

static void destroySwapchainDrawResources() {
  if (g_vkDevice != VK_NULL_HANDLE) {
    for (VkFramebuffer fb : g_vkFramebuffers) {
      if (fb != VK_NULL_HANDLE)
        vkDestroyFramebuffer(g_vkDevice, fb, nullptr);
    }
    g_vkFramebuffers.clear();

    for (auto &kv : g_vkTrianglePipelines) {
      if (kv.second != VK_NULL_HANDLE)
        vkDestroyPipeline(g_vkDevice, kv.second, nullptr);
    }
    g_vkTrianglePipelines.clear();
    g_vkTrianglePipeline = VK_NULL_HANDLE;
    if (g_vkUiPipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(g_vkDevice, g_vkUiPipeline, nullptr);
      g_vkUiPipeline = VK_NULL_HANDLE;
    }
    if (g_vkPipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(g_vkDevice, g_vkPipelineLayout, nullptr);
      g_vkPipelineLayout = VK_NULL_HANDLE;
    }
    if (g_vkTriangleDescriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(g_vkDevice, g_vkTriangleDescriptorPool, nullptr);
      g_vkTriangleDescriptorPool = VK_NULL_HANDLE;
    }
    for (auto &kv : g_vkTextures) {
      kv.second.descriptorSet = VK_NULL_HANDLE;
    }
    g_vkWhiteTexture.descriptorSet = VK_NULL_HANDLE;
    if (g_vkTriangleDescriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(g_vkDevice, g_vkTriangleDescriptorSetLayout,
                                   nullptr);
      g_vkTriangleDescriptorSetLayout = VK_NULL_HANDLE;
    }
    g_vkWhiteTextureReady = false;
    if (g_vkUiPipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(g_vkDevice, g_vkUiPipelineLayout, nullptr);
      g_vkUiPipelineLayout = VK_NULL_HANDLE;
    }
    if (g_vkUiDescriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(g_vkDevice, g_vkUiDescriptorPool, nullptr);
      g_vkUiDescriptorPool = VK_NULL_HANDLE;
    }
    if (g_vkUiDescriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(g_vkDevice, g_vkUiDescriptorSetLayout,
                                   nullptr);
      g_vkUiDescriptorSetLayout = VK_NULL_HANDLE;
    }
    g_vkUiDescriptorSet = VK_NULL_HANDLE;
    if (g_vkRenderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(g_vkDevice, g_vkRenderPass, nullptr);
      g_vkRenderPass = VK_NULL_HANDLE;
    }
    destroyDepthResources();

    for (VkImageView view : g_vkSwapImageViews) {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(g_vkDevice, view, nullptr);
    }
  }
  g_vkSwapImageViews.clear();
}

static void destroyVulkanRuntime() {
  if (g_vkDevice != VK_NULL_HANDLE)
    vkDeviceWaitIdle(g_vkDevice);

  destroyAllTextures(true);
  destroySwapchainDrawResources();
  destroyDynamicVertexBuffer();
  destroyAllCommandListGpuBuffers();
  destroyUiStagingBuffer();
  destroyUiImageResources();

  if (g_vkDevice != VK_NULL_HANDLE) {
    if (g_vkImageAvailable != VK_NULL_HANDLE) {
      vkDestroySemaphore(g_vkDevice, g_vkImageAvailable, nullptr);
      g_vkImageAvailable = VK_NULL_HANDLE;
    }
    if (g_vkRenderFinished != VK_NULL_HANDLE) {
      vkDestroySemaphore(g_vkDevice, g_vkRenderFinished, nullptr);
      g_vkRenderFinished = VK_NULL_HANDLE;
    }
    if (g_vkFence != VK_NULL_HANDLE) {
      vkDestroyFence(g_vkDevice, g_vkFence, nullptr);
      g_vkFence = VK_NULL_HANDLE;
    }
    if (g_vkCommandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(g_vkDevice, g_vkCommandPool, nullptr);
      g_vkCommandPool = VK_NULL_HANDLE;
      g_vkCommandBuffer = VK_NULL_HANDLE;
    }
    if (g_vkSwapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(g_vkDevice, g_vkSwapchain, nullptr);
      g_vkSwapchain = VK_NULL_HANDLE;
    }
    vkDestroyDevice(g_vkDevice, nullptr);
    g_vkDevice = VK_NULL_HANDLE;
    g_vkGraphicsQueue = VK_NULL_HANDLE;
  }

  if (g_vkSurface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(g_vkInstance, g_vkSurface, nullptr);
    g_vkSurface = VK_NULL_HANDLE;
  }
  if (g_vkInstance != VK_NULL_HANDLE) {
    vkDestroyInstance(g_vkInstance, nullptr);
    g_vkInstance = VK_NULL_HANDLE;
  }

  g_vkPhysicalDevice = VK_NULL_HANDLE;
  g_vkGraphicsFamily = 0;
  g_vkSwapImages.clear();
  g_vkSwapImageLayouts.clear();
  g_vkFrameVertexData.clear();
  g_vkQueuedDraws.clear();
  {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    g_vkCommandLists.clear();
  }
  g_vkIsRecordingCommandList = false;
  g_vkRecordingCommandListIndex = -1;
  g_vkSwapExtent = {0, 0};
  g_vkUiUpload.pixelsBGRA.clear();
  g_vkUiUpload.width = 0;
  g_vkUiUpload.height = 0;
  g_vkUiUpload.pending = false;
  resetThreadLocalRenderState();
  g_vkRecordingHasStateChanges = false;
  g_vkRecordingHasTextureStateChanges = false;
  g_vkRecordingHasAlphaStateChanges = false;
  g_vkRecordingFullStateList = false;
  g_vkInitialized = false;
}

static VkShaderModule createShaderModule(const uint32_t *code,
                                         size_t codeWordCount) {
  if (g_vkDevice == VK_NULL_HANDLE || !code || codeWordCount == 0)
    return VK_NULL_HANDLE;

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = codeWordCount * sizeof(uint32_t);
  createInfo.pCode = code;

  VkShaderModule module = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(g_vkDevice, &createInfo, nullptr, &module);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create shader module", result);
    return VK_NULL_HANDLE;
  }
  return module;
}

static VkPipeline createTrianglePipeline(bool depthTestEnable,
                                         bool depthWriteEnable,
                                         VkCompareOp depthCompareOp,
                                         bool blendEnable,
                                         VkBlendFactor srcBlendFactor,
                                         VkBlendFactor dstBlendFactor,
                                         VkColorComponentFlags colorWriteMask,
                                         bool cullEnable,
                                         bool cullClockwise) {
  if (g_vkDevice == VK_NULL_HANDLE || g_vkRenderPass == VK_NULL_HANDLE ||
      g_vkPipelineLayout == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  VkShaderModule vertModule =
      createShaderModule(kTriangleVertSpv,
                         sizeof(kTriangleVertSpv) / sizeof(kTriangleVertSpv[0]));
  VkShaderModule fragModule =
      createShaderModule(kTriangleFragSpv,
                         sizeof(kTriangleFragSpv) / sizeof(kTriangleFragSpv[0]));
  if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
    if (vertModule != VK_NULL_HANDLE)
      vkDestroyShaderModule(g_vkDevice, vertModule, nullptr);
    if (fragModule != VK_NULL_HANDLE)
      vkDestroyShaderModule(g_vkDevice, fragModule, nullptr);
    return VK_NULL_HANDLE;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertModule;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragModule;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bindingDesc = {};
  bindingDesc.binding = 0;
  bindingDesc.stride = kVertexStridePF3TF2CB4NB4XW1;
  bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribs[3] = {};
  attribs[0].location = 0;
  attribs[0].binding = 0;
  attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribs[0].offset = 0;
  attribs[1].location = 1;
  attribs[1].binding = 0;
  attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
  attribs[1].offset = 12;
  attribs[2].location = 2;
  attribs[2].binding = 0;
  attribs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  attribs[2].offset = 20;

  VkPipelineVertexInputStateCreateInfo viState = {};
  viState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  viState.vertexBindingDescriptionCount = 1;
  viState.pVertexBindingDescriptions = &bindingDesc;
  viState.vertexAttributeDescriptionCount = 3;
  viState.pVertexAttributeDescriptions = attribs;

  VkPipelineInputAssemblyStateCreateInfo iaState = {};
  iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  iaState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(g_vkSwapExtent.width);
  viewport.height = static_cast<float>(g_vkSwapExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = g_vkSwapExtent;

  VkPipelineViewportStateCreateInfo vpState = {};
  vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vpState.viewportCount = 1;
  vpState.pViewports = &viewport;
  vpState.scissorCount = 1;
  vpState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rsState = {};
  rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rsState.depthClampEnable = VK_FALSE;
  rsState.rasterizerDiscardEnable = VK_FALSE;
  rsState.polygonMode = VK_POLYGON_MODE_FILL;
  rsState.cullMode = cullEnable ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
  rsState.frontFace =
      cullClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
  rsState.depthBiasEnable = VK_FALSE;
  rsState.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo msState = {};
  msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment = {};
  blendAttachment.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
  blendAttachment.srcColorBlendFactor = srcBlendFactor;
  blendAttachment.dstColorBlendFactor = dstBlendFactor;
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcAlphaBlendFactor = srcBlendFactor;
  blendAttachment.dstAlphaBlendFactor = dstBlendFactor;
  blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.colorWriteMask = colorWriteMask;

  VkPipelineColorBlendStateCreateInfo cbState = {};
  cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cbState.attachmentCount = 1;
  cbState.pAttachments = &blendAttachment;

  VkPipelineDepthStencilStateCreateInfo depthState = {};
  depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthState.depthTestEnable = depthTestEnable ? VK_TRUE : VK_FALSE;
  depthState.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthState.depthCompareOp = depthCompareOp;
  depthState.depthBoundsTestEnable = VK_FALSE;
  depthState.stencilTestEnable = VK_FALSE;

  VkGraphicsPipelineCreateInfo gpCI = {};
  gpCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpCI.stageCount = 2;
  gpCI.pStages = stages;
  gpCI.pVertexInputState = &viState;
  gpCI.pInputAssemblyState = &iaState;
  gpCI.pViewportState = &vpState;
  gpCI.pRasterizationState = &rsState;
  gpCI.pMultisampleState = &msState;
  gpCI.pDepthStencilState = &depthState;
  gpCI.pColorBlendState = &cbState;
  gpCI.layout = g_vkPipelineLayout;
  gpCI.renderPass = g_vkRenderPass;
  gpCI.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult result =
      vkCreateGraphicsPipelines(g_vkDevice, VK_NULL_HANDLE, 1, &gpCI, nullptr,
                                &pipeline);
  vkDestroyShaderModule(g_vkDevice, vertModule, nullptr);
  vkDestroyShaderModule(g_vkDevice, fragModule, nullptr);
  if (result != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
    debugVkResult("Failed to create triangle graphics pipeline", result);
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

static VkPipeline getOrCreateTrianglePipeline(bool depthTestEnable,
                                              bool depthWriteEnable,
                                              VkCompareOp depthCompareOp,
                                              bool blendEnable,
                                              VkBlendFactor srcBlendFactor,
                                              VkBlendFactor dstBlendFactor,
                                              VkColorComponentFlags colorWriteMask,
                                              bool cullEnable,
                                              bool cullClockwise) {
  const uint64_t key =
      makeTrianglePipelineKey(depthTestEnable, depthWriteEnable, depthCompareOp,
                              blendEnable, srcBlendFactor, dstBlendFactor,
                              colorWriteMask, cullEnable, cullClockwise);
  auto it = g_vkTrianglePipelines.find(key);
  if (it != g_vkTrianglePipelines.end())
    return it->second;

  VkPipeline pipeline = createTrianglePipeline(
      depthTestEnable, depthWriteEnable, depthCompareOp, blendEnable,
      srcBlendFactor, dstBlendFactor, colorWriteMask, cullEnable,
      cullClockwise);
  if (pipeline == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;
  g_vkTrianglePipelines[key] = pipeline;
  return pipeline;
}

static bool createTriangleRenderResources() {
  if (g_vkDevice == VK_NULL_HANDLE || g_vkSwapImages.empty())
    return false;
  if (g_vkSwapExtent.width == 0 || g_vkSwapExtent.height == 0)
    return false;

  destroySwapchainDrawResources();

  g_vkSwapImageViews.assign(g_vkSwapImages.size(), VK_NULL_HANDLE);
  for (size_t i = 0; i < g_vkSwapImages.size(); ++i) {
    VkImageViewCreateInfo ivCI = {};
    ivCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCI.image = g_vkSwapImages[i];
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = g_vkSwapFormat;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.baseMipLevel = 0;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.baseArrayLayer = 0;
    ivCI.subresourceRange.layerCount = 1;

    VkResult result =
        vkCreateImageView(g_vkDevice, &ivCI, nullptr, &g_vkSwapImageViews[i]);
    if (result != VK_SUCCESS) {
      debugVkResult("Failed to create swapchain image view", result);
      return false;
    }
  }
  if (!createDepthResources(g_vkSwapExtent.width, g_vkSwapExtent.height))
    return false;

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = g_vkSwapFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment = {};
  depthAttachment.format = g_vkDepthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef = {};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription attachments[2] = {colorAttachment, depthAttachment};

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpCI = {};
  rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpCI.attachmentCount = 2;
  rpCI.pAttachments = attachments;
  rpCI.subpassCount = 1;
  rpCI.pSubpasses = &subpass;
  rpCI.dependencyCount = 1;
  rpCI.pDependencies = &dependency;
  VkResult result = vkCreateRenderPass(g_vkDevice, &rpCI, nullptr, &g_vkRenderPass);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create render pass", result);
    return false;
  }

  VkDescriptorSetLayoutBinding triangleSamplerBinding = {};
  triangleSamplerBinding.binding = 0;
  triangleSamplerBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  triangleSamplerBinding.descriptorCount = 1;
  triangleSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  triangleSamplerBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo triangleDslCI = {};
  triangleDslCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  triangleDslCI.bindingCount = 1;
  triangleDslCI.pBindings = &triangleSamplerBinding;
  result = vkCreateDescriptorSetLayout(g_vkDevice, &triangleDslCI, nullptr,
                                       &g_vkTriangleDescriptorSetLayout);
  if (result != VK_SUCCESS ||
      g_vkTriangleDescriptorSetLayout == VK_NULL_HANDLE) {
    debugVkResult("Failed to create triangle descriptor set layout", result);
    return false;
  }

  VkDescriptorPoolSize trianglePoolSize = {};
  trianglePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  trianglePoolSize.descriptorCount = kMaxTriangleTextureDescriptors;

  VkDescriptorPoolCreateInfo triangleDpCI = {};
  triangleDpCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  triangleDpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  triangleDpCI.maxSets = kMaxTriangleTextureDescriptors;
  triangleDpCI.poolSizeCount = 1;
  triangleDpCI.pPoolSizes = &trianglePoolSize;
  result = vkCreateDescriptorPool(g_vkDevice, &triangleDpCI, nullptr,
                                  &g_vkTriangleDescriptorPool);
  if (result != VK_SUCCESS || g_vkTriangleDescriptorPool == VK_NULL_HANDLE) {
    debugVkResult("Failed to create triangle descriptor pool", result);
    return false;
  }

  VkPipelineLayoutCreateInfo plCI = {};
  plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plCI.setLayoutCount = 1;
  plCI.pSetLayouts = &g_vkTriangleDescriptorSetLayout;
  VkPushConstantRange trianglePushRange = {};
  trianglePushRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  trianglePushRange.offset = 0;
  trianglePushRange.size = sizeof(TrianglePushConstants);
  plCI.pushConstantRangeCount = 1;
  plCI.pPushConstantRanges = &trianglePushRange;
  result = vkCreatePipelineLayout(g_vkDevice, &plCI, nullptr, &g_vkPipelineLayout);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create pipeline layout", result);
    return false;
  }

  g_vkTrianglePipelines.clear();
  g_vkTrianglePipeline = createTrianglePipeline(
      true, true, VK_COMPARE_OP_LESS_OR_EQUAL, false, VK_BLEND_FACTOR_ONE,
      VK_BLEND_FACTOR_ZERO,
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      false, true);
  if (g_vkTrianglePipeline == VK_NULL_HANDLE)
    return false;
  g_vkTrianglePipelines[makeTrianglePipelineKey(
      true, true, VK_COMPARE_OP_LESS_OR_EQUAL, false, VK_BLEND_FACTOR_ONE,
      VK_BLEND_FACTOR_ZERO,
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      false, true)] = g_vkTrianglePipeline;

  VkVertexInputBindingDescription bindingDesc = {};
  bindingDesc.binding = 0;
  bindingDesc.stride = kVertexStridePF3TF2CB4NB4XW1;
  bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribs[3] = {};
  attribs[0].location = 0;
  attribs[0].binding = 0;
  attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribs[0].offset = 0;
  attribs[1].location = 1;
  attribs[1].binding = 0;
  attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
  attribs[1].offset = 12;
  attribs[2].location = 2;
  attribs[2].binding = 0;
  attribs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  attribs[2].offset = 20;

  VkPipelineVertexInputStateCreateInfo viState = {};
  viState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  viState.vertexBindingDescriptionCount = 1;
  viState.pVertexBindingDescriptions = &bindingDesc;
  viState.vertexAttributeDescriptionCount = 3;
  viState.pVertexAttributeDescriptions = attribs;

  VkPipelineInputAssemblyStateCreateInfo iaState = {};
  iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  iaState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(g_vkSwapExtent.width);
  viewport.height = static_cast<float>(g_vkSwapExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = g_vkSwapExtent;

  VkPipelineViewportStateCreateInfo vpState = {};
  vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vpState.viewportCount = 1;
  vpState.pViewports = &viewport;
  vpState.scissorCount = 1;
  vpState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rsState = {};
  rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rsState.depthClampEnable = VK_FALSE;
  rsState.rasterizerDiscardEnable = VK_FALSE;
  rsState.polygonMode = VK_POLYGON_MODE_FILL;
  rsState.cullMode = VK_CULL_MODE_NONE;
  rsState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rsState.depthBiasEnable = VK_FALSE;
  rsState.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo msState = {};
  msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkDescriptorSetLayoutBinding samplerBinding = {};
  samplerBinding.binding = 0;
  samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerBinding.descriptorCount = 1;
  samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  samplerBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo dslCI = {};
  dslCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dslCI.bindingCount = 1;
  dslCI.pBindings = &samplerBinding;
  result = vkCreateDescriptorSetLayout(g_vkDevice, &dslCI, nullptr,
                                       &g_vkUiDescriptorSetLayout);
  if (result != VK_SUCCESS || g_vkUiDescriptorSetLayout == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI descriptor set layout", result);
    return false;
  }

  VkDescriptorPoolSize poolSize = {};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo dpCI = {};
  dpCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpCI.maxSets = 1;
  dpCI.poolSizeCount = 1;
  dpCI.pPoolSizes = &poolSize;
  result = vkCreateDescriptorPool(g_vkDevice, &dpCI, nullptr,
                                  &g_vkUiDescriptorPool);
  if (result != VK_SUCCESS || g_vkUiDescriptorPool == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI descriptor pool", result);
    return false;
  }

  VkDescriptorSetAllocateInfo dsAI = {};
  dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsAI.descriptorPool = g_vkUiDescriptorPool;
  dsAI.descriptorSetCount = 1;
  dsAI.pSetLayouts = &g_vkUiDescriptorSetLayout;
  result = vkAllocateDescriptorSets(g_vkDevice, &dsAI, &g_vkUiDescriptorSet);
  if (result != VK_SUCCESS || g_vkUiDescriptorSet == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate UI descriptor set", result);
    return false;
  }

  VkPipelineLayoutCreateInfo uiPlCI = {};
  uiPlCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  uiPlCI.setLayoutCount = 1;
  uiPlCI.pSetLayouts = &g_vkUiDescriptorSetLayout;
  VkPushConstantRange uiPushRange = {};
  uiPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  uiPushRange.offset = 0;
  uiPushRange.size = sizeof(float) * 16;
  uiPlCI.pushConstantRangeCount = 1;
  uiPlCI.pPushConstantRanges = &uiPushRange;
  result = vkCreatePipelineLayout(g_vkDevice, &uiPlCI, nullptr,
                                  &g_vkUiPipelineLayout);
  if (result != VK_SUCCESS || g_vkUiPipelineLayout == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI pipeline layout", result);
    return false;
  }

  VkShaderModule uiVertModule =
      createShaderModule(kUiCompositeVertSpv,
                         sizeof(kUiCompositeVertSpv) / sizeof(kUiCompositeVertSpv[0]));
  VkShaderModule uiFragModule =
      createShaderModule(kUiCompositeFragSpv,
                         sizeof(kUiCompositeFragSpv) / sizeof(kUiCompositeFragSpv[0]));
  if (uiVertModule == VK_NULL_HANDLE || uiFragModule == VK_NULL_HANDLE) {
    if (uiVertModule != VK_NULL_HANDLE)
      vkDestroyShaderModule(g_vkDevice, uiVertModule, nullptr);
    if (uiFragModule != VK_NULL_HANDLE)
      vkDestroyShaderModule(g_vkDevice, uiFragModule, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo uiStages[2] = {};
  uiStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  uiStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  uiStages[0].module = uiVertModule;
  uiStages[0].pName = "main";
  uiStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  uiStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  uiStages[1].module = uiFragModule;
  uiStages[1].pName = "main";

  VkPipelineColorBlendAttachmentState uiBlendAttachment = {};
  uiBlendAttachment.blendEnable = VK_TRUE;
  uiBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  uiBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  uiBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  uiBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  uiBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  uiBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  uiBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo uiCbState = {};
  uiCbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  uiCbState.attachmentCount = 1;
  uiCbState.pAttachments = &uiBlendAttachment;

  VkPipelineDepthStencilStateCreateInfo uiDepthState = {};
  uiDepthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  uiDepthState.depthTestEnable = VK_FALSE;
  uiDepthState.depthWriteEnable = VK_FALSE;
  uiDepthState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  uiDepthState.depthBoundsTestEnable = VK_FALSE;
  uiDepthState.stencilTestEnable = VK_FALSE;

  VkGraphicsPipelineCreateInfo uiGpCI = {};
  uiGpCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  uiGpCI.stageCount = 2;
  uiGpCI.pStages = uiStages;
  uiGpCI.pVertexInputState = &viState;
  uiGpCI.pInputAssemblyState = &iaState;
  uiGpCI.pViewportState = &vpState;
  uiGpCI.pRasterizationState = &rsState;
  uiGpCI.pMultisampleState = &msState;
  uiGpCI.pDepthStencilState = &uiDepthState;
  uiGpCI.pColorBlendState = &uiCbState;
  uiGpCI.layout = g_vkUiPipelineLayout;
  uiGpCI.renderPass = g_vkRenderPass;
  uiGpCI.subpass = 0;

  result = vkCreateGraphicsPipelines(g_vkDevice, VK_NULL_HANDLE, 1, &uiGpCI,
                                     nullptr, &g_vkUiPipeline);
  vkDestroyShaderModule(g_vkDevice, uiVertModule, nullptr);
  vkDestroyShaderModule(g_vkDevice, uiFragModule, nullptr);
  if (result != VK_SUCCESS || g_vkUiPipeline == VK_NULL_HANDLE) {
    debugVkResult("Failed to create UI graphics pipeline", result);
    return false;
  }

  if (g_vkUiImageView != VK_NULL_HANDLE && g_vkUiSampler != VK_NULL_HANDLE &&
      g_vkUiImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    updateUiDescriptorSet();
  }

  g_vkFramebuffers.assign(g_vkSwapImageViews.size(), VK_NULL_HANDLE);
  for (size_t i = 0; i < g_vkSwapImageViews.size(); ++i) {
    VkImageView attachments[] = {g_vkSwapImageViews[i], g_vkDepthImageView};
    VkFramebufferCreateInfo fbCI = {};
    fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCI.renderPass = g_vkRenderPass;
    fbCI.attachmentCount = 2;
    fbCI.pAttachments = attachments;
    fbCI.width = g_vkSwapExtent.width;
    fbCI.height = g_vkSwapExtent.height;
    fbCI.layers = 1;
    result = vkCreateFramebuffer(g_vkDevice, &fbCI, nullptr, &g_vkFramebuffers[i]);
    if (result != VK_SUCCESS) {
      debugVkResult("Failed to create framebuffer", result);
      return false;
    }
  }

  return true;
}

// ============================================================================
//  Identity matrix helper
// ============================================================================
static void mat4_identity(float *m) {
  memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(float *out, const float *a, const float *b) {
  float tmp[16];
  // Column-major matrix multiply (OpenGL convention):
  // out = a * b, index = row + col*4.
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a[row + k * 4] * b[k + col * 4];
      }
      tmp[row + col * 4] = sum;
    }
  }
  memcpy(out, tmp, 16 * sizeof(float));
}

static bool mat4_inverse(float *out, const float *m) {
  float inv[16];

  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
           m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
           m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
           m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
           m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
           m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
           m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
           m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] +
           m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
           m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] +
           m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
           m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] +
           m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
           m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
            m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  const float det =
      m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) <= 1.0e-8f) {
    return false;
  }

  const float invDet = 1.0f / det;
  for (int i = 0; i < 16; ++i) {
    out[i] = inv[i] * invDet;
  }
  return true;
}

static float *curMatrix() {
  ensureThreadLocalMatrixStacksInitialised();
  return g_matStacks[g_curMatrixMode].stack[g_matStacks[g_curMatrixMode].top];
}

static void appendOverlayQuad(std::vector<BootstrapVertex> &verts, float x0,
                              float y0, float x1, float y1, uint8_t r,
                              uint8_t g, uint8_t b, uint8_t a) {
  BootstrapVertex v0 = {x0, y0, 0.0f, 0.0f, 0.0f, r, g, b, a, 0, 0, 0, 0, 0};
  BootstrapVertex v1 = {x1, y0, 0.0f, 1.0f, 0.0f, r, g, b, a, 0, 0, 0, 0, 0};
  BootstrapVertex v2 = {x1, y1, 0.0f, 1.0f, 1.0f, r, g, b, a, 0, 0, 0, 0, 0};
  BootstrapVertex v3 = {x0, y1, 0.0f, 0.0f, 1.0f, r, g, b, a, 0, 0, 0, 0, 0};
  verts.push_back(v0);
  verts.push_back(v1);
  verts.push_back(v2);
  verts.push_back(v2);
  verts.push_back(v3);
  verts.push_back(v0);
}

static const char *const *overlayGlyph5x7Rows(char c) {
  static const char *const g0[7] = {"01110", "10001", "10011", "10101",
                                    "11001", "10001", "01110"};
  static const char *const g1[7] = {"00100", "01100", "00100", "00100",
                                    "00100", "00100", "01110"};
  static const char *const g2[7] = {"01110", "10001", "00001", "00010",
                                    "00100", "01000", "11111"};
  static const char *const g3[7] = {"11110", "00001", "00001", "01110",
                                    "00001", "00001", "11110"};
  static const char *const g4[7] = {"00010", "00110", "01010", "10010",
                                    "11111", "00010", "00010"};
  static const char *const g5[7] = {"11111", "10000", "10000", "11110",
                                    "00001", "00001", "11110"};
  static const char *const g6[7] = {"01110", "10000", "10000", "11110",
                                    "10001", "10001", "01110"};
  static const char *const g7[7] = {"11111", "00001", "00010", "00100",
                                    "01000", "01000", "01000"};
  static const char *const g8[7] = {"01110", "10001", "10001", "01110",
                                    "10001", "10001", "01110"};
  static const char *const g9[7] = {"01110", "10001", "10001", "01111",
                                    "00001", "00001", "01110"};
  static const char *const gF[7] = {"11111", "10000", "10000", "11110",
                                    "10000", "10000", "10000"};
  static const char *const gD[7] = {"11110", "10001", "10001", "10001",
                                    "10001", "10001", "11110"};
  static const char *const gV[7] = {"10001", "10001", "10001", "10001",
                                    "01010", "01010", "00100"};
  static const char *const gU[7] = {"10001", "10001", "10001", "10001",
                                    "10001", "10001", "01110"};
  static const char *const gMinus[7] = {"00000", "00000", "00000", "11111",
                                        "00000", "00000", "00000"};

  switch (c) {
  case '0':
    return g0;
  case '1':
    return g1;
  case '2':
    return g2;
  case '3':
    return g3;
  case '4':
    return g4;
  case '5':
    return g5;
  case '6':
    return g6;
  case '7':
    return g7;
  case '8':
    return g8;
  case '9':
    return g9;
  case 'F':
    return gF;
  case 'D':
    return gD;
  case 'V':
    return gV;
  case 'U':
    return gU;
  case '-':
    return gMinus;
  default:
    break;
  }
  return nullptr;
}

static void appendOverlayText5x7(std::vector<BootstrapVertex> &verts,
                                 const char *text, float x, float y, float cellW,
                                 float cellH, uint8_t r, uint8_t g, uint8_t b,
                                 uint8_t a) {
  if (text == nullptr)
    return;
  float cursorX = x;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p == ' ') {
      cursorX += cellW * 6.0f;
      continue;
    }
    const char *const *rows = overlayGlyph5x7Rows(*p);
    if (rows != nullptr) {
      for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
          if (rows[row][col] != '1')
            continue;
          const float x0 = cursorX + static_cast<float>(col) * cellW;
          const float y0 = y + static_cast<float>(row) * cellH;
          const float x1 = x0 + cellW * 0.86f;
          const float y1 = y0 + cellH * 0.86f;
          appendOverlayQuad(verts, x0, y0, x1, y1, r, g, b, a);
        }
      }
    }
    cursorX += cellW * 6.0f;
  }
}

static bool canMergeQueuedDraw(const VulkanQueuedDraw &a,
                               const VulkanQueuedDraw &b) {
  if (a.vertexBuffer != b.vertexBuffer)
    return false;
  const uint64_t expectedFirst =
      static_cast<uint64_t>(a.firstVertex) + static_cast<uint64_t>(a.vertexCount);
  if (expectedFirst != static_cast<uint64_t>(b.firstVertex))
    return false;
  if (static_cast<uint64_t>(a.vertexCount) + static_cast<uint64_t>(b.vertexCount) >
      0xffffffffull)
    return false;
  return a.depthTestEnable == b.depthTestEnable &&
         a.depthWriteEnable == b.depthWriteEnable &&
         a.depthCompareOp == b.depthCompareOp &&
         a.blendEnable == b.blendEnable &&
         a.srcBlendFactor == b.srcBlendFactor &&
         a.dstBlendFactor == b.dstBlendFactor &&
         a.colorWriteMask == b.colorWriteMask && a.cullEnable == b.cullEnable &&
         a.cullClockwise == b.cullClockwise &&
         a.descriptorSet == b.descriptorSet &&
         a.alphaTestEnable == b.alphaTestEnable && a.alphaFunc == b.alphaFunc &&
         a.alphaRef == b.alphaRef &&
         std::memcmp(a.blendConstants, b.blendConstants,
                     sizeof(a.blendConstants)) == 0 &&
         std::memcmp(a.mvp, b.mvp, sizeof(a.mvp)) == 0;
}

static void appendQueuedDrawMerged(const VulkanQueuedDraw &draw) {
  if (!g_vkEnableDrawMerge) {
    g_vkQueuedDraws.push_back(draw);
    return;
  }

  if (g_vkQueuedDraws.empty()) {
    g_vkQueuedDraws.push_back(draw);
    return;
  }

  VulkanQueuedDraw &last = g_vkQueuedDraws.back();
  if (canMergeQueuedDraw(last, draw)) {
    last.vertexCount += draw.vertexCount;
    return;
  }

  appendQueuedDrawMerged(draw);
}

static void queueCornerOverlayText() {
  if (!g_vkShowCornerOverlay)
    return;

  std::vector<BootstrapVertex> verts;
  verts.reserve(1400);

  const float xStart = -0.985f;
  const float cellW = 0.018f;
  const float cellH = 0.030f;

  const char *glyphV[7] = {"10001", "10001", "10001", "10001",
                           "01010", "01010", "00100"};
  const char *glyphK[7] = {"10001", "10010", "10100", "11000",
                           "10100", "10010", "10001"};

  auto emitGlyph = [&](const char *rows[7], float xOffset, float yStart) {
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row][col] != '1')
          continue;
        const float x0 = xStart + (xOffset + static_cast<float>(col)) * cellW;
        const float y0 = yStart + static_cast<float>(row) * cellH;
        const float x1 = x0 + cellW * 0.85f;
        const float y1 = y0 + cellH * 0.85f;
        appendOverlayQuad(verts, x0, y0, x1, y1, 255, 230, 40, 255);
      }
    }
  };

  auto emitBadgeAtY = [&](float yStart) {
    appendOverlayQuad(verts, xStart - 0.01f, yStart - 0.01f,
                      xStart + cellW * 13.0f, yStart + cellH * 8.0f, 0, 0, 0,
                      180);
    emitGlyph(glyphV, 0.0f, yStart);
    emitGlyph(glyphK, 7.0f, yStart);
  };

  emitBadgeAtY(-0.945f);
  emitBadgeAtY(0.705f);

  if (g_vkShowPerfOverlay) {
    const float px = xStart - 0.01f;
    const float py = -0.675f;
    const float pw = cellW * 20.0f;
    const float ph = cellH * 10.8f;
    appendOverlayQuad(verts, px, py, px + pw, py + ph, 0, 0, 0, 170);

    char fpsText[16];
    char drawText[16];
    char vertKText[16];
    char uploadKBText[16];

    std::snprintf(fpsText, sizeof(fpsText), "%u",
                  static_cast<unsigned int>(g_vkPerfFpsSmoothed + 0.5f));
    std::snprintf(drawText, sizeof(drawText), "%u", g_vkPerfDrawCount);
    std::snprintf(vertKText, sizeof(vertKText), "%u", g_vkPerfVertexCount / 1000u);
    std::snprintf(uploadKBText, sizeof(uploadKBText), "%u",
                  g_vkPerfUploadBytes / 1024u);

    const float sx = xStart + 0.005f;
    const float sy = py + 0.010f;
    const float sw = cellW * 0.64f;
    const float sh = cellH * 0.44f;
    const float dy = cellH * 2.2f;

    appendOverlayText5x7(verts, "F", sx, sy + dy * 0.0f, sw, sh, 255, 220, 60, 255);
    appendOverlayText5x7(verts, fpsText, sx + sw * 8.0f, sy + dy * 0.0f, sw, sh,
                         230, 230, 230, 255);
    appendOverlayText5x7(verts, "D", sx, sy + dy * 1.0f, sw, sh, 255, 220, 60, 255);
    appendOverlayText5x7(verts, drawText, sx + sw * 8.0f, sy + dy * 1.0f, sw, sh,
                         230, 230, 230, 255);
    appendOverlayText5x7(verts, "V", sx, sy + dy * 2.0f, sw, sh, 255, 220, 60, 255);
    appendOverlayText5x7(verts, vertKText, sx + sw * 8.0f, sy + dy * 2.0f, sw, sh,
                         230, 230, 230, 255);
    appendOverlayText5x7(verts, "U", sx, sy + dy * 3.0f, sw, sh, 255, 220, 60, 255);
    appendOverlayText5x7(verts, uploadKBText, sx + sw * 8.0f, sy + dy * 3.0f, sw, sh,
                         230, 230, 230, 255);
  }

  if (verts.empty())
    return;

  const size_t oldSize = g_vkFrameVertexData.size();
  const size_t addBytes = verts.size() * sizeof(BootstrapVertex);
  g_vkFrameVertexData.resize(oldSize + addBytes);
  std::memcpy(g_vkFrameVertexData.data() + oldSize, verts.data(), addBytes);

  VulkanQueuedDraw draw = {};
  draw.vertexBuffer = VK_NULL_HANDLE;
  draw.firstVertex =
      static_cast<uint32_t>(oldSize / kVertexStridePF3TF2CB4NB4XW1);
  draw.vertexCount = static_cast<uint32_t>(verts.size());
  mat4_identity(draw.mvp);
  draw.depthTestEnable = false;
  draw.depthWriteEnable = false;
  draw.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  draw.blendEnable = false;
  draw.srcBlendFactor = VK_BLEND_FACTOR_ONE;
  draw.dstBlendFactor = VK_BLEND_FACTOR_ZERO;
  draw.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  draw.cullEnable = false;
  draw.cullClockwise = true;
  draw.blendConstants[0] = 1.0f;
  draw.blendConstants[1] = 1.0f;
  draw.blendConstants[2] = 1.0f;
  draw.blendConstants[3] = 1.0f;
  appendQueuedDrawMerged(draw);
}

static void appendUiCompositeFullscreenQuad(uint32_t &firstVertexOut,
                                            uint32_t &vertexCountOut) {
  firstVertexOut = 0;
  vertexCountOut = 0;
  if (!g_vkUiImageReady)
    return;

  const BootstrapVertex verts[6] = {
      {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
      {+1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
      {+1.0f, +1.0f, 0.0f, 1.0f, 0.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
      {+1.0f, +1.0f, 0.0f, 1.0f, 0.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
      {-1.0f, +1.0f, 0.0f, 0.0f, 0.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
      {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 255, 255, 255, 255, 0, 0, 0, 0, 0},
  };

  const size_t oldSize = g_vkFrameVertexData.size();
  const size_t addBytes = sizeof(verts);
  g_vkFrameVertexData.resize(oldSize + addBytes);
  std::memcpy(g_vkFrameVertexData.data() + oldSize, verts, addBytes);

  firstVertexOut =
      static_cast<uint32_t>(oldSize / kVertexStridePF3TF2CB4NB4XW1);
  vertexCountOut = 6;
}

// ============================================================================
//  C4JRender â€” Matrix stack
// ============================================================================

void C4JRender::MatrixMode(int type) {
  ensureThreadLocalMatrixStacksInitialised();
  if (type >= 0 && type < NUM_MATRIX_MODES)
    g_curMatrixMode = type;
}

void C4JRender::MatrixSetIdentity() {
  mat4_identity(curMatrix());
  g_matrixDirty = true;
}

void C4JRender::MatrixTranslate(float x, float y, float z) {
  float t[16];
  mat4_identity(t);
  t[12] = x;
  t[13] = y;
  t[14] = z;
  mat4_multiply(curMatrix(), curMatrix(), t);
  g_matrixDirty = true;
}

void C4JRender::MatrixRotate(float angle, float x, float y, float z) {
  float c = cosf(angle), s = sinf(angle);
  float len = sqrtf(x * x + y * y + z * z);
  if (len < 1e-6f)
    return;
  x /= len;
  y /= len;
  z /= len;
  float t = 1.0f - c;

  float r[16];
  mat4_identity(r);
  r[0] = t * x * x + c;
  r[4] = t * x * y - s * z;
  r[8] = t * x * z + s * y;
  r[1] = t * x * y + s * z;
  r[5] = t * y * y + c;
  r[9] = t * y * z - s * x;
  r[2] = t * x * z - s * y;
  r[6] = t * y * z + s * x;
  r[10] = t * z * z + c;

  mat4_multiply(curMatrix(), curMatrix(), r);
  g_matrixDirty = true;
}

void C4JRender::MatrixScale(float x, float y, float z) {
  float s[16];
  mat4_identity(s);
  s[0] = x;
  s[5] = y;
  s[10] = z;
  mat4_multiply(curMatrix(), curMatrix(), s);
  g_matrixDirty = true;
}

void C4JRender::MatrixPerspective(float fovy, float aspect, float zNear,
                                  float zFar) {
  // gluPerspective-style input is degrees.
  const float fovyRadians = fovy * (3.14159265358979323846f / 180.0f);
  float f = 1.0f / tanf(fovyRadians * 0.5f);
  float p[16];
  memset(p, 0, sizeof(p));
  p[0] = f / aspect;
  p[5] = f;
  p[10] = (zFar + zNear) / (zNear - zFar);
  p[11] = -1.0f;
  p[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  mat4_multiply(curMatrix(), curMatrix(), p);
  g_matrixDirty = true;
}

void C4JRender::MatrixOrthogonal(float left, float right, float bottom,
                                 float top, float zNear, float zFar) {
  float o[16];
  memset(o, 0, sizeof(o));
  o[0] = 2.0f / (right - left);
  o[5] = 2.0f / (top - bottom);
  o[10] = -2.0f / (zFar - zNear);
  o[12] = -(right + left) / (right - left);
  o[13] = -(top + bottom) / (top - bottom);
  o[14] = -(zFar + zNear) / (zFar - zNear);
  o[15] = 1.0f;
  mat4_multiply(curMatrix(), curMatrix(), o);
  g_matrixDirty = true;
}

void C4JRender::MatrixPop() {
  auto &s = g_matStacks[g_curMatrixMode];
  if (s.top > 0)
    s.top--;
  g_matrixDirty = true;
}

void C4JRender::MatrixPush() {
  auto &s = g_matStacks[g_curMatrixMode];
  if (s.top < MATRIX_STACK_DEPTH - 1) {
    memcpy(s.stack[s.top + 1], s.stack[s.top], 16 * sizeof(float));
    s.top++;
  }
  g_matrixDirty = true;
}

void C4JRender::MatrixMult(float *mat) {
  mat4_multiply(curMatrix(), curMatrix(), mat);
  g_matrixDirty = true;
}

const float *C4JRender::MatrixGet(int type) {
  ensureThreadLocalMatrixStacksInitialised();
  if (type >= 0 && type < NUM_MATRIX_MODES)
    return g_matStacks[type].stack[g_matStacks[type].top];
  return g_matStacks[0].stack[g_matStacks[0].top];
}

void C4JRender::Set_matrixDirty() { g_matrixDirty = true; }

static const float *getCurrentDrawMvp() {
  ensureThreadLocalMatrixStacksInitialised();
  if (!g_vkEnableMvpCache || !g_cachedMvpValid || g_matrixDirty) {
    const float *modelView = RenderManager.MatrixGet(GL_MODELVIEW_MATRIX);
    const float *projection = RenderManager.MatrixGet(GL_PROJECTION_MATRIX);
    if (modelView != nullptr && projection != nullptr) {
      mat4_multiply(g_cachedMvp, projection, modelView);
    } else {
      mat4_identity(g_cachedMvp);
    }
    g_cachedMvpValid = true;
    if (g_vkEnableMvpCache) {
      g_matrixDirty = false;
    }
  }
  return g_cachedMvp;
}

// ============================================================================
//  C4JRender - Core (Vulkan init and present)
// ============================================================================

void C4JRender::Initialise(HWND hWnd, int width, int height) {
  g_vkWindow = hWnd;
  destroyVulkanRuntime();

  if (width <= 0 || height <= 0) {
    int clientW = 0;
    int clientH = 0;
    if (getWindowClientSize(hWnd, clientW, clientH)) {
      width = clientW;
      height = clientH;
    }
  }
  if (width <= 0 || height <= 0) {
    debugVk("C4JRender_Vulkan: Skipping init for zero-sized window.\n");
    return;
  }

  // Init matrix stacks
  ensureThreadLocalMatrixStacksInitialised();
  for (int i = 0; i < NUM_MATRIX_MODES; i++) {
    g_matStacks[i].top = 0;
    mat4_identity(g_matStacks[i].stack[0]);
  }
  resetThreadLocalRenderState();

  // ---- VkInstance ----
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "MinecraftConsoles";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "4JRender_Vulkan";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  const char *extensions[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
  };

  VkInstanceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &appInfo;
  ci.enabledExtensionCount = 2;
  ci.ppEnabledExtensionNames = extensions;

#ifdef _DEBUG
  const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
  if (hasValidationLayer(layers[0])) {
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
  } else {
    debugVk("C4JRender_Vulkan: Validation layer not available; continuing "
            "without it.\n");
  }
#endif

  VkResult result = vkCreateInstance(&ci, nullptr, &g_vkInstance);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create VkInstance", result);
    return;
  }

  // ---- Surface ----
  VkWin32SurfaceCreateInfoKHR surfCI = {};
  surfCI.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfCI.hinstance = GetModuleHandle(NULL);
  surfCI.hwnd = hWnd;
  result = vkCreateWin32SurfaceKHR(g_vkInstance, &surfCI, nullptr, &g_vkSurface);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create Win32 surface", result);
    return;
  }

  // ---- Physical device ----
  uint32_t devCount = 0;
  result = vkEnumeratePhysicalDevices(g_vkInstance, &devCount, nullptr);
  if (result != VK_SUCCESS || devCount == 0) {
    debugVk("C4JRender_Vulkan: No Vulkan physical devices found.\n");
    return;
  }

  std::vector<VkPhysicalDevice> devices(devCount);
  result = vkEnumeratePhysicalDevices(g_vkInstance, &devCount, devices.data());
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to enumerate Vulkan physical devices", result);
    return;
  }

  bool foundDeviceAndQueue = false;
  for (uint32_t d = 0; d < devCount && !foundDeviceAndQueue; ++d) {
    VkPhysicalDevice candidate = devices[d];
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qfCount, nullptr);
    if (qfCount == 0)
      continue;

    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qfCount,
                                             qfProps.data());
    for (uint32_t i = 0; i < qfCount; i++) {
      VkBool32 presentSupport = VK_FALSE;
      result = vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, g_vkSurface,
                                                    &presentSupport);
      if (result != VK_SUCCESS)
        continue;
      if ((qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
        g_vkPhysicalDevice = candidate;
        g_vkGraphicsFamily = i;
        foundDeviceAndQueue = true;
        break;
      }
    }
  }
  if (!foundDeviceAndQueue || g_vkPhysicalDevice == VK_NULL_HANDLE) {
    debugVk("C4JRender_Vulkan: Failed to find graphics+present queue family.\n");
    return;
  }

  // ---- Logical device ----
  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo qCI = {};
  qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qCI.queueFamilyIndex = g_vkGraphicsFamily;
  qCI.queueCount = 1;
  qCI.pQueuePriorities = &queuePriority;

  const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo devCI = {};
  devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  devCI.queueCreateInfoCount = 1;
  devCI.pQueueCreateInfos = &qCI;
  devCI.enabledExtensionCount = 1;
  devCI.ppEnabledExtensionNames = devExts;

  result = vkCreateDevice(g_vkPhysicalDevice, &devCI, nullptr, &g_vkDevice);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create VkDevice", result);
    return;
  }
  vkGetDeviceQueue(g_vkDevice, g_vkGraphicsFamily, 0, &g_vkGraphicsQueue);
  if (g_vkGraphicsQueue == VK_NULL_HANDLE) {
    debugVk("C4JRender_Vulkan: Failed to get graphics queue.\n");
    return;
  }

  // ---- Swapchain ----
  VkSurfaceCapabilitiesKHR caps;
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysicalDevice,
                                                     g_vkSurface, &caps);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to query surface capabilities", result);
    return;
  }

  uint32_t formatCount = 0;
  result =
      vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysicalDevice, g_vkSurface,
                                           &formatCount, nullptr);
  if (result != VK_SUCCESS || formatCount == 0) {
    debugVk("C4JRender_Vulkan: No swapchain surface formats available.\n");
    return;
  }

  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysicalDevice, g_vkSurface,
                                                &formatCount, formats.data());
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to query surface formats", result);
    return;
  }

  VkSurfaceFormatKHR chosenFormat = formats[0];
  for (const auto &f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosenFormat = f;
      break;
    }
  }
  g_vkSwapFormat = chosenFormat.format;

  g_vkSwapExtent.width = (uint32_t)width;
  g_vkSwapExtent.height = (uint32_t)height;
  if (caps.currentExtent.width != UINT32_MAX) {
    g_vkSwapExtent = caps.currentExtent;
  }

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    imageCount = caps.maxImageCount;

  VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
  uint32_t presentModeCount = 0;
  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      g_vkPhysicalDevice, g_vkSurface, &presentModeCount, nullptr);
  if (result == VK_SUCCESS && presentModeCount > 0) {
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(
        g_vkPhysicalDevice, g_vkSurface, &presentModeCount,
        presentModes.data());
    if (result == VK_SUCCESS) {
      for (VkPresentModeKHR mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
          chosenPresentMode = mode;
          break;
        }
      }
      if (chosenPresentMode == VK_PRESENT_MODE_FIFO_KHR) {
        for (VkPresentModeKHR mode : presentModes) {
          if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            chosenPresentMode = mode;
            break;
          }
        }
      }
    }
  }

  if (chosenPresentMode == VK_PRESENT_MODE_MAILBOX_KHR && imageCount < 3) {
    imageCount = 3;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
      imageCount = caps.maxImageCount;
  }

  VkSwapchainCreateInfoKHR scCI = {};
  scCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  scCI.surface = g_vkSurface;
  scCI.minImageCount = imageCount;
  scCI.imageFormat = chosenFormat.format;
  scCI.imageColorSpace = chosenFormat.colorSpace;
  scCI.imageExtent = g_vkSwapExtent;
  scCI.imageArrayLayers = 1;
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    debugVk("C4JRender_Vulkan: Surface does not support COLOR_ATTACHMENT usage.\n");
    return;
  }
  scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  scCI.preTransform = caps.currentTransform;
  if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    scCI.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else if (caps.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  } else if (caps.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
  } else {
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }
  scCI.presentMode = chosenPresentMode;
  scCI.clipped = VK_TRUE;

  const char *presentModeName = "FIFO";
  if (chosenPresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
    presentModeName = "MAILBOX";
  } else if (chosenPresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
    presentModeName = "IMMEDIATE";
  }
  char presentModeLog[96];
  std::snprintf(presentModeLog, sizeof(presentModeLog),
                "C4JRender_Vulkan: Present mode %s\n", presentModeName);
  debugVk(presentModeLog);

  result = vkCreateSwapchainKHR(g_vkDevice, &scCI, nullptr, &g_vkSwapchain);
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to create swapchain", result);
    return;
  }

  uint32_t swapImgCount = 0;
  result =
      vkGetSwapchainImagesKHR(g_vkDevice, g_vkSwapchain, &swapImgCount, nullptr);
  if (result != VK_SUCCESS || swapImgCount == 0) {
    debugVk("C4JRender_Vulkan: Failed to query swapchain image count.\n");
    return;
  }
  g_vkSwapImages.resize(swapImgCount);
  result = vkGetSwapchainImagesKHR(g_vkDevice, g_vkSwapchain, &swapImgCount,
                                   g_vkSwapImages.data());
  if (result != VK_SUCCESS) {
    debugVkResult("Failed to get swapchain images", result);
    return;
  }
  g_vkSwapImageLayouts.assign(swapImgCount, VK_IMAGE_LAYOUT_UNDEFINED);

  if (!createTriangleRenderResources()) {
    debugVk("C4JRender_Vulkan: Failed to create triangle render resources.\n");
    return;
  }

  // ---- Command pool + buffer ----
  VkCommandPoolCreateInfo cpCI = {};
  cpCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpCI.queueFamilyIndex = g_vkGraphicsFamily;
  cpCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  result = vkCreateCommandPool(g_vkDevice, &cpCI, nullptr, &g_vkCommandPool);
  if (result != VK_SUCCESS || g_vkCommandPool == VK_NULL_HANDLE) {
    debugVkResult("Failed to create command pool", result);
    return;
  }

  VkCommandBufferAllocateInfo cbAI = {};
  cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbAI.commandPool = g_vkCommandPool;
  cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbAI.commandBufferCount = 1;
  result = vkAllocateCommandBuffers(g_vkDevice, &cbAI, &g_vkCommandBuffer);
  if (result != VK_SUCCESS || g_vkCommandBuffer == VK_NULL_HANDLE) {
    debugVkResult("Failed to allocate command buffer", result);
    return;
  }

  // ---- Sync objects ----
  VkFenceCreateInfo fCI = {};
  fCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  result = vkCreateFence(g_vkDevice, &fCI, nullptr, &g_vkFence);
  if (result != VK_SUCCESS || g_vkFence == VK_NULL_HANDLE) {
    debugVkResult("Failed to create fence", result);
    return;
  }

  VkSemaphoreCreateInfo sCI = {};
  sCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  result = vkCreateSemaphore(g_vkDevice, &sCI, nullptr, &g_vkImageAvailable);
  if (result != VK_SUCCESS || g_vkImageAvailable == VK_NULL_HANDLE) {
    debugVkResult("Failed to create image-available semaphore", result);
    return;
  }
  result = vkCreateSemaphore(g_vkDevice, &sCI, nullptr, &g_vkRenderFinished);
  if (result != VK_SUCCESS || g_vkRenderFinished == VK_NULL_HANDLE) {
    debugVkResult("Failed to create render-finished semaphore", result);
    return;
  }

  if (!ensureWhiteTexture()) {
    debugVk("C4JRender_Vulkan: Failed to create fallback white texture.\n");
    return;
  }

  g_vkInitialized = true;
  g_isWidescreen = (height > 0) ? (((float)width / (float)height) > 1.5f) : true;

  debugVk("C4JRender_Vulkan: Initialised OK!\n");
}

void C4JRender::InitialiseContext() {}
void C4JRender::Tick() {}
void C4JRender::UpdateGamma(unsigned short) {}

void C4JRender::StartFrame() {
  g_vkFrameVertexData.clear();
  g_vkQueuedDraws.clear();
  if (g_vkFrameVertexData.capacity() < g_vkFrameVertexReserveBytes) {
    g_vkFrameVertexData.reserve(g_vkFrameVertexReserveBytes);
  }
  if (g_vkQueuedDraws.capacity() < g_vkFrameDrawReserveCount) {
    g_vkQueuedDraws.reserve(g_vkFrameDrawReserveCount);
  }

  if (!g_vkInitialized)
    return;

  VkResult result = vkWaitForFences(g_vkDevice, 1, &g_vkFence, VK_TRUE,
                                    UINT64_MAX);
  if (result != VK_SUCCESS) {
    debugVkResult("vkWaitForFences failed", result);
    g_vkInitialized = false;
    return;
  }

  processPendingTextureUploads();
}

static bool updateUiDescriptorSet() {
  if (g_vkDevice == VK_NULL_HANDLE || g_vkUiDescriptorSet == VK_NULL_HANDLE)
    return false;
  if (g_vkUiImageView == VK_NULL_HANDLE || g_vkUiSampler == VK_NULL_HANDLE)
    return false;

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.sampler = g_vkUiSampler;
  imageInfo.imageView = g_vkUiImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = g_vkUiDescriptorSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(g_vkDevice, 1, &write, 0, nullptr);
  return true;
}

static bool recordPendingUiUpload(VkCommandBuffer cmd) {
  if (!g_vkUiUpload.pending)
    return true;
  if (g_vkDevice == VK_NULL_HANDLE)
    return false;

  const size_t dataBytes = g_vkUiUpload.pixelsBGRA.size();
  if (dataBytes == 0 || g_vkUiUpload.width == 0 || g_vkUiUpload.height == 0)
    return true;

  if (!createOrResizeUiImageResources(g_vkUiUpload.width, g_vkUiUpload.height))
    return false;
  if (!ensureUiStagingBuffer(dataBytes))
    return false;
  if (g_vkUiStagingMapped == nullptr)
    return false;

  VkResult result = VK_SUCCESS;
  std::memcpy(g_vkUiStagingMapped, g_vkUiUpload.pixelsBGRA.data(), dataBytes);
  if (!g_vkUiStagingHostCoherent) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = g_vkUiStagingMemory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    result = vkFlushMappedMemoryRanges(g_vkDevice, 1, &range);
    if (result != VK_SUCCESS) {
      debugVkResult("Failed to flush UI staging memory", result);
      return false;
    }
  }

  VkImageMemoryBarrier toTransfer = {};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.oldLayout = g_vkUiImageLayout;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = g_vkUiImage;
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.baseMipLevel = 0;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.baseArrayLayer = 0;
  toTransfer.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (g_vkUiImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (g_vkUiImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    toTransfer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  } else {
    toTransfer.srcAccessMask = 0;
  }
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &toTransfer);

  VkBufferImageCopy copyRegion = {};
  copyRegion.bufferOffset = 0;
  copyRegion.bufferRowLength = 0;
  copyRegion.bufferImageHeight = 0;
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.mipLevel = 0;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageOffset = {0, 0, 0};
  copyRegion.imageExtent = {g_vkUiUpload.width, g_vkUiUpload.height, 1};
  vkCmdCopyBufferToImage(cmd, g_vkUiStagingBuffer, g_vkUiImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

  VkImageMemoryBarrier toRead = {};
  toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.image = g_vkUiImage;
  toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toRead.subresourceRange.baseMipLevel = 0;
  toRead.subresourceRange.levelCount = 1;
  toRead.subresourceRange.baseArrayLayer = 0;
  toRead.subresourceRange.layerCount = 1;
  toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toRead);

  g_vkUiImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  g_vkUiImageReady = true;
  g_vkUiUpload.pending = false;

  updateUiDescriptorSet();
  return true;
}

void C4JRender::Present() {
  if (!g_vkInitialized)
    return;
  if (g_vkSwapImages.empty() || g_vkSwapImageLayouts.empty() ||
      g_vkFramebuffers.empty() || g_vkCommandBuffer == VK_NULL_HANDLE ||
      g_vkRenderPass == VK_NULL_HANDLE || g_vkTrianglePipeline == VK_NULL_HANDLE)
    return;

  uint32_t imageIndex = 0;
  bool needRecreateSwapchain = false;
  VkResult result =
      vkAcquireNextImageKHR(g_vkDevice, g_vkSwapchain, UINT64_MAX,
                            g_vkImageAvailable, VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    needRecreateSwapchain = true;
  } else if (result == VK_SUBOPTIMAL_KHR) {
    needRecreateSwapchain = true;
  } else if (result != VK_SUCCESS) {
    debugVkResult("vkAcquireNextImageKHR failed", result);
    return;
  }
  if (needRecreateSwapchain) {
    if (g_vkWindow != NULL) {
      int width = 0;
      int height = 0;
      if (getWindowClientSize(g_vkWindow, width, height)) {
        Initialise(g_vkWindow, width, height);
      }
    }
    return;
  }
  if (imageIndex >= g_vkSwapImages.size() ||
      imageIndex >= g_vkSwapImageLayouts.size() ||
      imageIndex >= g_vkFramebuffers.size()) {
    debugVk("C4JRender_Vulkan: swapchain image index out of range.\n");
    return;
  }

  uint64_t frameVertexCount = 0;
  for (const VulkanQueuedDraw &draw : g_vkQueuedDraws) {
    frameVertexCount += static_cast<uint64_t>(draw.vertexCount);
  }
  g_vkPerfDrawCount = static_cast<uint32_t>(g_vkQueuedDraws.size());
  g_vkPerfVertexCount =
      (frameVertexCount > 0xffffffffull) ? 0xffffffffu
                                         : static_cast<uint32_t>(frameVertexCount);
  g_vkPerfUploadBytes = (g_vkFrameVertexData.size() > 0xffffffffull)
                            ? 0xffffffffu
                            : static_cast<uint32_t>(g_vkFrameVertexData.size());

  if (g_vkPerfFreq.QuadPart == 0) {
    QueryPerformanceFrequency(&g_vkPerfFreq);
  }
  LARGE_INTEGER nowCounter = {};
  QueryPerformanceCounter(&nowCounter);
  if (g_vkPerfLastPresent.QuadPart != 0 && g_vkPerfFreq.QuadPart > 0) {
    const double dtSeconds =
        static_cast<double>(nowCounter.QuadPart - g_vkPerfLastPresent.QuadPart) /
        static_cast<double>(g_vkPerfFreq.QuadPart);
    if (dtSeconds > 0.000001) {
      const float instFps = static_cast<float>(1.0 / dtSeconds);
      if (g_vkPerfFpsSmoothed <= 0.0f) {
        g_vkPerfFpsSmoothed = instFps;
      } else {
        g_vkPerfFpsSmoothed =
            g_vkPerfFpsSmoothed * 0.9f + instFps * 0.1f;
      }
    }
  }
  g_vkPerfLastPresent = nowCounter;

  queueCornerOverlayText();
  uint32_t uiQuadFirstVertex = 0;
  uint32_t uiQuadVertexCount = 0;
  if (g_vkUiImageReady || g_vkUiUpload.pending) {
    appendUiCompositeFullscreenQuad(uiQuadFirstVertex, uiQuadVertexCount);
  }

  if (!g_vkFrameVertexData.empty()) {
    if (!ensureDynamicVertexBuffer(g_vkFrameVertexData.size())) {
      debugVk("C4JRender_Vulkan: Failed to ensure dynamic vertex buffer.\n");
      return;
    }
    if (g_vkDynamicVertexMapped == nullptr) {
      debugVk("C4JRender_Vulkan: Dynamic vertex buffer not mapped.\n");
      return;
    }
    std::memcpy(g_vkDynamicVertexMapped, g_vkFrameVertexData.data(),
                g_vkFrameVertexData.size());
    if (!g_vkDynamicVertexHostCoherent) {
      VkMappedMemoryRange range = {};
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.memory = g_vkDynamicVertexMemory;
      range.offset = 0;
      range.size = VK_WHOLE_SIZE;
      result = vkFlushMappedMemoryRanges(g_vkDevice, 1, &range);
      if (result != VK_SUCCESS) {
        debugVkResult("vkFlushMappedMemoryRanges failed", result);
        return;
      }
    }
  }

  result = vkResetCommandBuffer(g_vkCommandBuffer, 0);
  if (result != VK_SUCCESS) {
    debugVkResult("vkResetCommandBuffer failed", result);
    return;
  }

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(g_vkCommandBuffer, &beginInfo);
  if (result != VK_SUCCESS) {
    debugVkResult("vkBeginCommandBuffer failed", result);
    return;
  }

  if (!recordPendingUiUpload(g_vkCommandBuffer)) {
    debugVk("C4JRender_Vulkan: Failed to record pending UI upload.\n");
  }

  VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (g_vkSwapImageLayouts[imageIndex] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = g_vkSwapImageLayouts[imageIndex];
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = g_vkSwapImages[imageIndex];
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  vkCmdPipelineBarrier(g_vkCommandBuffer, srcStageMask,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  VkClearValue clearValues[2] = {};
  clearValues[0].color.float32[0] = g_clearColour[0];
  clearValues[0].color.float32[1] = g_clearColour[1];
  clearValues[0].color.float32[2] = g_clearColour[2];
  clearValues[0].color.float32[3] = g_clearColour[3];
  clearValues[1].depthStencil.depth = 1.0f;
  clearValues[1].depthStencil.stencil = 0;

  VkRenderPassBeginInfo rpBegin = {};
  rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpBegin.renderPass = g_vkRenderPass;
  rpBegin.framebuffer = g_vkFramebuffers[imageIndex];
  rpBegin.renderArea.offset = {0, 0};
  rpBegin.renderArea.extent = g_vkSwapExtent;
  rpBegin.clearValueCount = 2;
  rpBegin.pClearValues = clearValues;
  vkCmdBeginRenderPass(g_vkCommandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

  if (!g_vkQueuedDraws.empty()) {
    VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundDescriptorSet = VK_NULL_HANDLE;
    float lastBlendConstants[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    for (const VulkanQueuedDraw &draw : g_vkQueuedDraws) {
      const VkBuffer drawVertexBuffer =
          (draw.vertexBuffer != VK_NULL_HANDLE) ? draw.vertexBuffer
                                                : g_vkDynamicVertexBuffer;
      if (drawVertexBuffer == VK_NULL_HANDLE) {
        continue;
      }
      if (drawVertexBuffer != boundVertexBuffer) {
        VkBuffer vertexBuffers[] = {drawVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(g_vkCommandBuffer, 0, 1, vertexBuffers, offsets);
        boundVertexBuffer = drawVertexBuffer;
      }

      VkPipeline pipeline = getOrCreateTrianglePipeline(
          draw.depthTestEnable, draw.depthWriteEnable, draw.depthCompareOp,
          draw.blendEnable, draw.srcBlendFactor, draw.dstBlendFactor,
          draw.colorWriteMask, draw.cullEnable, draw.cullClockwise);
      if (pipeline == VK_NULL_HANDLE)
        continue;
      if (pipeline != boundPipeline) {
        vkCmdBindPipeline(g_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline);
        boundPipeline = pipeline;
      }
      if (std::memcmp(lastBlendConstants, draw.blendConstants,
                      sizeof(lastBlendConstants)) != 0) {
        vkCmdSetBlendConstants(g_vkCommandBuffer, draw.blendConstants);
        std::memcpy(lastBlendConstants, draw.blendConstants,
                    sizeof(lastBlendConstants));
      }
      VkDescriptorSet descriptorSet = draw.descriptorSet;
      if (descriptorSet == VK_NULL_HANDLE) {
        descriptorSet = resolveTextureDescriptorSet(-1);
      }
      if (descriptorSet == VK_NULL_HANDLE) {
        continue;
      }
      if (descriptorSet != boundDescriptorSet) {
        vkCmdBindDescriptorSets(g_vkCommandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_vkPipelineLayout, 0, 1, &descriptorSet, 0,
                                nullptr);
        boundDescriptorSet = descriptorSet;
      }

      TrianglePushConstants push = {};
      std::memcpy(push.mvp, draw.mvp, sizeof(push.mvp));
      push.alphaState[0] = draw.alphaTestEnable ? 1.0f : 0.0f;
      push.alphaState[1] = draw.alphaRef;
      push.alphaState[2] = static_cast<float>(draw.alphaFunc);
      push.alphaState[3] = 0.0f;
      vkCmdPushConstants(g_vkCommandBuffer, g_vkPipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(push), &push);
      vkCmdDraw(g_vkCommandBuffer, draw.vertexCount, 1, draw.firstVertex, 0);
    }
  }

  if (uiQuadVertexCount > 0 && g_vkUiImageReady &&
      g_vkUiPipeline != VK_NULL_HANDLE &&
      g_vkUiPipelineLayout != VK_NULL_HANDLE &&
      g_vkUiDescriptorSet != VK_NULL_HANDLE) {
    VkBuffer vertexBuffers[] = {g_vkDynamicVertexBuffer};
    VkDeviceSize offsets[] = {0};
    float identity[16];
    mat4_identity(identity);

    vkCmdBindPipeline(g_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      g_vkUiPipeline);
    vkCmdBindVertexBuffers(g_vkCommandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindDescriptorSets(g_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_vkUiPipelineLayout, 0, 1, &g_vkUiDescriptorSet, 0,
                            nullptr);
    vkCmdPushConstants(g_vkCommandBuffer, g_vkUiPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(identity),
                       identity);
    vkCmdDraw(g_vkCommandBuffer, uiQuadVertexCount, 1, uiQuadFirstVertex, 0);
  }
  vkCmdEndRenderPass(g_vkCommandBuffer);

  result = vkEndCommandBuffer(g_vkCommandBuffer);
  if (result != VK_SUCCESS) {
    debugVkResult("vkEndCommandBuffer failed", result);
    return;
  }

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &g_vkImageAvailable;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &g_vkCommandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &g_vkRenderFinished;

  result = vkResetFences(g_vkDevice, 1, &g_vkFence);
  if (result != VK_SUCCESS) {
    debugVkResult("vkResetFences failed", result);
    g_vkInitialized = false;
    return;
  }

  result = vkQueueSubmit(g_vkGraphicsQueue, 1, &submitInfo, g_vkFence);
  if (result != VK_SUCCESS) {
    debugVkResult("vkQueueSubmit failed", result);
    g_vkInitialized = false;
    return;
  }

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &g_vkRenderFinished;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &g_vkSwapchain;
  presentInfo.pImageIndices = &imageIndex;
  result = vkQueuePresentKHR(g_vkGraphicsQueue, &presentInfo);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
    debugVkResult("vkQueuePresentKHR failed", result);
    return;
  }
  g_vkSwapImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  if (g_vkFrameVertexData.size() > g_vkFrameVertexReserveBytes) {
    g_vkFrameVertexReserveBytes = g_vkFrameVertexData.size();
  }
  if (g_vkQueuedDraws.size() > g_vkFrameDrawReserveCount) {
    g_vkFrameDrawReserveCount = g_vkQueuedDraws.size();
  }

  if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
    if (g_vkWindow != NULL) {
      int width = 0;
      int height = 0;
      if (getWindowClientSize(g_vkWindow, width, height)) {
        Initialise(g_vkWindow, width, height);
      }
    }
  }
}
void C4JRender::DoScreenGrabOnNextPresent() {}
void C4JRender::Clear(int, C4JRect *) {}
void C4JRender::SetClearColour(const float c[4]) {
  g_clearColour[0] = c[0];
  g_clearColour[1] = c[1];
  g_clearColour[2] = c[2];
  g_clearColour[3] = c[3];
}
bool C4JRender::IsWidescreen() { return g_isWidescreen; }
bool C4JRender::IsHiDef() { return true; }
void C4JRender::CaptureThumbnail(ImageFileBuffer *) {}
void C4JRender::CaptureScreen(ImageFileBuffer *, XSOCIAL_PREVIEWIMAGE *) {}
void C4JRender::BeginConditionalSurvey(int) {}
void C4JRender::EndConditionalSurvey() {}
void C4JRender::BeginConditionalRendering(int) {}
void C4JRender::EndConditionalRendering() {}

// ============================================================================
//  Draw calls
// ============================================================================
static inline void unpack565ToRGBA(uint16_t packed, uint8_t &r, uint8_t &g,
                                   uint8_t &b, uint8_t &a) {
  const uint8_t r5 = static_cast<uint8_t>((packed >> 11) & 0x1f);
  const uint8_t g6 = static_cast<uint8_t>((packed >> 5) & 0x3f);
  const uint8_t b5 = static_cast<uint8_t>(packed & 0x1f);
  r = static_cast<uint8_t>((r5 * 255) / 31);
  g = static_cast<uint8_t>((g6 * 255) / 63);
  b = static_cast<uint8_t>((b5 * 255) / 31);
  a = 255;
}

static inline uint8_t modulate8(uint8_t value, float mul) {
  int out = static_cast<int>(static_cast<float>(value) * mul + 0.5f);
  if (out < 0)
    out = 0;
  if (out > 255)
    out = 255;
  return static_cast<uint8_t>(out);
}

static inline float decodeLegacyNoMipmapU(float u) {
  // old tesselator stores "no mip" as u+1.0f.
  if (u > 1.0f && u < 2.0f) {
    return u - 1.0f;
  }
  return u;
}

static inline bool vkColourMulIsIdentity() {
  return (std::fabs(g_vkStateColour[0] - 1.0f) <= 0.0001f) &&
         (std::fabs(g_vkStateColour[1] - 1.0f) <= 0.0001f) &&
         (std::fabs(g_vkStateColour[2] - 1.0f) <= 0.0001f) &&
         (std::fabs(g_vkStateColour[3] - 1.0f) <= 0.0001f);
}

static uint32_t expandVertexStreamToBootstrap(
    std::vector<uint8_t> &out, C4JRender::ePrimitiveType primitiveType,
    int count, C4JRender::eVertexType vType, const uint8_t *srcBytes,
    bool applyColourMul, float colourMulR, float colourMulG, float colourMulB,
    float colourMulA) {
  const bool isStandardVertex =
      (vType == C4JRender::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1 ||
       vType == C4JRender::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN ||
       vType == C4JRender::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_LIT);
  const bool isCompressedVertex = (vType == C4JRender::VERTEX_TYPE_COMPRESSED);
  if (!isStandardVertex && !isCompressedVertex)
    return 0;

  if (primitiveType != C4JRender::PRIMITIVE_TYPE_TRIANGLE_LIST &&
      primitiveType != C4JRender::PRIMITIVE_TYPE_QUAD_LIST) {
    return 0;
  }

  if (count <= 0 || srcBytes == nullptr)
    return 0;

  const uint32_t outVertexCount =
      (primitiveType == C4JRender::PRIMITIVE_TYPE_TRIANGLE_LIST)
          ? static_cast<uint32_t>(count)
          : static_cast<uint32_t>((count / 4) * 6);
  if (outVertexCount == 0)
    return 0;

  const size_t oldSize = out.size();
  out.resize(oldSize +
             static_cast<size_t>(outVertexCount) * kVertexStridePF3TF2CB4NB4XW1);
  uint8_t *dst = out.data() + oldSize;
  const size_t sourceStride =
      isCompressedVertex ? 16 : kVertexStridePF3TF2CB4NB4XW1;

  auto emitExpandedVertex = [&](int srcVertexIndex) {
    const uint8_t *src =
        srcBytes + static_cast<size_t>(srcVertexIndex) * sourceStride;
    if (isStandardVertex) {
      std::memcpy(dst, src, kVertexStridePF3TF2CB4NB4XW1);
      float srcU = 0.0f;
      std::memcpy(&srcU, src + 12, sizeof(float));
      srcU = decodeLegacyNoMipmapU(srcU);
      std::memcpy(dst + 12, &srcU, sizeof(float));
      // Input color bytes are ABGR in little-endian memory. Convert to RGBA.
      const uint8_t srcA = src[20];
      const uint8_t srcB = src[21];
      const uint8_t srcG = src[22];
      const uint8_t srcR = src[23];
      dst[20] = srcR;
      dst[21] = srcG;
      dst[22] = srcB;
      dst[23] = srcA;
      if (applyColourMul) {
        dst[20] = modulate8(dst[20], colourMulR);
        dst[21] = modulate8(dst[21], colourMulG);
        dst[22] = modulate8(dst[22], colourMulB);
        dst[23] = modulate8(dst[23], colourMulA);
      }
      dst += kVertexStridePF3TF2CB4NB4XW1;
      return;
    }

    const int16_t *s = reinterpret_cast<const int16_t *>(src);
    const float x = static_cast<float>(s[0]) / 1024.0f;
    const float y = static_cast<float>(s[1]) / 1024.0f;
    const float z = static_cast<float>(s[2]) / 1024.0f;
    const uint16_t encodedColour = static_cast<uint16_t>(s[3]);
    const uint16_t packed565 = static_cast<uint16_t>(encodedColour + 32768u);
    // Compact path can also carry the legacy "no mip" u+1 marker.
    const float u = decodeLegacyNoMipmapU(static_cast<float>(s[4]) / 8192.0f);
    const float v = static_cast<float>(s[5]) / 8192.0f;
    uint8_t r = 255, g = 255, b = 255, a = 255;
    unpack565ToRGBA(packed565, r, g, b, a);
    if (applyColourMul) {
      r = modulate8(r, colourMulR);
      g = modulate8(g, colourMulG);
      b = modulate8(b, colourMulB);
      a = modulate8(a, colourMulA);
    }

    BootstrapVertex outVertex = {x, y, z, u, v, r, g, b, a, 0, 0, 0, 0, 0};
    std::memcpy(dst, &outVertex, sizeof(outVertex));
    dst += sizeof(outVertex);
  };

  if (primitiveType == C4JRender::PRIMITIVE_TYPE_TRIANGLE_LIST) {
    for (int i = 0; i < count; ++i) {
      emitExpandedVertex(i);
    }
  } else {
    const int quadCount = count / 4;
    for (int q = 0; q < quadCount; ++q) {
      const int base = q * 4;
      emitExpandedVertex(base + 0);
      emitExpandedVertex(base + 1);
      emitExpandedVertex(base + 2);
      emitExpandedVertex(base + 2);
      emitExpandedVertex(base + 3);
      emitExpandedVertex(base + 0);
    }
  }

  return outVertexCount;
}

static void queuePreparedExpandedDrawWithMvp(const uint8_t *vertexBytes,
                                             uint32_t vertexCount,
                                             const float *mvp) {
  if (!g_vkInitialized || vertexBytes == nullptr || vertexCount == 0)
    return;

  const size_t firstVertex =
      g_vkFrameVertexData.size() / kVertexStridePF3TF2CB4NB4XW1;
  const size_t oldSize = g_vkFrameVertexData.size();
  const size_t addBytes =
      static_cast<size_t>(vertexCount) * kVertexStridePF3TF2CB4NB4XW1;
  g_vkFrameVertexData.resize(oldSize + addBytes);
  std::memcpy(g_vkFrameVertexData.data() + oldSize, vertexBytes, addBytes);

  VulkanQueuedDraw draw = {};
  draw.vertexBuffer = VK_NULL_HANDLE;
  draw.firstVertex = static_cast<uint32_t>(firstVertex);
  draw.vertexCount = vertexCount;
  draw.depthTestEnable = g_vkStateDepthTestEnable;
  draw.depthWriteEnable = g_vkStateDepthWriteEnable;
  draw.depthCompareOp = g_vkStateDepthCompareOp;
  draw.blendEnable = g_vkStateBlendEnable;
  draw.srcBlendFactor = g_vkStateSrcBlendFactor;
  draw.dstBlendFactor = g_vkStateDstBlendFactor;
  draw.colorWriteMask = g_vkStateColorWriteMask;
  draw.cullEnable = g_vkStateCullEnable;
  draw.cullClockwise = g_vkStateCullClockwise;
  std::memcpy(draw.blendConstants, g_vkStateBlendConstants,
              sizeof(draw.blendConstants));
  draw.descriptorSet = resolveTextureDescriptorSet(g_vkStateTextureId);
  draw.alphaTestEnable = g_vkStateAlphaTestEnable;
  draw.alphaFunc = g_vkStateAlphaFunc;
  draw.alphaRef = g_vkStateAlphaRef;
  if (mvp != nullptr) {
    std::memcpy(draw.mvp, mvp, sizeof(draw.mvp));
  } else {
    mat4_identity(draw.mvp);
  }

  appendQueuedDrawMerged(draw);
}

static void queuePreparedExpandedDrawFromBufferWithMvp(VkBuffer vertexBuffer,
                                                       uint32_t firstVertex,
                                                       uint32_t vertexCount,
                                                       const float *mvp) {
  if (!g_vkInitialized || vertexBuffer == VK_NULL_HANDLE || vertexCount == 0)
    return;

  VulkanQueuedDraw draw = {};
  draw.vertexBuffer = vertexBuffer;
  draw.firstVertex = firstVertex;
  draw.vertexCount = vertexCount;
  draw.depthTestEnable = g_vkStateDepthTestEnable;
  draw.depthWriteEnable = g_vkStateDepthWriteEnable;
  draw.depthCompareOp = g_vkStateDepthCompareOp;
  draw.blendEnable = g_vkStateBlendEnable;
  draw.srcBlendFactor = g_vkStateSrcBlendFactor;
  draw.dstBlendFactor = g_vkStateDstBlendFactor;
  draw.colorWriteMask = g_vkStateColorWriteMask;
  draw.cullEnable = g_vkStateCullEnable;
  draw.cullClockwise = g_vkStateCullClockwise;
  std::memcpy(draw.blendConstants, g_vkStateBlendConstants,
              sizeof(draw.blendConstants));
  draw.descriptorSet = resolveTextureDescriptorSet(g_vkStateTextureId);
  draw.alphaTestEnable = g_vkStateAlphaTestEnable;
  draw.alphaFunc = g_vkStateAlphaFunc;
  draw.alphaRef = g_vkStateAlphaRef;
  if (mvp != nullptr) {
    std::memcpy(draw.mvp, mvp, sizeof(draw.mvp));
  } else {
    mat4_identity(draw.mvp);
  }

  appendQueuedDrawMerged(draw);
}

void C4JRender::DrawVertices(ePrimitiveType primitiveType, int count,
                             void *dataIn, eVertexType vType,
                             ePixelShaderType psType) {
  if (!g_vkInitialized || count <= 0 || dataIn == nullptr)
    return;

  if (primitiveType != PRIMITIVE_TYPE_TRIANGLE_LIST &&
      primitiveType != PRIMITIVE_TYPE_QUAD_LIST)
    return;

  const bool isStandardVertex =
      (vType == VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1 ||
       vType == VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN ||
       vType == VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_LIT);
  const bool isCompressedVertex = (vType == VERTEX_TYPE_COMPRESSED);
  if (!isStandardVertex && !isCompressedVertex)
    return;

  if (g_vkIsRecordingCommandList && g_vkRecordingCommandListIndex >= 0) {
    RecordedDrawCall call = {};
    call.primitiveType = primitiveType;
    call.count = count;
    call.vType = vType;
    call.psType = psType;
    call.preparedVertexCount = 0;
    call.fullStateList = g_vkRecordingFullStateList;
    call.hasLocalModelMatrix = true;
    const float *recordModelView = MatrixGet(GL_MODELVIEW_MATRIX);
    if (recordModelView != nullptr && g_vkRecordingBaseModelValid) {
      // Display-list semantics: matrix ops inside the list are relative to
      // matrix state at CBuffStart, not absolute compile-time camera state.
      mat4_multiply(call.localModelMatrix, g_vkRecordingBaseModelInv,
                    recordModelView);
    } else {
      mat4_identity(call.localModelMatrix);
    }
    call.useCapturedState =
        g_vkRecordingFullStateList && g_vkRecordingHasStateChanges;
    call.depthTestEnable = g_vkStateDepthTestEnable;
    call.depthWriteEnable = g_vkStateDepthWriteEnable;
    call.depthCompareOp = g_vkStateDepthCompareOp;
    call.blendEnable = g_vkStateBlendEnable;
    call.srcBlendFactor = g_vkStateSrcBlendFactor;
    call.dstBlendFactor = g_vkStateDstBlendFactor;
    call.colorWriteMask = g_vkStateColorWriteMask;
    call.cullEnable = g_vkStateCullEnable;
    call.cullClockwise = g_vkStateCullClockwise;
    std::memcpy(call.blendConstants, g_vkStateBlendConstants,
                sizeof(call.blendConstants));
    // bug we hit: replaying compile-time textureId broke chunk textures.
    call.captureTextureState =
        g_vkRecordingFullStateList && g_vkRecordingHasTextureStateChanges;
    call.textureId = g_vkStateTextureId;
    call.captureAlphaState =
        g_vkRecordingFullStateList && g_vkRecordingHasAlphaStateChanges;
    call.alphaTestEnable = g_vkStateAlphaTestEnable;
    call.alphaFunc = g_vkStateAlphaFunc;
    call.alphaRef = g_vkStateAlphaRef;
    const size_t sourceStride =
        isCompressedVertex ? 16 : kVertexStridePF3TF2CB4NB4XW1;
    call.vertexData.resize(static_cast<size_t>(count) * sourceStride);
    std::memcpy(call.vertexData.data(), dataIn, call.vertexData.size());
    call.preparedVertexCount = expandVertexStreamToBootstrap(
        call.preparedVertexData, primitiveType, count, vType,
        call.vertexData.data(), false, 1.0f, 1.0f, 1.0f, 1.0f);
    g_vkRecordingScratch.push_back(std::move(call));
    return;
  }

  const uint8_t *srcBytes = static_cast<const uint8_t *>(dataIn);
  const size_t firstVertex =
      g_vkFrameVertexData.size() / kVertexStridePF3TF2CB4NB4XW1;
  const bool applyColourMul = !vkColourMulIsIdentity();
  uint32_t outVertexCount = expandVertexStreamToBootstrap(
      g_vkFrameVertexData, primitiveType, count, vType, srcBytes, applyColourMul,
      g_vkStateColour[0], g_vkStateColour[1], g_vkStateColour[2],
      g_vkStateColour[3]);
  if (outVertexCount == 0)
    return;

  VulkanQueuedDraw draw = {};
  draw.vertexBuffer = VK_NULL_HANDLE;
  draw.firstVertex = static_cast<uint32_t>(firstVertex);
  draw.vertexCount = outVertexCount;
  draw.depthTestEnable = g_vkStateDepthTestEnable;
  draw.depthWriteEnable = g_vkStateDepthWriteEnable;
  draw.depthCompareOp = g_vkStateDepthCompareOp;
  draw.blendEnable = g_vkStateBlendEnable;
  draw.srcBlendFactor = g_vkStateSrcBlendFactor;
  draw.dstBlendFactor = g_vkStateDstBlendFactor;
  draw.colorWriteMask = g_vkStateColorWriteMask;
  draw.cullEnable = g_vkStateCullEnable;
  draw.cullClockwise = g_vkStateCullClockwise;
  std::memcpy(draw.blendConstants, g_vkStateBlendConstants,
              sizeof(draw.blendConstants));
  draw.descriptorSet = resolveTextureDescriptorSet(g_vkStateTextureId);
  draw.alphaTestEnable = g_vkStateAlphaTestEnable;
  draw.alphaFunc = g_vkStateAlphaFunc;
  draw.alphaRef = g_vkStateAlphaRef;

  if (g_vkEnableMvpCache) {
    const float *mvp = getCurrentDrawMvp();
    if (mvp != nullptr) {
      std::memcpy(draw.mvp, mvp, sizeof(draw.mvp));
    } else {
      mat4_identity(draw.mvp);
    }
  } else {
    const float *modelView = MatrixGet(GL_MODELVIEW_MATRIX);
    const float *projection = MatrixGet(GL_PROJECTION_MATRIX);
    if (modelView != nullptr && projection != nullptr) {
      mat4_multiply(draw.mvp, projection, modelView);
    } else {
      mat4_identity(draw.mvp);
    }
  }

  appendQueuedDrawMerged(draw);
}
void C4JRender::DrawVertexBuffer(ePrimitiveType, int, void *, eVertexType,
                                 ePixelShaderType) {}

// ============================================================================
//  Command buffers
// ============================================================================
void C4JRender::CBuffLockStaticCreations() {}
int C4JRender::CBuffCreate(int count) {
  if (count <= 0)
    count = 1;
  std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
  int first = g_nextCommandBufferId;
  g_nextCommandBufferId += count;
  for (int i = 0; i < count; ++i) {
    g_vkCommandLists[first + i] = std::make_shared<std::vector<RecordedDrawCall>>();
  }
  return first;
}
void C4JRender::CBuffDelete(int first, int count) {
  if (count <= 0)
    count = 1;
  {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    for (int i = 0; i < count; ++i) {
      const int index = first + i;
      g_vkCommandLists.erase(index);
      auto gpuIt = g_vkCommandListGpuData.find(index);
      if (gpuIt != g_vkCommandListGpuData.end()) {
        gpuIt->second.usedBytes = 0;
        gpuIt->second.uploadPending = false;
      }
    }
  }
  if (g_vkIsRecordingCommandList &&
      g_vkRecordingCommandListIndex >= first &&
      g_vkRecordingCommandListIndex < (first + count)) {
    g_vkIsRecordingCommandList = false;
    g_vkRecordingCommandListIndex = -1;
    g_vkRecordingScratch.clear();
    g_vkRecordingHasStateChanges = false;
    g_vkRecordingHasTextureStateChanges = false;
    g_vkRecordingHasAlphaStateChanges = false;
    g_vkRecordingFullStateList = false;
    g_vkRecordingBaseModelValid = false;
  }
}
void C4JRender::CBuffStart(int index, bool full) {
  ensureThreadLocalMatrixStacksInitialised();
  g_vkIsRecordingCommandList = true;
  g_vkRecordingCommandListIndex = index;
  {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    if (g_vkCommandLists.find(index) == g_vkCommandLists.end())
      g_vkCommandLists[index] = std::make_shared<std::vector<RecordedDrawCall>>();
  }
  g_vkRecordingScratch.clear();
  g_vkRecordingHasStateChanges = false;
  g_vkRecordingHasTextureStateChanges = false;
  g_vkRecordingHasAlphaStateChanges = false;
  g_vkRecordingFullStateList = full;
  const float *baseModel = MatrixGet(GL_MODELVIEW_MATRIX);
  if (baseModel != nullptr) {
    g_vkRecordingBaseModelValid =
        mat4_inverse(g_vkRecordingBaseModelInv, baseModel);
  } else {
    g_vkRecordingBaseModelValid = false;
  }
  if (!g_vkRecordingBaseModelValid) {
    mat4_identity(g_vkRecordingBaseModelInv);
    g_vkRecordingBaseModelValid = true;
  }
}
void C4JRender::CBuffClear(int index) {
  std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
  g_vkCommandLists[index] = std::make_shared<std::vector<RecordedDrawCall>>();
  auto gpuIt = g_vkCommandListGpuData.find(index);
  if (gpuIt != g_vkCommandListGpuData.end()) {
    gpuIt->second.usedBytes = 0;
    gpuIt->second.uploadPending = false;
  }
}
int C4JRender::CBuffSize(int index) {
  // old renderers used this as allocator pressure.
  // for vulkan, returning big values here made chunk rebuild stall.
  if (index == -1) {
    return 0;
  }

  size_t bytes = 0;
  std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
  auto it = g_vkCommandLists.find(index);
  if (it != g_vkCommandLists.end() && it->second) {
    for (const auto &call : *it->second) {
      bytes += call.vertexData.size();
    }
  }
  return static_cast<int>(bytes);
}
void C4JRender::CBuffEnd() {
  if (g_vkIsRecordingCommandList && g_vkRecordingCommandListIndex >= 0) {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    std::shared_ptr<std::vector<RecordedDrawCall>> newCalls =
        std::make_shared<std::vector<RecordedDrawCall>>(
            std::move(g_vkRecordingScratch));
    g_vkCommandLists[g_vkRecordingCommandListIndex] =
        newCalls;
    g_vkCommandListGpuData[g_vkRecordingCommandListIndex].uploadPending = true;
    g_vkRecordingScratch.clear();
  } else {
    g_vkRecordingScratch.clear();
  }
  g_vkIsRecordingCommandList = false;
  g_vkRecordingCommandListIndex = -1;
  g_vkRecordingHasStateChanges = false;
  g_vkRecordingHasTextureStateChanges = false;
  g_vkRecordingHasAlphaStateChanges = false;
  g_vkRecordingFullStateList = false;
  g_vkRecordingBaseModelValid = false;
}
bool C4JRender::CBuffCall(int index, bool) {
  std::shared_ptr<std::vector<RecordedDrawCall>> calls;
  VkBuffer commandListVertexBuffer = VK_NULL_HANDLE;
  {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    auto it = g_vkCommandLists.find(index);
    if (it == g_vkCommandLists.end())
      return false;
    calls = it->second;
    auto gpuIt = g_vkCommandListGpuData.find(index);
    if (gpuIt != g_vkCommandListGpuData.end() &&
        !gpuIt->second.uploadPending &&
        gpuIt->second.vertexBuffer != VK_NULL_HANDLE &&
        gpuIt->second.usedBytes > 0) {
      commandListVertexBuffer = gpuIt->second.vertexBuffer;
    }
  }
  if (!calls)
    return false;

  if (commandListVertexBuffer == VK_NULL_HANDLE && g_vkDevice != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> commandListsLock(g_vkCommandListsMutex);
    auto it = g_vkCommandLists.find(index);
    if (it != g_vkCommandLists.end() && it->second) {
      auto &gpuData = g_vkCommandListGpuData[index];
      if (gpuData.uploadPending || gpuData.vertexBuffer == VK_NULL_HANDLE ||
          gpuData.usedBytes == 0) {
        if (uploadCommandListGpuData(index, it->second)) {
          auto gpuIt = g_vkCommandListGpuData.find(index);
          if (gpuIt != g_vkCommandListGpuData.end() &&
              !gpuIt->second.uploadPending &&
              gpuIt->second.vertexBuffer != VK_NULL_HANDLE &&
              gpuIt->second.usedBytes > 0) {
            commandListVertexBuffer = gpuIt->second.vertexBuffer;
          }
        }
      } else {
        commandListVertexBuffer = gpuData.vertexBuffer;
      }
    }
  }

  ensureThreadLocalMatrixStacksInitialised();
  float callSiteModelView[16];
  const float *callSiteModelViewPtr = MatrixGet(GL_MODELVIEW_MATRIX);
  if (callSiteModelViewPtr != nullptr) {
    std::memcpy(callSiteModelView, callSiteModelViewPtr,
                sizeof(callSiteModelView));
  } else {
    mat4_identity(callSiteModelView);
  }
  float callSiteProjection[16];
  const float *callSiteProjectionPtr = MatrixGet(GL_PROJECTION_MATRIX);
  if (callSiteProjectionPtr != nullptr) {
    std::memcpy(callSiteProjection, callSiteProjectionPtr,
                sizeof(callSiteProjection));
  } else {
    mat4_identity(callSiteProjection);
  }

  const bool wasRecording = g_vkIsRecordingCommandList;
  const int oldRecordingIndex = g_vkRecordingCommandListIndex;
  const bool oldRecordingFullStateList = g_vkRecordingFullStateList;
  const bool oldDepthTestEnable = g_vkStateDepthTestEnable;
  const bool oldDepthWriteEnable = g_vkStateDepthWriteEnable;
  const VkCompareOp oldDepthCompareOp = g_vkStateDepthCompareOp;
  const bool oldBlendEnable = g_vkStateBlendEnable;
  const VkBlendFactor oldSrcBlendFactor = g_vkStateSrcBlendFactor;
  const VkBlendFactor oldDstBlendFactor = g_vkStateDstBlendFactor;
  const VkColorComponentFlags oldColorWriteMask = g_vkStateColorWriteMask;
  const bool oldCullEnable = g_vkStateCullEnable;
  const bool oldCullClockwise = g_vkStateCullClockwise;
  const int oldTextureId = g_vkStateTextureId;
  const bool oldAlphaTestEnable = g_vkStateAlphaTestEnable;
  const int oldAlphaFunc = g_vkStateAlphaFunc;
  const float oldAlphaRef = g_vkStateAlphaRef;
  float oldBlendConstants[4] = {g_vkStateBlendConstants[0],
                                g_vkStateBlendConstants[1],
                                g_vkStateBlendConstants[2],
                                g_vkStateBlendConstants[3]};
  g_vkIsRecordingCommandList = false;
  g_vkRecordingCommandListIndex = -1;
  g_vkRecordingFullStateList = false;

  for (const RecordedDrawCall &call : *calls) {
    if (call.vertexData.empty() || call.count <= 0)
      continue;
    if (call.fullStateList && call.useCapturedState) {
      g_vkStateDepthTestEnable = call.depthTestEnable;
      g_vkStateDepthWriteEnable = call.depthWriteEnable;
      g_vkStateDepthCompareOp = call.depthCompareOp;
      g_vkStateBlendEnable = call.blendEnable;
      g_vkStateSrcBlendFactor = call.srcBlendFactor;
      g_vkStateDstBlendFactor = call.dstBlendFactor;
      g_vkStateColorWriteMask = call.colorWriteMask;
      g_vkStateCullEnable = call.cullEnable;
      g_vkStateCullClockwise = call.cullClockwise;
      std::memcpy(g_vkStateBlendConstants, call.blendConstants,
                  sizeof(call.blendConstants));
    }
    if (call.fullStateList && call.captureTextureState) {
      // only change texture state if this draw recorded a TextureBind.
      g_vkStateTextureId = call.textureId;
    }
    if (call.fullStateList && call.captureAlphaState) {
      g_vkStateAlphaTestEnable = call.alphaTestEnable;
      g_vkStateAlphaFunc = call.alphaFunc;
      g_vkStateAlphaRef = call.alphaRef;
    }
    const bool canUsePrepared =
        vkColourMulIsIdentity() && (call.preparedVertexCount > 0) &&
        !call.preparedVertexData.empty();
    if (canUsePrepared) {
      float drawMvp[16];
      if (call.hasLocalModelMatrix) {
        float combinedModelView[16];
        mat4_multiply(combinedModelView, callSiteModelView, call.localModelMatrix);
        mat4_multiply(drawMvp, callSiteProjection, combinedModelView);
      } else {
        mat4_multiply(drawMvp, callSiteProjection, callSiteModelView);
      }
      if (commandListVertexBuffer != VK_NULL_HANDLE && call.gpuVertexCount > 0) {
        queuePreparedExpandedDrawFromBufferWithMvp(
            commandListVertexBuffer, call.gpuFirstVertex, call.gpuVertexCount,
            drawMvp);
      } else {
        queuePreparedExpandedDrawWithMvp(call.preparedVertexData.data(),
                                         call.preparedVertexCount, drawMvp);
      }
    } else {
      if (call.hasLocalModelMatrix) {
        float combinedModelView[16];
        mat4_multiply(combinedModelView, callSiteModelView, call.localModelMatrix);
        std::memcpy(g_matStacks[GL_MODELVIEW].stack[g_matStacks[GL_MODELVIEW].top],
                    combinedModelView, sizeof(combinedModelView));
        g_matrixDirty = true;
      }
      DrawVertices(call.primitiveType, call.count,
                   const_cast<uint8_t *>(call.vertexData.data()), call.vType,
                   call.psType);
      if (call.hasLocalModelMatrix) {
        std::memcpy(g_matStacks[GL_MODELVIEW].stack[g_matStacks[GL_MODELVIEW].top],
                    callSiteModelView, sizeof(callSiteModelView));
        g_matrixDirty = true;
      }
    }
  }

  g_vkStateDepthTestEnable = oldDepthTestEnable;
  g_vkStateDepthWriteEnable = oldDepthWriteEnable;
  g_vkStateDepthCompareOp = oldDepthCompareOp;
  g_vkStateBlendEnable = oldBlendEnable;
  g_vkStateSrcBlendFactor = oldSrcBlendFactor;
  g_vkStateDstBlendFactor = oldDstBlendFactor;
  g_vkStateColorWriteMask = oldColorWriteMask;
  g_vkStateCullEnable = oldCullEnable;
  g_vkStateCullClockwise = oldCullClockwise;
  g_vkStateTextureId = oldTextureId;
  g_vkStateAlphaTestEnable = oldAlphaTestEnable;
  g_vkStateAlphaFunc = oldAlphaFunc;
  g_vkStateAlphaRef = oldAlphaRef;
  std::memcpy(g_vkStateBlendConstants, oldBlendConstants,
              sizeof(oldBlendConstants));
  g_vkIsRecordingCommandList = wasRecording;
  g_vkRecordingCommandListIndex = oldRecordingIndex;
  g_vkRecordingFullStateList = oldRecordingFullStateList;
  return true;
}
void C4JRender::CBuffTick() {}
void C4JRender::CBuffDeferredModeStart() {}
void C4JRender::CBuffDeferredModeEnd() {}

// ============================================================================
//  Textures
// ============================================================================
int C4JRender::TextureCreate() {
  const int id = g_nextTextureId++;
  VulkanTexture tex = {};
  tex.id = id;
  tex.width = 0;
  tex.height = 0;
  tex.mipLevels = 1;
  tex.image = VK_NULL_HANDLE;
  tex.memory = VK_NULL_HANDLE;
  tex.imageView = VK_NULL_HANDLE;
  tex.sampler = VK_NULL_HANDLE;
  tex.descriptorSet = VK_NULL_HANDLE;
  tex.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  tex.minFilter = GL_NEAREST;
  tex.magFilter = GL_NEAREST;
  tex.wrapS = GL_REPEAT;
  tex.wrapT = GL_REPEAT;
  tex.requestedMipLevels = 1;
  tex.pendingUpload = false;
  tex.levelData.clear();
  g_vkTextures[id] = tex;
  return id;
}

void C4JRender::TextureFree(int idx) {
  if (idx <= 0)
    return;
  auto it = g_vkTextures.find(idx);
  if (it == g_vkTextures.end())
    return;
  destroyTextureGpuResources(it->second);
  g_vkTextures.erase(it);
  if (g_vkStateTextureId == idx)
    g_vkStateTextureId = -1;
  if (g_vkStateVertexTextureId == idx)
    g_vkStateVertexTextureId = -1;
}

void C4JRender::TextureBind(int idx) {
  g_vkStateTextureId = idx;
  // record that this bind happened, so replay uses it.
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasTextureStateChanges = true;
  if (idx < 0)
    return;
  auto it = g_vkTextures.find(idx);
  if (it == g_vkTextures.end())
    return;
  if (it->second.pendingUpload) {
    ensureTextureUploadedFromCache(it->second);
  } else if (it->second.image != VK_NULL_HANDLE &&
             it->second.descriptorSet == VK_NULL_HANDLE &&
             hasTextureUploadContext()) {
    updateTextureDescriptorSet(it->second);
  }
}

void C4JRender::TextureBindVertex(int idx) { g_vkStateVertexTextureId = idx; }

void C4JRender::TextureSetTextureLevels(int levels) {
  if (levels < 1)
    levels = 1;
  if (levels > 16)
    levels = 16;
  g_vkPendingTextureLevels = levels;
  auto it = g_vkTextures.find(g_vkStateTextureId);
  if (it != g_vkTextures.end()) {
    it->second.requestedMipLevels = static_cast<uint32_t>(levels);
  }
}

int C4JRender::TextureGetTextureLevels() {
  auto it = g_vkTextures.find(g_vkStateTextureId);
  if (it != g_vkTextures.end() && it->second.mipLevels > 0) {
    return static_cast<int>(it->second.mipLevels);
  }
  return g_vkPendingTextureLevels;
}

void C4JRender::TextureData(int width, int height, void *data, int level,
                            eTextureFormat format) {
  (void)format;
  if (g_vkStateTextureId < 0 || width <= 0 || height <= 0 || data == nullptr ||
      level < 0) {
    return;
  }

  auto it = g_vkTextures.find(g_vkStateTextureId);
  if (it == g_vkTextures.end()) {
    return;
  }

  VulkanTexture &tex = it->second;
  const uint32_t uploadLevel = static_cast<uint32_t>(level);
  cacheTextureLevelPixels(tex, uploadLevel, static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height), data);

  uint32_t requestedLevels =
      static_cast<uint32_t>((g_vkPendingTextureLevels > 0) ? g_vkPendingTextureLevels
                                                           : 1);
  if (requestedLevels <= uploadLevel)
    requestedLevels = uploadLevel + 1;
  tex.requestedMipLevels = requestedLevels;

  if (!hasTextureUploadContext()) {
    return;
  }

  if (ensureTextureUploadedFromCache(tex)) {
    return;
  }

  uint32_t targetLevels =
      static_cast<uint32_t>((g_vkPendingTextureLevels > 0) ? g_vkPendingTextureLevels
                                                           : 1);
  if (targetLevels <= uploadLevel)
    targetLevels = uploadLevel + 1;

  uint32_t baseWidth = static_cast<uint32_t>(width);
  uint32_t baseHeight = static_cast<uint32_t>(height);
  if (uploadLevel > 0) {
    baseWidth = baseWidth << uploadLevel;
    baseHeight = baseHeight << uploadLevel;
  }

  const bool needsRecreate =
      (tex.image == VK_NULL_HANDLE) || (tex.width != baseWidth) ||
      (tex.height != baseHeight) || (tex.mipLevels != targetLevels);
  if (needsRecreate) {
    if (!createTextureImageAndView(tex, baseWidth, baseHeight, targetLevels))
      return;
  }

  if (uploadTextureRegionImmediate(tex, uploadLevel, 0, 0,
                                   static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height), data)) {
    tex.pendingUpload = false;
  }
}

void C4JRender::TextureDataUpdate(int xoffset, int yoffset, int width, int height,
                                  void *data, int level) {
  if (g_vkStateTextureId < 0 || width <= 0 || height <= 0 || data == nullptr ||
      level < 0 || xoffset < 0 || yoffset < 0) {
    return;
  }

  auto it = g_vkTextures.find(g_vkStateTextureId);
  if (it == g_vkTextures.end()) {
    return;
  }

  VulkanTexture &tex = it->second;
  const uint32_t uploadLevel = static_cast<uint32_t>(level);
  const uint32_t uWidth = static_cast<uint32_t>(width);
  const uint32_t uHeight = static_cast<uint32_t>(height);
  const uint32_t uX = static_cast<uint32_t>(xoffset);
  const uint32_t uY = static_cast<uint32_t>(yoffset);
  bool cachedPatch = patchTextureLevelPixels(tex, uploadLevel, uX, uY, uWidth,
                                             uHeight, data);
  if (!cachedPatch && xoffset == 0 && yoffset == 0) {
    cacheTextureLevelPixels(tex, uploadLevel, uWidth, uHeight, data);
    cachedPatch = true;
  }

  if (!hasTextureUploadContext()) {
    if (!cachedPatch) {
      TextureData(width, height, data, level, TEXTURE_FORMAT_RxGyBzAw);
    }
    return;
  }

  if (cachedPatch && ensureTextureUploadedFromCache(tex)) {
    return;
  }

  if (tex.image == VK_NULL_HANDLE || uploadLevel >= tex.mipLevels) {
    TextureData(width, height, data, level, TEXTURE_FORMAT_RxGyBzAw);
    return;
  }

  uploadTextureRegionImmediate(
      tex, uploadLevel, uX, uY, uWidth, uHeight, data);
  tex.pendingUpload = false;
}

void C4JRender::TextureSetParam(int param, int value) {
  auto it = g_vkTextures.find(g_vkStateTextureId);
  if (it == g_vkTextures.end())
    return;

  VulkanTexture &tex = it->second;
  switch (param) {
  case GL_TEXTURE_MIN_FILTER:
    tex.minFilter = value;
    break;
  case GL_TEXTURE_MAG_FILTER:
    tex.magFilter = value;
    break;
  case GL_TEXTURE_WRAP_S:
    tex.wrapS = value;
    break;
  case GL_TEXTURE_WRAP_T:
    tex.wrapT = value;
    break;
  default:
    return;
  }
  if (tex.image != VK_NULL_HANDLE) {
    recreateTextureSampler(tex);
  }
}
void C4JRender::TextureDynamicUpdateStart() {}
void C4JRender::TextureDynamicUpdateEnd() {}

HRESULT C4JRender::LoadTextureData(const char *szFilename,
                                   D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut) {
  if (!szFilename || !pSrcInfo || !ppDataOut)
    return E_INVALIDARG;

  *ppDataOut = nullptr;
  pSrcInfo->Width = 0;
  pSrcInfo->Height = 0;

  HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool shouldUninit = SUCCEEDED(coHr);
  if (coHr == RPC_E_CHANGED_MODE)
    coHr = S_OK;
  if (FAILED(coHr))
    return coHr;

  std::wstring wpath = toWidePath(szFilename);
  if (wpath.empty()) {
    if (shouldUninit)
      CoUninitialize();
    return E_INVALIDARG;
  }

  IWICImagingFactory *factory = nullptr;
  IWICBitmapDecoder *decoder = nullptr;
  IWICBitmapFrameDecode *frame = nullptr;

  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) {
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr,
                                            GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad,
                                            &decoder);
  }
  if (SUCCEEDED(hr)) {
    hr = decoder->GetFrame(0, &frame);
  }
  if (SUCCEEDED(hr)) {
    hr = decodeFrameToArgb(frame, pSrcInfo, ppDataOut);
  }

  SafeReleaseCOM(frame);
  SafeReleaseCOM(decoder);
  SafeReleaseCOM(factory);
  if (shouldUninit)
    CoUninitialize();
  return hr;
}

HRESULT C4JRender::LoadTextureData(BYTE *pbData, DWORD dwBytes,
                                   D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut) {
  if (!pbData || dwBytes == 0 || !pSrcInfo || !ppDataOut)
    return E_INVALIDARG;

  *ppDataOut = nullptr;
  pSrcInfo->Width = 0;
  pSrcInfo->Height = 0;

  HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool shouldUninit = SUCCEEDED(coHr);
  if (coHr == RPC_E_CHANGED_MODE)
    coHr = S_OK;
  if (FAILED(coHr))
    return coHr;

  IWICImagingFactory *factory = nullptr;
  IWICStream *stream = nullptr;
  IWICBitmapDecoder *decoder = nullptr;
  IWICBitmapFrameDecode *frame = nullptr;

  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) {
    hr = factory->CreateStream(&stream);
  }
  if (SUCCEEDED(hr)) {
    hr = stream->InitializeFromMemory(pbData, dwBytes);
  }
  if (SUCCEEDED(hr)) {
    hr = factory->CreateDecoderFromStream(stream, nullptr,
                                          WICDecodeMetadataCacheOnLoad,
                                          &decoder);
  }
  if (SUCCEEDED(hr)) {
    hr = decoder->GetFrame(0, &frame);
  }
  if (SUCCEEDED(hr)) {
    hr = decodeFrameToArgb(frame, pSrcInfo, ppDataOut);
  }

  SafeReleaseCOM(frame);
  SafeReleaseCOM(decoder);
  SafeReleaseCOM(stream);
  SafeReleaseCOM(factory);
  if (shouldUninit)
    CoUninitialize();
  return hr;
}
HRESULT C4JRender::SaveTextureData(const char *, D3DXIMAGE_INFO *, int *) {
  return E_NOTIMPL;
}
HRESULT C4JRender::SaveTextureDataToMemory(void *, int, int *, int, int,
                                           int *) {
  return E_NOTIMPL;
}
void C4JRender::TextureGetStats() {}
void *C4JRender::TextureGetTexture(int) { return nullptr; }

// ============================================================================
//  State control
// ============================================================================
void C4JRender::StateSetColour(float r, float g, float b, float a) {
  auto clamp01 = [](float value) {
    if (value < 0.0f)
      return 0.0f;
    if (value > 1.0f)
      return 1.0f;
    return value;
  };
  g_vkStateColour[0] = clamp01(r);
  g_vkStateColour[1] = clamp01(g);
  g_vkStateColour[2] = clamp01(b);
  g_vkStateColour[3] = clamp01(a);
}
void C4JRender::StateSetDepthMask(bool enable) {
  g_vkStateDepthWriteEnable = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetBlendEnable(bool enable) {
  g_vkStateBlendEnable = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetBlendFunc(int src, int dst) {
  g_vkStateSrcBlendFactor = mapBlendFactorToVk(src);
  g_vkStateDstBlendFactor = mapBlendFactorToVk(dst);
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetBlendFactor(unsigned int colour) {
  const float a = static_cast<float>((colour >> 24) & 0xff) / 255.0f;
  const float r = static_cast<float>((colour >> 16) & 0xff) / 255.0f;
  const float g = static_cast<float>((colour >> 8) & 0xff) / 255.0f;
  const float b = static_cast<float>(colour & 0xff) / 255.0f;
  g_vkStateBlendConstants[0] = r;
  g_vkStateBlendConstants[1] = g;
  g_vkStateBlendConstants[2] = b;
  g_vkStateBlendConstants[3] = a;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetAlphaFunc(int func, float param) {
  g_vkStateAlphaFunc = func;
  g_vkStateAlphaRef = param;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasAlphaStateChanges = true;
}
void C4JRender::StateSetDepthFunc(int func) {
  g_vkStateDepthCompareOp = mapDepthFuncToVk(func);
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetFaceCull(bool enable) {
  g_vkStateCullEnable = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetFaceCullCW(bool enable) {
  g_vkStateCullClockwise = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetLineWidth(float) {}
void C4JRender::StateSetWriteEnable(bool red, bool green, bool blue, bool alpha) {
  VkColorComponentFlags mask = 0;
  if (red)
    mask |= VK_COLOR_COMPONENT_R_BIT;
  if (green)
    mask |= VK_COLOR_COMPONENT_G_BIT;
  if (blue)
    mask |= VK_COLOR_COMPONENT_B_BIT;
  if (alpha)
    mask |= VK_COLOR_COMPONENT_A_BIT;
  g_vkStateColorWriteMask = mask;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetDepthTestEnable(bool enable) {
  g_vkStateDepthTestEnable = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasStateChanges = true;
}
void C4JRender::StateSetAlphaTestEnable(bool enable) {
  g_vkStateAlphaTestEnable = enable;
  if (g_vkIsRecordingCommandList && g_vkRecordingFullStateList)
    g_vkRecordingHasAlphaStateChanges = true;
}
void C4JRender::StateSetDepthSlopeAndBias(float, float) {}
void C4JRender::StateSetFogEnable(bool) {}
void C4JRender::StateSetFogMode(int) {}
void C4JRender::StateSetFogNearDistance(float) {}
void C4JRender::StateSetFogFarDistance(float) {}
void C4JRender::StateSetFogDensity(float) {}
void C4JRender::StateSetFogColour(float, float, float) {}
void C4JRender::StateSetLightingEnable(bool) {}
void C4JRender::StateSetVertexTextureUV(float, float) {}
void C4JRender::StateSetLightColour(int, float, float, float) {}
void C4JRender::StateSetLightAmbientColour(float, float, float) {}
void C4JRender::StateSetLightDirection(int, float, float, float) {}
void C4JRender::StateSetLightEnable(int, bool) {}
void C4JRender::StateSetViewport(eViewportType) {}
void C4JRender::StateSetEnableViewportClipPlanes(bool) {}
void C4JRender::StateSetTexGenCol(int, float, float, float, float, bool) {}
void C4JRender::StateSetStencil(int, uint8_t, uint8_t, uint8_t) {}
void C4JRender::StateSetForceLOD(int) {}

// ============================================================================
//  Events + PLM
// ============================================================================
void C4JRender::BeginEvent(LPCWSTR) {}
void C4JRender::EndEvent() {}
void C4JRender::Suspend() {}
bool C4JRender::Suspended() { return false; }
void C4JRender::Resume() {}
