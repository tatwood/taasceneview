#ifndef FREECAM_H_
#define FREECAM_H_

#include <taa/mouse.h>
#include <taa/window.h>
#include <taa/vec4.h>

typedef struct freecam_s freecam;

struct freecam_s
{
    int32_t cursorx;
    int32_t cursory;
    float yaw;
    float pitch;
    float focallen;
    float minfocallen;
    float maxfocallen;
    taa_vec4 target;
    float fovy;
    float aspect;
    float znear;
    float zfar;
    taa_mat44 view;
    taa_mat44 proj;
};

#ifdef __cplusplus
extern "C"
{
#endif

void freecam_init(
    freecam* cam,
    float fovy,
    float aspect,
    float znear,
    float zfar,
    float yaw,
    float pitch,
    float focallen,
    float minfocallen,
    float maxfocallen,
    const taa_vec4* target);

void freecam_update(
    freecam* cam,
    int vieww,
    int viewh,
    taa_mouse_state* mouse,
    const taa_window_event* winevents,
    int numevents);

#ifdef __cplusplus
}
#endif

#endif // FREECAM_H_
