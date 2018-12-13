#include "library.h"

uint32_t gObjectId {0u};

template<class T>
const T* FindPNext( const void* pNext, VkStructureType sType )
{
  struct Header
  {
    VkStructureType sType;
    void* pNext;
  };

  while( pNext )
  {
    if( reinterpret_cast<const Header*>(pNext)->sType == sType )
    {
      break;
    }
  }

  if( pNext )
  {
    return reinterpret_cast<const T*>( pNext );
  }

  return nullptr;
}

struct ObjectTracker
{
  ObjectTracker()
  {
  }

  ~ObjectTracker()
  {
  }

  struct ObjectInfo
  {
    uint32_t          uid;
    void*             object;
    uint32_t          createThreadId;
    std::vector<char> createInfoData;
    std::vector<char> userData;

    // timestamp: create

    // timestamp: destroyed
  };

  std::vector<ObjectInfo> vulkanObjects{};
  std::vector<ObjectInfo> destroyedVulkanObjects{};

  template<class T>
  inline void* ToVoid( T& object )
  {
    return reinterpret_cast<void*>(object);
  }

  template<class TYPE>
  uint32_t FindUID( TYPE& object )
  {
    auto it = std::find_if( vulkanObjects.begin(), vulkanObjects.end(), [&](auto& item)
    {
      return item.object == this->ToVoid(object);
    });
    if( it == vulkanObjects.end() )
    {
      return 0u;
    }
    return it->uid;
  }

  template<class TYPE>
  TYPE FindByUID( uint32_t uid )
  {
    auto it = std::find_if( vulkanObjects.begin(), vulkanObjects.end(), [&](auto& item)
    {
      return item.uid == uid;
    });
    if( it == vulkanObjects.end() )
    {
      return 0u;
    }
    return reinterpret_cast<TYPE>(it->object);
  }

  template<class TYPE>
  uint32_t Add( TYPE& object )
  {
    ObjectInfo info{};
    ++uuid;
    info.uid = uuid;
    vulkanObjects.emplace_back( info );
    return uuid;
  }

  template<class TYPE>
  uint32_t Remove( TYPE& object )
  {
    auto it = std::find_if( vulkanObjects.begin(), vulkanObjects.end(), [&](auto& item)
    {
      return item.object == this->ToVoid(object);
    });
    if( it == vulkanObjects.end() )
    {
      return 0u;
    }

    auto retval = it->uid;
    vulkanObjects.erase(it);
    return retval;
  }

  template<class T1, class T2>
  void MakeDependency( T1& depender, T2& dependee )
  {
    auto t1 = ToVoid( depender );
    auto t2 = ToVoid( dependee );
    auto it = dependencyMap.find(depender);

    std::vector<void*>* list = nullptr;

    if( it == dependencyMap.end() )
    {
      dependencyMap[depender] = {};
      it = dependencyMap.find(depender);
    }

    it->second.emplace_back( dependee );
  }

  template<class T1, class T2>
  void LinkObjects( T1& user, T2& used )
  {

  }

  uint32_t uuid { 0u };
  std::map<void*, std::vector<void*>> dependencyMap{};
};

void test()
{
  ObjectTracker tracker;

  VkImage image{};
  tracker.Add( image );
  tracker.Remove( image );

  tracker.MakeDependency( image, image );
}

struct ImageResource
{
  uint32_t        uid;
  VkImage         image;
  VkDeviceMemory  memoryBound;
};

struct MemoryResource
{
  uint32_t       uid;
  VkDeviceMemory memory;
  VkMemoryAllocateInfo allocateInfo;
};

struct BufferResource
{
  VkImage                      image;
  VkDeviceMemory               memory;
  VkDevice                     device;
  std::vector<VkDescriptorSet> boundDS;
};

std::map<VkImageView, VkImage> g_ImageView2Image;
std::vector<ImageResource> g_Images;
std::vector<MemoryResource> g_memories;

VkImage find_image_by_view( VkImageView iv )
{
  lock();
  auto it = g_ImageView2Image.find( iv );
  if( it == g_ImageView2Image.end() )
  {
    return 0u;
  }
  return it->second;
}

ImageResource* find_image_resource( VkImage image )
{
  lock();
  auto it = std::find_if( g_Images.begin(), g_Images.end(), [&](auto& item)
  {
    return item.image == image;
  });
  if( it == g_Images.end() )
  {
    return nullptr;
  }
  return &*it;
}

MemoryResource* find_memory_resource( VkDeviceMemory memory )
{
  lock();
  auto it = std::find_if( g_memories.begin(), g_memories.end(), [&](auto& item)
  {
    return item.memory == memory;
  });
  if( it == g_memories.end() )
  {
    return nullptr;
  }
  return &*it;
}


uint32_t add_image( VkImage image )
{
  if(!image)
    return 0u;
  lock();
  gObjectId++;
  g_Images.emplace_back(ImageResource() = {gObjectId, image});
  return gObjectId;
}

uint32_t remove_image( VkImage image )
{
  if(!image)
    return 0u;
  lock();
  auto it = std::find_if( g_Images.begin(), g_Images.end(), [&]( auto& item)
  {
    return item.image == image;
  });

  uint32_t retval = 0u;
  if( it != g_Images.end() )
  {
    retval = it->uid;
    g_Images.erase(it);
  }
  return retval;
}

uint32_t get_image_uid( VkImage image )
{
  lock();
  auto it = std::find_if( g_Images.begin(), g_Images.end(), [&]( auto& item)
  {
    return item.image == image;
  });

  if( it != g_Images.end() )
  {
    return it->uid;
  }
  return 0u;
}

uint32_t add_memory( VkDeviceMemory memory, const VkMemoryAllocateInfo* pAllocateInfo )
{
  if(!memory)
    return 0u;
  lock();
  gObjectId++;
  g_memories.emplace_back(MemoryResource() = {gObjectId, memory, *pAllocateInfo });
  return gObjectId;
}

uint32_t remove_memory( VkDeviceMemory memory )
{
  if(!memory)
    return 0u;
  lock();
  auto it = std::find_if( g_memories.begin(), g_memories.end(), [&]( auto& item)
  {
    return item.memory == memory;
  });

  uint32_t retval = 0u;
  if( it != g_memories.end() )
  {
    retval = it->uid;
    g_memories.erase(it);
  }
  return retval;
}

uint32_t get_memory_uid( VkDeviceMemory memory )
{
  lock();
  auto it = std::find_if( g_memories.begin(), g_memories.end(), [&]( auto& item)
  {
    return item.memory == memory;
  });

  if( it != g_memories.end() )
  {
    return it->uid;
  }
  return 0u;
}

extern "C" uint32_t dali_vkGetMemoryUid( VkDeviceMemory memory )
{
  return get_memory_uid(memory);
}

extern "C" uint32_t dali_vkGetImageUid( VkImage image )
{
  return get_image_uid( image );
}


VkDeviceMemory get_image_memory( VkImage image )
{
  auto res = find_image_resource( image );
  if(!res)
    return 0u;
  return res->memoryBound;
}

bool remove_imageview( VkImageView iv )
{
  lock();
  auto it = g_ImageView2Image.find( iv );
  if( it == g_ImageView2Image.end() )
    return false;
  g_ImageView2Image.erase( it );
  return true;
}

// Vulkan functions
VKAPI_ATTR VkResult VKAPI_CALL dali_vkCreateImage(
  VkDevice                                    device,
  const VkImageCreateInfo*                    pCreateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkImage*                                    pImage)
{
  auto userData = FindPNext<VkDaliUserData>( pCreateInfo->pNext, VK_STRUCTURE_TYPE_DALI_USER_DATA );

  if( userData )
  {
    puts(reinterpret_cast<const char*>(userData->pUserData));
  }

  auto result = get_device_dispatch(device)->CreateImage(device, pCreateInfo, pAllocator, pImage);

  auto imageId = 0u;
  if( result == VK_SUCCESS )
  {
    imageId = add_image(*pImage);
  }

  LOGVK ("[%s]:[BI=%d FN=%s] vkCreateImage( %p, %p, %p, %p ( = %d ) ) := %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         pCreateInfo,
         pAllocator,
         pImage,
         int(imageId),
         result
         );

  LOGVK( "[%s]:[BI=%d FN=%s]    Image %d: w = %d, h = %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         int(imageId), int(pCreateInfo->extent.width), int(pCreateInfo->extent.height));
  return result;
}

VKAPI_ATTR void VKAPI_CALL dali_vkDestroyImage(
  VkDevice                                    device,
  VkImage                                     image,
  const VkAllocationCallbacks*                pAllocator)
{
  get_device_dispatch(device)->DestroyImage( device, image, pAllocator );

  auto uid = remove_image( image );

  LOGVK ("[%s]:[BI=%d FN=%s] vkDestroyImage( %p, %p ( %d ), %p )",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         ptr(image),
         int(uid),
         pAllocator
  );
}

VKAPI_ATTR VkResult VKAPI_CALL dali_vkCreateImageView(
  VkDevice                                    device,
  const VkImageViewCreateInfo*                pCreateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkImageView*                                pView)
{
  auto result = get_device_dispatch(device)->CreateImageView( device, pCreateInfo, pAllocator, pView );

  if( result == VK_SUCCESS )
  {
    lock();
    g_ImageView2Image[*pView] = pCreateInfo->image;
  }

  return result;
}

VKAPI_ATTR void VKAPI_CALL dali_vkDestroyImageView(
  VkDevice                                    device,
  VkImageView                                 imageView,
  const VkAllocationCallbacks*                pAllocator)
{
  get_device_dispatch(device)->DestroyImageView( device, imageView, pAllocator );

  remove_imageview( imageView );
}

VKAPI_ATTR VkResult VKAPI_CALL dali_vkBindImageMemory(
  VkDevice                                    device,
  VkImage                                     image,
  VkDeviceMemory                              memory,
  VkDeviceSize                                memoryOffset)
{
  auto result = get_device_dispatch(device)->BindImageMemory(
    device, image, memory, memoryOffset );

  auto imageUid = 0u;
  auto memoryUid = 0u;
  if( result == VK_SUCCESS )
  {
    lock();
    auto res = find_image_resource( image );
    imageUid = res->uid;
    memoryUid = find_memory_resource( memory )->uid;
    res->memoryBound = memory;
  }

  LOGVK ("[%s]:[BI=%d FN=%s] vkBindImageMemory( %p, %p ( %d ), %p ( %d ), %d ) := %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         ptr(image),
         int(imageUid),
         ptr(memory),
         int(memoryUid),
         int(memoryOffset),
         int(result)
  );
  return result;
}

VKAPI_ATTR VkResult VKAPI_CALL dali_vkAllocateMemory(
  VkDevice                                    device,
  const VkMemoryAllocateInfo*                 pAllocateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkDeviceMemory*                             pMemory)
{
  auto result = get_device_dispatch(device)->AllocateMemory(
    device, pAllocateInfo, pAllocator, pMemory );

  auto uid = 0u;

  if( result == VK_SUCCESS )
  {
    lock();
    add_memory( *pMemory, pAllocateInfo );
  }

  LOGVK ("[%s]:[BI=%d FN=%s] vkAllocateMemory( %p, %p, %p, %p ( %d ) ) := %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         pAllocateInfo,
         pAllocator,
         ptr(*pMemory),
         int(uid),
         int(result)
  );

  return result;
}

VKAPI_ATTR void VKAPI_CALL dali_vkFreeMemory(
  VkDevice                                    device,
  VkDeviceMemory                              memory,
  const VkAllocationCallbacks*                pAllocator)
{
  get_device_dispatch(device)->FreeMemory( device, memory, pAllocator );

  auto uid = remove_memory( memory );

  LOGVK ("[%s]:[BI=%d FN=%s] vkFreeMemory( %p, %p ( %d ), %p )",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         ptr(memory),
         int(uid),
         pAllocator);
}

static uint32_t gPipelines { 0u };

VKAPI_ATTR VkResult VKAPI_CALL dali_vkCreateGraphicsPipelines(
  VkDevice                                    device,
  VkPipelineCache                             pipelineCache,
  uint32_t                                    createInfoCount,
  const VkGraphicsPipelineCreateInfo*         pCreateInfos,
  const VkAllocationCallbacks*                pAllocator,
  VkPipeline*                                 pPipelines)
{
  auto result = get_device_dispatch(device)->CreateGraphicsPipelines(
    device,
    pipelineCache,
    createInfoCount,
    pCreateInfos,
    pAllocator,
    pPipelines );

  if( result == VK_SUCCESS )
  {
    lock();
    gPipelines += createInfoCount;
  }

  LOGVK ("[%s]:[BI=%d FN=%s] vkCreateGraphicsPipelines( %p, %p, %d, %p, %p, %p ) := %d , total: %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         ptr(pipelineCache),
         int(createInfoCount),
         pCreateInfos,
         pAllocator,
         pPipelines,
         int(result),
         int(gPipelines));

  return result;
}

VKAPI_ATTR void VKAPI_CALL dali_vkDestroyPipeline(
  VkDevice                                    device,
  VkPipeline                                  pipeline,
  const VkAllocationCallbacks*                pAllocator)
{
  get_device_dispatch(device)->DestroyPipeline( device, pipeline, pAllocator );

  {
    lock();
    gPipelines--;
  }

  LOGVK("[%s]:[BI=%d FN=%s] vkDestroyPipeline( %p, %p, %p ) , total: %d",
         get_process_name(), get_buffer_index(), get_caller_funtion_name(),
         ptr(device),
         ptr(pipeline),
         ptr(pAllocator),
         int(gPipelines));
}

void resources_update_dispatch_table( VkLayerDispatchTable& dispatchTable, PFN_vkGetDeviceProcAddr gdpa, VkDevice* pDevice )
{
  ADD_DISPATCH( CreateImage );
  ADD_DISPATCH( DestroyImage );
  ADD_DISPATCH( CreateImageView );
  ADD_DISPATCH( DestroyImageView );
  ADD_DISPATCH( BindImageMemory );
  ADD_DISPATCH( AllocateMemory );
  ADD_DISPATCH( FreeMemory );
  ADD_DISPATCH( CreateGraphicsPipelines );
  ADD_DISPATCH( DestroyPipeline );
}

PFN_vkVoidFunction resources_dispatch( const char* pName )
{
  DISPATCH( vkCreateImage );
  DISPATCH( vkDestroyImage )
  DISPATCH( vkCreateImageView );
  DISPATCH( vkDestroyImageView );
  DISPATCH( vkBindImageMemory );
  DISPATCH( vkAllocateMemory );
  DISPATCH( vkFreeMemory );
  DISPATCH( vkCreateGraphicsPipelines );
  DISPATCH( vkDestroyPipeline );
  return 0u;
}