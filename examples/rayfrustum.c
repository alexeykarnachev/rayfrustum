#include "../include/rayfrustum.h"

#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define RAYGIZMO_IMPLEMENTATION
#include "raygizmo.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define print_vec(v) (printf("%f, %f, %f\n", v.x, v.y, v.z))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

typedef struct DirectionalLight {
    float azimuth;
    float attitude;
} DirectionalLight;

typedef struct CameraShell {
    Transform transform;
    Camera3D *camera;
    Model model;
} CameraShell;

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

static DirectionalLight LIGHT;
static Camera3D CAMERA_0;
static Camera3D CAMERA_1;
static CameraShell CAMERA_1_SHELL;
static Model CAMERA_MODEL;
static RGizmo GIZMO;

static bool IS_CAMERA_PICKED = false;

static CameraShell create_camera_shell(Camera3D *camera);
static Matrix get_transform_matrix(Transform transform);
static Frustum get_frustum_of_camera(
    Camera3D camera, float aspect, float near, float far
);
static Frustum get_frustum_of_view_proj(Matrix view, Matrix proj);
static Frustum get_frustum_of_directional_light(
    Frustum camera_frustum, Vector3 light_direction
);
static FrustumsCascade get_frustums_cascade_of_camera(
    Camera3D camera,
    float aspect,
    float planes[MAX_N_FRUSTUMS_IN_CASCADE + 1],
    int n_planes
);
static FrustumsCascade get_frustums_cascade_of_directional_light(
    FrustumsCascade camera_frustums_cascade, Vector3 light_direction
);
static Vector3 get_direction_from_azimuth_attitude(float azimuth, float attitude);
static void update_free_orbit_camera(Camera3D *camera);
static void draw_camera_shell(CameraShell shell);
static void draw_frustum(Frustum frustum, Color color);
static void draw_frustum_wires(Frustum frustum, Color color);
static void draw_frustums_cascade(FrustumsCascade cascade, Vector3 eye);
static void draw_frustums_cascade_wires(FrustumsCascade cascade);
static void draw_gui(void);

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "rayfrustum");
    SetTargetFPS(60);

    LIGHT.attitude = 45.0;
    LIGHT.azimuth = 45.0;
    CAMERA_MODEL = LoadModel("./resources/camera.glb");
    GIZMO = rgizmo_create();

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

    CAMERA_1_SHELL = create_camera_shell(&CAMERA_1);

    while (!WindowShouldClose()) {
        update_free_orbit_camera(&CAMERA_0);

        if (IS_CAMERA_PICKED) {
            rgizmo_update(&GIZMO, CAMERA_0, CAMERA_1_SHELL.transform.translation);
        }
        CAMERA_1_SHELL.transform.translation = Vector3Add(
            CAMERA_1_SHELL.transform.translation, GIZMO.update.translation
        );
        CAMERA_1_SHELL.transform.rotation = QuaternionMultiply(
            QuaternionFromAxisAngle(GIZMO.update.axis, GIZMO.update.angle),
            CAMERA_1_SHELL.transform.rotation
        );

        CAMERA_1_SHELL.camera->position = CAMERA_1_SHELL.transform.translation;
        Vector3 dir = Vector3RotateByQuaternion(
            (Vector3){0.0, 0.0, -1.0}, CAMERA_1_SHELL.transform.rotation
        );
        CAMERA_1_SHELL.camera->target = Vector3Add(CAMERA_1_SHELL.camera->position, dir);

        float aspect = (float)GetScreenWidth() / GetScreenHeight();
        float planes[4] = {0.01, 2.0, 4.0, 16.0};
        FrustumsCascade camera_cascade = get_frustums_cascade_of_camera(
            CAMERA_1, aspect, planes, 4
        );
        FrustumsCascade light_cascade = get_frustums_cascade_of_directional_light(
            camera_cascade,
            get_direction_from_azimuth_attitude(LIGHT.azimuth, LIGHT.attitude)
        );

        BeginDrawing();
        {
            ClearBackground(CLEAR_COLOR);

            BeginMode3D(CAMERA_0);
            {
                draw_camera_shell(CAMERA_1_SHELL);
                draw_frustums_cascade_wires(light_cascade);
                draw_frustums_cascade(camera_cascade, CAMERA_0.position);
            }
            EndMode3D();

            BeginMode3D(CAMERA_0);
            {
                rlSetLineWidth(1.0);
                DrawGrid(20, 1.0);
            }
            EndMode3D();

            if (IS_CAMERA_PICKED) {
                BeginMode3D(CAMERA_0);
                rgizmo_draw(GIZMO, CAMERA_0, CAMERA_1.position);
                EndMode3D();
            }

            draw_gui();
        }
        EndDrawing();
    }
}

static Frustum get_frustum_of_view_proj(Matrix view, Matrix proj) {
    Frustum frustum = {
        .corners
        = {Vector3Unproject((Vector3){-1.0, -1.0, -1.0}, proj, view),
           Vector3Unproject((Vector3){-1.0, 1.0, -1.0}, proj, view),
           Vector3Unproject((Vector3){1.0, 1.0, -1.0}, proj, view),
           Vector3Unproject((Vector3){1.0, -1.0, -1.0}, proj, view),
           Vector3Unproject((Vector3){-1.0, -1.0, 1.0}, proj, view),
           Vector3Unproject((Vector3){-1.0, 1.0, 1.0}, proj, view),
           Vector3Unproject((Vector3){1.0, 1.0, 1.0}, proj, view),
           Vector3Unproject((Vector3){1.0, -1.0, 1.0}, proj, view)},
        .view = view,
        .proj = proj};

    return frustum;
}

static CameraShell create_camera_shell(Camera3D *camera) {
    CameraShell shell = {0};
    shell.camera = camera;
    shell.model = CAMERA_MODEL;
    shell.transform.scale = Vector3One();
    shell.transform.rotation = QuaternionIdentity();
    shell.transform.translation = shell.camera->position;
    Vector3 dir = Vector3Subtract(shell.camera->target, shell.camera->position);
    shell.transform.rotation = QuaternionFromVector3ToVector3(
        (Vector3){0.0, 0.0, -1.0}, Vector3Normalize(dir)
    );

    return shell;
}

static Matrix get_transform_matrix(Transform transform) {
    Vector3 t = transform.translation;
    Vector3 s = transform.scale;
    Quaternion q = transform.rotation;

    Matrix mt = MatrixTranslate(t.x, t.y, t.z);
    Matrix mr = QuaternionToMatrix(q);
    Matrix ms = MatrixScale(s.x, s.y, s.z);
    Matrix m = MatrixMultiply(ms, MatrixMultiply(mr, mt));

    return m;
}

static Frustum get_frustum_of_camera(
    Camera3D camera, float aspect, float near, float far
) {
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

static Frustum get_frustum_of_directional_light(
    Frustum camera_frustum, Vector3 light_direction
) {
    light_direction = Vector3Normalize(light_direction);

    // Calculate frustum bounding box in the light space
    Matrix light_view = MatrixLookAt(
        Vector3Zero(), light_direction, (Vector3){0.0, 1.0, 0.0}
    );
    float min_x = FLT_MAX, min_y = FLT_MAX, min_z = FLT_MAX, max_x = -FLT_MAX,
          max_y = -FLT_MAX, max_z = -FLT_MAX;

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
    light_view = MatrixLookAt(
        light_pos, Vector3Add(light_pos, light_direction), (Vector3){0.0, 1.0, 0.0}
    );
    min_x = FLT_MAX, min_y = FLT_MAX, min_z = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX,
    max_z = -FLT_MAX;
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
            MAX_N_FRUSTUMS_IN_CASCADE + 1,
            n_planes
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

        cascade.frustums[cascade.n_frustums++] = get_frustum_of_camera(
            camera, aspect, near, far
        );
    }

    return cascade;
}

static FrustumsCascade get_frustums_cascade_of_directional_light(
    FrustumsCascade camera_frustums_cascade, Vector3 light_direction
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
        cascade.frustums[i] = get_frustum_of_directional_light(
            camera_frustum, light_direction
        );
    }

    return cascade;
}

static Vector3 get_direction_from_azimuth_attitude(
    float azimuth_deg, float attitude_deg
) {
    Vector3 result;
    double azimuth_rad = DEG2RAD * azimuth_deg;
    double attitude_rad = DEG2RAD * attitude_deg;

    result.x = cos(azimuth_rad) * cos(attitude_rad);
    result.y = sin(azimuth_rad) * cos(attitude_rad);
    result.z = sin(attitude_rad);

    return Vector3Normalize(result);
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

static void draw_camera_shell(CameraShell shell) {
    shell.model.transform = get_transform_matrix(shell.transform);
    DrawModel(shell.model, Vector3Zero(), 1.0, WHITE);
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
    rlSetLineWidth(1.0);
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
        (Color){255, 0, 0, 80},  // Red
        (Color){0, 255, 0, 80},  // Green
        (Color){0, 0, 255, 80},  // Blue
        (Color){255, 255, 0, 80},  // Yellow
        (Color){255, 0, 255, 80},  // Magenta
        (Color){0, 255, 255, 80},  // Cyan
        (Color){255, 128, 0, 80},  // Orange
        (Color){128, 0, 128, 80},  // Purple
        (Color){0, 128, 128, 80}  // Teal
    };

    for (int i = 0; i < nearest_frustum_idx; i++) {
        draw_frustum(cascade.frustums[i], frustum_colors[i]);
    }

    for (int i = cascade.n_frustums - 1; i > nearest_frustum_idx; i--) {
        draw_frustum(cascade.frustums[i], frustum_colors[i]);
    }

    draw_frustum(
        cascade.frustums[nearest_frustum_idx], frustum_colors[nearest_frustum_idx]
    );
}

static void draw_frustums_cascade_wires(FrustumsCascade cascade) {
    for (int i = 0; i < cascade.n_frustums; ++i) {
        draw_frustum_wires(cascade.frustums[i], YELLOW);
    }
}

static void draw_gui(void) {
    GuiPanel((Rectangle){2, 2, 220, 200}, "Controls");
    GuiSliderBar(
        (Rectangle){55, 35, 130, 20},
        "Light    \nazimuth ",
        TextFormat("%.2f", LIGHT.azimuth),
        &LIGHT.azimuth,
        1.0,
        180.0
    );
    GuiSliderBar(
        (Rectangle){55, 70, 130, 20},
        "Light    \nattitude",
        TextFormat("%.2f", LIGHT.attitude),
        &LIGHT.attitude,
        1.0,
        180.0
    );
    GuiSliderBar(
        (Rectangle){55, 105, 130, 20},
        "Camera \nFOV",
        TextFormat("%.2f", CAMERA_1.fovy),
        &CAMERA_1.fovy,
        1.0,
        180.0
    );

    GuiCheckBox((Rectangle){8, 145, 20, 20}, "Pick camera", &IS_CAMERA_PICKED);
}
