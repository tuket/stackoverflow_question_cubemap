#include <stdio.h>
#include <assert.h>
#include <glad/glad.h>
//#include <GL/glu.h>
#include <GLFW/glfw3.h>
#include <tl/defer.hpp>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <stbi.h>

typedef int8_t i8;
typedef uint8_t u8;
typedef int32_t i32;
typedef uint32_t u32;
constexpr float PI = glm::pi<float>();

static GLFWwindow* window;

static const char s_vertShadSrc[] =
R"GLSL(
#version 330 core

in layout(location = 0) vec3 a_pos;

out vec3 v_modelPos;

uniform mat4 u_modelViewProjMat;


void main()
{
    v_modelPos = a_pos;
    gl_Position = u_modelViewProjMat * vec4(a_pos, 1);
}
)GLSL";

static const char s_fragShadSrc[] =
R"GLSL(
#version 330 core

layout(location = 0) out vec4 o_color;

in vec3 v_modelPos;

uniform samplerCube u_cubemap;

void main()
{
    o_color = texture(u_cubemap, normalize(v_modelPos));
}
)GLSL";

static const float cubeVerts[6*6*(3+2)] = {
    // x, y, z,
    // LEFT
    -1, -1, -1,
    -1, -1, +1,
    -1, +1, +1,
    -1, -1, -1,
    -1, +1, +1,
    -1, +1, -1,
    // RIGHT
    +1, -1, +1,
    +1, -1, -1,
    +1, +1, -1,
    +1, -1, +1,
    +1, +1, -1,
    +1, +1, +1,
    // BOTTOM
    -1, -1, -1,
    +1, -1, -1,
    +1, -1, +1,
    -1, -1, -1,
    +1, -1, +1,
    -1, -1, +1,
    // TOP
    -1, +1, +1,
    +1, +1, +1,
    +1, +1, -1,
    -1, +1, +1,
    +1, +1, -1,
    -1, +1, -1,
    // FRONT
    -1, -1, +1,
    +1, -1, +1,
    +1, +1, +1,
    -1, -1, +1,
    +1, +1, +1,
    -1, +1, +1,
    // BACK
    +1, -1, -1,
    -1, -1, -1,
    -1, +1, -1,
    +1, -1, -1,
    -1, +1, -1,
    +1, +1, -1,
};

struct UnifLocs {
    i32 modelViewProj;
    i32 cubemap;
} unifLocs;

static struct OrbitCameraInfo {
    float heading, pitch, distance;
} s_orbitCam;

static u32 s_environmentTextureUnit = 1;
static u32 s_cubeTextureUnit = 0;
static bool s_seamlessCubemapEnabled = false;
static bool s_seamlessCubemapEnabledChanged = false;
void onKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_1 && action == GLFW_PRESS)
        s_environmentTextureUnit ^= 1;
    if (key == GLFW_KEY_2 && action == GLFW_PRESS)
        s_cubeTextureUnit ^= 1;
    if(key == GLFW_KEY_S && action == GLFW_PRESS) {
        s_seamlessCubemapEnabled = s_seamlessCubemapEnabled != true;
        s_seamlessCubemapEnabledChanged = true;
        (s_seamlessCubemapEnabled ? glEnable : glDisable)(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }
}

glm::mat4 calcOrbitCameraMtx(float heading, float pitch, float distance)
{
    auto mtx = glm::eulerAngleXY(pitch, heading);
    mtx[3][2] = -distance;
    return mtx;
}

constexpr int s_bufferSize = 4 * 1024;
static char s_buffer[s_bufferSize];
char* getShaderCompileErrors(u32 shader)
{
    i32 ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if(!ok) {
        glGetShaderInfoLog(shader, s_bufferSize, nullptr, s_buffer);
        return s_buffer;
    }
    return nullptr;
}

char* getShaderLinkErrors(u32 prog)
{
    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(prog, s_bufferSize, nullptr, s_buffer);
        return s_buffer;
    }
    return nullptr;
}

static void glErrorCallback(const char *name, void *funcptr, int len_args, ...) {
    GLenum error_code;
    error_code = glad_glGetError();
    if (error_code != GL_NO_ERROR) {
        //fprintf(stderr, "ERROR %s in %s\n", gluErrorString(error_code), name);
        assert(false);
    }
}

GLFWwindow* simpleInitGlfwGL()
{
    glfwSetErrorCallback(+[](int error, const char* description) {
        fprintf(stderr, "Glfw Error %d: %s\n", error, description);
    });
    if (!glfwInit())
        return nullptr;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "test", nullptr, nullptr);
    if (window == nullptr)
        return nullptr;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Enable vsync

    if (gladLoadGL() == 0) {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return nullptr;
    }
    glad_set_post_callback(glErrorCallback);
    return window;
}


static bool mousePressed;
static double prevMouseX, prevMouseY;
static OrbitCameraInfo* s_orbitCamInfo = nullptr;
void addOrbitCameraBaviour(GLFWwindow* window, OrbitCameraInfo& orbitCamInfo)
{
    s_orbitCamInfo = &orbitCamInfo;
    mousePressed = false;

    auto onMouseClick = +[](GLFWwindow* window, int button, int action, int mods)
    {
        if(button == GLFW_MOUSE_BUTTON_1)
            mousePressed = action == GLFW_PRESS;
    };

    auto onMouseMove = [](GLFWwindow* window, double x, double y)
    {
        assert(s_orbitCamInfo);
        auto& orbitCam = *s_orbitCamInfo;
        if(mousePressed) {
            const float dx = (float)x - prevMouseX;
            const float dy = (float)y - prevMouseY;
            constexpr float speed = PI;
            int windowW, windowH;
            glfwGetWindowSize(window, &windowW, &windowH);
            orbitCam.heading += speed * dx / windowW;
            while(orbitCam.heading < 0)
                orbitCam.heading += 2*PI;
            while(orbitCam.heading > 2*PI)
                orbitCam.heading -= 2*PI;
            orbitCam.pitch += speed * dy / windowH;
            orbitCam.pitch = glm::clamp(orbitCam.pitch, -0.45f*PI, +0.45f*PI);
        }
        prevMouseX = (float)x;
        prevMouseY = (float)y;
    };

    auto onMouseWheel = [](GLFWwindow* window, double dx, double dy)
    {
        assert(s_orbitCamInfo);
        auto& orbitCam = *s_orbitCamInfo;
        constexpr float speed = 1.04f;
        orbitCam.distance *= pow(speed, (float)dy);
        orbitCam.distance = glm::max(orbitCam.distance, 0.01f);
    };

    glfwSetMouseButtonCallback(window, onMouseClick);
    glfwSetCursorPosCallback(window, onMouseMove);
    glfwSetScrollCallback(window, onMouseWheel);
}

u8 getNumChannels(u32 format)
{
	switch (format)
	{
	case GL_RED:
		return 1;
	case GL_RG:
		return 2;
	case GL_RGB:
	case GL_BGR:
		return 3;
	case GL_RGBA:
	case GL_BGRA:
		return 4;
		// There are many more but I'm lazy
	}
	assert(false);
	return 0;
}

u8 getGetPixelSize(u32 format, u32 type)
{
	const u32 nc = getNumChannels(format);
	switch (type)
	{
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:
		return nc;
	case GL_UNSIGNED_SHORT:
	case GL_SHORT:
	case GL_HALF_FLOAT:
		return 2 * nc;
	case GL_UNSIGNED_INT:
	case GL_INT:
	case GL_FLOAT:
		return 4 * nc;
		// there are many more but I'm lazy
	}
	assert(false);
	return 1;
}

void uploadCubemapTexture(u32 mipLevel, u32 w, u32 h, u32 internalFormat, u32 dataFormat, u32 dataType, u8* data)
{
    const u8 ps = getGetPixelSize(dataFormat, dataType);
    const u32 side = w / 4;
    assert(3*side == h);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
    defer(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    auto upload = [&](GLenum face, u32 offset) {
        glTexImage2D(face, mipLevel, internalFormat, side, side, 0, dataFormat, dataType, data + offset);
    };
    upload(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, ps * w*side);
    upload(GL_TEXTURE_CUBE_MAP_POSITIVE_X, ps * (w*side + 2*side));
    upload(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, ps * (w*2*side + side));
    upload(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, ps * side);
    upload(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, ps * (w*side + 3*side));
    upload(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, ps * (w*side + side));
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

int main()
{
    window = simpleInitGlfwGL();
    if(!window)
        return 1;

    glfwSetKeyCallback(window, onKey);

    s_orbitCam.heading = s_orbitCam.pitch = 0;
    s_orbitCam.distance = 4;
    addOrbitCameraBaviour(window, s_orbitCam);

    u32 cubemapTextures[2];
    glGenTextures(2, cubemapTextures);
    defer(glDeleteTextures(2, cubemapTextures));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTextures[0]);
    {
        int w, h, nc;
        u8* data = stbi_load("cubemap_test_rgb.png", &w, &h, &nc, 3);
        if(data) {
            uploadCubemapTexture(0, w, h, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            printf("error loading img\n");
            return 1;
        }
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTextures[1]);
    {
        int w, h, nc;
        u8* data = stbi_load("test.png", &w, &h, &nc, 3);
        if(data) {
            uploadCubemapTexture(0, w, h, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            printf("error loading img\n");
            return 1;
        }
    }

    u32 prog = glCreateProgram();
    defer(glDeleteProgram(prog));
    {
        u32 vertShad = glCreateShader(GL_VERTEX_SHADER);
        defer(glDeleteShader(vertShad));
        const char* vertShadSrc = s_vertShadSrc;
        glShaderSource(vertShad, 1, &vertShadSrc, nullptr);
        glCompileShader(vertShad);
        if(const char* errorMsg = getShaderCompileErrors(vertShad)) {
            printf("Error compiling vertex shader:\n%s\n", errorMsg);
            return 0;
        }

        u32 fragShad = glCreateShader(GL_FRAGMENT_SHADER);
        defer(glDeleteShader(fragShad));
        const char* fragShadSrc = s_fragShadSrc;
        glShaderSource(fragShad, 1, &fragShadSrc, nullptr);
        glCompileShader(fragShad);
        if(const char* errorMsg = getShaderCompileErrors(fragShad)) {
            printf("Error compiling fragment shader:\n%s\n", errorMsg);
            return 0;
        }

        glAttachShader(prog, vertShad);
        glAttachShader(prog, fragShad);
        glLinkProgram(prog);
        if(const char* errorMsg = getShaderLinkErrors(prog)) {
            printf("Error linking:\n%s\n", errorMsg);
            return 0;
        }
    }

    glUseProgram(prog);
    unifLocs.modelViewProj = glGetUniformLocation(prog, "u_modelViewProjMat");
    unifLocs.cubemap = glGetUniformLocation(prog, "u_cubemap");
    glUniform1i(unifLocs.cubemap, 0);

    u32 vao, vbo;
    glGenVertexArrays(1, &vao);
    defer(glDeleteVertexArrays(1, &vao));
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    defer(glDeleteVertexArrays(1, &vbo));
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // draw scene
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glScissor(0, 0, w, h);

        glClearColor(0, 0.2f, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 modelMtx(1);
        const glm::mat4 projMtx = glm::perspective(glm::radians(45.f), float(w)/h, 0.1f, 1000.f);
        const glm::mat4 viewMtx = calcOrbitCameraMtx(s_orbitCam.heading, s_orbitCam.pitch, s_orbitCam.distance);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        { // draw environment
            glm::mat4 viewMtxWithoutTranslation = viewMtx;
            viewMtxWithoutTranslation[3][0] = viewMtxWithoutTranslation[3][1] = viewMtxWithoutTranslation[3][2] = 0;
            const glm::mat4 modelViewProj = projMtx * viewMtxWithoutTranslation * modelMtx;
            glUniformMatrix4fv(unifLocs.modelViewProj, 1, GL_FALSE, &modelViewProj[0][0]);
            glUniform1i(unifLocs.cubemap, s_environmentTextureUnit);
            glDrawArrays(GL_TRIANGLES, 0, 6*6);
        }

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        { // draw cube
            const glm::mat4 modelViewProj = projMtx * viewMtx * modelMtx;
            glUniformMatrix4fv(unifLocs.modelViewProj, 1, GL_FALSE, &modelViewProj[0][0]);
            glUniform1i(unifLocs.cubemap, s_cubeTextureUnit);
            glDrawArrays(GL_TRIANGLES, 0, 6*6);
        }

        glfwSwapBuffers(window);
        glfwWaitEventsTimeout(0.01);
    }

    return true;
}
