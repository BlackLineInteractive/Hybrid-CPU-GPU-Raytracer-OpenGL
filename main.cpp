#include <iostream>
#include <vector>
#include <chrono>
#include <stdexcept>
#include <string>

#define GLEW_STATIC
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- Constants ---
const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 768;


// --- SHADERS (HEAVILY REVISED FRAGMENT SHADER) ---

const char* vertexShaderSource = R"(
#version 430 core
layout (location = 0) in vec2 aPos;
out vec2 TexCoords;
void main() {
    TexCoords = aPos;
    gl_Position = vec4(aPos.x * 2.0 - 1.0, aPos.y * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 430 core
out vec4 FragColor;
in vec2 TexCoords;

// --- Uniforms ---
uniform vec3 u_camera_pos;
uniform mat4 u_camera_view;
uniform float u_time;
uniform float u_aspect_ratio;

// --- Data Structures and Constants ---
const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_GLASS = 2;
const int MAT_EMISSIVE = 3;

struct MaterialData {
    vec4 baseColor;
    vec4 properties; // x: metallic, y: roughness, z: ior
    vec4 emission;
    int type;
};

struct ObjectData {
    mat4 modelMatrix;
    mat4 inverseModelMatrix;
    int materialIndex;
    int type; // 0: Sphere, 1: Cube, 2: Plane
    float radius;
    float _padding;
    vec3 halfSize;
};

struct Ray {
    vec3 origin;
    vec3 direction;
};

struct HitInfo {
    bool is_hit;
    float t;
    vec3 point;
    vec3 normal;
    int materialIndex;
    bool front_face;
};

// --- SSBO ---
layout(std430, binding = 0) buffer ObjectBuffer {
    ObjectData objects[];
};
layout(std430, binding = 1) buffer MaterialBuffer {
    MaterialData materials[];
};

// --- Utilities ---
uint seed = uint(gl_FragCoord.x) * uint(1973) + uint(gl_FragCoord.y) * uint(9277) + uint(u_time * 1000.0) * uint(26699);
float random() {
    seed = seed * uint(1664525) + uint(1013904223);
    return float(seed & uint(0x00FFFFFF)) / float(0x01000000);
}

vec3 random_in_unit_sphere() {
    while (true) {
        vec3 p = vec3(random() * 2.0 - 1.0, random() * 2.0 - 1.0, random() * 2.0 - 1.0);
        if (dot(p, p) < 1.0) return p;
    }
}

vec3 reflect(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

vec3 refract(vec3 uv, vec3 n, float etai_over_etat) {
    float cos_theta = min(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -sqrt(abs(1.0 - dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}

// --- Intersection Functions ---
void set_face_normal(inout HitInfo rec, Ray r, vec3 outward_normal) {
    rec.front_face = dot(r.direction, outward_normal) < 0.0;
    rec.normal = rec.front_face ? outward_normal : -outward_normal;
}

void intersect_sphere(Ray r, inout HitInfo hit_rec, int object_index) {
    ObjectData obj = objects[object_index];
    vec3 oc = r.origin - vec3(obj.modelMatrix[3]);
    float a = dot(r.direction, r.direction);
    float b = dot(oc, r.direction);
    float c = dot(oc, oc) - obj.radius * obj.radius;
    float discriminant = b * b - a * c;

    if (discriminant >= 0.0) {
        float t = (-b - sqrt(discriminant)) / a;
        if (t < 0.001) t = (-b + sqrt(discriminant)) / a;
        if (t > 0.001 && t < hit_rec.t) {
            hit_rec.is_hit = true;
            hit_rec.t = t;
            hit_rec.point = r.origin + r.direction * t;
            vec3 outward_normal = normalize(hit_rec.point - vec3(obj.modelMatrix[3]));
            set_face_normal(hit_rec, r, outward_normal);
            hit_rec.materialIndex = obj.materialIndex;
        }
    }
}

void intersect_plane(Ray r, inout HitInfo hit_rec, int object_index) {
    ObjectData obj = objects[object_index];
    vec3 plane_normal = normalize(vec3(obj.modelMatrix * vec4(0, 1, 0, 0)));
    vec3 plane_point = vec3(obj.modelMatrix[3]);

    float denom = dot(plane_normal, r.direction);
    if (abs(denom) > 0.001) {
        float t = dot(plane_point - r.origin, plane_normal) / denom;
        if (t > 0.001 && t < hit_rec.t) {
            hit_rec.is_hit = true;
            hit_rec.t = t;
            hit_rec.point = r.origin + r.direction * t;
            set_face_normal(hit_rec, r, plane_normal);
            hit_rec.materialIndex = obj.materialIndex;
        }
    }
}

// --- Material Logic ---
bool scatter(Ray r_in, HitInfo rec, out vec3 attenuation, out Ray scattered) {
    MaterialData mat = materials[rec.materialIndex];
    attenuation = mat.baseColor.rgb;
    
    if (mat.type == MAT_LAMBERTIAN) {
        vec3 scatter_direction = rec.normal + random_in_unit_sphere();
        if (length(scatter_direction) < 0.001) scatter_direction = rec.normal;
        scattered = Ray(rec.point, normalize(scatter_direction));
        return true;
    }
    if (mat.type == MAT_METAL) {
        vec3 reflected = reflect(r_in.direction, rec.normal);
        scattered = Ray(rec.point, normalize(reflected + mat.properties.y * random_in_unit_sphere()));
        return (dot(scattered.direction, rec.normal) > 0.0);
    }
    if (mat.type == MAT_GLASS) {
        float refraction_ratio = rec.front_face ? (1.0 / mat.properties.z) : mat.properties.z;
        
        float cos_theta = min(dot(-r_in.direction, rec.normal), 1.0);
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        
        // Check for total internal reflection
        bool cannot_refract = refraction_ratio * sin_theta > 1.0;
        vec3 direction;

        // Fresnel effect
        float r0 = (1.0 - refraction_ratio) / (1.0 + refraction_ratio);
        r0 = r0 * r0;
        float reflectance = r0 + (1.0 - r0) * pow((1.0 - cos_theta), 5.0);

        if (cannot_refract || reflectance > random()) {
            direction = reflect(r_in.direction, rec.normal);
        } else {
            direction = refract(r_in.direction, rec.normal, refraction_ratio);
        }
        scattered = Ray(rec.point, normalize(direction));
        return true;
    }
    return false; // For emissive and other materials
}


// --- Main Tracing Function ---
vec3 trace(Ray r) {
    vec3 final_color = vec3(0.0);
    vec3 attenuation = vec3(1.0);
    int MAX_DEPTH = 8; // Increased depth for glass

    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        HitInfo hit_rec;
        hit_rec.is_hit = false;
        hit_rec.t = 10000.0;

        for (int i = 0; i < objects.length(); ++i) {
            if (objects[i].type == 0) { // Sphere
                intersect_sphere(r, hit_rec, i);
            } else if (objects[i].type == 2) { // Plane
                intersect_plane(r, hit_rec, i);
            }
        }

        if (hit_rec.is_hit) {
            Ray scattered;
            vec3 current_attenuation;
            MaterialData mat = materials[hit_rec.materialIndex];

            vec3 emitted = mat.emission.rgb;

            if (scatter(r, hit_rec, current_attenuation, scattered)) {
                attenuation *= current_attenuation;
                r = scattered;
                final_color += emitted * attenuation;
            } else {
                final_color += emitted * attenuation;
                break;
            }
        } else {
            // Background (gradient)
            float t = 0.5 * (r.direction.y + 1.0);
            final_color += mix(vec3(1.0, 1.0, 1.0), vec3(0.5, 0.7, 1.0), t) * attenuation;
            break;
        }
    }
    return final_color;
}

void main() {
    vec2 uv = TexCoords;
    // uv.y = 1.0 - uv.y; // REMOVED to fix the inverted scene
    
    float fov_y = 60.0;
    float tan_half_fov = tan(radians(fov_y) / 2.0);

    vec3 ray_dir = normalize(vec3(
        (uv.x * 2.0 - 1.0) * u_aspect_ratio * tan_half_fov,
        (uv.y * 2.0 - 1.0) * tan_half_fov,
        -1.0
    ));
    
    Ray primary_ray;
    primary_ray.origin = u_camera_pos;
    primary_ray.direction = (inverse(u_camera_view) * vec4(ray_dir, 0.0)).xyz;

    // For anti-aliasing and soft effects, we would have a loop here,
    // but for the first run, one sample is enough.
    vec3 color = trace(primary_ray);

    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
)";


// --- CPU Data Structures ---
enum MaterialType { MAT_LAMBERTIAN = 0, MAT_METAL = 1, MAT_GLASS = 2, MAT_EMISSIVE = 3 };
struct MaterialData { glm::vec4 baseColor; glm::vec4 properties; glm::vec4 emission; int type; int _padding[3]; };
struct ObjectData { glm::mat4 modelMatrix; glm::mat4 inverseModelMatrix; int materialIndex; int type; float radius; float _padding; glm::vec3 halfSize; float _padding2; };
struct LightData { glm::vec4 position; glm::vec4 color; };
struct Material { std::string name; MaterialType type; glm::vec3 color; glm::vec3 emission; float metallic; float roughness; float ior; };

class SceneObject {
public:
    int id; glm::vec3 position; glm::mat4 rotation; int materialId;
    SceneObject(int matId) : id(-1), position(0.0f), rotation(1.0f), materialId(matId) {}
    virtual ~SceneObject() = default;
    glm::mat4 getModelMatrix() const { return glm::translate(glm::mat4(1.0f), position) * rotation; }
    virtual ObjectData getGPUData() const = 0;
};
class Sphere : public SceneObject {
public:
    float radius;
    Sphere(glm::vec3 pos, float r, int matId) : SceneObject(matId), radius(r) { position = pos; }
    ObjectData getGPUData() const override { ObjectData d{}; d.modelMatrix = getModelMatrix(); d.inverseModelMatrix = glm::inverse(d.modelMatrix); d.materialIndex = materialId; d.type = 0; d.radius = radius; return d; }
};
// ADDED CLASS FOR PLANE
class Plane : public SceneObject {
public:
    Plane(glm::vec3 pos, int matId) : SceneObject(matId) { position = pos; }
    ObjectData getGPUData() const override { ObjectData d{}; d.modelMatrix = getModelMatrix(); d.inverseModelMatrix = glm::inverse(d.modelMatrix); d.materialIndex = materialId; d.type = 2; return d; }
};
class Scene {
public:
    std::vector<SceneObject*> objects; std::vector<Material> materials;
    ~Scene() { for (auto obj : objects) delete obj; }
    int addMaterial(const Material& mat) { materials.push_back(mat); return materials.size() - 1; }
    void addObject(SceneObject* obj) { obj->id = objects.size(); objects.push_back(obj); }
};

// --- Shader Compilation Functions ---
void compileShader(GLuint shader, const std::string& type) { glCompileShader(shader); GLint success; glGetShaderiv(shader, GL_COMPILE_STATUS, &success); if (!success) { char infoLog[1024]; glGetShaderInfoLog(shader, 1024, NULL, infoLog); throw std::runtime_error("SHADER_COMPILATION_ERROR of type: " + type + "\n" + infoLog); } }
GLuint createShaderProgram() { GLuint vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &vertexShaderSource, NULL); compileShader(vs, "VERTEX"); GLuint fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fragmentShaderSource, NULL); compileShader(fs, "FRAGMENT"); GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog); GLint success; glGetProgramiv(prog, GL_LINK_STATUS, &success); if (!success) { char infoLog[1024]; glGetProgramInfoLog(prog, 1024, NULL, infoLog); throw std::runtime_error("SHADER_PROGRAM_LINKING_ERROR\n" + std::string(infoLog)); } glDeleteShader(vs); glDeleteShader(fs); return prog; }


// --- Main Program ---
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) throw std::runtime_error("SDL Init Failed");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("Hybrid Ray Tracer - Step 3 (Photoreal)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("GLEW Init Failed");

    // --- Scene Creation (CPU) ---
    Scene scene;
    int ground_mat_id = scene.addMaterial({"Ground", MAT_LAMBERTIAN, {0.5f, 0.5f, 0.5f}, {}, 0.0f, 1.0f, 1.0f});
    int center_mat_id = scene.addMaterial({"Center", MAT_GLASS, {1.0f, 1.0f, 1.0f}, {}, 0.0f, 0.0f, 1.52f});
    int left_mat_id = scene.addMaterial({"Left Metal", MAT_METAL, {0.8f, 0.8f, 0.8f}, {}, 1.0f, 0.0f, 1.0f});
    int right_mat_id = scene.addMaterial({"Right Metal", MAT_METAL, {0.8f, 0.6f, 0.2f}, {}, 1.0f, 0.3f, 1.0f});

    scene.addObject(new Plane({0.0f, -0.5f, 0.0f}, ground_mat_id));
    scene.addObject(new Sphere({0.0f, 0.0f, 0.0f}, 0.5f, center_mat_id));
    scene.addObject(new Sphere({-1.2f, 0.0f, 0.0f}, 0.5f, left_mat_id));
    scene.addObject(new Sphere({1.2f, 0.0f, 0.0f}, 0.5f, right_mat_id));

    // --- Preparing Data for GPU ---
    std::vector<ObjectData> object_gpu_data;
    for (const auto& obj : scene.objects) object_gpu_data.push_back(obj->getGPUData());
    std::vector<MaterialData> material_gpu_data;
    for (const auto& mat : scene.materials) {
        MaterialData d{}; d.baseColor=glm::vec4(mat.color,1); d.emission=glm::vec4(mat.emission,1); d.properties=glm::vec4(mat.metallic, mat.roughness, mat.ior,0); d.type=mat.type; material_gpu_data.push_back(d);
    }

    // --- Creating SSBOs ---
    GLuint object_ssbo, material_ssbo;
    glGenBuffers(1, &object_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, object_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, object_gpu_data.size() * sizeof(ObjectData), object_gpu_data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, object_ssbo);
    glGenBuffers(1, &material_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, material_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, material_gpu_data.size() * sizeof(MaterialData), material_gpu_data.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, material_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // --- Creating Shader Program and Fullscreen Quad ---
    GLuint shaderProgram = createShaderProgram();
    float quadVertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    GLuint VBO_quad, VAO_quad;
    glGenVertexArrays(1, &VAO_quad); glGenBuffers(1, &VBO_quad);
    glBindVertexArray(VAO_quad); glBindBuffer(GL_ARRAY_BUFFER, VBO_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Get uniform locations
    glUseProgram(shaderProgram);
    GLint cameraPosLoc = glGetUniformLocation(shaderProgram, "u_camera_pos");
    GLint cameraViewLoc = glGetUniformLocation(shaderProgram, "u_camera_view");
    GLint timeLoc = glGetUniformLocation(shaderProgram, "u_time");
    GLint aspectLoc = glGetUniformLocation(shaderProgram, "u_aspect_ratio");
    glUniform1f(aspectLoc, (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT);

    // --- Main Loop ---
    bool quit = false;
    SDL_Event e;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quit = true;
        }
        
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        
        // Simple camera animation
        glm::vec3 cam_pos = glm::vec3(cos(time * 0.3) * 4.0, 1.5, sin(time * 0.3) * 4.0);
        glm::mat4 view_matrix = glm::lookAt(cam_pos, glm::vec3(0,0,0), glm::vec3(0,1,0));

        glUniform1f(timeLoc, time);
        glUniform3fv(cameraPosLoc, 1, glm::value_ptr(cam_pos));
        glUniformMatrix4fv(cameraViewLoc, 1, GL_FALSE, glm::value_ptr(view_matrix));

        glBindVertexArray(VAO_quad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    glDeleteVertexArrays(1, &VAO_quad); glDeleteBuffers(1, &VBO_quad);
    glDeleteProgram(shaderProgram); glDeleteBuffers(1, &object_ssbo);
    glDeleteBuffers(1, &material_ssbo);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();

    return 0;
}
