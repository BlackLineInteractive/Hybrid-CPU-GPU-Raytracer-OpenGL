# Hybrid CPU+GPU Ray Tracer in OpenGL (No RTX Required)

**Photorealistic real-time ray tracing** implemented from scratch in C++ and GLSL — no RTX, no Vulkan, no proprietary APIs.  
This project demonstrates a hybrid rendering pipeline where the **CPU prepares the scene** and **GPU performs ray tracing** entirely inside a fragment shader via OpenGL 4.3 Shader Storage Buffer Objects (SSBO).

⚡ Runs even on non-gaming hardware. Tested on an **Android smartphone** with full desktop Linux via Termux + X11.


## ✨ Features
- **Hybrid architecture** — CPU handles scene setup, GPU executes path tracing.
- **Physically based materials**:
  - Lambertian diffuse
  - Metal with roughness
  - Glass with Fresnel reflections and refraction
  - Emissive materials (light sources)
- **Real-time** path tracing with up to 8 bounces.
- **SSBO-based scene storage** for unlimited objects/materials.
- Gradient sky background for natural lighting.
- Camera orbit animation around the scene.
- Gamma correction for display-ready output.


## 📷 Screenshots
*(Add screenshots or GIFs here — ideally 2–3 from different camera angles)*


## 🛠 Technical Overview
The **CPU side**:
- Defines scene geometry and materials (`Sphere`, `Plane` classes).
- Prepares GPU-ready structures and uploads them to SSBOs.
- Updates camera position and view matrix every frame.

The **GPU side** (fragment shader):
- Casts a ray from the camera for each pixel.
- Intersects rays with all scene objects (spheres, planes).
- Applies material logic: reflection, refraction, diffuse scattering.
- Combines emitted and reflected light for final pixel color.
- Outputs tone-mapped, gamma-corrected image.

No rasterization of 3D meshes — the screen is a full-screen quad, and every pixel is computed by tracing rays.



## 📋 Hardware & Environment
This project was written, compiled, and run **entirely on a rugged Android smartphone** in a full Linux desktop environment.

- **CPU:** MediaTek Helio P70 (8 cores)
- **RAM:** 4 GB LPDDR4
- **GPU:** ARM Mali-G72 MP3
- **OS:** Android 9 with Termux + X11 + Debian Linux (XFCE4 desktop)

> No discrete GPU. No NVIDIA RTX. No problem.



## 🚀 Build & Run
**Dependencies:**
- OpenGL 4.3+
- GLEW
- SDL2
- GLM
- g++ or clang++

**On Debian/Ubuntu:**
```bash
sudo apt install g++ libglew-dev libsdl2-dev libglm-dev
g++ main.cpp -lGLEW -lSDL2 -lGL -std=c++17 -o raytracer
./raytracer
