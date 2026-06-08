#define _CRT_SECURE_NO_WARNINGS
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <linmath.h>

#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cfloat>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =================== STRUKTURY ===================
struct Vertex { float x, y, z; };

struct Mesh {
    std::vector<Vertex> v;
    std::vector<unsigned> i;
    GLuint vao, vbo, ebo;
};

struct Transform {
    vec3 pos;
    vec3 rot; // Dodane obroty
    float scl;
};

struct AABB {
    vec3 minL, maxL; // Lokalne (raz obliczone)
    vec3 minW, maxW; // Œwiatowe (dynamiczne)
};

struct Object {
    Mesh* mesh;
    Transform t;
    mat4x4 world;
    AABB box;
};

// =================== KAMERA FPP ===================
vec3 camPos = { 0.0f, 2.0f, 6.0f };
vec3 camFront = { 0.0f, 0.0f, -1.0f };
vec3 camUp = { 0.0f, 1.0f, 0.0f };
float yaw = -90.0f, pitch = 0.0f;
float lastX = 640, lastY = 360;
bool firstMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    xoffset *= 0.1f; yoffset *= 0.1f;
    yaw += xoffset; pitch += yoffset;
    if (pitch > 89.0f) pitch = 89.0f; if (pitch < -89.0f) pitch = -89.0f;
    camFront[0] = cosf(yaw * M_PI / 180.0f) * cosf(pitch * M_PI / 180.0f);
    camFront[1] = sinf(pitch * M_PI / 180.0f);
    camFront[2] = sinf(yaw * M_PI / 180.0f) * cosf(pitch * M_PI / 180.0f);
}

// =================== OBJ LOADER ===================
void loadOBJ(const char* path, Mesh& m) {
    std::ifstream file(path);
    std::string line;
    if (!file.is_open()) { std::cout << "Brak pliku OBJ! Uzywam kostki.\n"; return; }
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string type; ss >> type;
        if (type == "v") { Vertex v; ss >> v.x >> v.y >> v.z; m.v.push_back(v); }
        else if (type == "f") {
            unsigned int v1, v2, v3;
            char dummy;
            for (int j = 0; j < 3; j++) {
                std::string segment; ss >> segment;
                size_t slash = segment.find('/');
                m.i.push_back(std::stoi(segment.substr(0, slash)) - 1);
            }
        }
    }
}

void setupMesh(Mesh& m) {
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, m.v.size() * sizeof(Vertex), m.v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * sizeof(unsigned), m.i.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
}

// =================== LOGIKA AABB ===================
void computeLocalAABB(Object& o) {
    o.box.minL[0] = o.box.minL[1] = o.box.minL[2] = FLT_MAX;
    o.box.maxL[0] = o.box.maxL[1] = o.box.maxL[2] = -FLT_MAX;
    for (auto& v : o.mesh->v) {
        for (int j = 0; j < 3; j++) {
            float val = (j == 0) ? v.x : (j == 1 ? v.y : v.z);
            if (val < o.box.minL[j]) o.box.minL[j] = val;
            if (val > o.box.maxL[j]) o.box.maxL[j] = val;
        }
    }
}



void updateWorldTransform(Object& o) {
    mat4x4 M, T, R, S;
    mat4x4_identity(T); mat4x4_translate(T, o.t.pos[0], o.t.pos[1], o.t.pos[2]);
    mat4x4_identity(R); mat4x4_rotate_Y(R, R, o.t.rot[1]);
    mat4x4_identity(S); S[0][0] = S[1][1] = S[2][2] = o.t.scl;
    mat4x4_mul(M, T, R);
    mat4x4_mul(o.world, M, S);

    // Dynamiczne AABB na podstawie 8 wierzcho³ków OBB
    o.box.minW[0] = o.box.minW[1] = o.box.minW[2] = FLT_MAX;
    o.box.maxW[0] = o.box.maxW[1] = o.box.maxW[2] = -FLT_MAX;

    for (int i = 0; i < 8; i++) {
        vec4 localCorner = {
            (i & 1) ? o.box.maxL[0] : o.box.minL[0], //x
            (i & 2) ? o.box.maxL[1] : o.box.minL[1],//y
            (i & 4) ? o.box.maxL[2] : o.box.minL[2], 1.0f
        };
        vec4 worldCorner;
        mat4x4_mul_vec4(worldCorner, o.world, localCorner);
        for (int j = 0; j < 3; j++) {
            if (worldCorner[j] < o.box.minW[j]) o.box.minW[j] = worldCorner[j];
            if (worldCorner[j] > o.box.maxW[j]) o.box.maxW[j] = worldCorner[j];
        }
    }
}

bool aabbIntersect(const AABB& a, const AABB& b) {
    return (a.minW[0] <= b.maxW[0] && a.maxW[0] >= b.minW[0]) &&
        (a.minW[1] <= b.maxW[1] && a.maxW[1] >= b.minW[1]) &&
        (a.minW[2] <= b.maxW[2] && a.maxW[2] >= b.minW[2]);
}

int main() {
    glfwInit();
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Scene Graph 3D", 0, 0);
    glfwMakeContextCurrent(win);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(win, mouse_callback);
    gladLoadGL(glfwGetProcAddress);
    glEnable(GL_DEPTH_TEST);

    const char* vs = "#version 330 core\nlayout(location=0) in vec3 p; uniform mat4 MVP; void main(){gl_Position=MVP*vec4(p,1.0);}\n";
    const char* fs = "#version 330 core\nout vec4 f; uniform vec3 col; void main(){f=vec4(col,1.0);}\n";
    GLuint prog = glCreateProgram();
    auto compile = [](GLenum t, const char* s) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, 0); glCompileShader(sh); return sh; };
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);

    Mesh mesh;
    loadOBJ("model.obj", mesh); // Upewnij siê, ¿e plik istnieje lub dodaj makeCube
    if (mesh.v.empty()) { // fallback do kostki
        mesh.v = { {-0.5,-0.5,-0.5},{0.5,-0.5,-0.5},{0.5,0.5,-0.5},{-0.5,0.5,-0.5},{-0.5,-0.5,0.5},{0.5,-0.5,0.5},{0.5,0.5,0.5},{-0.5,0.5,0.5} };
        mesh.i = { 0,1,2,2,3,0, 4,5,6,6,7,4, 0,1,5,5,4,0, 2,3,7,7,6,2, 1,2,6,6,5,1, 3,0,4,4,7,3 };
    }
    setupMesh(mesh);

    std::vector<Object> objects(8);
    for (int i = 0; i < 8; i++) {
        objects[i].mesh = &mesh;
        objects[i].t = { { (float)i * 2 - 7, 0, 0 }, {0,0,0}, 1.0f };
        computeLocalAABB(objects[i]);
        updateWorldTransform(objects[i]);
    }

    int selected = 0;
    bool wire = false;

    while (!glfwWindowShouldClose(win)) {
        // --- INPUT ---
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        for (int i = 0; i < 8; i++) if (glfwGetKey(win, GLFW_KEY_1 + i) == GLFW_PRESS) selected = i;
        if (glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS) wire = true; else wire = false;

        // Kamera FPP
        float camSpeed = 0.005f;
        if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) { camPos[0] += camFront[0] * camSpeed; camPos[2] += camFront[2] * camSpeed; }
        if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) { camPos[0] -= camFront[0] * camSpeed; camPos[2] -= camFront[2] * camSpeed; }

        // Edycja obiektu
        Object& sel = objects[selected];
        Transform prevT = sel.t;

        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) sel.t.pos[2] -= 0.01f;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) sel.t.pos[2] += 0.01f;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) sel.t.pos[0] -= 0.01f;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) sel.t.pos[0] += 0.01f;
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) sel.t.rot[1] += 0.002f;
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) sel.t.rot[1] -= 0.002f;
        if (glfwGetKey(win, GLFW_KEY_O) == GLFW_PRESS) sel.t.scl += 0.002f;
        if (glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS) sel.t.scl -= 0.002f;

        updateWorldTransform(sel);

        // --- KOLIZJE ---
        for (int i = 0; i < 8; i++) {
            if (i == selected) continue;
            if (aabbIntersect(sel.box, objects[i].box)) {
                sel.t = prevT; // Cofniecie
                updateWorldTransform(sel);
                break;
            }
        }

        // --- RENDER ---
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);
        mat4x4 P, V, VP;
        mat4x4_perspective(P, 60.0f * M_PI / 180.0f, 1280.f / 720.0f, 0.1f, 100.0f);
        vec3 target; vec3_add(target, camPos, camFront);
        mat4x4_look_at(V, camPos, target, camUp);
        mat4x4_mul(VP, P, V);

        for (int i = 0; i < 8; i++) {
            mat4x4 MVP;
            mat4x4_mul(MVP, VP, objects[i].world);
            glUniformMatrix4fv(glGetUniformLocation(prog, "MVP"), 1, 0, (float*)MVP);
            glUniform3f(glGetUniformLocation(prog, "col"), i == selected ? 1 : 0.5f, 0.5f, 1);

            glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
            glBindVertexArray(objects[i].mesh->vao);
            glDrawElements(GL_TRIANGLES, objects[i].mesh->i.size(), GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}