#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;

double c = 299792458.0;
double G = 6.67430e-11;
struct Ray;
bool Gravity = false;
double SimulationTime = 0.0;
float Exposure = 2.0f;
unsigned SceneRevision = 0;
void resetScene();

void toggleGravity() {
    Gravity = !Gravity;
    ++SceneRevision;
    cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << endl;
}

struct Camera {
    vec3 target = vec3(0.0f, 0.0f, 0.0f);
    float radius = 2.8e11f;
    float minRadius = 1.5e10f, maxRadius = 1e12f;

    float azimuth = 0.0f;
    float elevation = 1.28f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;

    bool dragging = false;
    bool panning = false;
    bool moving = false;
    double lastX = 0.0, lastY = 0.0;
    double lastChange = 0.0;

    vec3 position() const {
        float clampedElevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        return vec3(
            radius * sin(clampedElevation) * cos(azimuth),
            radius * cos(clampedElevation),
            radius * sin(clampedElevation) * sin(azimuth)
        );
    }
    void update() {
        target = vec3(0.0f, 0.0f, 0.0f);
        if(dragging || panning) {
            moving = true;
        } else {
            moving = false;
        }
    }

    void processMouseMove(double x, double y) {
        float dx = float(x - lastX);
        float dy = float(y - lastY);

        if (dragging && panning) {
        }
        else if (dragging && !panning) {
            azimuth += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
            lastChange = glfwGetTime();
        }

        lastX = x;
        lastY = y;
        update();
      }
      void processMouseButton(int button, int action, int, GLFWwindow* win) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS) {
                dragging = true;
                panning = false;
                glfwGetCursorPos(win, &lastX, &lastY);
            } else if (action == GLFW_RELEASE) {
                dragging = false;
                panning = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            toggleGravity();
        }
        update();
      }
      void zoom(double steps) {
        radius = glm::clamp(static_cast<float>(radius * std::pow(0.85, steps)),
                            minRadius, maxRadius);
        lastChange = glfwGetTime();
        update();
      }
      void processScroll(double, double yoffset) {
        zoom(yoffset);
    }
      void processKey(int key, int, int action, int) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
                zoom(1.0);
            } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
                zoom(-1.0);
            }
        }
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (key == GLFW_KEY_LEFT_BRACKET) Exposure = std::max(0.05f, Exposure / 1.15f);
            if (key == GLFW_KEY_RIGHT_BRACKET) Exposure = std::min(20.0f, Exposure * 1.15f);
        }
        if (action == GLFW_PRESS && key == GLFW_KEY_R) resetScene();
        if (action == GLFW_PRESS && key == GLFW_KEY_G) {
            toggleGravity();
        }
      }
    };
Camera camera;

struct BlackHole {
    vec3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(vec3 pos, double m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
    bool Intercept(float px, float py, float pz) const {
        double dx = double(px) - double(position.x);
        double dy = double(py) - double(position.y);
        double dz = double(pz) - double(position.z);
        double dist2 = dx * dx + dy * dy + dz * dz;
        return dist2 < r_s * r_s;

    }
};
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36);
struct ObjectData {
    vec4 posRadius;
    vec4 color;
    double mass;
    dvec3 position;
    dvec3 velocity = dvec3(0.0);
    bool fixed;

    ObjectData(vec4 initialPosRadius, vec4 initialColor, double initialMass, bool isFixed = false)
        : posRadius(initialPosRadius), color(initialColor), mass(initialMass),
          position(initialPosRadius.x, initialPosRadius.y, initialPosRadius.z),
          fixed(isFixed) {}
};
vector<ObjectData> objects = {
    { vec4(-7e10f, 3e10f,  8e10f, 4e9f), vec4(1.0f,0.60f,0.30f,1), 1.98892e30 },
    { vec4(-7e10f, 3e10f, -8e10f, 4e9f), vec4(0.55f,0.72f,1.0f,1), 1.98892e30 },
    { vec4(0.0f, 0.0f, 0.0f, static_cast<float>(SagA.r_s)),
      vec4(0,0,0,1), SagA.mass, true },
};

const vector<ObjectData> initialObjects = objects;
void resetScene() {
    camera = Camera{};
    objects = initialObjects;
    SimulationTime = 0.0;
    Gravity = false;
    ++SceneRevision;
    cout << "[INFO] Scene reset" << endl;
}

void advanceGravity(double elapsedSeconds) {
    if (!Gravity) return;

    double remaining = std::min(elapsedSeconds, 1.0) * 200.0;
    SimulationTime += remaining;
    while (remaining > 0.0) {
        double step = std::min(remaining, 10.0);
        std::vector<dvec3> accelerations(objects.size(), dvec3(0.0));

        for (size_t i = 0; i < objects.size(); ++i) {
            if (objects[i].fixed) continue;
            for (size_t j = 0; j < objects.size(); ++j) {
                if (i == j || objects[j].mass == 0.0) continue;
                dvec3 delta = objects[j].position - objects[i].position;
                double distanceSquared = dot(delta, delta);
                if (distanceSquared == 0.0) continue;
                double distance = std::max(std::sqrt(distanceSquared), SagA.r_s);
                accelerations[i] += G * objects[j].mass * delta /
                                    (distance * distance * distance);
            }
        }

        for (size_t i = 0; i < objects.size(); ++i) {
            auto& obj = objects[i];
            if (obj.fixed) continue;
            obj.velocity += accelerations[i] * step;
            obj.position += obj.velocity * step;
            if (length(obj.position - dvec3(SagA.position)) <= SagA.r_s + obj.posRadius.w) {
                obj.fixed = true;
                obj.mass = 0.0;
                obj.posRadius.w = 0.0f;
            } else {
                obj.posRadius.x = static_cast<float>(obj.position.x);
                obj.posRadius.y = static_cast<float>(obj.position.y);
                obj.posRadius.z = static_cast<float>(obj.position.z);
            }
        }
        remaining -= step;
    }
}

#include "Renderer.hpp"

void setupCameraCallbacks(GLFWwindow* window) {
    glfwSetWindowUserPointer(window, &camera);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseButton(button, action, mods, win);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processMouseMove(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processScroll(xoffset, yoffset);
    });

    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        Camera* cam = (Camera*)glfwGetWindowUserPointer(win);
        cam->processKey(key, scancode, action, mods);
    });
}


int main(int argc, char** argv) {
    try {
        RenderOptions options;
        for (int i = 1; i < argc; ++i) {
            std::string argument = argv[i];
            if (argument == "--render" && i + 1 < argc) {
                options.output = argv[++i];
                options.hidden = true;
            } else if (argument == "--size" && i + 2 < argc) {
                options.width = std::stoi(argv[++i]);
                options.height = std::stoi(argv[++i]);
            } else if (argument == "--samples" && i + 1 < argc) {
                options.samples = std::stoi(argv[++i]);
            } else if (argument == "--help") {
                std::cout << "./blackhole\n"
                          << "./blackhole --render image.ppm --size 1920 1080 --samples 64\n"
                          << "Drag: orbit | +/- or wheel: zoom | G/right click: animate/pause\n"
                          << "[/]: exposure | R: reset | Escape: close\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown or incomplete option: " + argument);
            }
        }
        if (options.width < 64 || options.height < 64 || options.width > 8192 || options.height > 8192 ||
            options.samples < 1 || options.samples > 4096)
            throw std::runtime_error("Size must be 64..8192 pixels and samples must be 1..4096");
        Engine engine(options);
        if (options.hidden) {
            engine.renderToFile(camera, SimulationTime, options, Exposure);
            return 0;
        }
        setupCameraCallbacks(engine.window);
        double lastTime = glfwGetTime();
        while (!glfwWindowShouldClose(engine.window)) {
            glfwPollEvents();
            if (glfwWindowShouldClose(engine.window)) break;
            double now = glfwGetTime();
            advanceGravity(now - lastTime);
            lastTime = now;
            engine.render(camera, SimulationTime, Gravity, SceneRevision, Exposure);
            glfwSwapBuffers(engine.window);
            if (!Gravity && engine.samples() >= Engine::MAX_SAMPLES)
                glfwWaitEventsTimeout(0.05);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
