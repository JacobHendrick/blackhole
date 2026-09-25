#pragma once
#include <array>
#include <stdexcept>
#include <string>

struct RenderOptions {
    bool hidden = false;
    int width = 1920, height = 1080, samples = 8;
    std::string output;
};

class Engine {
public:
    GLFWwindow* window = nullptr;
    int WIDTH = 0, HEIGHT = 0;
    static constexpr int MAX_SAMPLES = 1024;

    explicit Engine(const RenderOptions& options) : hidden(options.hidden) {
        glfwSetErrorCallback([](int, const char* message) { std::cerr << "GLFW: " << message << '\n'; });
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
        GLFWmonitor* monitor = hidden ? nullptr : glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (!mode) throw std::runtime_error("Could not read the monitor resolution");
            WIDTH = mode->width;
            HEIGHT = mode->height;
        } else {
            WIDTH = options.width;
            HEIGHT = options.height;
        }
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole | Ultra", monitor, nullptr);
        if (!window) throw std::runtime_error("Could not create the OpenGL 4.3 window");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(hidden ? 0 : 1);
        glfwGetFramebufferSize(window, &WIDTH, &HEIGHT);
        glewExperimental = GL_TRUE;
        GLenum result = glewInit();
        if (result != GLEW_OK)
            throw std::runtime_error(std::string("GLEW: ") + reinterpret_cast<const char*>(glewGetErrorString(result)));
        while (glGetError() != GL_NO_ERROR) {}
        std::cout << "OpenGL " << glGetString(GL_VERSION) << " | " << glGetString(GL_RENDERER) << '\n';
        std::cout << "Ultra: " << WIDTH << 'x' << HEIGHT << ", progressive sampling up to " << MAX_SAMPLES << " spp\n";

        rayProgram = link({compile("BlackBall.comp", GL_COMPUTE_SHADER)});
        bloomProgram = link({compile("Bloom.comp", GL_COMPUTE_SHADER)});
        screenProgram = link({compile("Screen.vert", GL_VERTEX_SHADER), compile("Screen.frag", GL_FRAGMENT_SHADER)});
        glGenVertexArrays(1, &vao);
        glGenFramebuffers(1, &clearFBO);
        previewWidth = std::min(640, WIDTH);
        previewHeight = std::max(1, int(std::round(double(previewWidth) * HEIGHT / WIDTH)));
        bloomWidth = (WIDTH + 3) / 4;
        bloomHeight = (HEIGHT + 3) / 4;
        nativeImage = texture2D(WIDTH, HEIGHT, GL_RGBA32F);
        previewImage = texture2D(previewWidth, previewHeight, GL_RGBA32F);
        bloomA = texture2D(bloomWidth, bloomHeight, GL_RGBA16F);
        bloomB = texture2D(bloomWidth, bloomHeight, GL_RGBA16F);
        createThermalLookup();
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Could not allocate the HDR render targets");
        clearNative();
        if (!hidden) glfwFocusWindow(window);
    }

    ~Engine() {
        if (!window) return;
        glfwMakeContextCurrent(window);
        GLuint textures[] = {nativeImage, previewImage, bloomA, bloomB, thermalLookup};
        glDeleteTextures(5, textures);
        glDeleteProgram(rayProgram);
        glDeleteProgram(bloomProgram);
        glDeleteProgram(screenProgram);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &clearFBO);
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    int samples() const { return completedSamples; }

    void render(const Camera& cam, double time, bool live, unsigned revision, float exposure) {
        bool changed = !hasScene || cam.radius != lastRadius || cam.azimuth != lastAzimuth ||
                       cam.elevation != lastElevation || live != lastLive || revision != lastRevision;
        bool navigating = cam.moving || glfwGetTime() - cam.lastChange < 0.25;
        if (changed) {
            clearNative();
            completedSamples = 0;
            nextRow = 0;
            lastRadius = cam.radius;
            lastAzimuth = cam.azimuth;
            lastElevation = cam.elevation;
            lastLive = live;
            lastRevision = revision;
            hasScene = true;
        }
        if (changed || (navigating && live))
            dispatch(cam, time, previewImage, previewWidth, previewHeight, 0, previewHeight, 0, true);

        if (!navigating) {
            if (live) {
                dispatch(cam, time, nativeImage, WIDTH, HEIGHT, 0, HEIGHT, 0, false);
                completedSamples = 1;
                nextRow = 0;
            } else if (completedSamples < MAX_SAMPLES) {
                int rows = std::min(64, HEIGHT - nextRow);
                dispatch(cam, time, nativeImage, WIDTH, HEIGHT, nextRow, rows, completedSamples, false);
                nextRow += rows;
                if (nextRow == HEIGHT) { nextRow = 0; ++completedSamples; }
            }
        }
        present(exposure, live);
    }

    void renderToFile(const Camera& cam, double time, const RenderOptions& options, float exposure) {
        clearNative();
        dispatch(cam, time, previewImage, previewWidth, previewHeight, 0, previewHeight, 0, true);
        double start = glfwGetTime();
        for (int sample = 0; sample < options.samples; ++sample) {
            for (int row = 0; row < HEIGHT; row += 64)
                dispatch(cam, time, nativeImage, WIDTH, HEIGHT, row, std::min(64, HEIGHT - row), sample, false);
            glFinish();
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL error during native rendering");
            if (sample == 0 || (sample + 1) % std::max(1, options.samples / 8) == 0 || sample + 1 == options.samples)
                std::cout << "Completed sample " << sample + 1 << '/' << options.samples << std::endl;
        }
        std::vector<float> hdr(size_t(WIDTH) * HEIGHT * 4);
        glBindTexture(GL_TEXTURE_2D, nativeImage);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, hdr.data());
        size_t invalid = 0, uncovered = 0;
        double meanLuminance = 0.0;
        for (size_t i = 0; i < hdr.size(); i += 4) {
            if (!std::isfinite(hdr[i]) || !std::isfinite(hdr[i + 1]) || !std::isfinite(hdr[i + 2])) ++invalid;
            if (hdr[i + 3] != float(options.samples)) ++uncovered;
            meanLuminance += 0.2126 * hdr[i] + 0.7152 * hdr[i + 1] + 0.0722 * hdr[i + 2];
        }
        std::cout << "GPU validation: " << invalid << " nonfinite pixels, " << uncovered
                  << " incomplete pixels; mean luminance " << meanLuminance / (WIDTH * HEIGHT)
                  << "; " << glfwGetTime() - start << " seconds\n";
        if (invalid || uncovered || meanLuminance <= 0.0) throw std::runtime_error("Invalid rendered image");
        present(exposure, false);
        glFinish();
        std::vector<unsigned char> pixels(size_t(WIDTH) * HEIGHT * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream out(options.output, std::ios::binary);
        if (!out) throw std::runtime_error("Could not create " + options.output);
        out << "P6\n" << WIDTH << ' ' << HEIGHT << "\n255\n";
        for (int row = HEIGHT - 1; row >= 0; --row)
            out.write(reinterpret_cast<const char*>(pixels.data() + size_t(row) * WIDTH * 3), WIDTH * 3);
        if (!out) throw std::runtime_error("Could not finish writing the rendered image");
        std::cout << "Saved " << options.output << std::endl;
    }

private:
    bool hidden = false, hasScene = false, lastLive = false;
    float lastRadius = 0, lastAzimuth = 0, lastElevation = 0;
    unsigned lastRevision = 0;
    int previewWidth = 0, previewHeight = 0, bloomWidth = 0, bloomHeight = 0;
    int nextRow = 0, completedSamples = 0;
    GLuint rayProgram = 0, bloomProgram = 0, screenProgram = 0, vao = 0, clearFBO = 0;
    GLuint nativeImage = 0, previewImage = 0, bloomA = 0, bloomB = 0, thermalLookup = 0;

    static GLuint compile(const char* path, GLenum type) {
        std::ifstream file(path);
        if (!file) throw std::runtime_error(std::string("Could not open shader: ") + path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        const char* text = source.c_str();
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string message(std::max(length, 1), '\0');
            glGetShaderInfoLog(shader, length, nullptr, message.data());
            glDeleteShader(shader);
            throw std::runtime_error(std::string(path) + ":\n" + message);
        }
        return shader;
    }
    static GLuint link(std::initializer_list<GLuint> shaders) {
        GLuint program = glCreateProgram();
        for (GLuint shader : shaders) glAttachShader(program, shader);
        glLinkProgram(program);
        for (GLuint shader : shaders) glDeleteShader(shader);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string message(std::max(length, 1), '\0');
            glGetProgramInfoLog(program, length, nullptr, message.data());
            glDeleteProgram(program);
            throw std::runtime_error("Shader link error:\n" + message);
        }
        return program;
    }
    static GLuint texture2D(int width, int height, GLint format) {
        GLuint image = 0;
        glGenTextures(1, &image);
        glBindTexture(GL_TEXTURE_2D, image);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return image;
    }
    void clearNative() {
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, clearFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nativeImage, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Native HDR framebuffer is incomplete");
        const GLfloat empty[4] = {0, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, empty);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    static float halton(unsigned index, unsigned base) {
        float value = 0, fraction = 1;
        while (index) { fraction /= base; value += fraction * (index % base); index /= base; }
        return value;
    }
    void dispatch(const Camera& cam, double time, GLuint image, int width, int height,
                  int row, int rows, int sample, bool preview) {
        glUseProgram(rayProgram);
        auto location = [&](const char* name) { return glGetUniformLocation(rayProgram, name); };
        glm::vec3 forward = glm::normalize(cam.target - cam.position());
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::cross(right, forward);
        glm::vec3 position = cam.position() / float(SagA.r_s);
        glUniform3fv(location("uCameraPosition"), 1, glm::value_ptr(position));
        glUniform3fv(location("uCameraForward"), 1, glm::value_ptr(forward));
        glUniform3fv(location("uCameraRight"), 1, glm::value_ptr(right));
        glUniform3fv(location("uCameraUp"), 1, glm::value_ptr(up));
        glUniform1f(location("uAspect"), float(WIDTH) / HEIGHT);
        glUniform1f(location("uTanHalfFov"), std::tan(glm::radians(25.0f)));
        glUniform1f(location("uTime"), float(time / (SagA.r_s / c)));
        glUniform1i(location("uPreview"), preview);
        glUniform1i(location("uSampleIndex"), sample);
        glUniform2f(location("uJitter"), halton(sample + 1, 2), halton(sample + 1, 3));
        glUniform2i(location("uOffset"), 0, row);
        glUniform2i(location("uExtent"), width, rows);
        std::array<glm::vec4, 16> positions{}, colors{};
        int count = 0;
        for (const auto& object : objects) {
            if (object.posRadius.w <= 0 || object.color.r + object.color.g + object.color.b <= 0) continue;
            positions[count] = object.posRadius / float(SagA.r_s);
            colors[count] = object.color;
            if (++count == 16) break;
        }
        glUniform1i(location("uObjectCount"), count);
        if (count) {
            glUniform4fv(location("uObjects"), count, glm::value_ptr(positions[0]));
            glUniform4fv(location("uObjectColors"), count, glm::value_ptr(colors[0]));
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_1D, thermalLookup);
        glBindImageTexture(0, image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glDispatchCompute((width + 15) / 16, (rows + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
        (void)height;
    }
    void bindScene() {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, nativeImage);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, previewImage);
    }
    void present(float exposure, bool gravity) {
        bindScene();
        glUseProgram(bloomProgram);
        for (int pass = 0; pass < 3; ++pass) {
            glUniform1i(glGetUniformLocation(bloomProgram, "uPass"), pass);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, pass == 2 ? bloomB : bloomA);
            glBindImageTexture(0, pass == 1 ? bloomB : bloomA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glDispatchCompute((bloomWidth + 15) / 16, (bloomHeight + 7) / 8, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, WIDTH, HEIGHT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glUseProgram(screenProgram);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, bloomA);
        glUniform1f(glGetUniformLocation(screenProgram, "uExposure"), exposure);
        glUniform1i(glGetUniformLocation(screenProgram, "uGravity"), gravity);
        glUniform1i(glGetUniformLocation(screenProgram, "uIndicator"), !hidden);
        glUniform2f(glGetUniformLocation(screenProgram, "uScreenSize"), float(WIDTH), float(HEIGHT));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void createThermalLookup() {
        auto gaussian = [](double wavelength, double center, double left, double right) {
            double t = (wavelength - center) * (wavelength < center ? left : right);
            return std::exp(-0.5 * t * t);
        };
        auto spectrum = [&](double temperature) {
            glm::dvec3 xyz(0.0);
            for (double nm = 380; nm <= 780; nm += 5) {
                double wavelength = nm / 1000.0;
                double planck = 1.0 / (std::pow(wavelength, 5) * std::expm1(14387.76877 / (wavelength * temperature)));
                double x = 0.362 * gaussian(nm, 442.0, 0.0624, 0.0374)
                         + 1.056 * gaussian(nm, 599.8, 0.0264, 0.0323)
                         - 0.065 * gaussian(nm, 501.1, 0.0490, 0.0382);
                double y = 0.821 * gaussian(nm, 568.8, 0.0213, 0.0247)
                         + 0.286 * gaussian(nm, 530.9, 0.0613, 0.0322);
                double z = 1.217 * gaussian(nm, 437.0, 0.0845, 0.0278)
                         + 0.681 * gaussian(nm, 459.0, 0.0385, 0.0725);
                xyz += glm::dvec3(x, y, z) * planck * 5.0;
            }
            return xyz;
        };
        double normalization = spectrum(10000.0).y;
        std::vector<glm::vec4> table(1024);
        for (size_t i = 0; i < table.size(); ++i) {
            double temperature = 800.0 * std::pow(100.0, double(i) / (table.size() - 1));
            glm::dvec3 xyz = spectrum(temperature) / normalization;
            glm::dvec3 rgb(3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
                         -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
                          0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z);
            table[i] = glm::vec4(glm::max(glm::vec3(rgb), glm::vec3(0.0)), 1.0);
        }
        glGenTextures(1, &thermalLookup);
        glBindTexture(GL_TEXTURE_1D, thermalLookup);
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, int(table.size()), 0, GL_RGBA, GL_FLOAT, table.data());
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
};
