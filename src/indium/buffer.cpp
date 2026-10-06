#include <indium/buffer.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <cstring>
#include <utility>

// TODO: use the VulkanMemoryAllocator library to manage memory allocation efficiently

Indium::Buffer::~Buffer() {};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options):
	_privateDevice(device),
	_length(length)
{
	_storageMode = static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf);

	VkBufferCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = length;
	info.usage =
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		;

	// TODO: maybe make this CONCURRENT instead? we already know all the queue families that can access it;
	//       that info is available in the PrivateDevice instance.
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (DynamicVK::vkCreateBuffer(_privateDevice->device(), &info, nullptr, &_buffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkMemoryRequirements requirements;

	DynamicVK::vkGetBufferMemoryRequirements(_privateDevice->device(), _buffer, &requirements);

	size_t targetIndex = SIZE_MAX;

	for (size_t i = 0; i < _privateDevice->memoryProperties().memoryTypeCount; ++i) {
		const auto& type = _privateDevice->memoryProperties().memoryTypes[i];

		if ((requirements.memoryTypeBits & (1 << i)) == 0) {
			continue;
		}

		if ((_storageMode == StorageMode::Managed || _storageMode == StorageMode::Shared) && (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
			continue;
		}

		if (_storageMode == StorageMode::Shared && (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
			continue;
		}

		// okay, this is good enough
		targetIndex = i;
		break;
	}

	if (targetIndex == SIZE_MAX) {
		throw std::runtime_error("No suitable memory region found for buffer with requested storage mode");
	}

	VkMemoryAllocateInfo allocateInfo {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = targetIndex;

	VkMemoryAllocateFlagsInfo allocateFlags {};
	allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	allocateInfo.pNext = &allocateFlags;

	if (DynamicVK::vkAllocateMemory(_privateDevice->device(), &allocateInfo, nullptr, &_memory) != VK_SUCCESS) {
		// TODO
		abort();
	}

	DynamicVK::vkBindBufferMemory(_privateDevice->device(), _buffer, _memory, 0);
};

Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, const void* pointer, size_t length, ResourceOptions options):
	PrivateBuffer(device, length, options)
{
	auto ptr = contents();

	if (!ptr) {
		// TODO: support non-host-visible memory
		abort();
	}

	memcpy(ptr, pointer, length);

	if (_storageMode == StorageMode::Managed) {
		VkMappedMemoryRange range {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = _memory;
		range.size = VK_WHOLE_SIZE;
		range.offset = 0;
		if (DynamicVK::vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &range) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}
};

// used by importHostMemory() for buffers that wrap caller-owned host memory;
// does no Vulkan work of its own
Indium::PrivateBuffer::PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, StorageMode storageMode, void* hostPointer):
	_privateDevice(device),
	_length(length),
	_storageMode(storageMode),
	_hostPointer(hostPointer)
{};

std::shared_ptr<Indium::PrivateBuffer> Indium::PrivateBuffer::importHostMemory(std::shared_ptr<PrivateDevice> device, void* pointer, size_t length, ResourceOptions options, std::function<void()> deallocator) {
	auto buffer = std::shared_ptr<PrivateBuffer>(new PrivateBuffer(device, length, static_cast<StorageMode>((static_cast<size_t>(options) >> 4) & 0xf), pointer));

	VkMemoryHostPointerPropertiesEXT hostPointerProps {};
	hostPointerProps.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;

	if (DynamicVK::vkGetMemoryHostPointerPropertiesEXT(device->device(), VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, pointer, &hostPointerProps) != VK_SUCCESS) {
		return nullptr;
	}

	VkExternalMemoryBufferCreateInfo externalInfo {};
	externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
	externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

	VkBufferCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.pNext = &externalInfo;
	info.size = length;
	info.usage =
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		;

	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (DynamicVK::vkCreateBuffer(device->device(), &info, nullptr, &buffer->_buffer) != VK_SUCCESS) {
		// ~PrivateBuffer has nothing to clean up yet
		return nullptr;
	}

	VkMemoryRequirements requirements;

	DynamicVK::vkGetBufferMemoryRequirements(device->device(), buffer->_buffer, &requirements);

	uint32_t compatibleTypeBits = hostPointerProps.memoryTypeBits & requirements.memoryTypeBits;

	size_t targetIndex = SIZE_MAX;
	size_t coherentIndex = SIZE_MAX;

	for (size_t i = 0; i < device->memoryProperties().memoryTypeCount; ++i) {
		const auto& type = device->memoryProperties().memoryTypes[i];

		if ((compatibleTypeBits & (1 << i)) == 0) {
			continue;
		}

		if ((type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
			continue;
		}

		if ((type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 && coherentIndex == SIZE_MAX) {
			// prefer host-coherent memory types so that no explicit flushing is needed
			coherentIndex = i;
		}

		if (targetIndex == SIZE_MAX) {
			// first compatible type; used if none of them are host-coherent
			targetIndex = i;
		}
	}

	if (coherentIndex != SIZE_MAX) {
		targetIndex = coherentIndex;
	}

	if (targetIndex == SIZE_MAX) {
		// no compatible host-visible memory type
		return nullptr;
	}

	if (requirements.size > length) {
		// the buffer needs more memory than the caller offered to wrap
		return nullptr;
	}

	VkMemoryAllocateFlagsInfo allocateFlags {};
	allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	VkImportMemoryHostPointerInfoEXT importInfo {};
	importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
	importInfo.pNext = &allocateFlags;
	importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
	importInfo.pHostPointer = pointer;

	VkMemoryAllocateInfo allocateInfo {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.pNext = &importInfo;
	allocateInfo.allocationSize = length;
	allocateInfo.memoryTypeIndex = targetIndex;

	if (DynamicVK::vkAllocateMemory(device->device(), &allocateInfo, nullptr, &buffer->_memory) != VK_SUCCESS) {
		// ~PrivateBuffer destroys the buffer and no-ops the free for the null memory handle
		return nullptr;
	}

	if (DynamicVK::vkBindBufferMemory(device->device(), buffer->_buffer, buffer->_memory, 0) != VK_SUCCESS) {
		// ~PrivateBuffer destroys the buffer and frees the memory
		return nullptr;
	}

	buffer->_hostPointerCoherent = (device->memoryProperties().memoryTypes[targetIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

	buffer->_deallocator = std::move(deallocator);

	return buffer;
};

Indium::PrivateBuffer::~PrivateBuffer() {
	if (_mapped) {
		DynamicVK::vkUnmapMemory(_privateDevice->device(), _memory);
	}

	DynamicVK::vkDestroyBuffer(_privateDevice->device(), _buffer, nullptr);
	DynamicVK::vkFreeMemory(_privateDevice->device(), _memory, nullptr);

	if (_deallocator) {
		// for buffers wrapping imported host memory, this releases the host memory;
		// command buffers may keep the PrivateBuffer alive after the MTLBuffer is
		// gone, so it must only run once Vulkan has fully let go of the memory.
		_deallocator();
	}
};

std::shared_ptr<Indium::Device> Indium::PrivateBuffer::device() {
	return _privateDevice;
};

size_t Indium::PrivateBuffer::length() const {
	return _length;
};

void* Indium::PrivateBuffer::contents() {
	if (_storageMode != StorageMode::Managed && _storageMode != StorageMode::Shared) {
		return nullptr;
	}

	if (_hostPointer) {
		// imported host memory is implicitly mapped at the host pointer;
		// the Vulkan memory object behind it must not be mapped again
		return _hostPointer;
	}

	if (!_mapped) {
		if (DynamicVK::vkMapMemory(_privateDevice->device(), _memory, 0, VK_WHOLE_SIZE, 0, &_mapped) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}

	return _mapped;
};

void Indium::PrivateBuffer::didModifyRange(Range<size_t> range) {
	if (_hostPointer && _hostPointerCoherent) {
		// imported memory from a host-coherent memory type is kept in sync automatically
		return;
	}

	VkMappedMemoryRange vulkanRange {};
	vulkanRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	vulkanRange.memory = _memory;
	vulkanRange.size = range.length;
	vulkanRange.offset = range.start;
	if (DynamicVK::vkFlushMappedMemoryRanges(_privateDevice->device(), 1, &vulkanRange) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

uint64_t Indium::PrivateBuffer::gpuAddress() {
	VkBufferDeviceAddressInfo info {};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	info.buffer = _buffer;
	return DynamicVK::vkGetBufferDeviceAddress(_privateDevice->device(), &info);
};
