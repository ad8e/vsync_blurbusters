#pragma once
#include "helper.h"

#if __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define GL_GLEXT_PROTOTYPES
#define GLFW_INCLUDE_GLCOREARB
#elif _WIN32
#define LOAD_WITH_GLAD 1
#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#endif

#define LINUX_EPOLL 0
#if LINUX_EPOLL
#define GLFW_EXPOSE_NATIVE_X11
//#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "GLFW/glfw3.h"
#else
#include "GLFW/glfw3.h"
#endif
GLFWmonitor* active_monitor; //what if there are multiple monitors active? future
single_def GLFWwindow* window;
inline bool time_to_exit() { return glfwWindowShouldClose(window); }