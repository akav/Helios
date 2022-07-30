#pragma once

#include "Pch.hpp"

#include "PipelineState.hpp"
#include "Resources.hpp"
#include "Descriptor.hpp"

namespace helios::gfx
{
	class Device;

	// Wrapper class for Graphics CommandList, which provides a set of easy and simple functions to record commands for execution by GPU.
	// The command queue will contain a queue of command list, which can be passed into the GraphicsContext's constructor to create a GraphicsContext object.
	// note (rtarun9) : This design is subject to change.
	class GraphicsContext
	{
	public:
		GraphicsContext(Device& device);
		ID3D12GraphicsCommandList* const GetCommandList() { return mCommandList.Get(); }

		// Core functionalities.

		// Resource related functions : 
		void ResourceBarrier(ID3D12Resource* const resource, D3D12_RESOURCE_STATES previousState, D3D12_RESOURCE_STATES newState) const;
		void ResourceBarrier(std::span<const RenderTarget*> renderTargets, D3D12_RESOURCE_STATES previousState, D3D12_RESOURCE_STATES newState) const;

		void ClearRenderTargetView(BackBuffer* const backBuffer, std::span<const float, 4> color);
		void ClearRenderTargetView(std::span<const RenderTarget*> renderTargets, std::span<const float, 4> color);

		void ClearDepthStencilView(Texture* const depthStencilTexture, float depth = 1.0f);

		void SetDescriptorHeaps(Descriptor* const descriptor) const;

		// COnfigure pipeline / root signature related functions.
		void SetGraphicsPipelineState(PipelineState* const pipelineState) const;
		void SetComputePipelineState(PipelineState* const pipelineState) const;
		void SetGraphicsRootSignature(PipelineState* const pipelineState) const;
		void SetComputeRootSignature(PipelineState* const pipelineState) const;
		void SetPipelineStateObject(PipelineState* constpipelineState) const;

		void SetIndexBuffer(Buffer* const buffer) const;
		void Set32BitGraphicsConstants(const void* renderResources) const;
		void Set32BitComputeConstants(const void* renderResources) const;
		
		void SetDefaultViewportAndScissor() const;
		
		void SetPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const;

		void SetRenderTarget(BackBuffer* const renderTarget, const Texture* depthStencilTexture) const;
		void SetRenderTarget(std::span<const RenderTarget*> renderTargets, const Texture* depthStencilTexture) const;

		// Draw functions.
		void DrawInstanceIndexed(uint32_t indicesCount) const;

	private:
		static constexpr uint32_t NUMBER_32_BIT_CONSTANTS = 64;

		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList{};

		// Functions such as SetRenderTargets() need to get the descriptor handle (present in the Device class) from the texture index.
		// For similar reasons, the GraphicsContext has a reference to the device.
		Device& mDevice;
	};
}


