#pragma once

#include "Pch.hpp"

#include "Scene/Camera.hpp"
#include "Scene/Model.hpp"
#include "Scene/Light.hpp"

namespace helios::scene
{
	// The reason for this abstraction is to seperate the code for managing scene objects (camera / model / light) from the SandBox, which is mostly related to 
	// rendering techniques and other stuff.
	// However, the scene will not hold a reference to the gfx::Device as this class is mostly handled from engien.
	// Note that all member variables are public, can be freely accessed from anywhere.
	class Scene
	{
	public:
		Scene(const gfx::Device* device);
		~Scene();

		void AddModel(const gfx::Device* device, const ModelCreationDesc& modelCreationDesc);

		// Note sure why using a regular pointer and then doing a unnamed object construction and move doesnt work here,
		// So the function directly takes a unique_ptr.
		void AddModel(std::unique_ptr<Model> model);

		void AddLight(const gfx::Device* device, const LightCreationDesc& modelCreationDesc);
		void AddCamera();

		// Aspect ratio is determined by engine.
		void Update(float cameraAspectRatio);

		void RenderModels(const gfx::GraphicsContext* graphicsContext);
		void RenderLights(const gfx::GraphicsContext* graphicsContext);

	public:
		std::vector<std::unique_ptr<Model>> mModels{};
		std::vector<std::unique_ptr<Light>> mLights{};
		std::unique_ptr<Camera> mCamera{};

		std::unique_ptr<gfx::Buffer> mSceneBuffer{};
	};
}