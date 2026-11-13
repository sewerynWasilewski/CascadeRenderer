#pragma once
#include "renderer/renderer.h"

struct GLFWwindow;

// ImGui/Vulkan wiring, kept outside Renderer entirely - Renderer only exposes the escape
// hatches (getNativeHandles/getCurrentCommandBuffer/...) this uses, it never knows ImGui exists.
void initDebugUI(Renderer& renderer, GLFWwindow* window);
void beginDebugUIFrame();
void renderDebugUI(Renderer& renderer);
void shutdownDebugUI();
