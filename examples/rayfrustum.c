#include "../include/rayfrustum.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rcamera.h"
#include <stdio.h>
#include <stdlib.h>

#define RAYGIZMO_IMPLEMENTATION
#include "raygizmo.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define print_vec(v) (printf("%f, %f, %f\n", v.x, v.y, v.z))

typedef struct Frustum {
    // near_left_bot, near_left_top, near_right_top, near_right_bot
    // far_left_bot, far_left_top, far_right_top, far_right_bot
    Vector3 corners[8];
} Frustum;

typedef struct Triangle {
    Vector3 v1;
    Vector3 v2;
    Vector3 v3;
} Triangle;

static Color CLEAR_COLOR = SKYBLUE;

static Camera3D CAMERA_0;
static Camera3D CAMERA_1;
static Model CAMERA_MODEL;

static Frustum get_frustum(Camera3D camera, float aspect, float near, float far);
static Vector3 get_triangle_center(Triangle triangle);
static void update_free_orbit_camera(Camera3D *camera);
static void draw_camera(Camera3D camera);
static void draw_frustum(Frustum frustum, Color color);

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "rayfrustum");
    SetTargetFPS(60);

    CAMERA_MODEL = LoadModel("./resources/camera.glb");

    CAMERA_0.fovy = 70.0;
    CAMERA_0.up = (Vector3){0.0, 1.0, 0.0};
    CAMERA_0.position = (Vector3){5.0, 5.0, 5.0};
    CAMERA_0.target = Vector3Zero();
    CAMERA_0.projection = CAMERA_PERSPECTIVE;

    CAMERA_1.fovy = 40.0;
    CAMERA_1.up = (Vector3){0.0, 1.0, 0.0};
    CAMERA_1.position = (Vector3){-2.0, 2.0, -2.0};
    CAMERA_1.target = Vector3Add(CAMERA_1.position, (Vector3){0.0, -1.0, 1.0});
    CAMERA_1.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        update_free_orbit_camera(&CAMERA_0);

        float aspect = (float)GetScreenWidth() / GetScreenHeight();
        Frustum frustum_0 = get_frustum(CAMERA_1, aspect, 0.1, 2.0);
        Frustum frustum_1 = get_frustum(CAMERA_1, aspect, 2.0, 6.0);
        Frustum frustum_2 = get_frustum(CAMERA_1, aspect, 6.0, 20.0);

        BeginDrawing();
        ClearBackground(CLEAR_COLOR);
        BeginMode3D(CAMERA_0);

        draw_camera(CAMERA_1);
        DrawCube((Vector3){-1.0, 0.0, 0.0}, 1.0, 1.0, 1.0, PURPLE);

        Vector3 d0 = Vector3Normalize(Vector3Subtract(CAMERA_0.target, CAMERA_0.position));
        Vector3 d1 = Vector3Normalize(Vector3Subtract(CAMERA_1.target, CAMERA_1.position));
        float f = Vector3DotProduct(d0, d1);
        printf("%f\n", f);
        if (f < 0.0) {
            draw_frustum(frustum_0, ColorAlpha(RED, 0.5));
            draw_frustum(frustum_1, ColorAlpha(GREEN, 0.5));
            draw_frustum(frustum_2, ColorAlpha(BLUE, 0.5));
        } else {
            draw_frustum(frustum_2, ColorAlpha(BLUE, 0.5));
            draw_frustum(frustum_1, ColorAlpha(GREEN, 0.5));
            draw_frustum(frustum_0, ColorAlpha(RED, 0.5));
        }
            
        DrawGrid(20, 1.0);

        EndMode3D();
        EndDrawing();
    }
}

static Frustum get_frustum(Camera3D camera, float aspect, float near, float far) {
    Matrix v = MatrixLookAt(camera.position, camera.target, camera.up);
    Matrix p = MatrixPerspective(DEG2RAD * camera.fovy, aspect, near, far);
    Matrix v_inv = MatrixInvert(v);
    Matrix p_inv = MatrixInvert(p);

    Frustum frustum = {
        Vector3Unproject((Vector3){-1.0, -1.0, -1.0}, p, v),
        Vector3Unproject((Vector3){-1.0, 1.0, -1.0}, p, v),
        Vector3Unproject((Vector3){1.0, 1.0, -1.0}, p, v),
        Vector3Unproject((Vector3){1.0, -1.0, -1.0}, p, v),
        Vector3Unproject((Vector3){-1.0, -1.0, 1.0}, p, v),
        Vector3Unproject((Vector3){-1.0, 1.0, 1.0}, p, v),
        Vector3Unproject((Vector3){1.0, 1.0, 1.0}, p, v),
        Vector3Unproject((Vector3){1.0, -1.0, 1.0}, p, v)
    };

    return frustum;
}

Vector3 get_triangle_center(Triangle triangle) {
    Vector3 center;
    
    center.x = (triangle.v1.x + triangle.v2.x + triangle.v3.x) / 3.0f;
    center.y = (triangle.v1.y + triangle.v2.y + triangle.v3.y) / 3.0f;
    center.z = (triangle.v1.z + triangle.v2.z + triangle.v3.z) / 3.0f;
    
    return center;
}

static int compare_triangles(const void *triangle_0, const void *triangle_1) {
    Triangle *t0 = (Triangle*)triangle_0;
    Triangle *t1 = (Triangle*)triangle_1;

    float da = Vector3Length(get_triangle_center(*t0));
    float db = Vector3Length(get_triangle_center(*t1));

    if (da < db) return 1;
    else if (da > db) return -1;
    else return 0;
}

static void update_free_orbit_camera(Camera3D *camera) {
    static float rot_speed = 0.003f;
    static float move_speed = 0.01f;
    static float zoom_speed = 1.0f;

    bool is_mmb_down = IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
    bool is_shift_down = IsKeyDown(KEY_LEFT_SHIFT);
    float mouse_wheel_move = GetMouseWheelMove();
    Vector2 mouse_delta = GetMouseDelta();

    if (is_mmb_down && is_shift_down) {
        // Shift + MMB + mouse move -> change the camera position in the
        // right-direction plane
        CameraMoveRight(camera, -move_speed * mouse_delta.x, true);

        Vector3 right = GetCameraRight(camera);
        Vector3 up = Vector3CrossProduct(
            Vector3Subtract(camera->position, camera->target), right
        );
        up = Vector3Scale(Vector3Normalize(up), move_speed * mouse_delta.y);
        camera->position = Vector3Add(camera->position, up);
        camera->target = Vector3Add(camera->target, up);
    } else if (is_mmb_down) {
        // Rotate the camera around the look-at point
        CameraYaw(camera, -rot_speed * mouse_delta.x, true);
        CameraPitch(camera, -rot_speed * mouse_delta.y, true, true, false);
    }

    // Bring camera closer (or move away), to the look-at point
    CameraMoveToTarget(camera, -mouse_wheel_move * zoom_speed);
}

static void draw_camera(Camera3D camera) {
    Vector3 axis;
    float angle;

    Matrix m = GetCameraViewMatrix(&camera);
    Quaternion q = QuaternionFromMatrix(m);
    QuaternionToAxisAngle(q, &axis, &angle);

    DrawModelEx(CAMERA_MODEL, camera.position, axis, RAD2DEG * angle, Vector3One(), WHITE);
}

static void draw_frustum(Frustum frustum, Color color) {
    rlEnableBackfaceCulling();

    Vector3 *corners = frustum.corners;
    Triangle triangles[12] = {
        {corners[1], corners[0], corners[2]},
        {corners[3], corners[2], corners[0]},
        {corners[2], corners[3], corners[6]},
        {corners[7], corners[6], corners[3]},
        {corners[5], corners[4], corners[1]},
        {corners[0], corners[1], corners[4]},
        {corners[6], corners[7], corners[5]},
        {corners[4], corners[5], corners[7]},
        {corners[0], corners[4], corners[3]},
        {corners[7], corners[3], corners[4]},
        {corners[5], corners[1], corners[6]},
        {corners[2], corners[6], corners[1]},
    };

    for (int i = 0; i < 12; ++i) {
        Triangle t = triangles[i];
        DrawTriangle3D(t.v1, t.v2, t.v3, color);
    }
}

