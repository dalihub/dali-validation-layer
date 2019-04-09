#ifndef VK_LAYER_DALI_LIBRARY_H
#define VK_LAYER_DALI_LIBRARY_H

#include "vulkan.h"
#include <stdio.h>
#include <string>
#include "vk_layer.h"
#include "vk_layer_dispatch_table.h"
#include "vk_platform.h"
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <memory>
#include <cstring>
#include <sstream>

#ifdef TIZEN
#include <dlog/dlog.h>
#undef LOG_TAG
#define LOG_TAG "VK_LAYER_DALI"
#define LOGVK(...) LOGE(__VA_ARGS__);
#else
#define LOGVK(...) printf(__VA_ARGS__);puts("");
#endif

#define ADD_DISPATCH(fn) dispatchTable.fn = (PFN_vk ## fn)gdpa( *pDevice, "vk" #fn );
#define DISPATCH(func) if( std::string(pName) == #func ) return PFN_vkVoidFunction(dali_ ## func );

using InstanceDispatch = std::map<void*,VkLayerInstanceDispatchTable>;
using DeviceDispatch = std::map<void*,VkLayerDispatchTable>;

extern "C" VkLayerDispatchTable*   get_device_dispatch( VkDevice device );
extern "C" VkLayerInstanceDispatchTable* get_instance_dispatch( VkInstance instance );

void resources_update_dispatch_table( VkLayerDispatchTable& dispatchTable, PFN_vkGetDeviceProcAddr gdpa, VkDevice* pDevice );
PFN_vkVoidFunction resources_dispatch( const char* pName );

const char* get_process_name();
uint32_t    get_buffer_index();
const char* get_caller_funtion_name();

VkImage find_image_by_view( VkImageView iv );
uint32_t get_image_uid( VkImage image );
uint32_t get_memory_uid( VkDeviceMemory memory );
VkDeviceMemory get_image_memory( VkImage image );

template<typename T>
void* ptr( T& vkobject )
{
  return (void*)(vkobject);
}

static const VkStructureType VK_STRUCTURE_TYPE_DALI_USER_DATA = VkStructureType(0xff00001);

struct VkDaliUserData
{
  VkStructureType sType;
  void*           pNext;
  void*           pUserData;
  uint32_t        userDataSize;
};

extern std::recursive_mutex gMutex;
#define lock() std::lock_guard<std::recursive_mutex> lck(gMutex);
#endif