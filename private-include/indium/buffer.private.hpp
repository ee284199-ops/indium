#pragma once

#include <indium/buffer.hpp>

#include <vulkan/vulkan.h>

#include <functional>

namespace Indium {
	class PrivateDevice;

	class PrivateBuffer: public Buffer {
	private:
		std::shared_ptr<PrivateDevice> _privateDevice;
		size_t _length;
		StorageMode _storageMode;
		void* _mapped = nullptr;
		// only set for buffers whose memory was imported from a host pointer;
		// contents() returns this instead of mapping the Vulkan memory object.
		void* _hostPointer = nullptr;
		// whether the imported memory came from a host-coherent memory type;
		// if not, didModifyRange() must flush explicitly.
		bool _hostPointerCoherent = false;
		std::function<void()> _deallocator;

		// used by importHostMemory(); does no Vulkan work of its own
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, StorageMode storageMode, void* hostPointer);

	public:
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options);
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, const void* pointer, size_t length, ResourceOptions options);
		~PrivateBuffer();

		/**
		 * Wraps caller-owned host memory without copying it.
		 *
		 * @param device      the device to create the Vulkan objects on.
		 * @param pointer     the host memory to wrap; must be aligned to the device's minimum
		 *                    imported host pointer alignment.
		 * @param length      the length of the host memory to wrap; must be aligned to the device's
		 *                    minimum imported host pointer alignment.
		 * @param options     resource options; only Shared and Managed storage modes are supported.
		 * @param deallocator invoked by ~PrivateBuffer once the buffer and its Vulkan memory have
		 *                    been destroyed; must not release the host memory before that.
		 *
		 * @return the wrapped buffer, or nullptr if the import isn't possible (e.g. no compatible
		 *         memory type exists or the import fails). The deallocator is NOT invoked if
		 *         nullptr is returned.
		 */
		static std::shared_ptr<PrivateBuffer> importHostMemory(std::shared_ptr<PrivateDevice> device, void* pointer, size_t length, ResourceOptions options, std::function<void()> deallocator);

		virtual std::shared_ptr<Device> device() override;

		virtual size_t length() const override;
		virtual void* contents() override;
		virtual void didModifyRange(Range<size_t> range) override;

		virtual uint64_t gpuAddress() override;

		INDIUM_PROPERTY(VkBuffer, b, B,uffer) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkDeviceMemory, m, M,emory) = VK_NULL_HANDLE;
	};
};
