# Hybrid-CPU-GPU-Raytracer-OpenGL
A real-time hybrid CPU+GPU ray tracer built with OpenGL 4.3 and GLSL. It performs physically-based path tracing entirely on the GPU fragment shader, while the CPU prepares the scene data and uploads it via SSBOs. No RTX or Vulkan required — runs on modest hardware including ARM Mali GPUs and even Android smartphones.
