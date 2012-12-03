#include <taa/keyboard.h>
#include <taa/gl.h>
#include <taa/glcontext.h>
#include <taa/timer.h>
#include <taa/scalar.h>
#include <taa/vec3.h>
#include <taa/scene.h>
#include "freecam.h"
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>

typedef struct pnvert_s pnvert;
typedef struct tvert_s tvert;
typedef struct jwvert_s jwvert;
typedef struct rendermesh_s rendermesh;

enum
{
    CAM_WIDTH = 672,
    CAM_HEIGHT = 480
};

struct pnvert_s
{
    taa_vec3 pos;
    taa_vec3 normal;
};

struct tvert_s
{
    taa_vec2 texcoord;
};

struct jwvert_s
{
    int32_t joints[4];
    float weights[4];
};

struct rendermesh_s
{
    // input position normal vertices
    const pnvert* pnvin;
    // input joint indices and weights
    const jwvert* jwvin;
    // skinned position normal vertices
    taa_vertexbuffer pnvb;
    // texture coordinates
    taa_vertexbuffer texvb;
    taa_indexbuffer ib;
    int numindices;
    int numvertices;
};

//****************************************************************************
static void calc_joint_transforms(
    const taa_sceneskel* skel,
    const taa_scenenode* nodes,
    taa_mat44* mats_out)
{
    int i;
    int iend;
    // calculate world space joint transforms
    for(i = 0, iend = skel->numjoints; i < iend; ++i)
    {
        taa_sceneskel_joint* joint = skel->joints + i;
        taa_mat44 localmat;
        taa_sceneskel_calc_transform(skel, nodes, i, &localmat);
        if(joint->parent >= 0)
        {
            taa_mat44_multiply(mats_out+joint->parent, &localmat, mats_out+i);
        }
        else
        {
            mats_out[i] = localmat;
        }
    }
}

//****************************************************************************
static void format_mesh(
    taa_scenemesh* mesh)
{
    taa_scenemesh_vertformat vf[] =
    {
        {
            "pn",
            taa_SCENEMESH_USAGE_POSITION,
            0,
            taa_SCENEMESH_VALUE_FLOAT32,
            3,
            0,
            0
        },
        {
            "pn",
            taa_SCENEMESH_USAGE_NORMAL,
            0,
            taa_SCENEMESH_VALUE_FLOAT32,
            3,
            12,
            0
        },
        {
            "t",
            taa_SCENEMESH_USAGE_TEXCOORD,
            0,
            taa_SCENEMESH_VALUE_FLOAT32,
            2,
            0,
            1
        },
        {
            "jw",
            taa_SCENEMESH_USAGE_BLENDINDEX,
            0,
            taa_SCENEMESH_VALUE_INT32,
            4,
            0,
            2
        },
        {
            "jw",
            taa_SCENEMESH_USAGE_BLENDWEIGHT,
            0,
            taa_SCENEMESH_VALUE_FLOAT32,
            4,
            16,
            2
        },
    };
    taa_scenemesh_format(mesh, vf, sizeof(vf)/sizeof(*vf));
    taa_scenemesh_triangulate(mesh);
}

//****************************************************************************
static void create_rendermesh(
    taa_scenemesh* mesh,
    rendermesh* rmesh)
{

    taa_vertexbuffer pnvb;
    taa_vertexbuffer texvb;
    taa_indexbuffer ib;
    int numverts;
    numverts = mesh->vertexstreams[0].numvertices;
    taa_vertexbuffer_create(&pnvb);
    taa_vertexbuffer_bind(pnvb);
    taa_vertexbuffer_data(
        numverts * 24,
        mesh->vertexstreams[0].buffer,
        taa_BUFUSAGE_DYNAMIC_DRAW);
    // copy texture coords to vertex buffer
    taa_vertexbuffer_create(&texvb);
    taa_vertexbuffer_bind(texvb);
    taa_vertexbuffer_data(
        numverts * 8,
        mesh->vertexstreams[1].buffer,
        taa_BUFUSAGE_STATIC_DRAW);
    // copy indices to index buffer
    taa_indexbuffer_create(&ib);
    taa_indexbuffer_bind(ib);
    taa_indexbuffer_data(
        mesh->numindices * 4,
        mesh->indices,
        taa_BUFUSAGE_STATIC_DRAW);
    // place results in rendermesh struct
    rmesh->pnvin = (const pnvert*) mesh->vertexstreams[0].buffer;
    rmesh->jwvin = (const jwvert*) mesh->vertexstreams[2].buffer;
    rmesh->pnvb = pnvb;
    rmesh->texvb = texvb;
    rmesh->ib = ib;
    rmesh->numvertices = numverts;
    rmesh->numindices = mesh->numindices;
}

static void destroy_rendermesh(
    rendermesh* rmesh)
{
    taa_vertexbuffer_destroy(rmesh->pnvb);
    taa_vertexbuffer_destroy(rmesh->texvb);
    taa_indexbuffer_destroy(rmesh->ib);
}

static void draw_rendermesh(
    const taa_scene* scene,
    const taa_scenemesh* mesh,
    const taa_mat44* viewmat,
    const taa_mat44* modelmat,
    taa_texture2d* textures,
    rendermesh* rmesh)
{
    taa_scenemesh_binding* binditr = mesh->bindings;
    taa_scenemesh_binding* bindend = binditr + mesh->numbindings;
    taa_mat44 vmmat;
    taa_mat44_multiply(viewmat, modelmat, &vmmat);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(&vmmat.x.x);
    glVertexPointer(3, GL_FLOAT, 24, &((pnvert**) rmesh->pnvb)[0]->pos);
    glNormalPointer(GL_FLOAT, 24, &((pnvert**) rmesh->pnvb)[0]->normal);
    glTexCoordPointer(2, GL_FLOAT, 8, *((void**) rmesh->texvb));
    while(binditr != bindend)
    {
        int32_t firstindex;
        int32_t indexend;
        // determine if material binding uses color map
        taa_scenematerial* mat = scene->materials + binditr->materialid;
        if(binditr->materialid >= 0 && mat->diffusetexture >= 0)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(
                GL_TEXTURE_2D,
                (GLuint) (size_t) textures[mat->diffusetexture]);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
        }
        // determine the index range for this material binding
        firstindex = 0;
        indexend = 0;
        if(binditr->numfaces > 0)
        {
            const taa_scenemesh_face* fface = mesh->faces+binditr->firstface;
            const taa_scenemesh_face* lface = fface + binditr->numfaces - 1;
            firstindex = fface->firstindex;
            indexend = lface->firstindex + lface->numindices;
        }
        glDrawElements(
            GL_TRIANGLES,
            indexend - firstindex,
            GL_UNSIGNED_INT,
            *((unsigned int**) rmesh->ib) + firstindex);
        ++binditr;
    }
}

static void skin_rendermesh(
    rendermesh* rmesh,
    taa_scenemesh* mesh,
    const taa_mat44* jointmats)
{
    pnvert* pnitr;
    pnvert* pnend;
    const pnvert* pnsrc;
    const jwvert* jwsrc;
    int numverts;
    numverts = rmesh->numvertices;
    taa_vertexbuffer_bind(rmesh->pnvb);
    pnitr = (pnvert*) *((void**) rmesh->pnvb); // TODO: fix this; it's nasty
    pnend = pnitr + numverts;
    pnsrc = rmesh->pnvin;
    jwsrc = rmesh->jwvin;
    while(pnitr != pnend)
    {
        uint32_t i = 0;
        taa_vec3_set(0.0f,0.0f,0.0f, &pnitr->pos);
        taa_vec3_set(0.0f,0.0f,0.0f, &pnitr->normal);
        for(i = 0; i < 4; ++i)
        {
            taa_scenemesh_skinjoint* sj = mesh->joints + jwsrc->joints[i];
            float w = jwsrc->weights[i];
            taa_mat44* invbindmat = &sj->invbindmatrix;
            taa_mat44 M;
            taa_vec3 v;
            taa_vec3 n;
            taa_mat44_multiply(jointmats + sj->animjoint, invbindmat, &M);
            taa_mat44_transform_vec3(&M, &pnsrc->pos, &v);
            taa_vec4_set(0.0f,0.0f,0.0f,1.0f,&M.w);
            taa_mat44_transform_vec3(&M, &pnsrc->normal, &n);
            taa_vec3_scale(&v, w, &v);
            taa_vec3_scale(&n, w, &n);
            taa_vec3_add(&pnitr->pos, &v, &pnitr->pos);
            taa_vec3_add(&pnitr->normal, &n, &pnitr->normal);
        }
        taa_vec3_normalize(&pnitr->normal, &pnitr->normal);
        ++pnsrc;
        ++jwsrc;
        ++pnitr;
    }
}

//****************************************************************************
void play(
    taa_window_display windisplay,
    taa_window win,
    taa_glcontext_display rcdisplay,
    taa_glcontext_surface rcsurface,
    taa_scene* scene)
{
    taa_mouse_state mouse;
    taa_scenenode* animnodes;
    rendermesh* rmeshes;
    taa_mat44** skelmats;
    taa_texture2d* textures;
    int i;
    int numnodes;
    int numskels;
    int nummeshes;
    int numtextures;

    numnodes = scene->numnodes;
    animnodes = (taa_scenenode*) taa_memalign(
        16,
        numnodes*sizeof(*animnodes));
    memcpy(animnodes,scene->nodes, numnodes*sizeof(*animnodes));

    numskels = scene->numskeletons;
    skelmats = (taa_mat44**) malloc(numskels * sizeof(*skelmats));
    for(i = 0; i < numskels; ++i)
    {
        taa_mat44* jointmats;
        int numjoints = scene->skeletons[i].numjoints;
        jointmats = (taa_mat44*)  taa_memalign(
            16,
            numjoints*sizeof(*jointmats));
        skelmats[i] = jointmats;
    }

    nummeshes = scene->nummeshes;
    rmeshes = (rendermesh*) malloc(nummeshes * sizeof(*rmeshes));
    for(i = 0; i < nummeshes; ++i)
    {
        format_mesh(scene->meshes + i);
        create_rendermesh(scene->meshes + i, rmeshes + i);
    }

    numtextures = scene->numtextures;
    textures = (taa_texture2d*) malloc(numtextures * sizeof(*textures));
    for(i = 0; i < numtextures; ++i)
    {
        taa_scenetexture* scntex = scene->textures + i;
        taa_texfilter minfilter = taa_TEXFILTER_NEAREST_MIPMAP_LINEAR;
        taa_texfilter magfilter = taa_TEXFILTER_LINEAR;
        taa_texformat format = taa_TEXFORMAT_LUM8;
        uint32_t level;
        uint32_t w;
        uint32_t h;
        switch(scntex->format)
        {
        case taa_SCENETEXTURE_LUM8 : format = taa_TEXFORMAT_LUM8 ; break;
        case taa_SCENETEXTURE_BGR8 : format = taa_TEXFORMAT_BGR8 ; break;
        case taa_SCENETEXTURE_BGRA8: format = taa_TEXFORMAT_BGRA8; break;
        case taa_SCENETEXTURE_RGB8 : format = taa_TEXFORMAT_RGB8 ; break;
        case taa_SCENETEXTURE_RGBA8: format = taa_TEXFORMAT_RGBA8; break;
        }
        if(scntex->numlevels == 1)
        {
            minfilter = taa_TEXFILTER_LINEAR;
        }
        taa_texture2d_create(textures + i);
        taa_texture2d_bind(textures[i]);
        taa_texture2d_setparameter(taa_TEXPARAM_MAX_LEVEL, scntex->numlevels-1);
        taa_texture2d_setparameter(taa_TEXPARAM_MIN_FILTER, minfilter);
        taa_texture2d_setparameter(taa_TEXPARAM_MAG_FILTER, magfilter);
        taa_texture2d_setparameter(taa_TEXPARAM_WRAP_S, taa_TEXWRAP_CLAMP);
        taa_texture2d_setparameter(taa_TEXPARAM_WRAP_T, taa_TEXWRAP_CLAMP);

        w = scntex->width;
        h = scntex->height;
        for(level = 0; level < scntex->numlevels; ++level)
        {
            taa_texture2d_image(
                level,
                format,
                w,
                h,
                scntex->images[level]);
            w >>= 1;
            h >>= 1;
        }
    }

    taa_mouse_query(windisplay, win, &mouse);
    {
        freecam cam;
        int64_t begintime;
        int64_t currenttime;
        taa_vec4 lightdiff = { 1.0f, 0.9f, 0.8f, 1.0f };
        taa_vec4 lightamb = { 0.4f, 0.4f, 0.7f, 1.0f };
        taa_vec4 lightdir = { 1.0f, 1.0f, 0.0f, 0.0f };
        const taa_vec4 o = { 0.0f, 0.0f, 0.0f, 1.0f };
        int quit = 0;
        freecam_init(
            &cam,
            taa_radians(45.0f),
            1.0f,
            0.01f,
            1000.0f,
            0.0f,
            0.0f,
            10.0f,
            1.0f,
            100.0f,
            &o);
        begintime = taa_timer_sample_cpu();
        currenttime = 0;
        while(!quit)
        {
            taa_window_event winevents[16];
            taa_window_event *evtitr;
            taa_window_event* evtend;
            int numevents;
            unsigned int vw;
            unsigned int vh;
            numevents = taa_window_update(windisplay, win, winevents, 16);
            taa_window_get_size(windisplay, win, &vw, &vh);
            taa_mouse_update(winevents, numevents, &mouse);

            evtitr = winevents;
            evtend = evtitr + numevents;
            while(evtitr != evtend)
            {
                switch(evtitr->type)
                {
                case taa_WINDOW_EVENT_CLOSE:
                    quit = 1; // true
                    break;
                case taa_WINDOW_EVENT_KEY_DOWN:
                    if(evtitr->key.keycode == taa_KEY_ESCAPE)
                    {
                        quit = 1;
                    }
                    break;
                default:
                    break;
                };
                ++evtitr;
            }

            // update animate sqts
            if(scene->numanimations > 0)
            {
                taa_sceneanim* anim = scene->animations;
                int64_t endtime = taa_timer_sample_cpu();
                int64_t dt;
                double sec;
                dt = endtime - begintime;
                if(dt >= 0 && dt < taa_TIMER_MS_TO_NS(1000))
                {
                    // clamp timer since it is unreliable
                    currenttime += dt;
                }
                sec = taa_TIMER_NS_TO_S((double) currenttime);
                sec = sec - anim->length*floor(sec/anim->length);
                taa_sceneanim_play(anim, (float) sec, animnodes, numnodes);
                begintime = endtime;
            }
            freecam_update(&cam, vw, vh, &mouse, winevents, numevents);
            // taa_mat44_transform_vec4(&cam.view, &o, &lightdir);
            taa_vec4_set(0.0f,0.0f,1.0f,0.0f,&lightdir);
            lightdir.w = 0.0f;
            taa_vec4_normalize(&lightdir, &lightdir);
            glViewport(0, 0, vw, vh);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(&cam.proj.x.x);
            // draw mesh
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(&cam.view.x.x);
            glBegin(GL_POINTS);
            glColor4f(1.0f,1.0f,1.0f,1.0f);
            glVertex3f(0.0f,0.0f,0.0f);
            glEnd();
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glLightfv(GL_LIGHT0, GL_POSITION, &lightdir.x);
            glLightfv(GL_LIGHT0, GL_DIFFUSE, &lightdiff.x);
            glLightfv(GL_LIGHT0, GL_AMBIENT, &lightamb.x);
            glColor3f (1.0f, 1.0f, 1.0f);
            glEnable(GL_NORMALIZE);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            for(i = 0; i < numskels; ++i)
            {
                calc_joint_transforms(
                    scene->skeletons + i,
                    animnodes,
                    skelmats[i]);
            }
            for(i = 0; i < numnodes; ++i)
            {
                taa_scenenode* node = scene->nodes + i;
                if(node->type == taa_SCENENODE_REF_MESH)
                {
                    rendermesh* rmesh;
                    taa_scenemesh* mesh;
                    taa_mat44 modelmat;
                    int meshid;
                    int skelid;
                    meshid = node->value.meshid;
                    rmesh = rmeshes + meshid;
                    mesh = scene->meshes + meshid;
                    skelid = mesh->skeleton;
                    if(skelid >= 0)
                    {
                        const taa_mat44* jointmats = skelmats[mesh->skeleton];
                        skin_rendermesh(rmesh, mesh, jointmats);
                    }
                    taa_scenenode_calc_transform(animnodes, i, &modelmat);
                    draw_rendermesh(
                        scene,
                        mesh,
                        &cam.view,
                        &modelmat,
                        textures,
                        rmesh);
                }
            }
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_NORMALIZE);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisable(GL_LIGHT0);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            // draw skeletons
            for(i = 0; i < numskels; ++i)
            {
                taa_sceneskel* skel = scene->skeletons + i;
                taa_sceneskel_joint* jitr;
                taa_mat44* jointmats;
                taa_mat44* jointmatitr;
                taa_mat44* jointmatend;
                jointmats = skelmats[i];
                jitr = skel->joints;
                jointmatitr = jointmats;
                jointmatend = jointmatitr + skel->numjoints;
                // draw bone lines
                glMatrixMode(GL_MODELVIEW);
                glLoadMatrixf(&cam.view.x.x);
                while(jointmatitr != jointmatend)
                {
                    if(jitr->parent >= 0)
                    {
                        glBegin(GL_LINES);
                        glColor4f(1.0f,1.0f,1.0f,1.0f);
                        glVertex3fv(&jointmatitr->w.x);
                        glVertex3fv(&jointmats[jitr->parent].w.x);
                        glEnd();
                    }
                    ++jointmatitr;
                    ++jitr;
                }
                // draw joint axes
                jointmatitr = jointmats;
                jointmatend = jointmatitr + skel->numjoints;
                while(jointmatitr != jointmatend)
                {
                    taa_mat44 vmmat;
                    taa_mat44_multiply(&cam.view, jointmatitr, &vmmat);
                    glLoadMatrixf(&vmmat.x.x);
                    glBegin(GL_LINES);
                    glColor4f(1.0f,0.0f,0.0f,1.0f);
                    glVertex3f(0.0f,0.0f,0.0f);
                    glVertex3f(1.0f,0.0f,0.0f);
                    glColor4f(0.0f,1.0f,0.0f,1.0f);
                    glVertex3f(0.0f,0.0f,0.0f);
                    glVertex3f(0.0f,1.0f,0.0f);
                    glColor4f(0.0f,0.0f,1.0f,1.0f);
                    glVertex3f(0.0f,0.0f,0.0f);
                    glVertex3f(0.0f,0.0f,1.0f);
                    glEnd();
                    ++jointmatitr;
                }
            }
            taa_glcontext_swap_buffers(rcdisplay, rcsurface);
        }
    }
    // clean up
    for(i = 0; i < numtextures; ++i)
    {
        taa_texture2d_destroy(textures[i]);
    }
    for(i = 0; i < numskels; ++i)
    {
        taa_memalign_free(skelmats[i]);
    }
    for(i = 0; i < nummeshes; ++i)
    {
        destroy_rendermesh(rmeshes + i);
    }
    taa_memalign_free(animnodes);
    free(skelmats);
    free(textures);
    free(rmeshes);
}
