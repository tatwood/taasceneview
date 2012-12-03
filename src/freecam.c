#include "freecam.h"
#include <taa/mouse.h>
#include <taa/mat44.h>
#include <string.h>

//****************************************************************************
static void freecam_calc_view_matrix(freecam* cam)
{
    taa_vec4 tv;
    taa_vec4 fv = { 0.0f, 0.0f, -cam->focallen, 1.0f };
    taa_mat44 Y;
    taa_mat44 P;
    taa_mat44_yaw(-cam->yaw, &Y);
    taa_mat44_pitch(-cam->pitch, &P);
    taa_mat44_multiply(&P, &Y, &cam->view);
    taa_mat44_transform_vec4(&cam->view, &cam->target, &tv);
    tv.w = 0.0f;
    taa_vec4_subtract(&fv, &tv, &cam->view.w);
}

//****************************************************************************
// unprojects a 2D device coordinate into 3D view space at focal plane
static void freecam_calc_view_pos(
    freecam* cam,
    float devx,
    float devy,
    taa_vec4* view_out)
{
    // project focal length as z and divide by projected w
    float n = cam->znear;
    float f = cam->zfar;
    float devz = -cam->proj.z.z + cam->proj.w.z/cam->focallen;
    taa_vec4 dev = { devx, devy, devz, 1.0f };
    taa_mat44 IP;
    // un project
    // inverse of a projection matrix with symmetric viewing volume
    taa_vec4_set(1.0f/cam->proj.x.x, 0.0f, 0.0f, 0.0f, &IP.x);
    taa_vec4_set(0.0f, 1.0f/cam->proj.y.y, 0.0f, 0.0f, &IP.y);
    taa_vec4_set(0.0f, 0.0f, 0.0f,-(f - n)/(2.0f*f*n), &IP.z);
    taa_vec4_set(0.0f, 0.0f,-1.0f, (f + n)/(2.0f*f*n), &IP.w);
    taa_mat44_transform_vec4(&IP, &dev, view_out);
    taa_vec4_scale(view_out, cam->focallen, view_out); // undivide w
    // these should already be equivalent, but help fp precision
    view_out->z = -cam->focallen;
    view_out->w = 1.0f;
}

//****************************************************************************
// transforms a 3D coordinate from view to world space
static void freecam_calc_world_pos(
    freecam* cam,
    const taa_vec4* view,
    taa_vec4* world_out)
{
    // view transform = inverse(Y * P) * (inverse(T) * v)
    // inverse(view) = T * (Y * P) * v
    taa_mat44 VR;
    taa_mat44 IV;
    taa_vec4 t = { -cam->view.w.x, -cam->view.w.y, -cam->view.w.z, 1.0f };
    taa_vec4 it;
    VR = cam->view;
    taa_vec4_set(0.0f, 0.0f, 0.0f, 1.0f, &VR.w);
    taa_mat44_transpose(&VR, &IV);
    taa_mat44_transform_vec4(&IV, &t, &it);
    IV.w = it;
    taa_mat44_transform_vec4(&IV, view, world_out);
}

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
    const taa_vec4* target)
{
    memset(cam, 0, sizeof(*cam));
    cam->fovy = fovy;
    cam->aspect = aspect;
    cam->znear = znear;
    cam->zfar = zfar;
    cam->yaw = yaw;
    cam->pitch = pitch;
    cam->focallen = focallen;
    cam->minfocallen = minfocallen;
    cam->maxfocallen = maxfocallen;
    cam->target = *target;
    taa_mat44_perspective(fovy, aspect, znear, zfar, &cam->proj);
    freecam_calc_view_matrix(cam);
}

void freecam_update(
    freecam* cam,
    int vieww,
    int viewh,
    taa_mouse_state* mouse,
    const taa_window_event* winevents,
    int numevents)
{
    // calculate cursor device coords relative to previous position
    // places cursor offset from center of screen
    float devx = ((mouse->cursorx - cam->cursorx)*  2.0f)/vieww;
    float devy = ((mouse->cursory - cam->cursory)* -2.0f)/viewh;

    if(mouse->button3 || (mouse->button1 && mouse->button2))
    {
        //zoom
        float flen;
        taa_vec4 view;
        freecam_calc_view_pos(cam, -devx, -devy, &view);
        flen = cam->focallen + (view.x - view.y);
        if(flen >= cam->minfocallen && flen <= cam->maxfocallen)
        {
            // if focal length is in range adjust it with_out moving the target
            cam->focallen = flen;
        }
        else
        {
            // to prevent focal length from getting too small or too large,
            // move the target position instead
            flen = cam->focallen - (view.x - view.y);
            taa_vec4_set(0.0f, 0.0f, -flen, 1.0f, &view);
            freecam_calc_world_pos(cam, &view, &cam->target);
        }
        freecam_calc_view_matrix(cam);
    }
    else if(mouse->button1)
    {
        //rotate
        // there are 360 degrees from the center of the window to the edges
        cam->yaw -= devx*(2.0f*taa_PI);
        cam->pitch += devy*(2.0f*taa_PI);
        freecam_calc_view_matrix(cam);
    }
    else if(mouse->button2 || (mouse->button1 && mouse->button3))
    {
        //pan
        taa_vec4 view;
        freecam_calc_view_pos(cam, -devx, -devy, &view);
        freecam_calc_world_pos(cam, &view, &cam->target);
        freecam_calc_view_matrix(cam);
    }

    cam->cursorx = mouse->cursorx;
    cam->cursory = mouse->cursory;

    if(viewh > 0)
    {
        float aspect = ((float) vieww)/viewh;
        cam->aspect = aspect;
        taa_mat44_perspective(cam->fovy,aspect,cam->znear,cam->zfar,&cam->proj);
    }
}
