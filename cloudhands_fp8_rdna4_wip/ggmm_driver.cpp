#include <vulkan/vulkan.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#define CK(x) do{VkResult r=(x); if(r){printf("VK err %d @ %d\n",r,__LINE__);exit(3);}}while(0)
static std::vector<uint32_t> spv(const char*p){FILE*f=fopen(p,"rb");if(!f){printf("no spv %s\n",p);exit(2);}fseek(f,0,2);long n=ftell(f);fseek(f,0,0);std::vector<uint32_t>v(n/4);if(fread(v.data(),1,n,f)){}fclose(f);return v;}
VkPhysicalDevice pd; VkDevice dev; uint32_t qfi; VkQueue queue; VkCommandPool cpool;
static uint32_t mt(uint32_t b,VkMemoryPropertyFlags w){VkPhysicalDeviceMemoryProperties m;vkGetPhysicalDeviceMemoryProperties(pd,&m);for(uint32_t i=0;i<m.memoryTypeCount;i++)if((b&(1u<<i))&&(m.memoryTypes[i].propertyFlags&w)==w)return i;return 0;}
static double now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
struct Buf{VkBuffer b;VkDeviceMemory m;VkDeviceSize sz;};
static Buf mkbuf(VkDeviceSize sz,VkBufferUsageFlags use,VkMemoryPropertyFlags props){
  Buf r{}; r.sz=sz; VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bc.size=sz; bc.usage=use; vkCreateBuffer(dev,&bc,0,&r.b);
  VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,r.b,&mr);
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=mr.size; a.memoryTypeIndex=mt(mr.memoryTypeBits,props); vkAllocateMemory(dev,&a,0,&r.m); vkBindBufferMemory(dev,r.b,r.m,0); return r;
}
static VkCommandBuffer beginCmd(){VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=cpool;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;VkCommandBuffer cb;vkAllocateCommandBuffers(dev,&a,&cb);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cb,&bi);return cb;}
static void endCmd(VkCommandBuffer cb){vkEndCommandBuffer(cb);VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;vkQueueSubmit(queue,1,&si,0);vkQueueWaitIdle(queue);}
static void upload(Buf&dst,const void*data,VkDeviceSize sz){
  Buf st=mkbuf(sz,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  void*p; vkMapMemory(dev,st.m,0,sz,0,&p); memcpy(p,data,sz); vkUnmapMemory(dev,st.m);
  VkCommandBuffer cb=beginCmd(); VkBufferCopy c{0,0,sz}; vkCmdCopyBuffer(cb,st.b,dst.b,1,&c); endCmd(cb);
  vkDestroyBuffer(dev,st.b,0); vkFreeMemory(dev,st.m,0);
}
const uint32_t M=4096,N=512,K=14336;
struct PC{uint32_t M,N,K,stride_a,stride_b,stride_d,bsa,bsb,bsd,base_wgz,num_batches,k_split,ne02,ne12,bc2,bc3;};
double run(const char*label,const char*path,uint32_t abE){
  VkDeviceSize aSz=(VkDeviceSize)M*K*abE, bSz=(VkDeviceSize)N*K*abE, dSz=(VkDeviceSize)M*N*4;
  Buf A=mkbuf(aSz,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Buf B=mkbuf(bSz,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Buf D=mkbuf(dSz,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  { std::vector<uint8_t> h(aSz); if(abE==2){uint16_t*q=(uint16_t*)h.data();for(size_t i=0;i<aSz/2;i++)q[i]=0x3C00;}else memset(h.data(),0x38,aSz); upload(A,h.data(),aSz);}
  { std::vector<uint8_t> h(bSz); if(abE==2){uint16_t*q=(uint16_t*)h.data();for(size_t i=0;i<bSz/2;i++)q[i]=0x3C00;}else memset(h.data(),0x38,bSz); upload(B,h.data(),bSz);}
  auto code=spv(path); VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=code.size()*4;sm.pCode=code.data();VkShaderModule mod;CK(vkCreateShaderModule(dev,&sm,0,&mod));
  VkDescriptorSetLayoutBinding lb[3];for(int i=0;i<3;i++) lb[i]={(uint32_t)i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0};
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=3;dl.pBindings=lb;VkDescriptorSetLayout dsl;vkCreateDescriptorSetLayout(dev,&dl,0,&dsl);
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&ps;VkDescriptorPool pool;vkCreateDescriptorPool(dev,&pc,0,&pool);
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&dsl;VkDescriptorSet ds;vkAllocateDescriptorSets(dev,&da,&ds);
  VkDescriptorBufferInfo bi[3]={{A.b,0,aSz},{B.b,0,bSz},{D.b,0,dSz}};VkWriteDescriptorSet w[3];for(int i=0;i<3;i++){w[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[i].dstSet=ds;w[i].dstBinding=i;w[i].descriptorCount=1;w[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[i].pBufferInfo=&bi[i];}vkUpdateDescriptorSets(dev,3,w,0,0);
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(PC)};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&dsl;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&pcr;VkPipelineLayout lay;vkCreatePipelineLayout(dev,&pl,0,&lay);
  uint32_t scv[10]={256,128,128, 32,64,2,16,16,16,32};
  uint32_t ids[10]={0,1,2,4,5,6,7,8,9,10};
  VkSpecializationMapEntry sme[10]; for(int i=0;i<10;i++) sme[i]={ids[i],(uint32_t)(i*4),4};
  VkSpecializationInfo spi{10,sme,sizeof(scv),scv};
  VkPipelineShaderStageCreateInfo st{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};st.stage=VK_SHADER_STAGE_COMPUTE_BIT;st.module=mod;st.pName="main";st.pSpecializationInfo=&spi;
  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rss{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};rss.requiredSubgroupSize=32;st.pNext=&rss;
  VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.stage=st;cp.layout=lay;VkPipeline pipe;CK(vkCreateComputePipelines(dev,0,1,&cp,0,&pipe));
  PC pcv{M,N,K, K,K,M, 0,0,0, 0,1,K,1,1,1,1};
  uint32_t gx=(M+128-1)/128, gy=(N+128-1)/128;
  auto rec=[&](){VkCommandBuffer cb=beginCmd();vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,lay,0,1,&ds,0,0);vkCmdPushConstants(cb,lay,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(PC),&pcv);vkCmdDispatch(cb,gx,gy,1);endCmd(cb);};
  rec();
  Buf rb=mkbuf(dSz,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkCommandBuffer cb=beginCmd();VkBufferCopy c{0,0,dSz};vkCmdCopyBuffer(cb,D.b,rb.b,1,&c);endCmd(cb);
  void*p;vkMapMemory(dev,rb.m,0,dSz,0,&p);float*cf=(float*)p;
  size_t z=0; for(size_t q=0;q<(size_t)M*N;q++) if(cf[q]==0.0f) z++;
  float d0=cf[0]; vkUnmapMemory(dev,rb.m);
  int it=40; double t0=now(); for(int i=0;i<it;i++) rec(); double t=(now()-t0)/it; double tflops=2.0*M*N*K/t/1e12;
  printf("  [%s] %.3f ms -> %.1f TFLOPS  (D0=%.0f zeros=%zu)\n", label, t*1e3, tflops, d0, z);
  vkDestroyBuffer(dev,rb.b,0);vkFreeMemory(dev,rb.m,0);
  vkDestroyPipeline(dev,pipe,0);vkDestroyShaderModule(dev,mod,0);
  vkDestroyBuffer(dev,A.b,0);vkFreeMemory(dev,A.m,0);vkDestroyBuffer(dev,B.b,0);vkFreeMemory(dev,B.m,0);vkDestroyBuffer(dev,D.b,0);vkFreeMemory(dev,D.m,0);
  return tflops;
}
int main(){
  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.apiVersion=VK_API_VERSION_1_3;VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&ai;VkInstance inst;CK(vkCreateInstance(&ici,0,&inst));
  uint32_t n=0;vkEnumeratePhysicalDevices(inst,&n,0);std::vector<VkPhysicalDevice>pds(n);vkEnumeratePhysicalDevices(inst,&n,pds.data());
  for(auto d:pds){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(d,&p);if(strstr(p.deviceName,"Radeon")||strstr(p.deviceName,"AMD")){pd=d;printf("device: %s\n",p.deviceName);break;}}
  uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,0);std::vector<VkQueueFamilyProperties>qf(qn);vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,qf.data());for(uint32_t i=0;i<qn;i++)if(qf[i].queueFlags&VK_QUEUE_COMPUTE_BIT){qfi=i;break;}
  float prio=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=qfi;qci.queueCount=1;qci.pQueuePriorities=&prio;
  VkPhysicalDevice16BitStorageFeatures s16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};s16.storageBuffer16BitAccess=VK_TRUE;
  VkPhysicalDevice8BitStorageFeatures s8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES};s8.storageBuffer8BitAccess=VK_TRUE;s8.pNext=&s16;
  VkPhysicalDeviceVulkanMemoryModelFeatures mm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};mm.vulkanMemoryModel=VK_TRUE;mm.pNext=&s8;
  VkPhysicalDeviceShaderFloat16Int8Features f16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};f16.shaderFloat16=VK_TRUE;f16.shaderInt8=VK_TRUE;f16.pNext=&mm;
  VkPhysicalDeviceCooperativeMatrixFeaturesKHR cm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};cm.cooperativeMatrix=VK_TRUE;cm.pNext=&f16;
  VkPhysicalDeviceShaderFloat8FeaturesEXT f8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};f8.shaderFloat8=VK_TRUE;f8.shaderFloat8CooperativeMatrix=VK_TRUE;f8.pNext=&cm;
  VkPhysicalDeviceSubgroupSizeControlFeatures ssc{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};ssc.subgroupSizeControl=VK_TRUE;ssc.pNext=&f8;
  const char*exts[]={"VK_KHR_cooperative_matrix","VK_EXT_shader_float8","VK_KHR_vulkan_memory_model","VK_EXT_subgroup_size_control","VK_KHR_shader_float16_int8","VK_KHR_8bit_storage","VK_KHR_16bit_storage"};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.pNext=&ssc;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=7;dci.ppEnabledExtensionNames=exts;
  CK(vkCreateDevice(pd,&dci,0,&dev));vkGetDeviceQueue(dev,qfi,0,&queue);
  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=qfi;cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;vkCreateCommandPool(dev,&cpi,0,&cpool);
  printf("[ggml mul_mm coopmat, %ux%ux%u, AMD-large warptile]\n",M,N,K);
  double a16=run("f16-aligned          ","/b/ggmm_f16a.spv",2);
  double u16=run("f16-unaligned        ","/b/ggmm_f16.spv",2);
  double cf8=run("fp8-compute(f16 wts) ","/b/ggmm_fp8.spv",2);
  double nu8=run("fp8-NATIVE unaligned ","/b/ggmm_fp8native.spv",1);
  double na8=run("fp8-NATIVE aligned   ","/b/ggmm_fp8na.spv",1);
  printf("\n--- vs f16-aligned (ggml ships, %.1f TFLOPS) ---\n", a16);
  printf("  fp8-compute(f16 wts) : %.1f  (%.2fx)\n", cf8, cf8/a16);
  printf("  fp8-NATIVE unaligned : %.1f  (%.2fx)\n", nu8, nu8/a16);
  printf("  fp8-NATIVE aligned   : %.1f  (%.2fx)\n", na8, na8/a16);
  printf("  f16-unaligned ref    : %.1f\n", u16);
  return 0;
}
