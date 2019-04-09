#include "library.h"

using std::map;

// must be set externally

template<typename DispatchableType>
void *GetKey(DispatchableType inst)
{
  return *(void **)inst;
}

std::recursive_mutex gMutex;

thread_local std::string gCallerFunctionName;

map<void*,VkLayerInstanceDispatchTable> instance_dispatch;
map<void*,VkLayerDispatchTable> device_dispatch;
static uint32_t vk_dali_bufferIndexDummy = 0xff;
uint32_t* vk_dali_bufferIndex = &vk_dali_bufferIndexDummy;
static const char* gProcessName = nullptr;
#define vk_dali_bufferIndex(device) buffer_index_map[device]

const char* get_process_name()
{
  return gProcessName;
}
uint32_t get_buffer_index()
{
  return *vk_dali_bufferIndex;
}
const char* get_caller_funtion_name()
{
  return gCallerFunctionName.c_str();
}

extern "C" VkLayerDispatchTable* get_device_dispatch( VkDevice device )
{
  return &device_dispatch[device];
}

extern "C" VkLayerInstanceDispatchTable* get_instance_dispatch( VkInstance instance )
{
  return &instance_dispatch[instance];
}

/**
 * Commands
 */
using Type = uint32_t;

struct CommandBase
{
  CommandBase() = default;
  virtual ~CommandBase() = default;

  virtual std::string GetString() = 0;

  virtual void AddLogLine( const std::string& s ) = 0;

  virtual const std::vector<std::string>& GetLog() = 0;
};

template<class... Args>
struct Command : public CommandBase
{
  explicit Command(const char* name, Args... args ) :
    mArgs( args... )
  {
    strcpy( mName, name );
  }

  explicit Command(const std::string& str, Args... args ) :
    mStrArgs(str),
    mArgs( args... )
  {
    mName[0] = 0;
  }

  ~Command() = default;

  void AddLogLine( const std::string& s ) override
  {
    mExtraLogs.emplace_back( s );
  }

  const std::vector<std::string>& GetLog() override
  {
    return mExtraLogs;
  }

  std::string GetString() override
  {
    if( !mName[0] )
    {
      return mStrArgs;
    }
    return std::string( mName );
  }

  char     mName[64];
  std::string mStrArgs{};
  std::vector<std::string> mExtraLogs;
  std::tuple<Args...> mArgs;
};

struct CommandBuffer
{
  VkCommandBuffer vkCommandBuffer;
  VkDevice        vkDevice;
  std::vector<std::unique_ptr<CommandBase>> commands;

  CommandBase* back()
  {
    return commands.back().get();
  }
};

std::map<VkCommandBuffer, CommandBuffer> g_commandBuffers;

// to be called by graphics backend
extern "C" void dali_vkDebugSetBufferIndexPtr(VkInstance instance, uint32_t* ptr )
{
  LOGVK("Linked Layer with buffer index %p", ptr);
  vk_dali_bufferIndex = ptr;
  gProcessName = getenv("_");
}

extern "C" void dali_vkDebugPrintCommandBuffer( VkCommandBuffer cmdbuf )
{
  lock();

  auto it = g_commandBuffers.find( cmdbuf );
  if( it == g_commandBuffers.end() )
  {
    LOGVK("ERROR, command buffer not found!");
  }

  LOGVK("[BI=%d FN=%s] Commands: %d", *vk_dali_bufferIndex, gCallerFunctionName.c_str(), int(it->second.commands.size()));
  for( auto& c : it->second.commands )
  {
    LOGVK("[BI=%d FN=%s]    CMD: %s", *vk_dali_bufferIndex, gCallerFunctionName.c_str(), c->GetString().c_str());
    const auto& logs = c->GetLog();
    if( !logs.empty() )
    {
      for( const auto& l : logs )
      {
        LOGVK("[BI=%d FN=%s]          >> %s", *vk_dali_bufferIndex, gCallerFunctionName.c_str(), l.c_str());
      }
    }
  }
}

extern "C" void dali_vkDebugPrintStr(const char* str )
{
  LOGVK("[BI=%d FN=%s] %s", *vk_dali_bufferIndex, gCallerFunctionName.c_str(), str);
}

extern "C" void dali_vkDebugSetCallerFunctionName( const char* callerName )
{
  gCallerFunctionName = callerName;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkWaitForFences(
  VkDevice                                    device,
  uint32_t                                    fenceCount,
  const VkFence*                              pFences,
  VkBool32                                    waitAll,
  uint64_t                                    timeout)
{
  auto result = device_dispatch[device].WaitForFences( device, fenceCount, pFences, waitAll, timeout );

  LOGVK("[%s]:[BI=%d FN=%s] vkWaitForFences( dev=%p, fc=%d, pF=%p, wA=%d, to=%d) := %d",
    gProcessName,
    *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device),
        int(fenceCount),
        ptr(pFences),
        int(waitAll),
        int(timeout),
        result);
  for( auto i = 0u; i < fenceCount; ++i )
  {
    LOGVK("[%s]:[BI=%d FN=%s]      Fence = %p",
      gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(pFences[i]));
  }

  return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkBeginCommandBuffer(
  VkCommandBuffer                             commandBuffer,
  const VkCommandBufferBeginInfo*             pBeginInfo)
{
  auto result = (device_dispatch.begin()->second).BeginCommandBuffer( commandBuffer, pBeginInfo );
  {
    lock();
    auto it = g_commandBuffers.find( commandBuffer );
    if( it != g_commandBuffers.end() )
    {

      // clear commands on reset
      it->second.commands.clear();
    }
  }

  LOGVK("[%s]:[BI=%d FN=%s] vkBeginCommandBuffer( %p, %p) := %d",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
  ptr(commandBuffer), pBeginInfo, result );

  return result;
}

extern "C"  VKAPI_ATTR VkResult VKAPI_CALL dali_vkEndCommandBuffer(
  VkCommandBuffer                             commandBuffer)
{

  auto result = (device_dispatch.begin()->second).EndCommandBuffer( commandBuffer );
  LOGVK("[%s]:[BI=%d FN=%s] vkEndCommandBuffer( %p ) := %d",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(commandBuffer), result );
  return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkAllocateCommandBuffers(
  VkDevice                                    device,
  const VkCommandBufferAllocateInfo*          pAllocateInfo,
  VkCommandBuffer*                            pCommandBuffers)
{
  auto result = device_dispatch[device].AllocateCommandBuffers( device, pAllocateInfo, pCommandBuffers );

  LOGVK("[%s]:[BI=%d FN=%s] vkAllocateCommandBuffers( %p, %p, %p ) := %d",
        gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device), ptr(pAllocateInfo), ptr(pCommandBuffers), result );

  {
    lock();

    for( auto i = 0u; i < pAllocateInfo->commandBufferCount; ++i )
    {
      CommandBuffer buffer;
      buffer.vkDevice = device;
      buffer.vkCommandBuffer = pCommandBuffers[i];
      pCommandBuffers[i];
      g_commandBuffers[buffer.vkCommandBuffer] = std::move(buffer);
    }
  }

  return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkFreeCommandBuffers(
  VkDevice                                    device,
  VkCommandPool                               commandPool,
  uint32_t                                    commandBufferCount,
  const VkCommandBuffer*                      pCommandBuffers)
{
  LOGVK("[%s]:[BI=%d FN=%s] vkFreeCommandBuffers( %p, %d, %p )",
        gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device), int(commandBufferCount), ptr(pCommandBuffers));

  device_dispatch[device].FreeCommandBuffers( device, commandPool, commandBufferCount, pCommandBuffers );

  lock();
  // remove from list
  for( auto i = 0u; i < commandBufferCount; ++i )
  {
    auto it =
    std::find_if( g_commandBuffers.begin(), g_commandBuffers.end(), [&](auto& item)
    {
      return ( item.first == pCommandBuffers[i] );
    });
    if( it != g_commandBuffers.end() )
    {
      g_commandBuffers.erase(it);
    }
  }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkResetCommandBuffer(
  VkCommandBuffer                             commandBuffer,
  VkCommandBufferResetFlags                   flags)
{
  {
    lock();
    auto it = g_commandBuffers.find( commandBuffer );
    if( it != g_commandBuffers.end() )
    {

      // clear commands on reset
      it->second.commands.clear();
    }
  }
  auto result = (device_dispatch.begin()->second).ResetCommandBuffer( commandBuffer, flags );
  LOGVK("[%s]:[BI=%d FN=%s] vkResetCommandBuffer( %p ) := %d",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(commandBuffer), result );
  return result;

}

extern "C"  VKAPI_ATTR VkResult VKAPI_CALL dali_vkResetFences(
  VkDevice                                    device,
  uint32_t                                    fenceCount,
  const VkFence*                              pFences)
{
  auto result = device_dispatch[device].ResetFences( device, fenceCount, pFences );
  LOGVK("[%s]:[BI=%d FN=%s] vkResetFences( dev=%p, fc=%d, pF=%p) := %d",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device),
        int(fenceCount),
        ptr(pFences), result
  );
  for( auto i = 0u; i < fenceCount; ++i )
  {
    LOGVK("[%s]:[BI=%d FN=%s]      Fence = %p", gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(pFences[i]));
  }

  return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL dali_vkCreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
  LOGVK("[%s]:[%d] CreateInstance()", gProcessName, *vk_dali_bufferIndex);
  VkLayerInstanceCreateInfo *layerCreateInfo = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;

  // step through the chain of pNext until we get to the link info
  while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
  {
    layerCreateInfo = (VkLayerInstanceCreateInfo *)layerCreateInfo->pNext;
  }

  if(layerCreateInfo == NULL)
  {
    // No loader instance create info
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
//  PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  // move chain on for next layer
  layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;
  
  PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");

  VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);
  
   // fetch our own dispatch table for the functions we need, into the next layer
  VkLayerInstanceDispatchTable dispatchTable{};
  dispatchTable.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gipa(*pInstance, "vkGetInstanceProcAddr");
  dispatchTable.CreateDevice = (PFN_vkCreateDevice)gipa(*pInstance, "vkCreateDevice");

  instance_dispatch[GetKey(pInstance)] = dispatchTable;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL dali_vkCreateDevice(
  VkPhysicalDevice                            physicalDevice,
  const VkDeviceCreateInfo*                   pCreateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkDevice*                                   pDevice)
{
  auto layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

  // step through the chain of pNext until we get to the link info
  while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
  {
    layerCreateInfo = decltype(layerCreateInfo)(layerCreateInfo->pNext);
  }

  if(layerCreateInfo == NULL)
  {
    // No loader instance create info
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  // move chain on for next layeEnd
  layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

  PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");

  auto retval = createFunc( physicalDevice, pCreateInfo, pAllocator, pDevice );

  VkLayerDispatchTable dispatchTable{};
  dispatchTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gdpa( *pDevice, "vkGetDeviceProcAddr" );
  dispatchTable.WaitForFences = (PFN_vkWaitForFences)gdpa( *pDevice, "vkWaitForFences" );
  dispatchTable.ResetFences = (PFN_vkResetFences)gdpa( *pDevice, "vkResetFences" );
  dispatchTable.BeginCommandBuffer = (PFN_vkBeginCommandBuffer)gdpa( *pDevice, "vkBeginCommandBuffer" );
  dispatchTable.EndCommandBuffer = (PFN_vkEndCommandBuffer)gdpa( *pDevice, "vkEndCommandBuffer" );
  dispatchTable.ResetCommandBuffer = (PFN_vkResetCommandBuffer)gdpa( *pDevice, "vkResetCommandBuffer" );
  dispatchTable.QueueSubmit = (PFN_vkQueueSubmit)gdpa( *pDevice, "vkQueueSubmit" );

  ADD_DISPATCH( CreateDescriptorPool );
  ADD_DISPATCH( DestroyDescriptorPool );
  ADD_DISPATCH( AllocateDescriptorSets );
  ADD_DISPATCH( FreeDescriptorSets );
  ADD_DISPATCH( UpdateDescriptorSets );
  ADD_DISPATCH( GetDeviceQueue );
  ADD_DISPATCH( AllocateCommandBuffers );
  ADD_DISPATCH( FreeCommandBuffers );
  // Commands
  ADD_DISPATCH( CmdBindPipeline );
  ADD_DISPATCH( CmdSetViewport );
  ADD_DISPATCH( CmdSetScissor );
  ADD_DISPATCH( CmdSetLineWidth );
  ADD_DISPATCH( CmdSetDepthBias );
  ADD_DISPATCH( CmdSetBlendConstants );
  ADD_DISPATCH( CmdSetDepthBounds );
  ADD_DISPATCH( CmdSetStencilCompareMask );
  ADD_DISPATCH( CmdSetStencilWriteMask );
  ADD_DISPATCH( CmdSetStencilReference );
  ADD_DISPATCH( CmdBindDescriptorSets );
  ADD_DISPATCH( CmdBindIndexBuffer );
  ADD_DISPATCH( CmdBindVertexBuffers );
  ADD_DISPATCH( CmdDraw );
  ADD_DISPATCH( CmdDrawIndexed );
  ADD_DISPATCH( CmdDrawIndirect );
  ADD_DISPATCH( CmdDrawIndexedIndirect );
  ADD_DISPATCH( CmdDispatch );
  ADD_DISPATCH( CmdDispatchIndirect );
  ADD_DISPATCH( CmdCopyBuffer );
  ADD_DISPATCH( CmdCopyImage );
  ADD_DISPATCH( CmdBlitImage );
  ADD_DISPATCH( CmdCopyBufferToImage );
  ADD_DISPATCH( CmdCopyImageToBuffer );
  ADD_DISPATCH( CmdUpdateBuffer );
  ADD_DISPATCH( CmdFillBuffer );
  ADD_DISPATCH( CmdClearColorImage );
  ADD_DISPATCH( CmdClearDepthStencilImage );
  ADD_DISPATCH( CmdClearAttachments );
  ADD_DISPATCH( CmdResolveImage );
  ADD_DISPATCH( CmdSetEvent );
  ADD_DISPATCH( CmdResetEvent );
  ADD_DISPATCH( CmdWaitEvents );
  ADD_DISPATCH( CmdPipelineBarrier );
  ADD_DISPATCH( CmdBeginQuery );
  ADD_DISPATCH( CmdEndQuery );
  ADD_DISPATCH( CmdResetQueryPool );
  ADD_DISPATCH( CmdWriteTimestamp );
  ADD_DISPATCH( CmdCopyQueryPoolResults );
  ADD_DISPATCH( CmdPushConstants );
  ADD_DISPATCH( CmdBeginRenderPass );
  ADD_DISPATCH( CmdNextSubpass );
  ADD_DISPATCH( CmdEndRenderPass );
  ADD_DISPATCH( CmdExecuteCommands );

  resources_update_dispatch_table( dispatchTable, gdpa, pDevice );

  device_dispatch[GetKey(pDevice)] = dispatchTable;

  return VK_SUCCESS;
}

static std::map<VkQueue, VkDevice> g_QueueDeviceMap{};
extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkQueueSubmit(
  VkQueue                                     queue,
  uint32_t                                    submitCount,
  const VkSubmitInfo*                         pSubmits,
  VkFence                                     fence)
{
  auto result = device_dispatch[g_QueueDeviceMap[queue]].QueueSubmit( queue, submitCount, pSubmits, fence );
  LOGVK("[%s]:[BI=%d FN=%s] vkQueueSubmit( q=%p, sc=%d, pS=%p, fence=%p) := %d",
    gProcessName,
    *vk_dali_bufferIndex,
    gCallerFunctionName.c_str(),
        ptr(queue),
        submitCount,
        ptr(pSubmits),
        ptr(fence),
        int(result));

  return result;
}

/***********************************************************************
 *
 * Pool verification
 */

struct DsPoolAllocation
{
  struct Binding
  {
    VkDescriptorType  type;
    uint32_t          binding;
    VkImageView       imageView;
    VkBuffer          buffer;
  };

  struct Set
  {
    VkDescriptorSet       set{0u};
    std::vector<Binding>  bindings{};
  };
  std::vector<Set> sets;

  VkDescriptorPoolCreateInfo  createInfo;
};

static std::map<VkDescriptorPool, DsPoolAllocation> g_dsPoolMap{};


extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkCreateDescriptorPool(
  VkDevice                                    device,
  const VkDescriptorPoolCreateInfo*           pCreateInfo,
  const VkAllocationCallbacks*                pAllocator,
  VkDescriptorPool*                           pDescriptorPool)
{
  auto result = device_dispatch[device].CreateDescriptorPool( device, pCreateInfo, pAllocator, pDescriptorPool );

  LOGVK("[%s]:[BI=%d FN=%s] vkCreateDescriptorPool( %p, %p, %p, %p ) := %d",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
    ptr(device), pCreateInfo, pAllocator, ptr(pDescriptorPool), result
  );

  lock();
  g_dsPoolMap[*pDescriptorPool] = { {}, *pCreateInfo };
  return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkDestroyDescriptorPool(
  VkDevice                                    device,
  VkDescriptorPool                            descriptorPool,
  const VkAllocationCallbacks*                pAllocator)
{
  device_dispatch[device].DestroyDescriptorPool( device, descriptorPool, pAllocator );

  LOGVK("[%s]:[BI=%d FN=%s] vkDestroyDescriptorPool( %p, %p, %p )",
    gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device), ptr(descriptorPool), pAllocator
  );

  lock();
  auto it = g_dsPoolMap.find( descriptorPool );
  if( it != g_dsPoolMap.end())
  {
    g_dsPoolMap.erase(it);
  }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkAllocateDescriptorSets(
  VkDevice                                    device,
  const VkDescriptorSetAllocateInfo*          pAllocateInfo,
  VkDescriptorSet*                            pDescriptorSets)
{
  auto result = device_dispatch[device].AllocateDescriptorSets( device, pAllocateInfo, pDescriptorSets );

  LOGVK("[%s]:[BI=%d FN=%s] vkAllocateyDescriptorSet( %p, %p, %p ) := %d", gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
        ptr(device), pAllocateInfo, ptr(pDescriptorSets), result
  );

  lock();
  auto& pool = g_dsPoolMap[pAllocateInfo->descriptorPool];
  for( auto i = 0; i < pAllocateInfo->descriptorSetCount; ++i )
  {
    pool.sets.emplace_back();
    pool.sets.back().set = pDescriptorSets[i];
    pool.sets.back().bindings = {};
  }

  return result;
}

VKAPI_ATTR void VKAPI_CALL dali_vkGetDeviceQueue(
  VkDevice                                    device,
  uint32_t                                    queueFamilyIndex,
  uint32_t                                    queueIndex,
  VkQueue*                                    pQueue)
{
  device_dispatch[device].GetDeviceQueue( device, queueFamilyIndex, queueIndex, pQueue );

  {
    lock();
    g_QueueDeviceMap[*pQueue] = device;
  }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL dali_vkFreeDescriptorSets(
  VkDevice                                    device,
  VkDescriptorPool                            descriptorPool,
  uint32_t                                    descriptorSetCount,
  const VkDescriptorSet*                      pDescriptorSets)
{
  // validate before freeing
  lock()

  auto it = g_dsPoolMap.find(descriptorPool);
  if( it == g_dsPoolMap.end() )
  {
    LOGVK("[%s]:[BI=%d FN=%s] vkFreeDescriptorSets WARNING Pool %p has been destroyed!",
      gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(descriptorPool) );
  }
  else
  {
    auto& pool = it->second;
    std::vector<VkDescriptorSet> invalidSets{};

    for( auto i = 0u; i < descriptorSetCount; ++i )
    {
      auto it1 = std::find_if( pool.sets.begin(), pool.sets.end(), [&](auto& item)
      {
        return item.set == pDescriptorSets[i];
      });
      if( it1 == pool.sets.end() )
      {
        invalidSets.emplace_back( pDescriptorSets[i] );
      }
    }

    if(!invalidSets.empty())
    {
      LOGVK("[%s]:[BI=%d FN=%s] vkFreeDescriptorSets WARNING Found %d of invalid sets requested to be freed!",
            gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), int(invalidSets.size()) );
      for( auto& set : invalidSets )
      {
        LOGVK("[%s]:[BI=%d FN=%s]         DS: %p",
              gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(set) );
      }
    }

    // rewrite list
    decltype(pool.sets) newSets{};
    for( auto& s : pool.sets )
    {
      bool found = false;
      for( auto k = 0u; k < descriptorSetCount; ++k )
      {
        if( pDescriptorSets[k] == s.set )
        {
          found = true;
          break;
        }
      }
      if( !found )
        newSets.emplace_back( s );
    }
    pool.sets = std::move(newSets);
  }

  auto result = device_dispatch[device].FreeDescriptorSets( device, descriptorPool, descriptorSetCount, pDescriptorSets );

  LOGVK ("[%s]:[BI=%d FN=%s] vkFreeDescriptorSets( %p, %p, %d, %p ) = %d",
         gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
         ptr(device), ptr(descriptorPool), descriptorSetCount, ptr(pDescriptorSets), result
    );

  return result;
}

static bool if_ds_exists( VkDescriptorSet set )
{
  for( auto& item : g_dsPoolMap )
  {
    if( (std::find_if( item.second.sets.begin(), item.second.sets.end() , [&]( auto& ds )
    {
      return set == ds.set;
    })) != item.second.sets.end() )
    {
      return true;;
    }
  }
  return false;
}

DsPoolAllocation::Set* find_descriptor_set( VkDescriptorSet ds )
{
  for( auto& pool : g_dsPoolMap )
  {
    auto it = std::find_if( pool.second.sets.begin(),
                            pool.second.sets.end(),
                            [&](auto& item)
                            {
                              return( ds == item.set );
                            });
    if( it != pool.second.sets.end() )
    {
      return &*it;
    }
  }
  return nullptr;
}

VKAPI_ATTR void VKAPI_CALL dali_vkUpdateDescriptorSets(
  VkDevice                                    device,
  uint32_t                                    descriptorWriteCount,
  const VkWriteDescriptorSet*                 pDescriptorWrites,
  uint32_t                                    descriptorCopyCount,
  const VkCopyDescriptorSet*                  pDescriptorCopies)
{
  // validate written descriptor sets
  std::vector<VkDescriptorSet> notExistingSets{};
  {
    for( auto i = 0u; i < descriptorWriteCount; ++i )
    {
      auto exists = if_ds_exists( pDescriptorWrites[i].dstSet );
      if(!exists)
      {
        notExistingSets.emplace_back(pDescriptorWrites[i].dstSet );
      }
    }
  }

  if( !notExistingSets.empty() )
  {
    LOGVK ("[%s]:[BI=%d FN=%s] vkUpdateDescriptorSets() = WARNING, writing to not existing sets!!!",
           gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str()
    );

    for( auto&set : notExistingSets )
    {
      LOGVK ("[%s]:[BI=%d FN=%s] vkUpdateDescriptorSets() = WARNING SET %p has been freed or doesn't exist!",
             gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(), ptr(set)
      );
    }
  }

  LOGVK ("[%s]:[BI=%d FN=%s] vkUpdateDescriptorSets( %p, %d, %p, %d, %p ) = void",
         gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
         ptr(device), descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies );

  // print objects to be bound
  for( auto i = 0u; i < descriptorWriteCount; ++i )
  {
    auto& write = pDescriptorWrites[i];
    if( write.pImageInfo )
    {
      LOGVK ("[%s]:[BI=%d FN=%s]     * Image: %d, binding: %d",
             gProcessName, *vk_dali_bufferIndex, gCallerFunctionName.c_str(),
             get_image_uid( find_image_by_view( write.pImageInfo->imageView ) ), write.dstBinding );

      auto setInfo = find_descriptor_set( write.dstSet );
      if( write.dstBinding >= setInfo->bindings.size())
      {
        setInfo->bindings.resize(write.dstBinding + 1);
      }
      setInfo->bindings[write.dstBinding].imageView = write.pImageInfo->imageView;
      setInfo->bindings[write.dstBinding].binding = write.dstBinding;
      setInfo->bindings[write.dstBinding].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
  }

  device_dispatch[device].UpdateDescriptorSets( device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies );
}







template<class... Args>
void RecordCommand( VkCommandBuffer cmdbuf, const char* commandName, void(*cmd)(Args...), Args... args )
{
  auto& cmdbufinfo = g_commandBuffers[cmdbuf];
  cmdbufinfo.commands.emplace_back( new Command<Args...>(commandName, args...) );
  cmd( args... );
}

template<class... Args>
CommandBuffer& RecordCommand2( std::function<void(std::stringstream& ss)> printer, VkCommandBuffer cmdbuf, const char* commandName, void(*cmd)(Args...), Args... args )
{
  auto& cmdbufinfo = g_commandBuffers[cmdbuf];
  std::stringstream ss;
  ss << commandName << "( ";
  printer(ss);
  ss << " )";
  cmdbufinfo.commands.emplace_back( new Command<Args...>(ss.str(), args...) );
  cmd( args... );
  return cmdbufinfo;
}

#define RECORD( x, ... ) RecordCommand( commandBuffer, "vk" #x, device_dispatch[g_commandBuffers[commandBuffer].vkDevice].x, __VA_ARGS__ )
#define RECORD2( x, y, ... ) RecordCommand2( y, commandBuffer, "vk" #x, device_dispatch[g_commandBuffers[commandBuffer].vkDevice].x, __VA_ARGS__ )
#define PRINTER [&]( std::stringstream& ss )
#define PR(x) [&]( std::stringstream& ss ){ ss << x; }
#define sep << ", " <<

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBindPipeline(
  VkCommandBuffer                             commandBuffer,
  VkPipelineBindPoint                         pipelineBindPoint,
  VkPipeline                                  pipeline)
{
  RECORD2(CmdBindPipeline, PRINTER {
    ss << commandBuffer << ", " << pipelineBindPoint << ", " << pipeline;
    },
  commandBuffer, pipelineBindPoint, pipeline );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetViewport(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    firstViewport,
  uint32_t                                    viewportCount,
  const VkViewport*                           pViewports)
{
  RECORD2( CmdSetViewport,  PRINTER {
    ss << commandBuffer << ", " << firstViewport << ", " << viewportCount << ", " << pViewports;
  }, commandBuffer, firstViewport, viewportCount, pViewports );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetScissor(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    firstScissor,
  uint32_t                                    scissorCount,
  const VkRect2D*                             pScissors)
{
  RECORD2( CmdSetScissor,
    PR( commandBuffer << ", " << firstScissor << ", " << scissorCount << ", " << pScissors ),
    commandBuffer, firstScissor, scissorCount, pScissors );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetLineWidth(
  VkCommandBuffer                             commandBuffer,
  float                                       lineWidth)
{
  RECORD2( CmdSetLineWidth, PR( commandBuffer sep lineWidth ),
  commandBuffer, lineWidth );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetDepthBias(
  VkCommandBuffer                             commandBuffer,
  float                                       depthBiasConstantFactor,
  float                                       depthBiasClamp,
  float                                       depthBiasSlopeFactor)
{
  RECORD2( CmdSetDepthBias, PR( commandBuffer << ", " << depthBiasConstantFactor << ", " << depthBiasClamp << ", " << depthBiasSlopeFactor ),
  commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetBlendConstants(
  VkCommandBuffer                             commandBuffer,
  const float                                 blendConstants[4])
{
  //RECORD( CmdSetDepthBounds, commandBuffer, blendConstants );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetDepthBounds(
  VkCommandBuffer                             commandBuffer,
  float                                       minDepthBounds,
  float                                       maxDepthBounds)
{
  RECORD2( CmdSetDepthBounds, PR( commandBuffer sep minDepthBounds sep maxDepthBounds ), commandBuffer, minDepthBounds, maxDepthBounds );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetStencilCompareMask(
  VkCommandBuffer                             commandBuffer,
  VkStencilFaceFlags                          faceMask,
  uint32_t                                    compareMask)
{
  RECORD2( CmdSetStencilCompareMask, PR( commandBuffer sep faceMask sep compareMask ), commandBuffer, faceMask, compareMask );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetStencilWriteMask(
  VkCommandBuffer                             commandBuffer,
  VkStencilFaceFlags                          faceMask,
  uint32_t                                    writeMask)
{
  RECORD2( CmdSetStencilWriteMask, PR( commandBuffer sep faceMask sep writeMask ), commandBuffer, faceMask, writeMask );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetStencilReference(
  VkCommandBuffer                             commandBuffer,
  VkStencilFaceFlags                          faceMask,
  uint32_t                                    reference)
{
  RECORD2( CmdSetStencilReference, PR( commandBuffer sep faceMask sep reference ), commandBuffer, faceMask, reference );
}



extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBindDescriptorSets(
  VkCommandBuffer                             commandBuffer,
  VkPipelineBindPoint                         pipelineBindPoint,
  VkPipelineLayout                            layout,
  uint32_t                                    firstSet,
  uint32_t                                    descriptorSetCount,
  const VkDescriptorSet*                      pDescriptorSets,
  uint32_t                                    dynamicOffsetCount,
  const uint32_t*                             pDynamicOffsets)
{
  auto& cmdbuf =
  RECORD2( CmdBindDescriptorSets, PR(
      commandBuffer sep
      pipelineBindPoint sep
      layout sep
      firstSet sep
      descriptorSetCount sep
      pDescriptorSets sep
      dynamicOffsetCount sep
      pDynamicOffsets
    ),commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount,
    pDescriptorSets, dynamicOffsetCount, pDynamicOffsets );

  for( auto i = 0u; i < descriptorSetCount; ++i )
  {
    auto& ds = pDescriptorSets[i];

    auto dsinfo = find_descriptor_set( ds );
    if(!dsinfo)
    {
      LOGVK("ERROR! Unknown descriptor set!");
    }
    else
    {
      {
        std::stringstream ss;
        ss << "    DS: " << ds;
        cmdbuf.back()->AddLogLine( ss.str() );
      }
      for( auto& binding : dsinfo->bindings )
      {
        if( binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER )
        {
          std::stringstream ss;
          ss << "        B = " << binding.binding << ", T = " << binding.type <<
          " IV = " << binding.imageView << " (I=" << get_image_uid( find_image_by_view(binding.imageView) ) << ")";
          cmdbuf.back()->AddLogLine( ss.str() );
        }
      }
    }
  }
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBindIndexBuffer(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    buffer,
  VkDeviceSize                                offset,
  VkIndexType                                 indexType)
{
  RECORD2( CmdBindIndexBuffer, PR(commandBuffer sep buffer sep offset sep indexType), commandBuffer, buffer, offset, indexType );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBindVertexBuffers(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    firstBinding,
  uint32_t                                    bindingCount,
  const VkBuffer*                             pBuffers,
  const VkDeviceSize*                         pOffsets)
{
  RECORD2( CmdBindVertexBuffers, PR(
    commandBuffer sep firstBinding sep bindingCount sep pBuffers sep pOffsets
    ), commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDraw(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    vertexCount,
  uint32_t                                    instanceCount,
  uint32_t                                    firstVertex,
  uint32_t                                    firstInstance)
{
  RECORD2( CmdDraw, PR(
    commandBuffer sep vertexCount sep instanceCount sep firstVertex sep firstInstance
    ), commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDrawIndexed(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    indexCount,
  uint32_t                                    instanceCount,
  uint32_t                                    firstIndex,
  int32_t                                     vertexOffset,
  uint32_t                                    firstInstance)
{
  RECORD2( CmdDrawIndexed, PR(
    commandBuffer sep indexCount sep instanceCount sep firstIndex sep vertexOffset sep firstInstance
    ), commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDrawIndirect(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    buffer,
  VkDeviceSize                                offset,
  uint32_t                                    drawCount,
  uint32_t                                    stride)
{
  RECORD2( CmdDrawIndirect, PR(
    commandBuffer sep buffer sep offset sep drawCount sep stride
    ), commandBuffer, buffer, offset, drawCount, stride );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDrawIndexedIndirect(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    buffer,
  VkDeviceSize                                offset,
  uint32_t                                    drawCount,
  uint32_t                                    stride)
{
  RECORD2( CmdDrawIndexedIndirect, PR(
    commandBuffer sep buffer sep offset sep drawCount sep stride
    ), commandBuffer, buffer, offset, drawCount, stride );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDispatch(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    groupCountX,
  uint32_t                                    groupCountY,
  uint32_t                                    groupCountZ)
{
  RECORD2( CmdDispatch, PR(
    commandBuffer sep groupCountX sep groupCountY sep groupCountZ
    ),
    commandBuffer, groupCountX, groupCountY, groupCountZ );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdDispatchIndirect(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    buffer,
  VkDeviceSize                                offset)
{
  RECORD2( CmdDispatchIndirect, PR(
    commandBuffer sep buffer sep offset
    ), commandBuffer, buffer, offset );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdCopyBuffer(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    srcBuffer,
  VkBuffer                                    dstBuffer,
  uint32_t                                    regionCount,
  const VkBufferCopy*                         pRegions)
{
  RECORD2( CmdCopyBuffer, PR(
    commandBuffer sep srcBuffer sep dstBuffer sep regionCount sep pRegions
    ), commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdCopyImage(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     srcImage,
  VkImageLayout                               srcImageLayout,
  VkImage                                     dstImage,
  VkImageLayout                               dstImageLayout,
  uint32_t                                    regionCount,
  const VkImageCopy*                          pRegions)
{
  RECORD2( CmdCopyImage, PR(
    commandBuffer sep srcImage sep srcImageLayout sep dstImage sep dstImageLayout sep regionCount sep pRegions
    ),
    commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBlitImage(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     srcImage,
  VkImageLayout                               srcImageLayout,
  VkImage                                     dstImage,
  VkImageLayout                               dstImageLayout,
  uint32_t                                    regionCount,
  const VkImageBlit*                          pRegions,
  VkFilter                                    filter)
{
  RECORD2( CmdBlitImage, PR(
    commandBuffer sep srcImage sep srcImageLayout sep dstImage sep dstImageLayout sep regionCount sep pRegions sep filter
    ),
    commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdCopyBufferToImage(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    srcBuffer,
  VkImage                                     dstImage,
  VkImageLayout                               dstImageLayout,
  uint32_t                                    regionCount,
  const VkBufferImageCopy*                    pRegions)
{
  RECORD2( CmdCopyBufferToImage, PR(
    commandBuffer sep srcBuffer sep dstImage sep dstImageLayout sep regionCount sep pRegions
    ), commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdCopyImageToBuffer(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     srcImage,
  VkImageLayout                               srcImageLayout,
  VkBuffer                                    dstBuffer,
  uint32_t                                    regionCount,
  const VkBufferImageCopy*                    pRegions)
{
  RECORD2( CmdCopyImageToBuffer, PR(
    commandBuffer sep srcImage sep srcImageLayout sep dstBuffer sep regionCount sep pRegions
    ),
    commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdUpdateBuffer(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    dstBuffer,
  VkDeviceSize                                dstOffset,
  VkDeviceSize                                dataSize,
  const void*                                 pData)
{
  RECORD2( CmdUpdateBuffer, PR(
    commandBuffer sep dstBuffer sep dstOffset sep dataSize sep pData
    ),
    commandBuffer, dstBuffer, dstOffset, dataSize, pData );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdFillBuffer(
  VkCommandBuffer                             commandBuffer,
  VkBuffer                                    dstBuffer,
  VkDeviceSize                                dstOffset,
  VkDeviceSize                                size,
  uint32_t                                    data)
{
  RECORD2( CmdFillBuffer, PR(
    commandBuffer sep dstBuffer sep dstOffset sep size sep data
    ),
    commandBuffer, dstBuffer, dstOffset, size, data );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdClearColorImage(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     image,
  VkImageLayout                               imageLayout,
  const VkClearColorValue*                    pColor,
  uint32_t                                    rangeCount,
  const VkImageSubresourceRange*              pRanges)
{
  RECORD2( CmdClearColorImage, PR(
    commandBuffer sep image sep imageLayout sep pColor sep rangeCount sep pRanges
    ),
    commandBuffer, image, imageLayout, pColor, rangeCount, pRanges );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdClearDepthStencilImage(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     image,
  VkImageLayout                               imageLayout,
  const VkClearDepthStencilValue*             pDepthStencil,
  uint32_t                                    rangeCount,
  const VkImageSubresourceRange*              pRanges)
{
  RECORD( CmdClearDepthStencilImage, commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdClearAttachments(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    attachmentCount,
  const VkClearAttachment*                    pAttachments,
  uint32_t                                    rectCount,
  const VkClearRect*                          pRects)
{
  RECORD( CmdClearAttachments, commandBuffer, attachmentCount, pAttachments, rectCount, pRects );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdResolveImage(
  VkCommandBuffer                             commandBuffer,
  VkImage                                     srcImage,
  VkImageLayout                               srcImageLayout,
  VkImage                                     dstImage,
  VkImageLayout                               dstImageLayout,
  uint32_t                                    regionCount,
  const VkImageResolve*                       pRegions)
{
  RECORD( CmdResolveImage, commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdSetEvent(
  VkCommandBuffer                             commandBuffer,
  VkEvent                                     event,
  VkPipelineStageFlags                        stageMask)
{
  RECORD( CmdSetEvent, commandBuffer, event, stageMask );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdResetEvent(
  VkCommandBuffer                             commandBuffer,
  VkEvent                                     event,
  VkPipelineStageFlags                        stageMask)
{
  RECORD( CmdResetEvent, commandBuffer, event, stageMask );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdWaitEvents(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    eventCount,
  const VkEvent*                              pEvents,
  VkPipelineStageFlags                        srcStageMask,
  VkPipelineStageFlags                        dstStageMask,
  uint32_t                                    memoryBarrierCount,
  const VkMemoryBarrier*                      pMemoryBarriers,
  uint32_t                                    bufferMemoryBarrierCount,
  const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
  uint32_t                                    imageMemoryBarrierCount,
  const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
  RECORD( CmdWaitEvents,
    commandBuffer,
    eventCount,
    pEvents,
    srcStageMask,
    dstStageMask,
    memoryBarrierCount,
    pMemoryBarriers,
    bufferMemoryBarrierCount,
    pBufferMemoryBarriers,
    imageMemoryBarrierCount,
    pImageMemoryBarriers );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdPipelineBarrier(
  VkCommandBuffer                             commandBuffer,
  VkPipelineStageFlags                        srcStageMask,
  VkPipelineStageFlags                        dstStageMask,
  VkDependencyFlags                           dependencyFlags,
  uint32_t                                    memoryBarrierCount,
  const VkMemoryBarrier*                      pMemoryBarriers,
  uint32_t                                    bufferMemoryBarrierCount,
  const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
  uint32_t                                    imageMemoryBarrierCount,
  const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
  auto& cmdbuf =
  RECORD2( CmdPipelineBarrier, PR(
    commandBuffer sep
    srcStageMask sep
    dstStageMask sep
    dependencyFlags sep
    memoryBarrierCount sep
    pMemoryBarriers sep
    bufferMemoryBarrierCount sep
    pBufferMemoryBarriers sep
    imageMemoryBarrierCount sep
    pImageMemoryBarriers
    ),
    commandBuffer,
    srcStageMask,
    dstStageMask,
    dependencyFlags,
    memoryBarrierCount,
    pMemoryBarriers,
    bufferMemoryBarrierCount,
    pBufferMemoryBarriers,
    imageMemoryBarrierCount,
    pImageMemoryBarriers );

  for( auto i = 0u; i < imageMemoryBarrierCount; ++i )
  {
    std::stringstream ss;
    ss << "    Image: " << get_image_uid( pImageMemoryBarriers[i].image );
    cmdbuf.back()->AddLogLine( ss.str() );
  }
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBeginQuery(
  VkCommandBuffer                             commandBuffer,
  VkQueryPool                                 queryPool,
  uint32_t                                    query,
  VkQueryControlFlags                         flags)
{
  RECORD( CmdBeginQuery, commandBuffer, queryPool, query, flags );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdEndQuery(
  VkCommandBuffer                             commandBuffer,
  VkQueryPool                                 queryPool,
  uint32_t                                    query)
{
  RECORD( CmdEndQuery, commandBuffer, queryPool, query );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdResetQueryPool(
  VkCommandBuffer                             commandBuffer,
  VkQueryPool                                 queryPool,
  uint32_t                                    firstQuery,
  uint32_t                                    queryCount)
{
  RECORD( CmdResetQueryPool, commandBuffer, queryPool, firstQuery, queryCount );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdWriteTimestamp(
  VkCommandBuffer                             commandBuffer,
  VkPipelineStageFlagBits                     pipelineStage,
  VkQueryPool                                 queryPool,
  uint32_t                                    query)
{
  RECORD( CmdWriteTimestamp, commandBuffer, pipelineStage, queryPool, query );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdCopyQueryPoolResults(
  VkCommandBuffer                             commandBuffer,
  VkQueryPool                                 queryPool,
  uint32_t                                    firstQuery,
  uint32_t                                    queryCount,
  VkBuffer                                    dstBuffer,
  VkDeviceSize                                dstOffset,
  VkDeviceSize                                stride,
  VkQueryResultFlags                          flags)
{
  RECORD( CmdCopyQueryPoolResults,
    commandBuffer,
    queryPool,
    firstQuery,
    queryCount,
    dstBuffer,
    dstOffset,
    stride,
    flags );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdPushConstants(
  VkCommandBuffer                             commandBuffer,
  VkPipelineLayout                            layout,
  VkShaderStageFlags                          stageFlags,
  uint32_t                                    offset,
  uint32_t                                    size,
  const void*                                 pValues)
{
  RECORD( CmdPushConstants,
    commandBuffer,
    layout,
    stageFlags,
    offset,
    size,
    pValues );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdBeginRenderPass(
  VkCommandBuffer                             commandBuffer,
  const VkRenderPassBeginInfo*                pRenderPassBegin,
  VkSubpassContents                           contents)
{
  RECORD2( CmdBeginRenderPass, PR(
           commandBuffer sep
           pRenderPassBegin sep
           contents ),
    commandBuffer,
    pRenderPassBegin,
    contents );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdNextSubpass(
  VkCommandBuffer                             commandBuffer,
  VkSubpassContents                           contents)
{
  RECORD( CmdNextSubpass, commandBuffer, contents );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdEndRenderPass(
  VkCommandBuffer                             commandBuffer)
{
  RECORD2( CmdEndRenderPass, PR( commandBuffer ), commandBuffer );
}

extern "C" VKAPI_ATTR void VKAPI_CALL dali_vkCmdExecuteCommands(
  VkCommandBuffer                             commandBuffer,
  uint32_t                                    commandBufferCount,
  const VkCommandBuffer*                      pCommandBuffers)
{
  RECORD2( CmdExecuteCommands, PR( commandBuffer sep commandBufferCount sep pCommandBuffers ), commandBuffer, commandBufferCount, pCommandBuffers );
}


















extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
  VkInstance                                  instance,
  const char*                                 pName)
{
  LOGVK( "[Instance] Function: %s", pName );
  DISPATCH(vkDebugSetBufferIndexPtr);
  DISPATCH(vkCreateInstance);
  DISPATCH(vkCreateDevice);
  auto value = instance_dispatch[instance].GetInstanceProcAddr(instance, pName);
  return value;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
  VkDevice                                    device,
  const char*                                 pName)
{
  LOGVK( "[Device] Function: %s", pName );
  DISPATCH(vkWaitForFences);
  DISPATCH(vkResetFences);
  DISPATCH(vkBeginCommandBuffer);
  DISPATCH(vkEndCommandBuffer);
  DISPATCH(vkResetCommandBuffer);
  DISPATCH(vkQueueSubmit);
  DISPATCH(vkCreateDescriptorPool);
  DISPATCH(vkDestroyDescriptorPool);
  DISPATCH(vkAllocateDescriptorSets);
  DISPATCH(vkFreeDescriptorSets);
  DISPATCH(vkUpdateDescriptorSets);
  DISPATCH(vkGetDeviceQueue);

  // Commands
  DISPATCH( vkCmdBindPipeline );
  DISPATCH( vkCmdSetViewport );
  DISPATCH( vkCmdSetScissor );
  DISPATCH( vkCmdSetLineWidth );
  DISPATCH( vkCmdSetDepthBias );
  DISPATCH( vkCmdSetBlendConstants );
  DISPATCH( vkCmdSetDepthBounds );
  DISPATCH( vkCmdSetStencilCompareMask );
  DISPATCH( vkCmdSetStencilWriteMask );
  DISPATCH( vkCmdSetStencilReference );
  DISPATCH( vkCmdBindDescriptorSets );
  DISPATCH( vkCmdBindIndexBuffer );
  DISPATCH( vkCmdBindVertexBuffers );
  DISPATCH( vkCmdDraw );
  DISPATCH( vkCmdDrawIndexed );
  DISPATCH( vkCmdDrawIndirect );
  DISPATCH( vkCmdDrawIndexedIndirect );
  DISPATCH( vkCmdDispatch );
  DISPATCH( vkCmdDispatchIndirect );
  DISPATCH( vkCmdCopyBuffer );
  DISPATCH( vkCmdCopyImage );
  DISPATCH( vkCmdBlitImage );
  DISPATCH( vkCmdCopyBufferToImage );
  DISPATCH( vkCmdCopyImageToBuffer );
  DISPATCH( vkCmdUpdateBuffer );
  DISPATCH( vkCmdFillBuffer );
  DISPATCH( vkCmdClearColorImage );
  DISPATCH( vkCmdClearDepthStencilImage );
  DISPATCH( vkCmdClearAttachments );
  DISPATCH( vkCmdResolveImage );
  DISPATCH( vkCmdSetEvent );
  DISPATCH( vkCmdResetEvent );
  DISPATCH( vkCmdWaitEvents );
  DISPATCH( vkCmdPipelineBarrier );
  DISPATCH( vkCmdBeginQuery );
  DISPATCH( vkCmdEndQuery );
  DISPATCH( vkCmdResetQueryPool );
  DISPATCH( vkCmdWriteTimestamp );
  DISPATCH( vkCmdCopyQueryPoolResults );
  DISPATCH( vkCmdPushConstants );
  DISPATCH( vkCmdBeginRenderPass );
  DISPATCH( vkCmdNextSubpass );
  DISPATCH( vkCmdEndRenderPass );
  DISPATCH( vkCmdExecuteCommands );

  DISPATCH( vkAllocateCommandBuffers );
  DISPATCH( vkFreeCommandBuffers );

  auto retval = resources_dispatch( pName );

  if(retval == nullptr )
    return device_dispatch[device].GetDeviceProcAddr(device, pName);
  else
    return retval;
}