#include <Logging.h>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <json.hpp>
#include <fstream>

#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/type_ptr.hpp>

#include "Graphics/IndexBuffer.h"
#include "Graphics/VertexBuffer.h"
#include "Graphics/VertexArrayObject.h"
#include "Graphics/Shader.h"
#include "Gameplay/Camera.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Behaviours/CameraControlBehaviour.h"
#include "Behaviours/FollowPathBehaviour.h"
#include "Behaviours/SimpleMoveBehaviour.h"
#include "Gameplay/Application.h"
#include "Gameplay/GameObjectTag.h"
#include "Gameplay/IBehaviour.h"
#include "Gameplay/Transform.h"
#include "Graphics/Texture2D.h"
#include "Graphics/Texture2DData.h"
#include "Utilities/InputHelpers.h"
#include "Utilities/MeshBuilder.h"
#include "Utilities/MeshFactory.h"
#include "Utilities/NotObjLoader.h"
#include "Utilities/ObjLoader.h"
#include "Utilities/VertexTypes.h"
#include "Gameplay/Scene.h"
#include "Gameplay/ShaderMaterial.h"
#include "Gameplay/RendererComponent.h"
#include "Gameplay/Timing.h"
#include "Graphics/TextureCubeMap.h"
#include "Graphics/TextureCubeMapData.h"
#include "Utilities/Util.h"

#define LOG_GL_NOTIFICATIONS
#define NUM_TREES 25
#define PLANE_X 19.0f
#define PLANE_Y 19.0f
#define DNS_X 3.0f
#define DNS_Y 3.0f

/*
	Handles debug messages from OpenGL
	https://www.khronos.org/opengl/wiki/Debug_Output#Message_Components
	@param source    Which part of OpenGL dispatched the message
	@param type      The type of message (ex: error, performance issues, deprecated behavior)
	@param id        The ID of the error or message (to distinguish between different types of errors, like nullref or index out of range)
	@param severity  The severity of the message (from High to Notification)
	@param length    The length of the message
	@param message   The human readable message from OpenGL
	@param userParam The pointer we set with glDebugMessageCallback (should be the game pointer)
*/
void GlDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	std::string sourceTxt;
	switch (source) {
	case GL_DEBUG_SOURCE_API: sourceTxt = "DEBUG"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceTxt = "WINDOW"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceTxt = "SHADER"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY: sourceTxt = "THIRD PARTY"; break;
	case GL_DEBUG_SOURCE_APPLICATION: sourceTxt = "APP"; break;
	case GL_DEBUG_SOURCE_OTHER: default: sourceTxt = "OTHER"; break;
	}
	switch (severity) {
	case GL_DEBUG_SEVERITY_LOW:          LOG_INFO("[{}] {}", sourceTxt, message); break;
	case GL_DEBUG_SEVERITY_MEDIUM:       LOG_WARN("[{}] {}", sourceTxt, message); break;
	case GL_DEBUG_SEVERITY_HIGH:         LOG_ERROR("[{}] {}", sourceTxt, message); break;
		#ifdef LOG_GL_NOTIFICATIONS
	case GL_DEBUG_SEVERITY_NOTIFICATION: LOG_INFO("[{}] {}", sourceTxt, message); break;
		#endif
	default: break;
	}
}

GLFWwindow* window;

void GlfwWindowResizedCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	Application::Instance().ActiveScene->Registry().view<Camera>().each([=](Camera & cam) {
		cam.ResizeWindow(width, height);
	});
}

bool InitGLFW() {
	if (glfwInit() == GLFW_FALSE) {
		LOG_ERROR("Failed to initialize GLFW");
		return false;
	}

#ifdef _DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif
	
	//Create a new GLFW window
	window = glfwCreateWindow(800, 800, "INFR1350U", nullptr, nullptr);
	glfwMakeContextCurrent(window);

	// Set our window resized callback
	glfwSetWindowSizeCallback(window, GlfwWindowResizedCallback);

	// Store the window in the application singleton
	Application::Instance().Window = window;

	return true;
}

bool InitGLAD() {
	if (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) == 0) {
		LOG_ERROR("Failed to initialize Glad");
		return false;
	}
	return true;
}

void InitImGui() {
	// Creates a new ImGUI context
	ImGui::CreateContext();
	// Gets our ImGUI input/output 
	ImGuiIO& io = ImGui::GetIO();
	// Enable keyboard navigation
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// Allow docking to our window
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// Allow multiple viewports (so we can drag ImGui off our window)
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	// Allow our viewports to use transparent backbuffers
	io.ConfigFlags |= ImGuiConfigFlags_TransparentBackbuffers;

	// Set up the ImGui implementation for OpenGL
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 410");

	// Dark mode FTW
	ImGui::StyleColorsDark();

	// Get our imgui style
	ImGuiStyle& style = ImGui::GetStyle();
	//style.Alpha = 1.0f;
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 0.8f;
	}
}

void ShutdownImGui()
{
	// Cleanup the ImGui implementation
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	// Destroy our ImGui context
	ImGui::DestroyContext();
}

std::vector<std::function<void()>> imGuiCallbacks;
void RenderImGui() {
	// Implementation new frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	// ImGui context new frame
	ImGui::NewFrame();

	if (ImGui::Begin("Debug")) {
		// Render our GUI stuff
		for (auto& func : imGuiCallbacks) {
			func();
		}
		ImGui::End();
	}
	
	// Make sure ImGui knows how big our window is
	ImGuiIO& io = ImGui::GetIO();
	int width{ 0 }, height{ 0 };
	glfwGetWindowSize(window, &width, &height);
	io.DisplaySize = ImVec2((float)width, (float)height);

	// Render all of our ImGui elements
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// If we have multiple viewports enabled (can drag into a new window)
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		// Update the windows that ImGui is using
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		// Restore our gl context
		glfwMakeContextCurrent(window);
	}
}

void RenderVAO(
	const Shader::sptr& shader,
	const VertexArrayObject::sptr& vao,
	const glm::mat4& viewProjection,
	const Transform& transform)
{
	shader->SetUniformMatrix("u_ModelViewProjection", viewProjection * transform.WorldTransform());
	shader->SetUniformMatrix("u_Model", transform.WorldTransform()); 
	shader->SetUniformMatrix("u_NormalMatrix", transform.WorldNormalMatrix());
	vao->Render();
}

void SetupShaderForFrame(const Shader::sptr& shader, const glm::mat4& view, const glm::mat4& projection) {
	shader->Bind();
	// These are the uniforms that update only once per frame
	shader->SetUniformMatrix("u_View", view);
	shader->SetUniformMatrix("u_ViewProjection", projection * view);
	shader->SetUniformMatrix("u_SkyboxMatrix", projection * glm::mat4(glm::mat3(view)));
	glm::vec3 camPos = glm::inverse(view) * glm::vec4(0,0,0,1);
	shader->SetUniform("u_CamPos", camPos);
}

int main() {
	Logger::Init(); // We'll borrow the logger from the toolkit, but we need to initialize it

	//Initialize GLFW
	if (!InitGLFW())
		return 1;

	//Initialize GLAD
	if (!InitGLAD())
		return 1;

	int frameIx = 0;
	float fpsBuffer[128];
	float minFps, maxFps, avgFps;
	int selectedVao = 0; // select cube by default
	std::vector<GameObject> controllables;

	// Let OpenGL know that we want debug output, and route it to our handler function
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(GlDebugMessage, nullptr);

	// Enable texturing
	glEnable(GL_TEXTURE_2D);

	// Push another scope so most memory should be freed *before* we exit the app
	{
		#pragma region Shader and ImGui

		// Load our shaders
		Shader::sptr shader = Shader::Create();
		shader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		shader->LoadShaderPartFromFile("shaders/frag_blinn_phong_textured.glsl", GL_FRAGMENT_SHADER);
		shader->Link();

		glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 2.0f);
		glm::vec3 lightCol = glm::vec3(0.9f, 0.85f, 0.5f);
		float     lightAmbientPow = 0.7f;
		float     lightSpecularPow = 1.0f;
		glm::vec3 ambientCol = glm::vec3(1.0f);
		float     ambientPow = 0.1f;
		float     lightLinearFalloff = 0.009;
		float     lightQuadraticFalloff = 0.032f;
		
		int lightoff = 0;
		int ambientonly = 0;
		int specularonly = 0;
		int ambientandspecular = 0;
		int ambientspeculartoon = 0;

		// These are our application / scene level uniforms that don't necessarily update
		// every frame
		shader->SetUniform("u_LightPos", lightPos);
		shader->SetUniform("u_LightCol", lightCol);
		shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
		shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
		shader->SetUniform("u_AmbientCol", ambientCol);
		shader->SetUniform("u_AmbientStrength", ambientPow);
		shader->SetUniform("u_LightAttenuationConstant", 1.0f);
		shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
		shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		shader->SetUniform("u_lightoff", lightoff);
		shader->SetUniform("u_ambient", ambientonly);
		shader->SetUniform("u_specular", specularonly);
		shader->SetUniform("u_ambientspecular", ambientandspecular);
		shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon);

		// We'll add some ImGui controls to control our shader

		imGuiCallbacks.push_back([&]() {
			if (ImGui::CollapsingHeader("Scene Level Lighting Settings"))
			{
				if (ImGui::ColorPicker3("Ambient Color", glm::value_ptr(ambientCol))) {
					shader->SetUniform("u_AmbientCol", ambientCol);
				}
				if (ImGui::SliderFloat("Fixed Ambient Power", &ambientPow, 0.01f, 1.0f)) {
					shader->SetUniform("u_AmbientStrength", ambientPow);
				}
			}
			if (ImGui::CollapsingHeader("Light Level Lighting Settings"))
			{
				if (ImGui::DragFloat3("Light Pos", glm::value_ptr(lightPos), 0.01f, -10.0f, 10.0f)) {
					shader->SetUniform("u_LightPos", lightPos);
				}
				if (ImGui::ColorPicker3("Light Col", glm::value_ptr(lightCol))) {
					shader->SetUniform("u_LightCol", lightCol);
				}
				if (ImGui::SliderFloat("Light Ambient Power", &lightAmbientPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
				}
				if (ImGui::SliderFloat("Light Specular Power", &lightSpecularPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				if (ImGui::DragFloat("Light Linear Falloff", &lightLinearFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
				}
				if (ImGui::DragFloat("Light Quadratic Falloff", &lightQuadraticFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
				}
			}

			//Toggle buttons
			if (ImGui::CollapsingHeader("Toggle buttons"))
			{
				if (ImGui::Button("No Lighting")) {
					shader->SetUniform("u_lightoff", lightoff = 1);
					shader->SetUniform("u_ambient", ambientonly = 0);
					shader->SetUniform("u_specular", specularonly = 0);
					shader->SetUniform("u_ambientspecular", ambientandspecular = 0);
					shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon = 0);
				}

				if (ImGui::Button("Ambient only"))
				{
					shader->SetUniform("u_lightoff", lightoff = 0);
					shader->SetUniform("u_ambient", ambientonly = 1);
					shader->SetUniform("u_specular", specularonly = 0);
					shader->SetUniform("u_ambientspecular", ambientandspecular = 0);
					shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon = 0);
				}

				if (ImGui::Button("specular only"))
				{
					shader->SetUniform("u_lightoff", lightoff = 0);
					shader->SetUniform("u_ambient", ambientonly = 0);
					shader->SetUniform("u_specular", specularonly = 1);
					shader->SetUniform("u_ambientspecular", ambientandspecular = 0);
					shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon = 0);
				}

				if (ImGui::Button("Ambient and Specular"))
				{
					shader->SetUniform("u_lightoff", lightoff = 0);
					shader->SetUniform("u_ambient", ambientonly = 0);
					shader->SetUniform("u_specular", specularonly = 0);
					shader->SetUniform("u_ambientspecular", ambientandspecular = 1);
					shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon = 0);
				}

				if (ImGui::Button("Ambient, Specular, and Toon Shading"))
				{
					shader->SetUniform("u_lightoff", lightoff = 0);
					shader->SetUniform("u_ambient", ambientonly = 0);
					shader->SetUniform("u_specular", specularonly = 0);
					shader->SetUniform("u_ambientspecular", ambientandspecular = 0);
					shader->SetUniform("u_ambientspeculartoon", ambientspeculartoon = 1);
				}
			}

			auto name = controllables[selectedVao].get<GameObjectTag>().Name;
			ImGui::Text(name.c_str());
			auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
			ImGui::Checkbox("Relative Rotation", &behaviour->Relative);

			ImGui::Text("Q/E -> Yaw\nLeft/Right -> Roll\nUp/Down -> Pitch\nY -> Toggle Mode");

			minFps = FLT_MAX;
			maxFps = 0;
			avgFps = 0;
			for (int ix = 0; ix < 128; ix++) {
				if (fpsBuffer[ix] < minFps) { minFps = fpsBuffer[ix]; }
				if (fpsBuffer[ix] > maxFps) { maxFps = fpsBuffer[ix]; }
				avgFps += fpsBuffer[ix];
			}
			ImGui::PlotLines("FPS", fpsBuffer, 128);
			ImGui::Text("MIN: %f MAX: %f AVG: %f", minFps, maxFps, avgFps / 128.0f);
			});

		#pragma endregion 

		// GL states
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL); // New 

		#pragma region TEXTURE LOADING

		// Load some textures from files
		Texture2D::sptr diffuse = Texture2D::LoadFromFile("images/Stone_001_Diffuse.png");
		Texture2D::sptr diffuseGround = Texture2D::LoadFromFile("images/grass.jpg");
		Texture2D::sptr diffuseDunce = Texture2D::LoadFromFile("images/Dunce.png");
		Texture2D::sptr diffuseDuncet = Texture2D::LoadFromFile("images/Duncet.png");
		Texture2D::sptr diffuseSlide = Texture2D::LoadFromFile("images/Slide.png");
		Texture2D::sptr diffuseSwing = Texture2D::LoadFromFile("images/Swing.png");
		Texture2D::sptr diffuseTable = Texture2D::LoadFromFile("images/Table.png");
		Texture2D::sptr diffuseTreeBig = Texture2D::LoadFromFile("images/TreeBig.png");
		Texture2D::sptr diffuseRedBalloon = Texture2D::LoadFromFile("images/BalloonRed.png");
		Texture2D::sptr diffuseYellowBalloon = Texture2D::LoadFromFile("images/BalloonYellow.png");
		Texture2D::sptr diffuse2 = Texture2D::LoadFromFile("images/box.bmp");
		Texture2D::sptr specular = Texture2D::LoadFromFile("images/Stone_001_Specular.png");
		Texture2D::sptr reflectivity = Texture2D::LoadFromFile("images/box-reflections.bmp");

		// Load the cube map
		//TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/sample.jpg");
		TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/ocean.jpg"); 

		// Creating an empty texture
		Texture2DDescription desc = Texture2DDescription();  
		desc.Width = 1;
		desc.Height = 1;
		desc.Format = InternalFormat::RGB8;
		Texture2D::sptr texture2 = Texture2D::Create(desc);
		// Clear it with a white colour
		texture2->Clear();

		#pragma endregion

		///////////////////////////////////// Scene Generation //////////////////////////////////////////////////
		#pragma region Scene Generation
		
		// We need to tell our scene system what extra component types we want to support
		GameScene::RegisterComponentType<RendererComponent>();
		GameScene::RegisterComponentType<BehaviourBinding>();
		GameScene::RegisterComponentType<Camera>();

		// Create a scene, and set it to be the active scene in the application
		GameScene::sptr scene = GameScene::Create("test");
		Application::Instance().ActiveScene = scene;

		// We can create a group ahead of time to make iterating on the group faster
		entt::basic_group<entt::entity, entt::exclude_t<>, entt::get_t<Transform>, RendererComponent> renderGroup =
			scene->Registry().group<RendererComponent>(entt::get_t<Transform>());

		// Create a material and set some properties for it
		ShaderMaterial::sptr materialGround = ShaderMaterial::Create();  
		materialGround->Shader = shader;
		materialGround->Set("s_Diffuse", diffuseGround);
		materialGround->Set("s_Diffuse2", diffuse2);
		materialGround->Set("s_Specular", specular);
		materialGround->Set("u_Shininess", 8.0f);
		materialGround->Set("u_TextureMix", 0.0f); 
		
		ShaderMaterial::sptr materialDunce = ShaderMaterial::Create();  
		materialDunce->Shader = shader;
		materialDunce->Set("s_Diffuse", diffuseDunce);
		materialDunce->Set("s_Diffuse2", diffuse2);
		materialDunce->Set("s_Specular", specular);
		materialDunce->Set("u_Shininess", 8.0f);
		materialDunce->Set("u_TextureMix", 0.0f); 
		
		ShaderMaterial::sptr materialDuncet = ShaderMaterial::Create();  
		materialDuncet->Shader = shader;
		materialDuncet->Set("s_Diffuse", diffuseDuncet);
		materialDuncet->Set("s_Diffuse2", diffuse2);
		materialDuncet->Set("s_Specular", specular);
		materialDuncet->Set("u_Shininess", 8.0f);
		materialDuncet->Set("u_TextureMix", 0.0f); 

		ShaderMaterial::sptr materialSlide = ShaderMaterial::Create();  
		materialSlide->Shader = shader;
		materialSlide->Set("s_Diffuse", diffuseSlide);
		materialSlide->Set("s_Diffuse2", diffuse2);
		materialSlide->Set("s_Specular", specular);
		materialSlide->Set("u_Shininess", 8.0f);
		materialSlide->Set("u_TextureMix", 0.0f);
		
		ShaderMaterial::sptr materialSwing = ShaderMaterial::Create();  
		materialSwing->Shader = shader;
		materialSwing->Set("s_Diffuse", diffuseSwing);
		materialSwing->Set("s_Diffuse2", diffuse2);
		materialSwing->Set("s_Specular", specular);
		materialSwing->Set("u_Shininess", 8.0f);
		materialSwing->Set("u_TextureMix", 0.0f);
		
		ShaderMaterial::sptr materialTable = ShaderMaterial::Create();  
		materialTable->Shader = shader;
		materialTable->Set("s_Diffuse", diffuseTable);
		materialTable->Set("s_Diffuse2", diffuse2);
		materialTable->Set("s_Specular", specular);
		materialTable->Set("u_Shininess", 8.0f);
		materialTable->Set("u_TextureMix", 0.0f);
		
		ShaderMaterial::sptr materialTreeBig = ShaderMaterial::Create();  
		materialTreeBig->Shader = shader;
		materialTreeBig->Set("s_Diffuse", diffuseTreeBig);
		materialTreeBig->Set("s_Diffuse2", diffuse2);
		materialTreeBig->Set("s_Specular", specular);
		materialTreeBig->Set("u_Shininess", 8.0f);
		materialTreeBig->Set("u_TextureMix", 0.0f);
		
		ShaderMaterial::sptr materialredballoon = ShaderMaterial::Create();  
		materialredballoon->Shader = shader;
		materialredballoon->Set("s_Diffuse", diffuseRedBalloon);
		materialredballoon->Set("s_Diffuse2", diffuse2);
		materialredballoon->Set("s_Specular", specular);
		materialredballoon->Set("u_Shininess", 8.0f);
		materialredballoon->Set("u_TextureMix", 0.0f);
		
		ShaderMaterial::sptr materialyellowballoon = ShaderMaterial::Create();  
		materialyellowballoon->Shader = shader;
		materialyellowballoon->Set("s_Diffuse", diffuseYellowBalloon);
		materialyellowballoon->Set("s_Diffuse2", diffuse2);
		materialyellowballoon->Set("s_Specular", specular);
		materialyellowballoon->Set("u_Shininess", 8.0f);
		materialyellowballoon->Set("u_TextureMix", 0.0f);
		

		// Load a second material for our reflective material!
		Shader::sptr reflectiveShader = Shader::Create();
		reflectiveShader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflectiveShader->LoadShaderPartFromFile("shaders/frag_reflection.frag.glsl", GL_FRAGMENT_SHADER);
		reflectiveShader->Link();

		Shader::sptr reflective = Shader::Create();
		reflective->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		reflective->LoadShaderPartFromFile("shaders/frag_blinn_phong_reflection.glsl", GL_FRAGMENT_SHADER);
		reflective->Link();
		
		// 
		ShaderMaterial::sptr material1 = ShaderMaterial::Create(); 
		material1->Shader = reflective;
		material1->Set("s_Diffuse", diffuse);
		material1->Set("s_Diffuse2", diffuse2);
		material1->Set("s_Specular", specular);
		material1->Set("s_Reflectivity", reflectivity); 
		material1->Set("s_Environment", environmentMap); 
		material1->Set("u_LightPos", lightPos);
		material1->Set("u_LightCol", lightCol);
		material1->Set("u_AmbientLightStrength", lightAmbientPow); 
		material1->Set("u_SpecularLightStrength", lightSpecularPow); 
		material1->Set("u_AmbientCol", ambientCol);
		material1->Set("u_AmbientStrength", ambientPow);
		material1->Set("u_LightAttenuationConstant", 1.0f);
		material1->Set("u_LightAttenuationLinear", lightLinearFalloff);
		material1->Set("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		material1->Set("u_Shininess", 8.0f);
		material1->Set("u_TextureMix", 0.5f);
		material1->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
		
		ShaderMaterial::sptr reflectiveMat = ShaderMaterial::Create();
		reflectiveMat->Shader = reflectiveShader;
		reflectiveMat->Set("s_Environment", environmentMap);
		reflectiveMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));

		GameObject objGround = scene->CreateEntity("Ground"); 
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Ground.obj");
			objGround.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialGround);
			objGround.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			objGround.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objGround.get<Transform>().SetLocalScale(0.5f, 0.25f, 0.5f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objGround);
		}

		GameObject objDunce = scene->CreateEntity("Dunce");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Dunce.obj");
			objDunce.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialDunce);
			objDunce.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.9f);
			objDunce.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objDunce.get<Transform>().SetLocalScale(1.0f, 1.0f, 1.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objDunce);
		}

		GameObject objDuncet = scene->CreateEntity("Duncet");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Duncet.obj");
			objDuncet.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialDuncet);
			objDuncet.get<Transform>().SetLocalPosition(2.0f, 0.0f, 0.8f);
			objDuncet.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objDuncet.get<Transform>().SetLocalScale(1.0f, 1.0f, 1.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objDuncet);
		}


		GameObject objSlide = scene->CreateEntity("Slide");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Slide.obj");
			objSlide.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialSlide);
			objSlide.get<Transform>().SetLocalPosition(0.0f, 5.0f, 3.0f);
			objSlide.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objSlide.get<Transform>().SetLocalScale(0.5f, 0.5f, 0.5f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objSlide);
		}
		
		GameObject objRedBalloon = scene->CreateEntity("Redballoon");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Balloon.obj");
			objRedBalloon.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialredballoon);
			objRedBalloon.get<Transform>().SetLocalPosition(2.5f, -10.0f, 3.0f);
			objRedBalloon.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objRedBalloon.get<Transform>().SetLocalScale(0.5f, 0.5f, 0.5f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objRedBalloon);

			// Bind returns a smart pointer to the behaviour that was added
			auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(objRedBalloon);
			// Set up a path for the object to follow
			pathing->Points.push_back({ -2.5f, -10.0f, 3.0f });
			pathing->Points.push_back({ 2.5f, -10.0f, 3.0f });
			pathing->Points.push_back({ 2.5f, -5.0f, 3.0f });
			pathing->Points.push_back({ -2.5f, -5.0f, 3.0f });
			pathing->Speed = 2.0f;
		}
		
		GameObject objYellowBalloon = scene->CreateEntity("Yellowballoon");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Balloon.obj");
			objYellowBalloon.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialyellowballoon);
			objYellowBalloon.get<Transform>().SetLocalPosition(-2.5f, -10.0f, 3.0f);
			objYellowBalloon.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objYellowBalloon.get<Transform>().SetLocalScale(0.5f, 0.5f, 0.5f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objYellowBalloon);

			// Bind returns a smart pointer to the behaviour that was added
			auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(objYellowBalloon);
			// Set up a path for the object to follow
			pathing->Points.push_back({ 2.5f, -10.0f, 3.0f });
			pathing->Points.push_back({ -2.5f, -10.0f, 3.0f });
			pathing->Points.push_back({ -2.5f,  -5.0f, 3.0f });
			pathing->Points.push_back({ 2.5f,  -5.0f, 3.0f });
			pathing->Speed = 2.0f;
		}
		
		//Taken from week 3 tutorial because I wanted random trees from our game
		std::vector<GameObject> randomTrees;
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/TreeBig.obj");
			for (int i = 0; i < NUM_TREES / 2; i++)
			{
				randomTrees.push_back(scene->CreateEntity("simplePine" + (std::to_string(i + 1))));
				randomTrees[i].emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialTreeBig);
				//Randomly places
				randomTrees[i].get<Transform>().SetLocalPosition(glm::vec3(Util::GetRandomNumberBetween(glm::vec2(-PLANE_X, -PLANE_Y), glm::vec2(PLANE_X, PLANE_Y), glm::vec2(-DNS_X, -DNS_Y), glm::vec2(DNS_X, DNS_Y)), 6.0f));
				randomTrees[i].get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
				randomTrees[i].get<Transform>().SetLocalScale(0.5f, 0.5f, 0.5f);
			}
		}

		GameObject objSwing = scene->CreateEntity("Swing");
		{
			// Build a mesh
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Swing.obj");
			objSwing.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialSwing);
			objSwing.get<Transform>().SetLocalPosition(-5.0f, 0.0f, 3.5f);
			objSwing.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objSwing.get<Transform>().SetLocalScale(0.5f, 0.5f, 0.5f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(objSwing);

			// Bind returns a smart pointer to the behaviour that was added
			/*auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj4);
			// Set up a path for the object to follow
			pathing->Points.push_back({ -4.0f, -4.0f, 0.0f });
			pathing->Points.push_back({ 4.0f, -4.0f, 0.0f });
			pathing->Points.push_back({ 4.0f,  4.0f, 0.0f });
			pathing->Points.push_back({ -4.0f,  4.0f, 0.0f });
			pathing->Speed = 2.0f;*/
		}

		GameObject objTable = scene->CreateEntity("table");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Table.obj");
			objTable.emplace<RendererComponent>().SetMesh(vao).SetMaterial(materialTable);
			objTable.get<Transform>().SetLocalPosition(5.0f, 0.0f, 1.25f);
			objTable.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			objTable.get<Transform>().SetLocalScale(0.35f, 0.35f, 0.35f);
			//obj6.get<Transform>().SetParent(obj4);
			
			/*auto pathing = BehaviourBinding::Bind<FollowPathBehaviour>(obj6);
			// Set up a path for the object to follow
			pathing->Points.push_back({ 0.0f, 0.0f, 1.0f });
			pathing->Points.push_back({ 0.0f, 0.0f, 3.0f });
			pathing->Speed = 2.0f;*/
		}
		
		// Create an object to be our camera
		GameObject cameraObject = scene->CreateEntity("Camera");
		{
			cameraObject.get<Transform>().SetLocalPosition(0, 3, 3).LookAt(glm::vec3(0, 0, 0));

			// We'll make our camera a component of the camera object
			Camera& camera = cameraObject.emplace<Camera>();// Camera::Create();
			camera.SetPosition(glm::vec3(0, 3, 3));
			camera.SetUp(glm::vec3(0, 0, 1));
			camera.LookAt(glm::vec3(0));
			camera.SetFovDegrees(90.0f); // Set an initial FOV
			camera.SetOrthoHeight(3.0f);
			BehaviourBinding::Bind<CameraControlBehaviour>(cameraObject);
		}

		#pragma endregion 
		//////////////////////////////////////////////////////////////////////////////////////////

		/////////////////////////////////// SKYBOX ///////////////////////////////////////////////
		{
			// Load our shaders
			Shader::sptr skybox = std::make_shared<Shader>();
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.vert.glsl", GL_VERTEX_SHADER);
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.frag.glsl", GL_FRAGMENT_SHADER);
			skybox->Link();

			ShaderMaterial::sptr skyboxMat = ShaderMaterial::Create();
			skyboxMat->Shader = skybox;  
			skyboxMat->Set("s_Environment", environmentMap);
			skyboxMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
			skyboxMat->RenderLayer = 100;

			MeshBuilder<VertexPosNormTexCol> mesh;
			MeshFactory::AddIcoSphere(mesh, glm::vec3(0.0f), 1.0f);
			MeshFactory::InvertFaces(mesh);
			VertexArrayObject::sptr meshVao = mesh.Bake();
			
			GameObject skyboxObj = scene->CreateEntity("skybox");  
			skyboxObj.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			skyboxObj.get_or_emplace<RendererComponent>().SetMesh(meshVao).SetMaterial(skyboxMat);
		}
		////////////////////////////////////////////////////////////////////////////////////////


		// We'll use a vector to store all our key press events for now (this should probably be a behaviour eventually)
		std::vector<KeyPressWatcher> keyToggles;
		{
			// This is an example of a key press handling helper. Look at InputHelpers.h an .cpp to see
			// how this is implemented. Note that the ampersand here is capturing the variables within
			// the scope. If you wanted to do some method on the class, your best bet would be to give it a method and
			// use std::bind
			keyToggles.emplace_back(GLFW_KEY_T, [&]() { cameraObject.get<Camera>().ToggleOrtho(); });

			controllables.push_back(objDunce);
			controllables.push_back(objDuncet);

			keyToggles.emplace_back(GLFW_KEY_KP_ADD, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao++;
				if (selectedVao >= controllables.size())
					selectedVao = 0;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});
			keyToggles.emplace_back(GLFW_KEY_KP_SUBTRACT, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao--;
				if (selectedVao < 0)
					selectedVao = controllables.size() - 1;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});

			keyToggles.emplace_back(GLFW_KEY_Y, [&]() {
				auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
				behaviour->Relative = !behaviour->Relative;
				});
		}
		
		InitImGui();

		// Initialize our timing instance and grab a reference for our use
		Timing& time = Timing::Instance();
		time.LastFrame = glfwGetTime();

		///// Game loop /////
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			// Update the timing
			time.CurrentFrame = glfwGetTime();
			time.DeltaTime = static_cast<float>(time.CurrentFrame - time.LastFrame);

			time.DeltaTime = time.DeltaTime > 1.0f ? 1.0f : time.DeltaTime;

			// Update our FPS tracker data
			fpsBuffer[frameIx] = 1.0f / time.DeltaTime;
			frameIx++;
			if (frameIx >= 128)
				frameIx = 0;

			// We'll make sure our UI isn't focused before we start handling input for our game
			if (!ImGui::IsAnyWindowFocused()) {
				// We need to poll our key watchers so they can do their logic with the GLFW state
				// Note that since we want to make sure we don't copy our key handlers, we need a const
				// reference!
				for (const KeyPressWatcher& watcher : keyToggles) {
					watcher.Poll(window);
				}
			}

			// Iterate over all the behaviour binding components
			scene->Registry().view<BehaviourBinding>().each([&](entt::entity entity, BehaviourBinding& binding) {
				// Iterate over all the behaviour scripts attached to the entity, and update them in sequence (if enabled)
				for (const auto& behaviour : binding.Behaviours) {
					if (behaviour->Enabled) {
						behaviour->Update(entt::handle(scene->Registry(), entity));
					}
				}
			});

			// Clear the screen
			glClearColor(0.08f, 0.17f, 0.31f, 1.0f);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Update all world matrices for this frame
			scene->Registry().view<Transform>().each([](entt::entity entity, Transform& t) {
				t.UpdateWorldMatrix();
			});
			
			// Grab out camera info from the camera object
			Transform& camTransform = cameraObject.get<Transform>();
			glm::mat4 view = glm::inverse(camTransform.LocalTransform());
			glm::mat4 projection = cameraObject.get<Camera>().GetProjection();
			glm::mat4 viewProjection = projection * view;
						
			// Sort the renderers by shader and material, we will go for a minimizing context switches approach here,
			// but you could for instance sort front to back to optimize for fill rate if you have intensive fragment shaders
			renderGroup.sort<RendererComponent>([](const RendererComponent& l, const RendererComponent& r) {
				// Sort by render layer first, higher numbers get drawn last
				if (l.Material->RenderLayer < r.Material->RenderLayer) return true;
				if (l.Material->RenderLayer > r.Material->RenderLayer) return false;

				// Sort by shader pointer next (so materials using the same shader run sequentially where possible)
				if (l.Material->Shader < r.Material->Shader) return true;
				if (l.Material->Shader > r.Material->Shader) return false;

				// Sort by material pointer last (so we can minimize switching between materials)
				if (l.Material < r.Material) return true;
				if (l.Material > r.Material) return false;
				
				return false;
			});

			// Start by assuming no shader or material is applied
			Shader::sptr current = nullptr;
			ShaderMaterial::sptr currentMat = nullptr;

			// Iterate over the render group components and draw them
			renderGroup.each( [&](entt::entity e, RendererComponent& renderer, Transform& transform) {
				// If the shader has changed, set up it's uniforms
				if (current != renderer.Material->Shader) {
					current = renderer.Material->Shader;
					current->Bind();
					SetupShaderForFrame(current, view, projection);
				}
				// If the material has changed, apply it
				if (currentMat != renderer.Material) {
					currentMat = renderer.Material;
					currentMat->Apply();
				}
				// Render the mesh
				RenderVAO(renderer.Material->Shader, renderer.Mesh, viewProjection, transform);
			});

			// Draw our ImGui content
			RenderImGui();

			scene->Poll();
			glfwSwapBuffers(window);
			time.LastFrame = time.CurrentFrame;
		}

		// Nullify scene so that we can release references
		Application::Instance().ActiveScene = nullptr;
		ShutdownImGui();
	}	

	// Clean up the toolkit logger so we don't leak memory
	Logger::Uninitialize();
	return 0;
}