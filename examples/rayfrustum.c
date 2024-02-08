#include "../include/rayfrustum.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rcamera.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define print_vec(v) (printf("%f, %f, %f\n", v.x, v.y, v.z))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

typedef struct Frustum {
    // near_left_bot, near_left_top, near_right_top, near_right_bot
    // far_left_bot, far_left_top, far_right_top, far_right_bot
    Vector3 corners[8];

    Matrix view;
    Matrix proj;
} Frustum;

#define MAX_N_FRUSTUMS_IN_CASCADE 9
typedef struct FrustumsCascade {
    int n_frustums;
    Frustum frustums[MAX_N_FRUSTUMS_IN_CASCADE];
    float planes[MAX_N_FRUSTUMS_IN_CASCADE + 1];
} FrustumsCascade;

typedef struct Triangle {
    Vector3 v1;
    Vector3 v2;
    Vector3 v3;
} Triangle;

static Color CLEAR_COLOR = SKYBLUE;

static Vector3 LIGHT_DIRECTION;
static Camera3D CAMERA_0;
static Camera3D CAMERA_1;
static Model CAMERA_MODEL;

static Matrix get_camera_transform(Camera3D camera);
static Frustum get_frustum_of_camera(Camera3D camera, float aspect, float near, float far);
static Frustum get_frustum_of_view_proj(Matrix view, Matrix proj);
static Frustum get_frustum_of_directional_light(Frustum camera_frustum, Vector3 light_direction);
static FrustumsCascade get_frustums_cascade_of_camera(
    Camera3D camera,
    float aspect,
    float planes[MAX_N_FRUSTUMS_IN_CASCADE + 1],
    int n_planes
);
static FrustumsCascade get_frustums_cascade_of_directional_light(
    FrustumsCascade camera_frustums_cascade,
    Vector3 light_direction
);
static void update_free_orbit_camera(Camera3D *camera);
static void draw_frustum(Frustum frustum, Color color);
static void draw_frustum_wires(Frustum frustum, Color color);
static void draw_frustums_cascade(FrustumsCascade cascade, Vector3 eye);
static void draw_frustums_cascade_wires(FrustumsCascade cascade);

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "rayfrustum");
    SetTargetFPS(60);

    LIGHT_DIRECTION = Vector3Normalize((Vector3){0.0, 0.0, 1.0});
    CAMERA_MODEL = LoadModel("./resources/camera.glb");

    CAMERA_0.fovy = 70.0;
    CAMERA_0.up = (Vector3){0.0, 1.0, 0.0};
    CAMERA_0.position = (Vector3){15.0, 5.0, 15.0};
    CAMERA_0.target = Vector3Zero();
    CAMERA_0.projection = CAMERA_PERSPECTIVE;

    CAMERA_1.fovy = 40.0;
    CAMERA_1.up = (Vector3){0.0, 1.0, 0.0};
    CAMERA_1.position = (Vector3){0.0, 3.0, -5.0};
    CAMERA_1.target = Vector3Add(CAMERA_1.position, (Vector3){0.0, -1.0, 1.0});
    CAMERA_1.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        update_free_orbit_camera(&CAMERA_0);

        float aspect = (float)GetScreenWidth() / GetScreenHeight();
        float planes[4] = {0.01, 2.0, 4.0, 16.0};
        FrustumsCascade camera_cascade = get_frustums_cascade_of_camera(CAMERA_1, aspect, planes, 4);
        FrustumsCascade light_cascade = get_frustums_cascade_of_directional_light(camera_cascade, LIGHT_DIRECTION);

        Matrix camera_transform = get_camera_transform(CAMERA_1);

        BeginDrawing();
        ClearBackground(CLEAR_COLOR);

        BeginMode3D(CAMERA_0);
            CAMERA_MODEL.transform = camera_transform;
            DrawModel(CAMERA_MODEL, Vector3Zero(), 1.0, WHITE);
            draw_frustums_cascade_wires(light_cascade);
            draw_frustums_cascade(camera_cascade, CAMERA_0.position);
        EndMode3D();
            
        BeginMode3D(CAMERA_0);
            rlSetLineWidth(1.0);
            DrawGrid(20, 1.0);
        EndMode3D();

        EndDrawing();
    }
}

static Frustum get_frustum_of_view_proj(Matrix view, Matrix proj) {
    Frustum frustum = {
        .corners = {
            Vector3Unproject((Vector3){-1.0, -1.0, -1.0}, proj, view),
            Vector3Unproject((Vector3){-1.0, 1.0, -1.0},  proj, view),
            Vector3Unproject((Vector3){1.0, 1.0, -1.0},   proj, view),
            Vector3Unproject((Vector3){1.0, -1.0, -1.0},  proj, view),
            Vector3Unproject((Vector3){-1.0, -1.0, 1.0},  proj, view),
            Vector3Unproject((Vector3){-1.0, 1.0, 1.0},   proj, view),
            Vector3Unproject((Vector3){1.0, 1.0, 1.0},    proj, view),
            Vector3Unproject((Vector3){1.0, -1.0, 1.0},   proj, view)
        },
        .view = view,
        .proj = proj
    };

    return frustum;
}


static Frustum get_frustum_of_camera(Camera3D camera, float aspect, float near, float far) {
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    Matrix proj = {0};

    if (camera.projection == CAMERA_PERSPECTIVE) {
        proj = MatrixPerspective(DEG2RAD * camera.fovy, aspect, near, far);
    } else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy / 2.0;
        double right = top * aspect;
        proj = MatrixOrtho(-right, right, -top, top, near, far);
    }

    return get_frustum_of_view_proj(view, proj);
}

static Frustum get_frustum_of_directional_light(Frustum camera_frustum, Vector3 light_direction) {
    light_direction = Vector3Normalize(light_direction);

    // Calculate frustum bounding box in the light space
    Matrix light_view = MatrixLookAt(Vector3Zero(), light_direction, (Vector3){0.0, 1.0, 0.0});
    float min_x = FLT_MAX, min_y = FLT_MAX, min_z = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX, max_z = -FLT_MAX;

    for (int i = 0; i < 8; ++i) {
        // Project frustum to the light space
        Vector3 corner = Vector3Transform(camera_frustum.corners[i], light_view);

        min_x = min(min_x, corner.x);
        min_y = min(min_y, corner.y);
        min_z = min(min_z, corner.z);
        max_x = max(max_x, corner.x);
        max_y = max(max_y, corner.y);
        max_z = max(max_z, corner.z);
    }

    // Calculate light position in the light space
    Vector3 light_pos = {
        (min_x + max_x) / 2.0,
        (min_y + max_y) / 2.0,
        (min_z + max_z) / 2.0,
    };

    // Calculate light position in the world space
    light_pos = Vector3Transform(light_pos, MatrixInvert(light_view));

    // Calculate frustum bounding box in the light space (now with light position known)
    light_view = MatrixLookAt(light_pos, Vector3Add(light_pos, light_direction), (Vector3){0.0, 1.0, 0.0});
    min_x = FLT_MAX, min_y = FLT_MAX, min_z = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX, max_z = -FLT_MAX;
    for (int i = 0; i < 8; ++i) {
        // Project frustum to the light space
        Vector3 corner = Vector3Transform(camera_frustum.corners[i], light_view);

        min_x = min(min_x, corner.x);
        min_y = min(min_y, corner.y);
        min_z = min(min_z, corner.z);
        max_x = max(max_x, corner.x);
        max_y = max(max_y, corner.y);
        max_z = max(max_z, corner.z);
    }
    Matrix light_proj = MatrixOrtho(min_x, max_x, min_y, max_y, min_z, max_z);
    Frustum light_frustum = get_frustum_of_view_proj(light_view, light_proj);

    return light_frustum;
}

static FrustumsCascade get_frustums_cascade_of_camera(
    Camera3D camera,
    float aspect,
    float planes[MAX_N_FRUSTUMS_IN_CASCADE + 1],
    int n_planes
) {
    if (n_planes < 2 || n_planes > MAX_N_FRUSTUMS_IN_CASCADE + 1) {
        fprintf(
            stderr,
            "ERROR: Number of frustum planes must be >= 2 and <= %d, but you passed %d\n",
            MAX_N_FRUSTUMS_IN_CASCADE + 1, n_planes
        );
        exit(1);
    }
    
    FrustumsCascade cascade = {0};
    memcpy(cascade.planes, planes, sizeof(planes[0]) * n_planes);

    for (int i = 0; i < n_planes - 1; ++i) {
        float near = planes[i];
        float far = planes[i + 1];
        if (far <= near) {
            fprintf(stderr, "ERROR: Frustum planes must be in ascending order\n");
            exit(1);
        }

        cascade.frustums[cascade.n_frustums++] = get_frustum_of_camera(camera, aspect, near, far);
    }

    return cascade;
}

static FrustumsCascade get_frustums_cascade_of_directional_light(
    FrustumsCascade camera_frustums_cascade,
    Vector3 light_direction
) {
    FrustumsCascade cascade = {0};
    int n_planes = camera_frustums_cascade.n_frustums + 1;
    memcpy(
        cascade.planes,
        camera_frustums_cascade.planes,
        sizeof(camera_frustums_cascade.planes[0]) * n_planes
    );
    cascade.n_frustums = camera_frustums_cascade.n_frustums;

    for (int i = 0; i < cascade.n_frustums; ++i) {
        Frustum camera_frustum = camera_frustums_cascade.frustums[i];
        cascade.frustums[i] = get_frustum_of_directional_light(camera_frustum, light_direction);
    }

    return cascade;
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

static Matrix get_camera_transform(Camera3D camera) {
    Vector3 axis;
    float angle;

    Matrix m = GetCameraViewMatrix(&camera);
    Quaternion q = QuaternionFromMatrix(m);
    QuaternionToAxisAngle(q, &axis, &angle);

    Matrix r = MatrixRotate(axis, angle);
    Matrix t = MatrixTranslate(camera.position.x, camera.position.y, camera.position.z);
    Matrix s = MatrixIdentity();
    Matrix transform = MatrixMultiply(MatrixMultiply(s, r), t);

    return transform;
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

static void draw_frustum_wires(Frustum frustum, Color color) {
    rlSetLineWidth(2.0);
    Vector3 *corners = frustum.corners;
    DrawLine3D(corners[0], corners[1], color);
    DrawLine3D(corners[1], corners[2], color);
    DrawLine3D(corners[2], corners[3], color);
    DrawLine3D(corners[3], corners[0], color);

    DrawLine3D(corners[4], corners[5], color);
    DrawLine3D(corners[5], corners[6], color);
    DrawLine3D(corners[6], corners[7], color);
    DrawLine3D(corners[7], corners[4], color);

    DrawLine3D(corners[0], corners[4], color);
    DrawLine3D(corners[1], corners[5], color);
    DrawLine3D(corners[2], corners[6], color);
    DrawLine3D(corners[3], corners[7], color);
}

static void draw_frustums_cascade(FrustumsCascade cascade, Vector3 eye) {
    // -------------------------------------------------------------------
    // Get the distance of the eye on the z (view) axis of the cascade
    Matrix view = cascade.frustums[0].view;
    float z = -Vector3Transform(eye, view).z;

    // -------------------------------------------------------------------
    // Find nearest frustum (this will be drawn last)
    int nearest_frustum_idx = 0;
    if (z <= cascade.planes[0]) {
        nearest_frustum_idx = 0;
    } else if (z >= cascade.planes[cascade.n_frustums]) {
        nearest_frustum_idx = cascade.n_frustums - 1;
    } else {
        for (int i = 0; i < cascade.n_frustums; ++i) {
            float near = cascade.planes[i];
            float far = cascade.planes[i + 1];
            if (z >= near && z <= far) {
                nearest_frustum_idx = i;
                break;
            }
        }
    }

    // -------------------------------------------------------------------
    // Draw frustums in the order furthest -> nearest
    static Color frustum_colors[MAX_N_FRUSTUMS_IN_CASCADE] = {
        (Color){255, 0, 0, 80},     // Red
        (Color){0, 255, 0, 80},     // Green
        (Color){0, 0, 255, 80},     // Blue
        (Color){255, 255, 0, 80},   // Yellow
        (Color){255, 0, 255, 80},   // Magenta
        (Color){0, 255, 255, 80},   // Cyan
        (Color){255, 128, 0, 80},   // Orange
        (Color){128, 0, 128, 80},   // Purple
        (Color){0, 128, 128, 80}    // Teal
    };

    for (int i = 0; i < nearest_frustum_idx; i++) {
        draw_frustum(cascade.frustums[i], frustum_colors[i]);
    }

    for (int i = cascade.n_frustums - 1; i > nearest_frustum_idx; i--) {
        draw_frustum(cascade.frustums[i], frustum_colors[i]);
    }

    draw_frustum(cascade.frustums[nearest_frustum_idx], frustum_colors[nearest_frustum_idx]);
}

static void draw_frustums_cascade_wires(FrustumsCascade cascade) {
    for (int i = 0; i < cascade.n_frustums; ++i) {
        draw_frustum_wires(cascade.frustums[i], YELLOW);
    }
}
