#if defined(_DEBUG) && defined(_MSC_FULL_VER)
#include <crtdbg.h>
#include <float.h>
#endif

#include "freecam.h"
#include <taa/scenefile.h>
#include <taa/path.h>
#include <taa/glcontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void play(
    taa_window_display windisplay,
    taa_window win,
    taa_glcontext_display rcdisplay,
    taa_glcontext_surface rcsurface,
    taa_scene* scene);

typedef struct main_win_s main_win;

struct main_win_s
{
    taa_window_display windisplay;
    taa_window win;
    taa_glcontext_display rcdisplay;
    taa_glcontext_surface rcsurface;
    taa_glcontext rc;
};

//****************************************************************************
static int main_init_window(
    main_win* mwin)
{
    int err = 0;
    int rcattribs[] =
    {
        taa_GLCONTEXT_BLUE_SIZE   ,  8,
        taa_GLCONTEXT_GREEN_SIZE  ,  8,
        taa_GLCONTEXT_RED_SIZE    ,  8,
        taa_GLCONTEXT_DEPTH_SIZE  , 24,
        taa_GLCONTEXT_STENCIL_SIZE,  8,
        taa_GLCONTEXT_NONE
    };
    taa_glcontext_config rcconfig;
    memset(mwin, 0, sizeof(*mwin));
    mwin->windisplay = taa_window_open_display();
    err = (mwin->windisplay != NULL) ? 0 : -1;
    if(err == 0)
    {
        err = taa_window_create(
            mwin->windisplay,
            "taascene viewer",
            720,
            405,
            0, //taa_WINDOW_FULLSCREEN,
            &mwin->win);
    }
    if(err == 0)
    {
        mwin->rcdisplay = taa_glcontext_get_display(mwin->windisplay);
        err = (mwin->rcdisplay != NULL) ? 0 : -1;
    }
    if(err == 0)
    {
        err = (taa_glcontext_initialize(mwin->rcdisplay)) ? 0 : -1;
    };
    if(err == 0)
    {
        int numconfig = 0;
        taa_glcontext_choose_config(
            mwin->rcdisplay,
            rcattribs,
            &rcconfig,
            1,
            &numconfig);
        err = (numconfig >= 1) ? 0 : -1;
    }
    if(err == 0)
    {
        mwin->rcsurface = taa_glcontext_create_surface(
            mwin->rcdisplay,
            rcconfig,
            mwin->win);
        err = (mwin->rcsurface != 0) ? 0 : -1;
    }
    if(err == 0)
    {
        mwin->rc = taa_glcontext_create(
            mwin->rcdisplay,
            mwin->rcsurface,
            rcconfig,
            NULL,
            NULL);
        err = (mwin->rc != NULL) ? 0 : -1;
    }
    if(err == 0)
    {
        int success = taa_glcontext_make_current(
            mwin->rcdisplay,
            mwin->rcsurface,
            mwin->rc);
        err = (success) ? 0 : -1;
    }
    if(err == 0)
    {
        taa_window_show(mwin->windisplay, mwin->win, 1);
    }
    return err;
}

//****************************************************************************
static void main_close_window(
    main_win* mwin)
{
    taa_window_show(mwin->windisplay, mwin->win, 0);
    if(mwin->rc != NULL)
    {
        taa_glcontext_make_current(mwin->rcdisplay,mwin->rcsurface,NULL);
        taa_glcontext_destroy(mwin->rcdisplay, mwin->rc);
        taa_glcontext_destroy_surface(
            mwin->rcdisplay,
            mwin->win,
            mwin->rcsurface);
    }
    if(mwin->rcdisplay != NULL)
    {
        taa_glcontext_terminate(mwin->rcdisplay);
    }
    taa_window_destroy(mwin->windisplay, mwin->win);
    if(mwin->windisplay != NULL)
    {
        taa_window_close_display(mwin->windisplay);
    }
}

int main(int argc, char* argv[])
{
    enum
    {
        ARG_SCENE = 1,
        NUM_ARGS
    };
    int err = 0;
    main_win mwin;
    taa_scene scene;
    FILE* fp = NULL;

    taa_scene_create(&scene, taa_SCENE_Y_UP);
    if(argc != NUM_ARGS)
    {
        // check arguments
        puts("usage: taasceneview <taascene path>\n");
        err = -1;
    }
    if(err == 0)
    {
        // open input file
        fp = fopen(argv[ARG_SCENE], "rb");
        if(fp == NULL)
        {
            printf("could not open input file %s\n", argv[ARG_SCENE]);
            err = -1;
        }
    }
    if(err == 0)
    {
        // read file and deserialize contents
        taa_filestream infs;
        taa_filestream_create(fp, 1024 * 1024, taa_FILESTREAM_READ, &infs);
        err = taa_scenefile_deserialize(&infs, &scene);
        taa_filestream_destroy(&infs);
        fclose(fp);
        if(err != 0)
        {
            printf("error parsing scene file\n");
        }
    }
    if(err == 0)
    {
        err = main_init_window(&mwin);
        if(err == 0)
        {
            play(mwin.windisplay,
                mwin.win,
                mwin.rcdisplay,
                mwin.rcsurface,
                &scene);
        }
        main_close_window(&mwin);
    }
    taa_scene_destroy(&scene);

#if defined(_DEBUG) && defined(_MSC_FULL_VER)
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtCheckMemory();
    _CrtDumpMemoryLeaks();
#endif
    return err;
}
